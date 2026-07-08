#include <stddef.h>
#include <stdint.h>
#include <riscv_vector.h>

#define MINU(a, b) ((a) < (b) ? (a) : (b))

void *memchr(const void *src, int c, size_t n) {
    const uint8_t *s = (const uint8_t *)src;
    uint8_t target = (uint8_t)c;

    if (n == 0) {
        return NULL;
    }

    if (n <= 32) {
        for (size_t i = 0; i < n; ++i) {
            if (s[i] == target) return (void *)(s + i);
        }
        return NULL;
    }

    if (n <= 256) {
        size_t head = MINU(((size_t)(-(uintptr_t)s)) & 7u, n);
        if (head != 0) {
            size_t vl = __riscv_vsetvl_e8m1(head);
            vuint8m1_t v_data = __riscv_vle8_v_u8m1(s, vl);
            vbool8_t mask = __riscv_vmseq_vx_u8m1_b8(v_data, target, vl);
            long idx = __riscv_vfirst_m_b8(mask, vl);
            if (idx >= 0) {
                return (void *)(s + idx);
            }
            s += vl;
            n -= vl;
        }

        while (n != 0) {
            size_t vl = __riscv_vsetvl_e8m8(n);
            vuint8m8_t v_data = __riscv_vle8_v_u8m8(s, vl);
            vbool1_t mask = __riscv_vmseq_vx_u8m8_b1(v_data, target, vl);
            long idx = __riscv_vfirst_m_b1(mask, vl);

            if (idx >= 0) {
                return (void *)(s + idx);
            }

            s += vl;
            n -= vl;
        }
        return NULL;
    }

    while (n != 0) {
        size_t vl = __riscv_vsetvl_e8m8(n);
        vuint8m8_t v_data = __riscv_vle8_v_u8m8(s, vl);
        vbool1_t mask = __riscv_vmseq_vx_u8m8_b1(v_data, target, vl);
        long idx = __riscv_vfirst_m_b1(mask, vl);

        if (idx >= 0) {
            return (void *)(s + idx);
        }

        s += vl;
        n -= vl;
    }

    return NULL;
}
