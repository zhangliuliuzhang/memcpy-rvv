#define _GNU_SOURCE
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void *memcpy_rvv_intrin(void *dst, const void *src, size_t n);
void *memmove_rvv_intrin(void *dst, const void *src, size_t n);
void *memset_rvv_intrin(void *dst, int c, size_t n);
void *memchr_rvv_intrin(const void *src, int c, size_t n);
int memcmp_rvv_intrin(const void *s1, const void *s2, size_t n);

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

static int sign3(int x) {
    return (x > 0) - (x < 0);
}

static int test_memcpy_one(size_t size, size_t src_align, size_t dst_align) {
    size_t total = size + 128;
    unsigned char *src_base = aligned_alloc(64, total + 64);
    unsigned char *dst_base = aligned_alloc(64, total + 64);
    unsigned char *ref_base = aligned_alloc(64, total + 64);
    if (src_base == NULL || dst_base == NULL || ref_base == NULL) {
        fprintf(stderr, "allocation failed in memcpy test\n");
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
        fprintf(stderr, "memcpy mismatch: size=%zu src_align=%zu dst_align=%zu\n",
                size, src_align, dst_align);
    }

    free(src_base);
    free(dst_base);
    free(ref_base);
    return ok;
}

static int test_memmove_one(size_t size, size_t src_off, size_t dst_off) {
    size_t total = size + 256;
    unsigned char *buf1 = aligned_alloc(64, total + 64);
    unsigned char *buf2 = aligned_alloc(64, total + 64);
    if (buf1 == NULL || buf2 == NULL) {
        fprintf(stderr, "allocation failed in memmove test\n");
        return 0;
    }

    fill_pattern(buf1, total + 64, 11u + (unsigned int) size);
    memcpy(buf2, buf1, total + 64);

    memmove(buf1 + dst_off, buf1 + src_off, size);
    memmove_rvv_intrin(buf2 + dst_off, buf2 + src_off, size);

    int ok = memcmp(buf1, buf2, total + 64) == 0;
    if (!ok) {
        fprintf(stderr, "memmove mismatch: size=%zu src_off=%zu dst_off=%zu\n",
                size, src_off, dst_off);
    }

    free(buf1);
    free(buf2);
    return ok;
}

static int test_memset_one(size_t size, size_t dst_align, int c) {
    size_t total = size + 128;
    unsigned char *buf1 = aligned_alloc(64, total + 64);
    unsigned char *buf2 = aligned_alloc(64, total + 64);
    if (buf1 == NULL || buf2 == NULL) {
        fprintf(stderr, "allocation failed in memset test\n");
        return 0;
    }

    fill_pattern(buf1, total + 64, 21u + (unsigned int) size);
    memcpy(buf2, buf1, total + 64);

    memset(buf1 + dst_align, c, size);
    memset_rvv_intrin(buf2 + dst_align, c, size);

    int ok = memcmp(buf1, buf2, total + 64) == 0;
    if (!ok) {
        fprintf(stderr, "memset mismatch: size=%zu dst_align=%zu c=%d\n",
                size, dst_align, c);
    }

    free(buf1);
    free(buf2);
    return ok;
}

static int test_memchr_one(size_t size, size_t align, int c, size_t hit_pos, int miss) {
    size_t total = size + 128;
    unsigned char *buf = aligned_alloc(64, total + 64);
    if (buf == NULL) {
        fprintf(stderr, "allocation failed in memchr test\n");
        return 0;
    }

    fill_pattern(buf, total + 64, 31u + (unsigned int) size);
    unsigned char *p = buf + align;
    if (!miss && hit_pos < size) {
        p[hit_pos] = (unsigned char) c;
    } else {
        for (size_t i = 0; i < size; ++i) {
            if (p[i] == (unsigned char) c) {
                p[i] ^= 0x5Au;
            }
        }
    }

    void *ref = memchr(p, c, size);
    void *got = memchr_rvv_intrin(p, c, size);
    int ok = ref == got;
    if (!ok) {
        fprintf(stderr,
                "memchr mismatch: size=%zu align=%zu c=%d hit_pos=%zu miss=%d\n",
                size, align, c, hit_pos, miss);
    }

    free(buf);
    return ok;
}

static int test_memcmp_one(size_t size, size_t a1, size_t a2, size_t diff_pos, int kind) {
    size_t total = size + 128;
    unsigned char *buf1 = aligned_alloc(64, total + 64);
    unsigned char *buf2 = aligned_alloc(64, total + 64);
    if (buf1 == NULL || buf2 == NULL) {
        fprintf(stderr, "allocation failed in memcmp test\n");
        return 0;
    }

    fill_pattern(buf1, total + 64, 41u + (unsigned int) size);
    memcpy(buf2, buf1, total + 64);
    unsigned char *p1 = buf1 + a1;
    unsigned char *p2 = buf2 + a2;
    if (kind != 0 && diff_pos < size) {
        p2[diff_pos] = (unsigned char) (p1[diff_pos] + kind);
    } else if (a1 != a2) {
        memcpy(p2, p1, size);
    }

    int ref = sign3(memcmp(p1, p2, size));
    int got = sign3(memcmp_rvv_intrin(p1, p2, size));
    int ok = ref == got;
    if (!ok) {
        fprintf(stderr,
                "memcmp mismatch: size=%zu a1=%zu a2=%zu diff_pos=%zu kind=%d ref=%d got=%d\n",
                size, a1, a2, diff_pos, kind, ref, got);
    }

    free(buf1);
    free(buf2);
    return ok;
}

