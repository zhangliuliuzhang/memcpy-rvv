#include <stddef.h>
#include <stdint.h>
#include <riscv_vector.h>

// 辅助宏：无分支求最小值（假设已开启 Zbb）
#define MINU(a, b) ((a) < (b) ? (a) : (b))

void *memset(void *dst, int c, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    void *ret = dst;

    // 1. 极小数据：利用宽标量“重叠拷贝”直接消灭循环 (Fast Path)
    if (n <= 16) {
        if (n == 0) return ret;

        // 将传入的 int (通常是 0~255) 提取为单字节
        uint8_t val = (uint8_t)c;

        if (n >= 8) {
            // 巧用乘法将 1 字节扩展成 8 字节图案 (例如 0xAB -> 0xABABABABABABABAB)
            // 0x0101010101010101ULL 是一个魔法常数
            uint64_t pattern64 = (uint64_t)val * 0x0101010101010101ULL;
            *(uint64_t*)d = pattern64;
            *(uint64_t*)(d + n - 8) = pattern64;
            return ret;
        }
        if (n >= 4) {
            uint32_t pattern32 = (uint32_t)val * 0x01010101U;
            *(uint32_t*)d = pattern32;
            *(uint32_t*)(d + n - 4) = pattern32;
            return ret;
        }
        if (n >= 2) {
            uint16_t pattern16 = (uint16_t)val * 0x0101U;
            *(uint16_t*)d = pattern16;
            *(uint16_t*)(d + n - 2) = pattern16;
            return ret;
        }
        *d = val;
        return ret;
    }

    // 2. 将单字节的 c 扩展为 64 位的 pattern，为了后续 e64 狂飙做准备
    uint8_t val = (uint8_t)c;
    uint64_t pattern64 = (uint64_t)val * 0x0101010101010101ULL;

    // 3. 头部捡漏：保证写地址 8 字节对齐
    size_t head_align = ((size_t)(-(uintptr_t)d)) & 7u;
    size_t head = MINU(head_align, n);

    if (head != 0) {
        size_t vl = __riscv_vsetvl_e8m1(head);
        // 用标量 pattern 填充向量
        vuint8m1_t v8 = __riscv_vmv_v_x_u8m1(val, vl);
        __riscv_vse8_v_u8m1(d, v8, vl);
        d += vl;
        n -= vl;
    }

    // 4. 主干狂飙：e64m8，开着 8 个 64 位大托盘的重型卡车
    size_t num_e64 = n / 8;

    // 如果是清零操作，我们可以充分利用 RISC-V 的 zero 寄存器零开销特性
    if (val == 0) {
        while (num_e64 != 0) {
            size_t vl_e64 = __riscv_vsetvl_e64m8(num_e64);

#if defined(__riscv_zicbop)
            // memset 只有写没有读，只需预取写目标
            __asm__ volatile("prefetch.w 256(%0)\n\t" : : "r"(d) : "memory");
#endif

            // 注意这里的参数 0！
            // 编译器会生成 vmv.v.x v8, zero。硬件可能触发 Zero-Idiom 直接标记清零。
            vuint64m8_t v64_zero = __riscv_vmv_v_x_u64m8(0, vl_e64);
            __riscv_vse64_v_u64m8((uint64_t*)d, v64_zero, vl_e64);

            size_t bytes_processed = vl_e64 * 8;
            d += bytes_processed;
            n -= bytes_processed;
            num_e64 -= vl_e64;
        }
    } else {
        // 如果不是清零，正常写入填充图案
        while (num_e64 != 0) {
            size_t vl_e64 = __riscv_vsetvl_e64m8(num_e64);

#if defined(__riscv_zicbop)
            __asm__ volatile("prefetch.w 256(%0)\n\t" : : "r"(d) : "memory");
#endif

            // 将我们手工扩展好的 64 位图案铺满向量寄存器
            vuint64m8_t v64_pattern = __riscv_vmv_v_x_u64m8(pattern64, vl_e64);
            __riscv_vse64_v_u64m8((uint64_t*)d, v64_pattern, vl_e64);

            size_t bytes_processed = vl_e64 * 8;
            d += bytes_processed;
            n -= bytes_processed;
            num_e64 -= vl_e64;
        }
    }

    // 5. 尾部收尾 (e8m1)：毫无分支的 Strip-mining
    // 当 n == 0 时，vl 返回 0，什么也不会做
    size_t vl = __riscv_vsetvl_e8m1(n);
    vuint8m1_t v8 = __riscv_vmv_v_x_u8m1(val, vl);
    __riscv_vse8_v_u8m1(d, v8, vl);

    return ret;
}