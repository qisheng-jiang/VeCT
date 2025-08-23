#ifndef AVX512_STORE_H
#define AVX512_STORE_H

#include <immintrin.h>

#include "stat.h"

int avx512_masked_load_flush(void *addr, __mmask16 mask, uint64_t *diff, void *dst) {
    int *src = (int *)addr;
    __m512i *data = (__m512i *)dst;
    clean_cache(addr, NUM_LINE);

    data[0] = _mm512_mask_load_epi32(_mm512_setzero_si512(), mask, src);
    // data[1] = _mm512_mask_load_epi32(_mm512_setzero_si512(), mask, src + NUM_INT);
    // data[2] = _mm512_mask_load_epi32(_mm512_setzero_si512(), mask, src + NUM_INT*NUM_LINE/2);
    // data[3] = _mm512_mask_load_epi32(_mm512_setzero_si512(), mask, src + NUM_INT*(NUM_LINE/2+1));

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

int avx512_masked_store_flush(void *addr, __mmask16 mask, uint64_t *diff) {
    int *dst = (int *)addr;

    __m512i data = _mm512_setr_epi32(100, 200, 300, 400, 500, 600, 700, 800, 100, 200, 300, 400, 500, 600, 700, 800);

    clean_cache(addr, NUM_LINE);

    _mm512_mask_store_epi32(dst, mask, data); // 0
    // _mm512_mask_store_epi32(dst + NUM_INT, mask, data); // 1
    // _mm512_mask_store_epi32(dst + NUM_INT*NUM_LINE/2, mask, data); // 4
    // _mm512_mask_store_epi32(dst + NUM_INT*(NUM_LINE/2+1), mask, data); // 5

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

uint64_t avx512_masked_load_random(void *addr, __mmask16 mask, void *dst) {
    int *src = (int *)addr;
    __m512i *data = (__m512i *)dst;
#ifndef CACHE_PERF
    uint64_t start, end;
    int temp = 0;
    _mm_mfence();
    start = __rdtscp(&temp);
#endif // CACHE_PERF
    data[0] = _mm512_mask_load_epi32(_mm512_setzero_si512(), mask, src);
#ifndef CACHE_PERF
    _mm_mfence();
    end = __rdtscp(&temp);

    return end - start;
#else
    return 0;
#endif // CACHE_PERF
}

uint64_t avx512_masked_store_random(void *addr, __mmask16 mask) {
    int *dst = (int *)addr;
    __m512i data = _mm512_setr_epi32(100, 200, 300, 400, 500, 600, 700, 800, 100, 200, 300, 400, 500, 600, 700, 800);
#ifndef CACHE_PERF
    uint64_t start, end;
    int temp = 0;
    _mm_mfence();
    start = __rdtscp(&temp);
#endif // CACHE_PERF
    _mm512_mask_store_epi32(dst, mask, data);
#ifndef CACHE_PERF
    _mm_mfence();
    end = __rdtscp(&temp);

    return end - start;
#else
    return 0;
#endif // CACHE_PERF
}

#endif // AVX512_STORE_H
