#include <link.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef GLIBC_MEMCPY_VECTOR_OFFSET
#define GLIBC_MEMCPY_VECTOR_OFFSET 0
#endif
#ifndef GLIBC_MEMCPY_GENERIC_OFFSET
#define GLIBC_MEMCPY_GENERIC_OFFSET 0
#endif

void *memcpy_rvv_intrin(void *dst, const void *src, size_t n);

typedef void *(*memcpy_fn_t)(void *, const void *, size_t);

static uintptr_t libc_base;

static int find_libc_callback(struct dl_phdr_info *info, size_t size, void *data) {
    (void) size;
    (void) data;
    if (info->dlpi_name != NULL && strstr(info->dlpi_name, "libc.so.6") != NULL) {
        libc_base = (uintptr_t) info->dlpi_addr;
        return 1;
    }
    return 0;
}

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

static double bench_one(memcpy_fn_t fn, size_t size, size_t src_align, size_t dst_align, int iters) {
    size_t total = size + 256;
    unsigned char *src_base = aligned_alloc(64, total + 64);
    unsigned char *dst_base = aligned_alloc(64, total + 64);
    if (src_base == NULL || dst_base == NULL) {
        fprintf(stderr, "allocation failed\n");
        free(src_base);
        free(dst_base);
        return -1.0;
    }

    unsigned char *src = src_base + src_align;
    unsigned char *dst = dst_base + dst_align;
    fill_pattern(src_base, total + 64, 11u + (unsigned int) size + (unsigned int) src_align * 17u + (unsigned int) dst_align * 31u);
    memset(dst_base, 0, total + 64);

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

    free(src_base);
    free(dst_base);

    return ((double) size * (double) iters) / (double) (end - start);
}

static void run_case(const char *label, size_t src_align, size_t dst_align) {
    static const size_t sizes[] = {64, 128, 256, 512, 1024, 4096, 16384, 65536, 262144, 1048576};
    memcpy_fn_t glibc_vector = (memcpy_fn_t) (libc_base + GLIBC_MEMCPY_VECTOR_OFFSET);
    memcpy_fn_t glibc_generic = (memcpy_fn_t) (libc_base + GLIBC_MEMCPY_GENERIC_OFFSET);

    printf("case=%s src_align=%zu dst_align=%zu\n", label, src_align, dst_align);
    printf("%12s %12s %12s %12s %12s\n", "size", "rvv", "vector", "generic", "rvv/vector");

    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
        size_t size = sizes[i];
        int iters = 200000;
        if (size >= 1024) iters = 50000;
        if (size >= 16384) iters = 5000;
        if (size >= 262144) iters = 500;
        if (size >= 1048576) iters = 100;

        double rvv = bench_one(memcpy_rvv_intrin, size, src_align, dst_align, iters);
        double vec = bench_one(glibc_vector, size, src_align, dst_align, iters);
        double gen = bench_one(glibc_generic, size, src_align, dst_align, iters);
        double ratio = vec > 0.0 ? rvv / vec : 0.0;
        printf("%12zu %12.3f %12.3f %12.3f %12.3f\n", size, rvv, vec, gen, ratio);
    }

    printf("\n");
}

int main(void) {
    dl_iterate_phdr(find_libc_callback, NULL);
    if (libc_base == 0) {
        fprintf(stderr, "failed to locate libc.so.6 base\n");
        return 1;
    }

    run_case("aligned", 0, 0);
    run_case("same-offset", 1, 1);
    run_case("different-offset", 0, 1);
    return 0;
}
