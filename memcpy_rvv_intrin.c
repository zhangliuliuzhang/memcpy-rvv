#include <stddef.h>
#include <stdint.h>
#include <riscv_vector.h>

#define MINU(a, b) ((a) < (b) ? (a) : (b))

static inline void tiny_copy(uint8_t *d, const uint8_t *s, size_t n) {
    if (n >= 8) {                              // 8~16 字节:两段 8 字节重叠拷
        *(uint64_t*)d = *(const uint64_t*)s;
        *(uint64_t*)(d + n - 8) = *(const uint64_t*)(s + n - 8);
    } else if (n >= 4) {                       // 4~7 字节
        *(uint32_t*)d = *(const uint32_t*)s;
        *(uint32_t*)(d + n - 4) = *(const uint32_t*)(s + n - 4);
    } else if (n >= 2) {                       // 2~3 字节
        *(uint16_t*)d = *(const uint16_t*)s;
        *(uint16_t*)(d + n - 2) = *(const uint16_t*)(s + n - 2);
    } else if (n == 1) {                       // 1 字节
        *d = *s;
    }
    // n==0 什么都不做
}


static inline void copy_bytes_vector(uint8_t *d, const uint8_t *s, size_t n) {
    while (n != 0) {
        size_t vl = __riscv_vsetvl_e8m8(n);
        vuint8m8_t v8 = __riscv_vle8_v_u8m8(s, vl);
        __riscv_vse8_v_u8m8(d, v8, vl);
        d += vl;
        s += vl;
        n -= vl;
    }
}

void *memcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *) dst;
    const uint8_t *s = (const uint8_t *) src;
    void *ret = dst;

    if (n <= 16) {
        tiny_copy(d, s, n);
        return ret;
    }

    if ((((uintptr_t) d ^ (uintptr_t) s) & 7u) == 0) {
        size_t head = MINU((size_t) (-(uintptr_t) d) & 7u, n);
        size_t vl = __riscv_vsetvl_e8m1(head);
        vuint8m1_t v8 = __riscv_vle8_v_u8m1(s, vl);
        __riscv_vse8_v_u8m1(d, v8, vl);
        d += vl;
        s += vl;
        n -= vl;

        size_t num_e64 = n / 8;
        while (num_e64 != 0) {
            size_t vl_e64 = __riscv_vsetvl_e64m8(num_e64);

#if defined(__riscv_zicbop)
            __asm__ volatile(
                "prefetch.r 256(%0)\n\t"
                "prefetch.w 256(%1)\n\t"
                :
                : "r"(s), "r"(d)
                : "memory");
#endif

            vuint64m8_t v64 = __riscv_vle64_v_u64m8((const uint64_t *) s, vl_e64);
            __riscv_vse64_v_u64m8((uint64_t *) d, v64, vl_e64);

            size_t bytes = vl_e64 * 8;
            s += bytes;
            d += bytes;
            n -= bytes;
            num_e64 -= vl_e64;
        }
    }

    if (n != 0) {
#if defined(__riscv_zicbop)
        __asm__ volatile(
            "prefetch.r 256(%0)\n\t"
            "prefetch.w 256(%1)\n\t"
            :
            : "r"(s), "r"(d)
            : "memory");
#endif
        copy_bytes_vector(d, s, n);
    }

    return ret;
}
