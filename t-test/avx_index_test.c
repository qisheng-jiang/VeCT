#include <immintrin.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <string.h>

#include "stat.h"

#define NUM_INDEX 11
#define NUM_ONCE 16

#define NUM_ACCESS (NUM_ONCE/2)
#define NUM_LINE NUM_ACCESS 

int avx512_masked_gather_flush(void *addr, int *indices, uint64_t *diff, void *dst) {
    uint64_t start, end;
    int temp = 0;
    int *src = (int *)addr;
    __m512i *data = (__m512i *)dst;

    __mmask16 mask = 0xFFFF; 
    __m512i index_vector = _mm512_loadu_si512((__m512i *)indices);
    for (int64_t i = 0; i < NUM_LINE; i++) {
        clean_cache(addr, NUM_LINE);

        data[0] = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), 
                        mask, index_vector, src, 4);
        
        _mm_mfence();
        start = __rdtscp(&temp);
        _mm_clflush(addr + i * CACHE_LINE_SIZE);
        _mm_mfence();
        end = __rdtscp(&temp);
        diff[i] = end - start;
    }
    return 0;
}

int avx512_masked_scatter_flush(void *addr, int *indices, uint64_t *diff) {
    uint64_t start, end;
    int temp = 0;
    int *dst = (int *)addr;
    __m512i data = _mm512_setr_epi32(100, 200, 300, 400, 500, 600, 700, 800, 100, 200, 300, 400, 500, 600, 700, 800);

    __mmask16 mask = 0xFFFF; 
    __m512i index_vector = _mm512_loadu_si512((__m512i *)indices);
    for (int64_t i = 0; i < NUM_LINE; i++) {
        clean_cache(addr, NUM_LINE);

        _mm512_mask_i32scatter_epi32(
            dst,                        // Destination address
            mask,                       // Mask
            index_vector,               // Index vector
            data,                       // Data to scatter
            4                           // Element size in bytes (int = 4 bytes)
        );

        _mm_mfence();
        start = __rdtscp(&temp);
        _mm_clflush(addr + i * CACHE_LINE_SIZE);
        _mm_mfence();
        end = __rdtscp(&temp);
        diff[i] = end - start;
    }
    return 0;
}