static int run_correctness(void) {
    static const size_t sizes[] = {
        0, 1, 2, 3, 4, 5, 7, 8, 9, 15, 16, 17, 31, 32, 33, 63, 64, 65,
        127, 128, 129, 255, 256, 257, 511, 512, 513, 1024, 4096, 65536
    };

    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
        size_t n = sizes[i];
        for (size_t sa = 0; sa < 8; ++sa) {
            for (size_t da = 0; da < 8; ++da) {
                if (!test_memcpy_one(n, sa, da)) return 0;
                if (!test_memset_one(n, da, 0x00)) return 0;
                if (!test_memset_one(n, da, 0x5A)) return 0;
                if (!test_memcmp_one(n, sa, da, n ? (n / 2) : 0, 0)) return 0;
                if (!test_memcmp_one(n, sa, da, n ? (n / 2) : 0, +1)) return 0;
                if (!test_memcmp_one(n, sa, da, n ? (n / 2) : 0, -1)) return 0;
                if (!test_memchr_one(n, sa, 0x7B, n ? (n / 2) : 0, 0)) return 0;
                if (!test_memchr_one(n, sa, 0xEE, 0, 1)) return 0;
            }
        }

        if (!test_memmove_one(n, 0, 0)) return 0;
        if (!test_memmove_one(n, 0, 1)) return 0;
        if (!test_memmove_one(n, 1, 0)) return 0;
        if (!test_memmove_one(n, 4, 12)) return 0;
        if (!test_memmove_one(n, 12, 4)) return 0;
        if (!test_memmove_one(n, 8, 8)) return 0;
    }

    return 1;
}

static double bench_copy_like(void *(*fn)(void *, const void *, size_t), size_t size, int iters) {
    size_t total = size + 256;
    unsigned char *src = aligned_alloc(64, total + 64);
    unsigned char *dst = aligned_alloc(64, total + 64);
    if (src == NULL || dst == NULL) return -1.0;
    fill_pattern(src, total + 64, 51u + (unsigned int) size);
    memset(dst, 0, total + 64);
    long long start = now_ns();
    for (int i = 0; i < iters; ++i) fn(dst + 5, src + 3, size);
    long long end = now_ns();
    free(src);
    free(dst);
    return ((double) size * (double) iters) / (double) (end - start);
}

static double bench_memset_like(void *(*fn)(void *, int, size_t), size_t size, int iters) {
    size_t total = size + 256;
    unsigned char *dst = aligned_alloc(64, total + 64);
    if (dst == NULL) return -1.0;
    long long start = now_ns();
    for (int i = 0; i < iters; ++i) fn(dst + 5, 0x5A, size);
    long long end = now_ns();
    free(dst);
    return ((double) size * (double) iters) / (double) (end - start);
}

static double bench_memchr_like(void *(*fn)(const void *, int, size_t), size_t size, int iters) {
    size_t total = size + 256;
    unsigned char *src = aligned_alloc(64, total + 64);
    if (src == NULL) return -1.0;
    fill_pattern(src, total + 64, 61u + (unsigned int) size);
    src[3 + size - 1] = 0xA5;
    long long start = now_ns();
    for (int i = 0; i < iters; ++i) fn(src + 3, 0xA5, size);
    long long end = now_ns();
    free(src);
    return ((double) size * (double) iters) / (double) (end - start);
}

static double bench_memcmp_like(int (*fn)(const void *, const void *, size_t), size_t size, int iters) {
    size_t total = size + 256;
    unsigned char *a = aligned_alloc(64, total + 64);
    unsigned char *b = aligned_alloc(64, total + 64);
    if (a == NULL || b == NULL) return -1.0;
    fill_pattern(a, total + 64, 71u + (unsigned int) size);
    memcpy(b, a, total + 64);
    b[3 + size - 1] ^= 1u;
    long long start = now_ns();
    for (int i = 0; i < iters; ++i) fn(a + 3, b + 5, size);
    long long end = now_ns();
    free(a);
    free(b);
    return ((double) size * (double) iters) / (double) (end - start);
}

static void run_perf(void) {
    static const size_t sizes[] = {64, 256, 1024, 4096, 16384, 65536};
    printf("performance bytes/ns (higher is better)\n");
    printf("%-8s %-12s %-12s %-12s\n", "size", "func", "rvv", "libc");

    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
        size_t n = sizes[i];
        int iters = n >= 16384 ? 5000 : 50000;

        printf("%-8zu %-12s %-12.3f %-12.3f\n", n, "memcpy",
               bench_copy_like(memcpy_rvv_intrin, n, iters),
               bench_copy_like(memcpy, n, iters));
        printf("%-8zu %-12s %-12.3f %-12.3f\n", n, "memmove",
               bench_copy_like(memmove_rvv_intrin, n, iters),
               bench_copy_like(memmove, n, iters));
        printf("%-8zu %-12s %-12.3f %-12.3f\n", n, "memset",
               bench_memset_like(memset_rvv_intrin, n, iters),
               bench_memset_like(memset, n, iters));
        printf("%-8zu %-12s %-12.3f %-12.3f\n", n, "memchr",
               bench_memchr_like(memchr_rvv_intrin, n, iters),
               bench_memchr_like(memchr, n, iters));
        printf("%-8zu %-12s %-12.3f %-12.3f\n", n, "memcmp",
               bench_memcmp_like(memcmp_rvv_intrin, n, iters),
               bench_memcmp_like(memcmp, n, iters));
    }
}

int main(void) {
    if (!run_correctness()) {
        return 1;
    }

    printf("correctness: PASS\n");
    run_perf();
    return 0;
}
