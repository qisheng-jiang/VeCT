#define _GNU_SOURCE

#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <sched.h>

#include <immintrin.h>
#include <x86intrin.h>

#include "vector-t-test-false-depen.h"

static inline uint64_t rdtscp() {
    unsigned int aux;
    _mm_mfence();
    return __rdtscp(&aux);
}

#define WARMUP_COUNT 0
#define REPEAT_COUNT 200000

#define CPU_CORE 1
#define PAGE_SIZE 4096
#define CACHE_LINE_SIZE 64
#define OFFSET_SIZE sizeof(TEST_TYPE)
#define B_SIZE 1
int B_Offset[B_SIZE] = {0};

volatile bool is_loop = true;

#define ARRAY_SIZE 100 // (PAGE_SIZE * 10 / sizeof(TEST_TYPE))

volatile static TEST_TYPE __attribute__((annotate("secret"))) array[ARRAY_SIZE] = {0};

volatile unsigned int __attribute__((annotate("secret"))) myindex[UPDATE_SIZE] = {0};

static volatile TEST_TYPE res[20] = {0};

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

void* attacker_thread(void* arg) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(CPU_CORE, &cpuset);
    if (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) != 0) {
        perror("pthread_setaffinity_np");
        return NULL;
    }

    int value = *(int*)arg;
    while (is_loop) {
        update(value);
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(CPU_CORE, &cpuset);
    if (sched_setaffinity(0, sizeof(cpu_set_t), &cpuset) != 0) {
        perror("sched_setaffinity");
        return 1;
    }

    read(0, &myindex, sizeof(unsigned int) * UPDATE_SIZE);
    // read(0, array, sizeof(TEST_TYPE) * ARRAY_SIZE);
    srand(0);

    uint64_t start, end;
    int temp = 0;
    unsigned int upper_bound = ARRAY_SIZE - 1;

    uint64_t exec_times[REPEAT_COUNT] = {0};

    char *buffer = array;
    int page_size = ARRAY_SIZE * sizeof(TEST_TYPE);
    // if (posix_memalign((void**)&buffer, PAGE_SIZE, page_size)) {
    //     perror("memalign");
    //     return 1;
    // }
    // array = buffer;
    
    for (int ii = 0; ii < 2; ii++) {
        for (int j = 0; j < UPDATE_SIZE; j++) {
            myindex[j] = (myindex[j] + rand()) % upper_bound;
        }
        int value = rand();
        pthread_t attacker;
        is_loop = true;
        if (pthread_create(&attacker, NULL, attacker_thread, &value) != 0) {
            perror("pthread_create");
            return 1;
        }
        for (uint64_t i = 0; i < WARMUP_COUNT + REPEAT_COUNT; i++) {
            int useless = 0;
            // warm up: fill buffer to keep in cache
            for (int i = 0; i < page_size; i += CACHE_LINE_SIZE) {
                buffer[i] = i;
            }
            uint64_t start = rdtscp();
            for (int b_j = 0; b_j < B_SIZE; b_j++) {
                #ifdef IS_LOAD != 1
                useless += buffer[B_Offset[b_j]];
                #else
                buffer[B_Offset[b_j]] = start;
                #endif
            }
            exec_times[i] = rdtscp() - start;
        }
    fprintf(stderr, "Run %s\n", __FILE__);
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
    is_loop = false;
    pthread_join(attacker, NULL);
    }
    
    int ret = 0;
    for (int i = 0; i < UPDATE_SIZE; i++) {
        ret += res[i];
    }
    return ret;
}
