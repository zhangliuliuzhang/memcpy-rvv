#include <stddef.h>
#include <stdint.h>
#include <riscv_vector.h>

#define MINU(a, b) ((a) < (b) ? (a) : (b))

int memcmp(const void *s1, const void *s2, size_t n) {
    const uint8_t *p1 = (const uint8_t *)s1;
    const uint8_t *p2 = (const uint8_t *)s2;

    if (n == 0 || p1 == p2) {
        return 0;
    }

    // 1. 极小数据标量特判 (Fast Path)
    // 两个指针都要走内存，对于极小数据，启动向量的成本远超标量比对。
    if (n <= 16) {
        for (size_t i = 0; i < n; ++i) {
            if (p1[i] != p2[i]) {
                return p1[i] - p2[i];
            }
        }
        return 0;
    }

    // 2. 头部捡漏，优先对齐 p1 (或者 p2)
    // 因为涉及双指针读取，我们通常选择优先让其中一个指针（比如 p1）8 字节对齐，
    // 这样能至少保证一半的内存读取是顺滑的。
    size_t head_align = ((size_t)(-(uintptr_t)p1)) & 7u;
    size_t head = MINU(head_align, n);

    if (head != 0) {
        size_t vl = __riscv_vsetvl_e8m1(head);

        vuint8m1_t v1 = __riscv_vle8_v_u8m1(p1, vl);
        vuint8m1_t v2 = __riscv_vle8_v_u8m1(p2, vl);

        // vmsne: Vector Mask Set Not Equal
        vbool8_t mask = __riscv_vmsne_vv_u8m1_b8(v1, v2, vl);
        long idx = __riscv_vfirst_m_b8(mask, vl);

        if (idx >= 0) {
            // 找到了不相等的字节，用标量方式返回结果
            return p1[idx] - p2[idx];
        }

        p1 += vl;
        p2 += vl;
        n -= vl;
    }

    // 3. 主干狂飙 (e8m8)
    // 这里保持 e8，原因同 memchr：我们要精确到字节级的差异。
    while (n > 0) {
        size_t vl = __riscv_vsetvl_e8m8(n);

#if defined(__riscv_zicbop)
        // memcmp 是双路读取，预取对性能提升极大
        __asm__ volatile(
            "prefetch.r 256(%0)\n\t"
            "prefetch.r 256(%1)\n\t"
            : : "r"(p1), "r"(p2) : "memory");
#endif

        vuint8m8_t v1 = __riscv_vle8_v_u8m8(p1, vl);
        vuint8m8_t v2 = __riscv_vle8_v_u8m8(p2, vl);

        // 核心：比较不等，生成掩码
        vbool1_t mask = __riscv_vmsne_vv_u8m8_b1(v1, v2, vl);

        // 寻首
        long idx = __riscv_vfirst_m_b1(mask, vl);

        if (idx >= 0) {
            // 找到差异点，立刻打断循环，返回差值
            return p1[idx] - p2[idx];
        }

        p1 += vl;
        p2 += vl;
        n -= vl;
    }

    return 0;
}