#define _GNU_SOURCE

#include <immintrin.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <string.h>
#include <stdbool.h>
#include <pthread.h>
#include <sched.h>

#include "stat.h"

#define NUM_INDEX 8
#define NUM_ONCE 16

#define NUM_ACCESS (NUM_ONCE/2)
#define NUM_LINE NUM_ACCESS 

#define CPU_CORE 1
#define PAGE_SIZE 4096
#define CACHE_LINE_SIZE 64

volatile uint8_t* buffer;

#define B_SIZE 8
int B_Offset[B_SIZE] = {0, PAGE_SIZE, 
    PAGE_SIZE * 2, PAGE_SIZE * 3, 
    PAGE_SIZE * 4, PAGE_SIZE * 5, 
    PAGE_SIZE * 6, PAGE_SIZE * 7};

// base offset is the address indexed by 2. 
int base_offset = sizeof(uint32_t) * 2; // %r10 

volatile bool is_loop = true;
int test_index = 0;

int avx_indices[NUM_INDEX][NUM_ONCE] = {
        0, 1, 16, 17, 32, 33, 48, 49, 64, 65, 80, 81, 96, 97, 112, 113, 
        0, 1, 16, 20, 32, 33, 48, 49, 64, 65, 80, 81, 96, 97, 112, 113, 
        2, 3, 18, 19, 34, 35, 50, 51, 66, 67, 82, 83, 98, 99, 114, 115, 
        0, 0, 16, 17, 32, 32, 48, 49, 64, 64, 80, 81, 96, 96, 112, 113, 

        0, 1, 10, 17, 32, 33, 40, 49, 64, 65, 70, 81, 96, 97, 100, 113, 
        0, 1, 15, 17, 32, 33, 40, 49, 64, 65, 70, 81, 96, 97, 100, 113, 
        2, 3, 12, 19, 34, 35, 42, 51, 66, 67, 72, 83, 98, 99, 102, 115, 
        0, 0, 0, 17, 32, 32, 32, 49, 64, 64, 64, 81, 96, 96, 96, 113, 

        // 0, 1, 10, 11, 32, 33, 40, 41, 64, 65, 70, 71, 96, 97, 100, 101, 
        // 0, 1, 2, 8, 9, 10, 11, 12, 64, 65, 66, 70, 71, 72, 73, 74, 
        // 0, 1, 2, 8, 9, 10, 11, 12, 96, 97, 100, 101, 102, 103, 104, 105
};

static inline uint64_t rdtscp() {
    unsigned int aux;
    _mm_mfence();
    return __rdtscp(&aux);
}

void* avx512_masked_gather_random(void *arg) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(CPU_CORE, &cpuset);
    if (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) != 0) {
        perror("pthread_setaffinity_np");
        return NULL;
    }
    fprintf(stderr,"Attacker thread started on index group %d\n", test_index);
    uint8_t *src = buffer;
    int i = 0;

    int *indices = avx_indices[test_index];

    __mmask16 mask = 0xFFFF; 
    __m512i index_vector = _mm512_loadu_si512((__m512i *)indices);
    while (is_loop) {
        i += _mm512_reduce_add_epi32(
        _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), 
                        mask, index_vector, src, 4));
    }
    fprintf(stderr, "%d\n", i);
    return NULL;
}

void* avx512_masked_scatter_random(void* arg) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(CPU_CORE, &cpuset);
    if (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) != 0) {
        perror("pthread_setaffinity_np");
        return NULL;
    }
    fprintf(stderr,"Attacker thread started on index group %d\n", test_index);
    uint8_t *dst = buffer;
    int i = 0;

    int *indices = avx_indices[test_index];
    __m512i data = _mm512_setr_epi32(100, 200, 300, 400, 500, 600, 700, 800, 100, 200, 300, 400, 500, 600, 700, 800);
    __mmask16 mask = 0xFFFF;
    __m512i index_vector = _mm512_loadu_si512((__m512i *)indices);

    while (is_loop) {
        _mm512_mask_i32scatter_epi32(
            dst,                        // Destination address
            mask,                       // Mask
            index_vector,               // Index vector
            data,                       // Data to scatter
            4                           // Element size in bytes (int = 4 bytes)
        );
    }
    return NULL;
}

int main(int argc, char* argv[]) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(CPU_CORE, &cpuset);
    if (sched_setaffinity(0, sizeof(cpu_set_t), &cpuset) != 0) {
        perror("sched_setaffinity");
        return 1;
    }

    uint64_t* time_list = NULL;
    time_list = calloc(NUM_ITERATIONS, sizeof(uint64_t));

    int page_size = PAGE_SIZE * 20;
    buffer = aligned_alloc(PAGE_SIZE, page_size);
    memset(buffer, INIT_VALUE, page_size);

    int useless = 0;
    for (int i_mask = 0; i_mask < NUM_INDEX; i_mask++) {
        pthread_t attacker;
        is_loop = true;
        test_index = i_mask;
        if (pthread_create(&attacker, NULL, avx512_masked_scatter_random, NULL) != 0) {
            perror("pthread_create");
            free((void*)buffer);
            return 1;
        }

        for (int64_t i = 0; i < NUM_ITERATIONS; i++) {
            for (int i = 0; i < page_size; i += CACHE_LINE_SIZE) {
                buffer[i] = i;
            }
            uint64_t start = rdtscp();
            for (int b_j = 0; b_j < B_SIZE; b_j++) {
                useless += buffer[base_offset + B_Offset[b_j]];
            }
            time_list[i] = rdtscp() - start;

        }
        is_loop = false;
        pthread_join(attacker, NULL);
        printf("store,Index %d,", i_mask);
        summary(time_list);
    }

    for (int i_mask = 0; i_mask < NUM_INDEX; i_mask++) {
        pthread_t attacker;
        is_loop = true;
        test_index = i_mask;
        if (pthread_create(&attacker, NULL, avx512_masked_gather_random, NULL) != 0) {
            perror("pthread_create");
            free((void*)buffer);
            return 1;
        }

        for (int64_t i = 0; i < NUM_ITERATIONS; i++) {
            for (int i = 0; i < page_size; i += CACHE_LINE_SIZE) {
                buffer[i] = i;
            }
            uint64_t start = rdtscp();
            for (int b_j = 0; b_j < B_SIZE; b_j++) {
                buffer[base_offset + B_Offset[b_j]] = start;
            }
            time_list[i] = rdtscp() - start;

        }
        is_loop = false;
        pthread_join(attacker, NULL);
        printf("load,Index %d,", i_mask);
        summary(time_list);
    }

    free(buffer);
    free(time_list);
    return 0;
}
