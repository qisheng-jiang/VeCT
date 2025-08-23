#include <immintrin.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <string.h>

#include "stat.h"

#include "avx512_store.h"
#include "avx512_scatter.h"
#include "normal_store.h"

typedef int (*load_flush_t)(void *addr, __mmask16 mask, uint64_t *diff, void *dst);
typedef int (*store_flush_t)(void *addr, __mmask16 mask, uint64_t *diff);
typedef uint64_t (*load_random_t)(void *addr, __mmask16 mask, void *dst);
typedef uint64_t (*store_random_t)(void *addr, __mmask16 mask);

load_flush_t load_flush[] = {avx512_masked_load_flush, avx512_masked_gather_flush, normal_masked_load_flush};
store_flush_t store_flush[] = {avx512_masked_store_flush, avx512_masked_scatter_flush, normal_masked_store_flush};
load_random_t load_random[] = {avx512_masked_load_random, avx512_masked_gather_random, normal_masked_load_random};
store_random_t store_random[] = {avx512_masked_store_random, avx512_masked_scatter_random, normal_masked_store_random};

#if VMOV == 0
    #define FUNC_INDEX 2
    #define NUM_ACCESS 0
#elif VMOV == 1
    #define FUNC_INDEX 0
    #define NUM_ACCESS 0
#else
    #define FUNC_INDEX 1
    #define NUM_ACCESS (NUM_ONCE/2)
    #define NUM_LINE NUM_ACCESS
#endif // VMOV

int main(int argc, char* argv[]) {
    uint64_t *time_list[NUM_LINE] = {0};
    for (int64_t i = 0; i < NUM_LINE; i++) {
        time_list[i] = calloc(NUM_ITERATIONS, sizeof(uint64_t));
    }

    void *addr = NULL;

#if VMOV == 0
    int loaded_data[4] = {0};
#else
    __m512i loaded_data[4] = {0};
#endif // VMOV

    __mmask16 masks[NUM_MASK] = {
        0b1111111111111111,
        // 0b1111110110111011,
        0b0101010101010101,
        0b0101100110011001,
        0b0101010100000000,
        0b0100010001000100,
        // 0b0100100001000010,
        // 0b0111000000010000, 
        0b0100001000000100,
        0b0110000000000000,
        0b1010000000000000,
        0b0000000100000000,
        0b0000001000000000,
        0b0100000000000000,
        0b1000000000000000, 
        0b0000000000000000
    };
    // __mmask16 masks[NUM_MASK] = {0b1111111111111111, 
    //     0b0101010101010101, 
    //     0b0101010100000000, 
    //     0b0100000000000000, 
    //     0b0000000000000000};

// test cache perf for instructions
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
            store_random[FUNC_INDEX](access_addr, masks[mask_index]);
        }
        printf("Mask store %x Random for perf:\n", masks[mask_index]);
    } else {
        for (int64_t i = 0; i < NUM_ITERATIONS; i++) {
            void *access_addr = addr + rand() % NUM_ITERATIONS * CACHE_LINE_SIZE;
            load_random[FUNC_INDEX](access_addr, masks[mask_index], loaded_data);
        }
        printf("Mask load %x Random for perf:\n", masks[mask_index]);
    }
    
    clean_cache(addr, NUM_ITERATIONS + NUM_ACCESS);
    free(addr);

#else

    addr = aligned_alloc(CACHE_LINE_SIZE, 
        CACHE_LINE_SIZE * NUM_LINE);
    int num_line_print = NUM_LINE;
    #if VMOV == 1 || (VMOV == 2 && defined(ONELINE))
        num_line_print = 1;
    #endif

    uint64_t res_diff[NUM_LINE] = {0};
    for (int i_mask = 0; i_mask < NUM_MASK; i_mask++) {
        for (int64_t i_ex = 0; i_ex < NUM_ITERATIONS; i_ex++) {
            memset(addr, INIT_VALUE, CACHE_LINE_SIZE * NUM_LINE);
            store_flush[FUNC_INDEX](addr, masks[i_mask], res_diff);
            for (int i_line = 0; i_line < num_line_print; i_line++) {
                time_list[i_line][i_ex] = res_diff[i_line];
            }
        }
        for (int i_line = 0; i_line < num_line_print; i_line++) {
            printf("store-flush,Mask %x,Line %d,", masks[i_mask], i_line);
            summary(time_list[i_line]);
        }
    }

    for (int i_mask = 0; i_mask < NUM_MASK; i_mask++) {
        for (int64_t i_ex = 0; i_ex < NUM_ITERATIONS; i_ex++) {
            memset(addr, INIT_VALUE, CACHE_LINE_SIZE * NUM_LINE);
            load_flush[FUNC_INDEX](addr, masks[i_mask], res_diff, loaded_data);
            for (int i_line = 0; i_line < num_line_print; i_line++) {
                time_list[i_line][i_ex] = res_diff[i_line];
            }
        }
        for (int i_line = 0; i_line < num_line_print; i_line++) {
            printf("load-flush,Mask %x,Line %d,", masks[i_mask], i_line);
            summary(time_list[i_line]);
        }
    }

    free(addr);

    addr = aligned_alloc(CACHE_LINE_SIZE, 
        CACHE_LINE_SIZE * (NUM_ITERATIONS + NUM_ACCESS));
    memset(addr, INIT_VALUE, CACHE_LINE_SIZE * (NUM_ITERATIONS + NUM_ACCESS));

    for (int i_mask = 0; i_mask < NUM_MASK; i_mask++) {
        srand(0);
        clean_cache(addr, NUM_ITERATIONS + NUM_ACCESS);
        for (int64_t i = 0; i < NUM_ITERATIONS; i++) {
            void *access_addr = addr + rand() % NUM_ITERATIONS * CACHE_LINE_SIZE;
            time_list[0][i] = store_random[FUNC_INDEX](access_addr, masks[i_mask]);
        }
        printf("store-random,Mask %x,Line 0,", masks[i_mask]);
        summary(time_list[0]);
    }

    for (int i_mask = 0; i_mask < NUM_MASK; i_mask++) {
        srand(0);
        clean_cache(addr, NUM_ITERATIONS + NUM_ACCESS);
        for (int64_t i = 0; i < NUM_ITERATIONS; i++) {
            void *access_addr = addr + rand() % NUM_ITERATIONS * CACHE_LINE_SIZE;
            time_list[0][i] = load_random[FUNC_INDEX](access_addr, masks[i_mask], loaded_data);
        }
        printf("load-random,Mask %x,Line 0,", masks[i_mask]);
        summary(time_list[0]);
    }

    free(addr);

#endif // CACHE_PERF

    for (int64_t i = 0; i < NUM_LINE; i++) {
        free(time_list[i]);
    }
    return 0;
}
