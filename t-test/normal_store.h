#ifndef NORMAL_STORE_H
#define NORMAL_STORE_H

#include <immintrin.h>

#include "stat.h"

int normal_masked_load_flush(void *addr, __mmask16 mask, uint64_t *diff, void *dst) {
    int *src = (int *)addr;
    int *data = (int *)dst;
    clean_cache(addr, NUM_LINE);

    if (mask != 0) {
        data[0] = src[0];
        data[1] = src[NUM_INT];
        data[2] = src[NUM_INT*NUM_LINE/2];
        data[3] = src[NUM_INT*(NUM_LINE/2+1)];
    }

    uint64_t start, end;
    int temp = 0;

    for (int64_t i = 0; i < NUM_LINE; i++) {
        _mm_mfence();
        start = __rdtscp(&temp);
        _mm_clflush(addr + i * CACHE_LINE_SIZE);
        _mm_mfence();
        end = __rdtscp(&temp);
        diff[i] = end - start;
    }
    return 0;
}

int normal_masked_store_flush(void *addr, __mmask16 mask, uint64_t *diff) {
    int *dst = (int *)addr;

    int data = 800;

    clean_cache(addr, NUM_LINE);

    if (mask != 0) {
        dst[0] = data;
        dst[NUM_INT] = data;
        dst[NUM_INT*NUM_LINE/2] = data;
        dst[NUM_INT*(NUM_LINE/2+1)] = data;
    }

    uint64_t start, end;
    int temp = 0;

    for (int64_t i = 0; i < NUM_LINE; i++) {
        _mm_mfence();
        start = __rdtscp(&temp);
        _mm_clflush(addr + i * CACHE_LINE_SIZE);
        _mm_mfence();
        end = __rdtscp(&temp);
        diff[i] = end - start;
    }
    return 0;
}

uint64_t normal_masked_load_random(void *addr, __mmask16 mask, void *dst) {
    int *src = (int *)addr;
    int *data = (int *)dst;
#ifndef CACHE_PERF
    uint64_t start, end;
    int temp = 0;
    _mm_mfence();
    start = __rdtscp(&temp);
#endif // CACHE_PERF
    if (mask != 0)
        data[0] = src[0];
#ifndef CACHE_PERF
    _mm_mfence();
    end = __rdtscp(&temp);

    return end - start;
#else
    return 0;
#endif // CACHE_PERF
}

uint64_t normal_masked_store_random(void *addr, __mmask16 mask) {
    int *dst = (int *)addr;
    int data = 800;
#ifndef CACHE_PERF
    uint64_t start, end;
    int temp = 0;
    _mm_mfence();
    start = __rdtscp(&temp);
#endif // CACHE_PERF
    if (mask != 0)
        dst[0] = data;
#ifndef CACHE_PERF
    _mm_mfence();
    end = __rdtscp(&temp);

    return end - start;
#else
    return 0;
#endif // CACHE_PERF
}

#endif // NORMAL_STORE_H
