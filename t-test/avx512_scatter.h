#ifndef AVX512_SCATTER_H
#define AVX512_SCATTER_H

#include <immintrin.h>

#include "stat.h"

#define NUM_ONCE 16

int avx512_masked_gather_flush(void *addr, __mmask16 mask, uint64_t *diff, void *dst) {
    int *src = (int *)addr;
    __m512i *data = (__m512i *)dst;
#ifdef ONELINE
    __m512i index_vector = _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
#else
    int indices[NUM_ONCE] = {0};
    for (int64_t i = 0; i < NUM_ONCE; i++) {
        indices[i] = NUM_ONCE * (i / 2) + i % 2;
    }
    __m512i index_vector = _mm512_loadu_si512((__m512i *)indices);
#endif // ONELINE
    clean_cache(addr, NUM_LINE);

    data[0] = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), 
                        mask, index_vector, src, 4);

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

int avx512_masked_scatter_flush(void *addr, __mmask16 mask, uint64_t *diff) {
    int *dst = (int *)addr;

    __m512i data = _mm512_setr_epi32(100, 200, 300, 400, 500, 600, 700, 800, 100, 200, 300, 400, 500, 600, 700, 800);
#ifdef ONELINE
    __m512i index_vector = _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
#else
    int indices[NUM_ONCE] = {0};
    for (int64_t i = 0; i < NUM_ONCE; i++) {
        indices[i] = NUM_ONCE * (i / 2) + i % 2;
    }
    // 0, 1, 16, 17, 32, 33, 48, 49, 
    __m512i index_vector = _mm512_loadu_si512((__m512i *)indices);
#endif // ONELINE
    clean_cache(addr, NUM_LINE);

    _mm512_mask_i32scatter_epi32(
        dst,                        // Destination address
        mask,                       // Mask
        index_vector,               // Index vector
        data,                       // Data to scatter
        4                           // Element size in bytes (int = 4 bytes)
    );

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

uint64_t avx512_masked_gather_random(void *addr, __mmask16 mask, void *dst) {
    int *src = (int *)addr;
    __m512i *data = (__m512i *)dst;
#ifdef ONELINE
    __m512i index_vector = _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    // 0, 16, 32, 48, 64, 80, 96, 112, ... first element in each cache line 
    // 1, 17, 33, 49, 65, 81, 97, 113, ... second element in each cache line
    // 0, 1, 16, 17, 32, 33, 48, 49 first and second element in each cache line
#else
    int indices[16] = {0};
    for (int64_t i = 0; i < 16; i++) {
        indices[i] = NUM_ONCE * (i / 2) + i % 2;
        // printf("indices: %lld: %lld %lld %lld\n", i, indices[i], indices[i]*sizeof(int)/64, indices[i]*sizeof(int)%64);
    }
    // for (int64_t i = 0; i < 16; i++) {
    //     indices[i] = i * (NUM_INT * 2 / 16);
    // }
    __m512i index_vector = _mm512_loadu_si512((__m512i *)indices);
#endif // ONELINE
#ifndef CACHE_PERF
    uint64_t start, end;
    int temp = 0;
    _mm_mfence();
    start = __rdtscp(&temp);
#endif // CACHE_PERF
    data[0] = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), 
                    mask, index_vector, src, 4);
#ifndef CACHE_PERF
    _mm_mfence();
    end = __rdtscp(&temp);

    return end - start;
#else
    return 0;
#endif // CACHE_PERF
}

uint64_t avx512_masked_scatter_random(void *addr, __mmask16 mask) {
    int *dst = (int *)addr;
    __m512i data = _mm512_setr_epi32(100, 200, 300, 400, 500, 600, 700, 800, 100, 200, 300, 400, 500, 600, 700, 800);
#ifdef ONELINE
    __m512i index_vector = _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
#else
    int indices[16] = {0};
    for (int64_t i = 0; i < 16; i++) {
        indices[i] = NUM_ONCE * (i / 2) + i % 2;
    }
    // for (int64_t i = 0; i < 16; i++) {
    //     indices[i] = i * (NUM_INT * 2 / 16);
    // }
    __m512i index_vector = _mm512_loadu_si512((__m512i *)indices);
#endif // ONELINE
#ifndef CACHE_PERF
    uint64_t start, end;
    int temp = 0;
    _mm_mfence();
    start = __rdtscp(&temp);
#endif // CACHE_PERF
    _mm512_mask_i32scatter_epi32(
        dst,                        // Destination address
        mask,                       // Mask
        index_vector,               // Index vector
        data,                       // Data to scatter
        4                           // Element size in bytes (int = 4 bytes)
    );
#ifndef CACHE_PERF
    _mm_mfence();
    end = __rdtscp(&temp);

    return end - start;
#else
    return 0;
#endif // CACHE_PERF
}

#endif // AVX512_SCATTER_H