uint64_t avx512_masked_gather_random(void *addr, int *indices, void *dst) {
    int *src = (int *)addr;
    __m512i *data = (__m512i *)dst;

    __mmask16 mask = 0xFFFF; 
    __m512i index_vector = _mm512_loadu_si512((__m512i *)indices);

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

uint64_t avx512_masked_scatter_random(void *addr, int *indices) {
    int *dst = (int *)addr;
    __m512i data = _mm512_setr_epi32(100, 200, 300, 400, 500, 600, 700, 800, 100, 200, 300, 400, 500, 600, 700, 800);

    __mmask16 mask = 0xFFFF;
    __m512i index_vector = _mm512_loadu_si512((__m512i *)indices);

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

int main(int argc, char* argv[]) {
    uint64_t *time_list[NUM_LINE] = {0};
    for (int64_t i = 0; i < NUM_LINE; i++) {
        time_list[i] = calloc(NUM_ITERATIONS, sizeof(uint64_t));
    }

    void *addr = NULL;
    __m512i loaded_data[4] = {0};
    int avx_indices[NUM_INDEX][NUM_ONCE] = {
        0, 1, 16, 17, 32, 33, 48, 49, 64, 65, 80, 81, 96, 97, 112, 113, 
        0, 1, 16, 20, 32, 33, 48, 49, 64, 65, 80, 81, 96, 97, 112, 113, 
        2, 3, 18, 19, 34, 35, 50, 51, 66, 67, 82, 83, 98, 99, 114, 115, 
        0, 0, 16, 17, 32, 32, 48, 49, 64, 64, 80, 81, 96, 96, 112, 113, 

        0, 1, 10, 17, 32, 33, 40, 49, 64, 65, 70, 81, 96, 97, 100, 113, 
        0, 1, 15, 17, 32, 33, 40, 49, 64, 65, 70, 81, 96, 97, 100, 113, 
        2, 3, 12, 19, 34, 35, 42, 51, 66, 67, 72, 83, 98, 99, 102, 115, 
        0, 0, 0, 17, 32, 32, 32, 49, 64, 64, 64, 81, 96, 96, 96, 113, 

        0, 1, 10, 11, 32, 33, 40, 41, 64, 65, 70, 71, 96, 97, 100, 101, 
        0, 1, 2, 8, 9, 10, 11, 12, 64, 65, 66, 70, 71, 72, 73, 74, 
        0, 1, 2, 8, 9, 10, 11, 12, 96, 97, 100, 101, 102, 103, 104, 105
    };

    for (int64_t i = 0; i < NUM_INDEX; i++) {
        printf("Index %d: ", i);
        for (int64_t j = 0; j < NUM_ONCE; j++) {
            printf("%d ", avx_indices[i][j]);
        }
        printf("\n");
    }

#ifdef CACHE_PERF
    int mask_index = argc > 1 ? atoi(argv[1]) : 0;
    int is_store = argc > 2 ? atoi(argv[2]) : 0;
    addr = aligned_alloc(CACHE_LINE_SIZE, 
        CACHE_LINE_SIZE * (NUM_ITERATIONS + NUM_ACCESS));
    memset(addr, INIT_VALUE, CACHE_LINE_SIZE * (NUM_ITERATIONS + NUM_ACCESS));
    clean_cache(addr, NUM_ITERATIONS + NUM_ACCESS);

    srand(0);
    if (is_store != 0) {
        for (int64_t i = 0; i < NUM_ITERATIONS; i++) {
            void *access_addr = addr + rand() % NUM_ITERATIONS * CACHE_LINE_SIZE;
            avx512_masked_scatter_random(access_addr, avx_indices[mask_index]);
        }
        printf("Mask store Random for perf with index %d: ", mask_index);
    } else {
        for (int64_t i = 0; i < NUM_ITERATIONS; i++) {
            void *access_addr = addr + rand() % NUM_ITERATIONS * CACHE_LINE_SIZE;
            avx512_masked_gather_random(access_addr, avx_indices[mask_index], loaded_data);
        }
        printf("Mask load Random for perf with index %d: ", mask_index);
    }
    
    clean_cache(addr, NUM_ITERATIONS + NUM_ACCESS);
    free(addr);

#else

    addr = aligned_alloc(CACHE_LINE_SIZE, 
        CACHE_LINE_SIZE * NUM_LINE);

    uint64_t res_diff[NUM_LINE] = {0};
    for (int i_mask = 0; i_mask < NUM_INDEX; i_mask++) {
        for (int64_t i_ex = 0; i_ex < NUM_ITERATIONS; i_ex++) {
            memset(addr, INIT_VALUE, CACHE_LINE_SIZE * NUM_LINE);
            avx512_masked_scatter_flush(addr, avx_indices[i_mask], res_diff);
            for (int i_line = 0; i_line < NUM_LINE; i_line++) {
                time_list[i_line][i_ex] = res_diff[i_line];
            }
        }
        for (int i_line = 0; i_line < NUM_LINE; i_line++) {
            printf("store-flush,Index %d,Line %d,", i_mask, i_line);
            summary(time_list[i_line]);
        }
    }

    for (int i_mask = 0; i_mask < NUM_INDEX; i_mask++) {
        for (int64_t i_ex = 0; i_ex < NUM_ITERATIONS; i_ex++) {
            memset(addr, INIT_VALUE, CACHE_LINE_SIZE * NUM_LINE);
            avx512_masked_gather_flush(addr, avx_indices[i_mask], res_diff, loaded_data);
            for (int i_line = 0; i_line < NUM_LINE; i_line++) {
                time_list[i_line][i_ex] = res_diff[i_line];
            }
        }
        for (int i_line = 0; i_line < NUM_LINE; i_line++) {
            printf("load-flush,Index %d,Line %d,", i_mask, i_line);
            summary(time_list[i_line]);
        }
    }

    free(addr);

    addr = aligned_alloc(CACHE_LINE_SIZE, 
        CACHE_LINE_SIZE * (NUM_ITERATIONS + NUM_ACCESS));
    memset(addr, INIT_VALUE, CACHE_LINE_SIZE * (NUM_ITERATIONS + NUM_ACCESS));

    for (int i_mask = 0; i_mask < NUM_INDEX; i_mask++) {
        /*clean_cache(addr, NUM_ITERATIONS + NUM_ACCESS);
        for (int64_t i = 0; i < NUM_ITERATIONS; i++) {
            void *access_addr = addr + i * CACHE_LINE_SIZE;
            time_list[0][i] = avx512_masked_scatter_random(access_addr, avx_indices[i_mask]);
        }
        printf("store-sequential,Index %d,Line 0,", i_mask);
        summary(time_list[0]);

        clean_cache(addr, NUM_ITERATIONS + NUM_ACCESS);
        for (int64_t i = NUM_ITERATIONS - 1; i >= 0; i--) {
            void *access_addr = addr + i * CACHE_LINE_SIZE;
            time_list[0][i] = avx512_masked_scatter_random(access_addr, avx_indices[i_mask]);
        }
        printf("store-reverse,Index %d,Line 0,", i_mask);
        summary(time_list[0]);*/

        srand(0);
        clean_cache(addr, NUM_ITERATIONS + NUM_ACCESS);
        for (int64_t i = 0; i < NUM_ITERATIONS; i++) {
            void *access_addr = addr + rand() % NUM_ITERATIONS * CACHE_LINE_SIZE;
            time_list[0][i] = avx512_masked_scatter_random(access_addr, avx_indices[i_mask]);
        }
        printf("store-random,Index %d,Line 0,", i_mask);
        summary(time_list[0]);
    }

    for (int i_mask = 0; i_mask < NUM_INDEX; i_mask++) {
        /*clean_cache(addr, NUM_ITERATIONS + NUM_ACCESS);
        for (int64_t i = 0; i < NUM_ITERATIONS; i++) {
            void *access_addr = addr + i * CACHE_LINE_SIZE;
            time_list[0][i] = avx512_masked_gather_random(access_addr, avx_indices[i_mask], loaded_data);
        }
        printf("load-sequential,Index %d,Line 0,", i_mask);
        summary(time_list[0]);

        clean_cache(addr, NUM_ITERATIONS + NUM_ACCESS);
        for (int64_t i = NUM_ITERATIONS - 1; i >= 0; i--) {
            void *access_addr = addr + i * CACHE_LINE_SIZE;
            time_list[0][i] = avx512_masked_gather_random(access_addr, avx_indices[i_mask], loaded_data);
        }
        printf("load-reverse,Index %d,Line 0,", i_mask);
        summary(time_list[0]);*/

        srand(0);
        clean_cache(addr, NUM_ITERATIONS + NUM_ACCESS);
        for (int64_t i = 0; i < NUM_ITERATIONS; i++) {
            void *access_addr = addr + rand() % NUM_ITERATIONS * CACHE_LINE_SIZE;
            time_list[0][i] = avx512_masked_gather_random(access_addr, avx_indices[i_mask], loaded_data);
        }
        printf("load-random,Index %d,Line 0,", i_mask);
        summary(time_list[0]);
    }

    free(addr);

#endif // CACHE_PERF

    for (int64_t i = 0; i < NUM_LINE; i++) {
        free(time_list[i]);
    }
    return 0;
}
