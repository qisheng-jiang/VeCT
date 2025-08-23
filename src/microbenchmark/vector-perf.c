#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <stdlib.h>
#include <stdint.h>

#include <immintrin.h>
#include <x86intrin.h>

#include "vector-perf.h"

#define WARMUP_COUNT 100 // 00
#define REPEAT_COUNT 10000 // 00

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
        res[1] = array[myindex[1]];
        #else
        array[myindex[0]] = _value + 0; // 2
        array[myindex[1]] = _value + 1; // 17 
                                            // now 2, 17, 32, 16 * 3, ..., 16 * (8-2+1-1), 0
                                            // now 2, 2, 16, 32, 16 * 3, ..., 16 * (8-2+1-1)
                                            // X new 0, 2, 16, 17, 32, 32, ....
        #endif // IS_LOAD
    #if UPDATE_SIZE >= 4
        #if IS_LOAD == 1
        res[2] = array[myindex[2]];
        res[3] = array[myindex[3]];
        #else
        array[myindex[2]] = _value + 2;
        array[myindex[3]] = _value + 3;
        #endif // IS_LOAD
    #endif // UPDATE_SIZE == 4
    #if UPDATE_SIZE >= 6
        #if IS_LOAD == 1
        res[4] = array[myindex[4]];
        res[5] = array[myindex[5]];
        #else
        array[myindex[4]] = _value + 4;
        array[myindex[5]] = _value + 5;
        #endif // IS_LOAD
    #endif // UPDATE_SIZE == 6
    #if UPDATE_SIZE >= 7
        #if IS_LOAD == 1
        res[6] = array[myindex[6]];
        #else
        array[myindex[6]] = _value + 6;
        #endif // IS_LOAD
    #endif // UPDATE_SIZE == 7
    #if UPDATE_SIZE >= 8
        #if IS_LOAD == 1
        res[7] = array[myindex[7]];
        #else
        array[myindex[7]] = _value + 7;
        #endif // IS_LOAD
    #endif // UPDATE_SIZE == 8
    #if UPDATE_SIZE >= 10
        #if IS_LOAD == 1
        res[8] = array[myindex[8]];
        res[9] = array[myindex[9]];
        #else
        array[myindex[8]] = _value + 8;
        array[myindex[9]] = _value + 9;
        #endif // IS_LOAD
    #endif // UPDATE_SIZE == 10
    #if UPDATE_SIZE >= 12
        #if IS_LOAD == 1
        res[10] = array[myindex[10]];
        res[11] = array[myindex[11]];
        #else
        array[myindex[10]] = _value + 10;
        array[myindex[11]] = _value + 11;
        #endif // IS_LOAD
    #endif // UPDATE_SIZE == 12
    #if UPDATE_SIZE >= 14
        #if IS_LOAD == 1
        res[12] = array[myindex[12]];
        res[13] = array[myindex[13]];
        #else
        array[myindex[12]] = _value + 12;
        array[myindex[13]] = _value + 13;
        #endif // IS_LOAD
    #endif // UPDATE_SIZE == 14
    #if UPDATE_SIZE >= 15
        #if IS_LOAD == 1
        res[14] = array[myindex[14]];
        #else
        array[myindex[14]] = _value + 14;
        #endif // IS_LOAD
    #endif // UPDATE_SIZE == 15
    return 0;
}

int main(int argc, char *argv[]) {
    read(0, &myindex, sizeof(unsigned int) * UPDATE_SIZE);
    // read(0, array, sizeof(TEST_TYPE) * ARRAY_SIZE);
    srand(0);

    uint64_t start, end;
    uint64_t cpu_time_used = 0;
    int temp = 0;
    unsigned int upper_bound = ARRAY_SIZE - 1;
    
    for (uint64_t i = 0; i < WARMUP_COUNT + REPEAT_COUNT; i++) {
        for (int j = 0; j < UPDATE_SIZE; j++) {
            myindex[j] = (myindex[j] + rand()) % upper_bound;
        }
        int value = rand();
        // if (i % 1000 == 0){
        //     printf("i: %lu, index: %d, value: %d\n", i, myindex[0], value);
        // }
        clean_cache(array, ARRAY_SIZE * sizeof(TEST_TYPE));
        _mm_mfence();
        start = __rdtscp(&temp);
        update(value);
        _mm_mfence();
        end = __rdtscp(&temp);
        cpu_time_used += ( i < WARMUP_COUNT ? 0 : (end - start));
    }
    fprintf(stderr, "Elapsed time in %s: %lu\n", __FILE__, cpu_time_used);
    // fprintf(stderr, "%lu", cpu_time_used);

    // for (int i = 0; i < UPDATE_SIZE; i++) {
    //     printf("res[%d] = %d\n", i, res[i]);
    // }
    int ret = 0;
    for (int i = 0; i < UPDATE_SIZE; i++) {
        ret += res[i];
    }
    // for (int i = 0; i <= 2; i++) {
    //     printf("array[%d] = %d\n", i, array[i]);
    // }
    return ret;
}
