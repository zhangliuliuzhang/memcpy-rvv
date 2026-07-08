#include <stddef.h>
#include <stdint.h>
#include <riscv_vector.h>

#define MINU(a, b) ((a) < (b) ? (a) : (b))

void *memmove(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    void *ret = dst;

    if (n == 0 || d == s) {
        return ret;
    }

    // ==========================================
    // 场景 A：无重叠覆盖风险 (等价于 memcpy)
    // 目标在源之前，或者两者完全脱节
    // ==========================================
    if (d < s || d >= s + n) {

        // 1. 极小数据重叠写 (Overlapping)
        if (n <= 16) {
            if (n >= 8) {
                *(uint64_t*)d = *(const uint64_t*)s;
                *(uint64_t*)(d + n - 8) = *(const uint64_t*)(s + n - 8);
                return ret;
            }
            if (n >= 4) {
                *(uint32_t*)d = *(const uint32_t*)s;
                *(uint32_t*)(d + n - 4) = *(const uint32_t*)(s + n - 4);
                return ret;
            }
            if (n >= 2) {
                *(uint16_t*)d = *(const uint16_t*)s;
                *(uint16_t*)(d + n - 2) = *(const uint16_t*)(s + n - 2);
                return ret;
            }
            *d = *s;
            return ret;
        }

        // 2. 头部对齐捡漏
        size_t head_align = ((size_t)(-(uintptr_t)d)) & 7u;
        size_t head = MINU(head_align, n);

        if (head != 0) {
            size_t vl = __riscv_vsetvl_e8m1(head);
            vuint8m1_t v = __riscv_vle8_v_u8m1(s, vl);
            __riscv_vse8_v_u8m1(d, v, vl);
            d += vl;
            s += vl;
            n -= vl;
        }

        // 3. 主干狂飙 (e64m8)
        size_t num_e64 = n / 8;
        while (num_e64 != 0) {
            size_t vl_e64 = __riscv_vsetvl_e64m8(num_e64);

#if defined(__riscv_zicbop)
            __asm__ volatile(
                "prefetch.r 256(%0)\n\t"
                "prefetch.w 256(%1)\n\t"
                : : "r"(s), "r"(d) : "memory");
#endif

            vuint64m8_t v64 = __riscv_vle64_v_u64m8((const uint64_t*)s, vl_e64);
            __riscv_vse64_v_u64m8((uint64_t*)d, v64, vl_e64);

            size_t bytes_processed = vl_e64 * 8;
            s += bytes_processed;
            d += bytes_processed;
            n -= bytes_processed;
            num_e64 -= vl_e64;
        }

        // 4. 尾部收尾 (e8m1)
        size_t vl = __riscv_vsetvl_e8m1(n);
        vuint8m1_t v = __riscv_vle8_v_u8m1(s, vl);
        __riscv_vse8_v_u8m1(d, v, vl);

        return ret;
    }

    // ==========================================
    // 场景 B：危险的重叠覆盖风险
    // d > s 且 d < s + n。必须从后往前拷 (Reverse Copy)
    // ==========================================

    // 指针移动到末尾边界
    d += n;
    s += n;

    // 为了最大化效率，我们同样试图让目标写操作达到 8 字节对齐。
    // 从后往前拷时，我们要对齐的是 d 指针向后退落到的地址。
    // 计算末尾有几个不对齐的字节
    size_t tail_align = ((uintptr_t)d) & 7u;
    size_t tail = MINU(tail_align, n);

    // 1. 尾巴捡漏 (从后往前拷的“头部”)：切掉不对齐的几个字节
    if (tail != 0) {
        // 这里必须用一个 e8m1 循环，因为没法保证刚好等于 vsetvl 返回的值
        // 我们用标量老老实实回退处理
        while (tail > 0) {
            d--;
            s--;
            *d = *s;
            tail--;
            n--;
        }
    }

    // 2. 主干反向狂飙 (e64m8)
    // 此时 d 已经 8 字节对齐
    size_t num_e64 = n / 8;
    while (num_e64 != 0) {
        // 请求本次能够处理的 64-bit 元素个数 (大托盘数)
        size_t vl_e64 = __riscv_vsetvl_e64m8(num_e64);
        size_t bytes_processed = vl_e64 * 8;

        // 【关键】将指针回退本次要处理的总字节数，找到这块数据的起点
        s -= bytes_processed;
        d -= bytes_processed;

#if defined(__riscv_zicbop)
        // 注意：反向拷贝时，预取指针应该是负数！往前（低地址）预取
        __asm__ volatile(
            "prefetch.r -256(%0)\n\t"
            "prefetch.w -256(%1)\n\t"
            : : "r"(s), "r"(d) : "memory");
#endif

        // 以正向块 (Chunk) 读出并写入
        vuint64m8_t v64 = __riscv_vle64_v_u64m8((const uint64_t*)s, vl_e64);
        __riscv_vse64_v_u64m8((uint64_t*)d, v64, vl_e64);

        num_e64 -= vl_e64;
        n -= bytes_processed;
    }

    // 3. 最后剩下的残余收尾 (e8m8)
    // 同样，采用 Chunk-by-Chunk 回退方式
    while (n > 0) {
        size_t vl = __riscv_vsetvl_e8m8(n);
        s -= vl;
        d -= vl;

        vuint8m8_t v8 = __riscv_vle8_v_u8m8(s, vl);
        __riscv_vse8_v_u8m8(d, v8, vl);

        n -= vl;
    }

    return ret;
}