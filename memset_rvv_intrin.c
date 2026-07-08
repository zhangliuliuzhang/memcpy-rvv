#include <stddef.h>
#include <stdint.h>
#include <riscv_vector.h>

#define MINU(a, b) ((a) < (b) ? (a) : (b))
#define CBO_ZERO_BLOCK_SIZE 64u
#define CBO_ZERO_THRESHOLD 512u

static inline void fill_bytes_vector(uint8_t *d, uint8_t val, size_t n) {
    while (n != 0) {
        size_t vl = __riscv_vsetvl_e8m8(n);
        vuint8m8_t v8 = __riscv_vmv_v_x_u8m8(val, vl);
        __riscv_vse8_v_u8m8(d, v8, vl);
        d += vl;
        n -= vl;
    }
}

static inline void zero_cache_block(void *p) {
    __asm__ volatile(
        ".option push\n\t"
        ".option arch, +zicboz\n\t"
        "cbo.zero 0(%0)\n\t"
        ".option pop\n\t"
        :
        : "r"(p)
        : "memory");
}

void *memset(void *dst, int c, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    void *ret = dst;

    if (n <= 16) {
        if (n == 0) return ret;

        uint8_t val = (uint8_t)c;

        if (n >= 8) {
            uint64_t pattern64 = (uint64_t)val * 0x0101010101010101ULL;
            *(uint64_t *)d = pattern64;
            *(uint64_t *)(d + n - 8) = pattern64;
            return ret;
        }
        if (n >= 4) {
            uint32_t pattern32 = (uint32_t)val * 0x01010101U;
            *(uint32_t *)d = pattern32;
            *(uint32_t *)(d + n - 4) = pattern32;
            return ret;
        }
        if (n >= 2) {
            uint16_t pattern16 = (uint16_t)val * 0x0101U;
            *(uint16_t *)d = pattern16;
            *(uint16_t *)(d + n - 2) = pattern16;
            return ret;
        }
        *d = val;
        return ret;
    }

    uint8_t val = (uint8_t)c;

    if (val == 0 && n >= CBO_ZERO_THRESHOLD) {
        size_t block_head = MINU(((size_t)(-(uintptr_t)d)) & (CBO_ZERO_BLOCK_SIZE - 1u), n);
        if (block_head != 0) {
            fill_bytes_vector(d, 0, block_head);
            d += block_head;
            n -= block_head;
        }

        while (n >= CBO_ZERO_BLOCK_SIZE) {
            zero_cache_block(d);
            d += CBO_ZERO_BLOCK_SIZE;
            n -= CBO_ZERO_BLOCK_SIZE;
        }

        if (n != 0) {
            fill_bytes_vector(d, 0, n);
        }
        return ret;
    }

    if (n >= 32) {
        size_t vlmax = __riscv_vsetvlmax_e8m8();
        vuint8m8_t v8_full = __riscv_vmv_v_x_u8m8(val, vlmax);

        while (n >= vlmax) {
            __riscv_vse8_v_u8m8(d, v8_full, vlmax);
            d += vlmax;
            n -= vlmax;
        }

        if (n != 0) {
            __riscv_vse8_v_u8m8(d, v8_full, n);
        }
        return ret;
    }

    fill_bytes_vector(d, val, n);
    return ret;
}
