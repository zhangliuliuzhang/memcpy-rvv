#define _GNU_SOURCE
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void *memset_rvv_intrin(void *dst, int c, size_t n);

static long long now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long) ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static double bench_one(void *(*fn)(void *, int, size_t), size_t size, int iters) {
    size_t total = size + 256;
    unsigned char *dst = aligned_alloc(64, total + 64);
    if (dst == NULL) {
        return -1.0;
    }

    memset(dst, 0xA5, total + 64);

    for (int i = 0; i < 8; ++i) {
        fn(dst + 5, 0, size);
    }

    long long start = now_ns();
    for (int i = 0; i < iters; ++i) {
        fn(dst + 5, 0, size);
    }
    long long end = now_ns();

    volatile unsigned char sink = dst[size ? size - 1 : 0];
    (void) sink;

    free(dst);
    return ((double) size * (double) iters) / (double) (end - start);
}

int main(void) {
    static const size_t sizes[] = {64, 256, 1024, 4096, 16384, 65536};

    printf("memset zero benchmark bytes/ns (higher is better)\n");
    printf("%-8s %-12s %-12s %-12s\n", "size", "rvv_zero", "libc_zero", "rvv/libc");

    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
        size_t size = sizes[i];
        int iters = 200000;
        if (size >= 1024) iters = 50000;
        if (size >= 16384) iters = 5000;
        if (size >= 65536) iters = 1000;

        double rvv = bench_one(memset_rvv_intrin, size, iters);
        double libc = bench_one(memset, size, iters);
        double ratio = libc > 0.0 ? rvv / libc : 0.0;

        printf("%-8zu %-12.3f %-12.3f %-12.3f\n", size, rvv, libc, ratio);
    }

    return 0;
}
