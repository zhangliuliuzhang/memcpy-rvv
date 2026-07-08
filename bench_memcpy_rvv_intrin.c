#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void *memcpy_rvv_intrin(void *dst, const void *src, size_t n);

static long long now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long) ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static void fill_pattern(unsigned char *buf, size_t len, unsigned int seed) {
    unsigned int x = seed;
    for (size_t i = 0; i < len; ++i) {
        x = x * 1664525u + 1013904223u;
        buf[i] = (unsigned char) (x >> 24);
    }
}

static int check_case(size_t size, size_t src_align, size_t dst_align) {
    size_t pad = 256;
    size_t total = size + pad;
    unsigned char *src_base = aligned_alloc(64, total + 64);
    unsigned char *dst_base = aligned_alloc(64, total + 64);
    unsigned char *ref_base = aligned_alloc(64, total + 64);
    if (src_base == NULL || dst_base == NULL || ref_base == NULL) {
        fprintf(stderr, "allocation failed in check_case\n");
        free(src_base);
        free(dst_base);
        free(ref_base);
        return 0;
    }

    unsigned char *src = src_base + src_align;
    unsigned char *dst = dst_base + dst_align;
    unsigned char *ref = ref_base + dst_align;

    fill_pattern(src_base, total + 64, 1u + (unsigned int) size);
    memset(dst_base, 0xA5, total + 64);
    memset(ref_base, 0xA5, total + 64);

    memcpy(ref, src, size);
    memcpy_rvv_intrin(dst, src, size);

    int ok = memcmp(dst_base, ref_base, total + 64) == 0;
    if (!ok) {
        fprintf(stderr, "mismatch: size=%zu src_align=%zu dst_align=%zu\n",
                size, src_align, dst_align);
    }

    free(src_base);
    free(dst_base);
    free(ref_base);
    return ok;
}

static int run_correctness(void) {
    static const size_t sizes[] = {
        0, 1, 2, 3, 4, 5, 7, 8, 9, 15, 16, 17, 31, 32, 33, 63, 64, 65,
        127, 128, 129, 255, 256, 257, 511, 512, 513, 1024, 4096, 65536
    };

    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
        for (size_t src_align = 0; src_align < 16; ++src_align) {
            for (size_t dst_align = 0; dst_align < 16; ++dst_align) {
                if (!check_case(sizes[i], src_align, dst_align)) {
                    return 0;
                }
            }
        }
    }
    return 1;
}

static double bench_one(void *(*fn)(void *, const void *, size_t),
                        size_t size,
                        int iters) {
    size_t extra = 256;
    unsigned char *src_base = aligned_alloc(64, size + extra + 64);
    unsigned char *dst_base = aligned_alloc(64, size + extra + 64);
    if (src_base == NULL || dst_base == NULL) {
        fprintf(stderr, "allocation failed in bench_one\n");
        free(src_base);
        free(dst_base);
        return -1.0;
    }

    unsigned char *src = src_base + 3;
    unsigned char *dst = dst_base + 5;
    fill_pattern(src_base, size + extra + 64, 7u + (unsigned int) size);
    memset(dst_base, 0, size + extra + 64);

    for (int i = 0; i < 8; ++i) {
        fn(dst, src, size);
    }

    long long start = now_ns();
    for (int i = 0; i < iters; ++i) {
        fn(dst, src, size);
    }
    long long end = now_ns();

    volatile unsigned char sink = dst[size ? (size - 1) : 0];
    (void) sink;

    double elapsed = (double) (end - start);
    double bytes = (double) size * (double) iters;

    free(src_base);
    free(dst_base);

    return bytes / elapsed;
}

int main(void) {
    static const size_t sizes[] = {
        1, 4, 8, 16, 32, 64, 128, 256, 512,
        1024, 4096, 16384, 65536, 262144, 1048576
    };

    if (!run_correctness()) {
        return 1;
    }

    printf("correctness: PASS\n");
    printf("%12s %14s %14s %10s\n", "size", "rvv_gbps", "libc_gbps", "speedup");

    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
        size_t size = sizes[i];
        int iters = 200000;
        if (size >= 1024) iters = 50000;
        if (size >= 16384) iters = 5000;
        if (size >= 262144) iters = 500;
        if (size >= 1048576) iters = 100;

        double rvv = bench_one(memcpy_rvv_intrin, size, iters);
        double libc_v = bench_one(memcpy, size, iters);
        double rvv_gbps = rvv < 0.0 ? -1.0 : rvv;
        double libc_gbps = libc_v < 0.0 ? -1.0 : libc_v;
        double speedup = (libc_gbps > 0.0) ? (rvv_gbps / libc_gbps) : 0.0;

        printf("%12zu %14.3f %14.3f %10.3f\n",
               size, rvv_gbps, libc_gbps, speedup);
    }

    return 0;
}
