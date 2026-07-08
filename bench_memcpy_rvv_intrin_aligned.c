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

static int check_aligned(size_t size) {
    size_t total = size + 128;
    unsigned char *src = aligned_alloc(64, total);
    unsigned char *dst = aligned_alloc(64, total);
    unsigned char *ref = aligned_alloc(64, total);
    if (src == NULL || dst == NULL || ref == NULL) {
        fprintf(stderr, "allocation failed\n");
        free(src);
        free(dst);
        free(ref);
        return 0;
    }

    fill_pattern(src, total, 123u + (unsigned int) size);
    memset(dst, 0x5A, total);
    memset(ref, 0x5A, total);

    memcpy(ref, src, size);
    memcpy_rvv_intrin(dst, src, size);

    int ok = memcmp(dst, ref, total) == 0;
    if (!ok) {
        fprintf(stderr, "aligned mismatch: size=%zu\n", size);
    }

    free(src);
    free(dst);
    free(ref);
    return ok;
}

static double bench_one(void *(*fn)(void *, const void *, size_t),
                        size_t size,
                        int iters) {
    size_t total = size + 128;
    unsigned char *src = aligned_alloc(64, total);
    unsigned char *dst = aligned_alloc(64, total);
    if (src == NULL || dst == NULL) {
        fprintf(stderr, "allocation failed\n");
        free(src);
        free(dst);
        return -1.0;
    }

    fill_pattern(src, total, 77u + (unsigned int) size);
    memset(dst, 0, total);

    for (int i = 0; i < 8; ++i) {
        fn(dst, src, size);
    }

    long long start = now_ns();
    for (int i = 0; i < iters; ++i) {
        fn(dst, src, size);
    }
    long long end = now_ns();

    volatile unsigned char sink = dst[size ? size - 1 : 0];
    (void) sink;

    free(src);
    free(dst);

    return ((double) size * (double) iters) / (double) (end - start);
}

int main(void) {
    static const size_t sizes[] = {
        1, 4, 8, 16, 32, 64, 128, 256, 512,
        1024, 4096, 16384, 65536, 262144, 1048576
    };

    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
        if (!check_aligned(sizes[i])) {
            return 1;
        }
    }

    printf("correctness_aligned: PASS\n");
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
        double speedup = libc_v > 0.0 ? rvv / libc_v : 0.0;

        printf("%12zu %14.3f %14.3f %10.3f\n", size, rvv, libc_v, speedup);
    }

    return 0;
}
