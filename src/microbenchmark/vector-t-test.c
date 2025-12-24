#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <stdlib.h>
#include <stdint.h>

#include <immintrin.h>
#include <x86intrin.h>

#include "vector-t-test.h"

#define WARMUP_COUNT 0
#define REPEAT_COUNT 200000

#define CACHELINE_NUM ((ARRAY_SIZE * sizeof(TEST_TYPE) + 63) / 64)

static TEST_TYPE __attribute__((annotate("secret"))) array[ARRAY_SIZE] = {0};

unsigned int __attribute__((annotate("secret"))) myindex[UPDATE_SIZE] = {0};

static TEST_TYPE res[UPDATE_SIZE] = {0};

__attribute__((target("clflushopt")))
void clean_cache(void *addr, int64_t size) {
    for (int64_t i = 0; i < size; i += 64) {
        _mm_clflushopt(addr + i);
    }
    _mm_mfence();
}

int update(int _value) {
        #if IS_LOAD == 1
        res[0] = array[myindex[0]];
        #else
        array[myindex[0]] = _value + 0;
        #endif // IS_LOAD
    #if UPDATE_SIZE == 2
        #if IS_LOAD == 1
        res[1] = array[myindex[1]];
        #else
        array[myindex[1]] = _value + 1;
        #endif // IS_LOAD
    #endif // UPDATE_SIZE == 2
    return 0;
}

int main(int argc, char *argv[]) {
    read(0, &myindex, sizeof(unsigned int) * UPDATE_SIZE);
    // read(0, array, sizeof(TEST_TYPE) * ARRAY_SIZE);
    srand(0);

    uint64_t start, end;
    int temp = 0;
    unsigned int upper_bound = ARRAY_SIZE - 1;

    uint64_t exec_times[REPEAT_COUNT] = {0};
    uint64_t *cacheflush_times[CACHELINE_NUM] = {0};
    for (int64_t i = 0; i < CACHELINE_NUM; i++) {
        cacheflush_times[i] = calloc(REPEAT_COUNT, sizeof(uint64_t));
    }
    
    for (int ii = 0; ii < 2; ii++) {
        for (int j = 0; j < UPDATE_SIZE; j++) {
            myindex[j] = (myindex[j] + rand()) % upper_bound;
        }
        int value = rand();
    for (uint64_t i = 0; i < WARMUP_COUNT + REPEAT_COUNT; i++) {
        // if (i % 1000 == 0){
        //     printf("i: %lu, index: %d, value: %d\n", i, myindex[0], value);
        // }
        // execution time 
        clean_cache(array, ARRAY_SIZE * sizeof(TEST_TYPE));
        _mm_mfence();
        start = __rdtscp(&temp);
        update(value);
        _mm_mfence();
        end = __rdtscp(&temp);
        if (i >= WARMUP_COUNT) {
            exec_times[i - WARMUP_COUNT] = end - start;
        }
        // flush cache line
        for (int j = 0; j < CACHELINE_NUM; j++) {
            clean_cache(array, ARRAY_SIZE * sizeof(TEST_TYPE));
            _mm_mfence();
            update(value);
            _mm_mfence();
            unsigned char *addr = (unsigned char *)array;
            if (i >= WARMUP_COUNT) {
                _mm_mfence();
                start = __rdtscp(&temp);
                _mm_clflush(addr + j * 64);
                _mm_mfence();
                cacheflush_times[j][i - WARMUP_COUNT] = __rdtscp(&temp) - start;
            }
        }
    }
    fprintf(stderr, "Run %s\n", __FILE__);
    for (int i_line = 0; i_line < CACHELINE_NUM; i_line++) {
#if IS_LOAD == 1
        printf("load-flush %lu,index %d,Line %d,", sizeof(TEST_TYPE), ii, i_line);
#else
        printf("store-flush %lu,index %d,Line %d,", sizeof(TEST_TYPE), ii, i_line);
#endif
        for (int i = 0; i < REPEAT_COUNT; i++) {
            printf("%ld", cacheflush_times[i_line][i]);
            if (i < REPEAT_COUNT - 1) {
                printf(",");
            }
        }
        printf("\n");
    }
#if IS_LOAD == 1
        printf("load-random %lu,index %d,Line 0,", sizeof(TEST_TYPE), ii);
#else
        printf("store-random %lu,index %d,Line 0,", sizeof(TEST_TYPE), ii);
#endif
    for (int i = 0; i < REPEAT_COUNT; i++) {
        printf("%ld", exec_times[i]);
        if (i < REPEAT_COUNT - 1) {
            printf(",");
        }
    }
    printf("\n");
    }
    
    int ret = 0;
    for (int i = 0; i < UPDATE_SIZE; i++) {
        ret += res[i];
    }
    return ret;
}
