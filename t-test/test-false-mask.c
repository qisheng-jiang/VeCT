#define _GNU_SOURCE

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <x86intrin.h>
#include <string.h>
#include <stdbool.h>
#include <pthread.h>
#include <sched.h>

#define CPU_CORE 1
#define ITER 1000000
#define PAGE_SIZE 4096
#define CACHE_LINE_SIZE 64

volatile uint8_t* buffer;

// rdtscp to get stable timestamps
static inline uint64_t rdtscp() {
    unsigned int aux;
    _mm_mfence();
    return __rdtscp(&aux);
}

#define OFFSET_SIZE sizeof(uint32_t)

#define NUM_MASK 13
__mmask16 masks[NUM_MASK] =
        {0b1111111111111111,
        0b0101010101010101,
        0b0101100110011001,
        0b0101010100000000,
        0b0100010001000100,
        0b0100001000000100,
        0b0110000000000000,
        0b1010000000000000,
        0b0000000100000000,
        0b0000001000000000,
        0b0100000000000000,
        0b1000000000000000,
        0b0000000000000000
    };

#define B_SIZE 8
int B_Offset[B_SIZE] = {0, PAGE_SIZE, 
    PAGE_SIZE * 2, PAGE_SIZE * 3, 
    PAGE_SIZE * 4, PAGE_SIZE * 5, 
    PAGE_SIZE * 6, PAGE_SIZE * 7};

int base_offset = OFFSET_SIZE * 12; // %r10 

volatile bool is_loop = true;
int test_index = 0;

void* attacker_thread(void* arg) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(CPU_CORE, &cpuset);
    if (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) != 0) {
        perror("pthread_setaffinity_np");
        return NULL;
    }
    fprintf(stderr,"Attacker thread started on mask %x\n", masks[test_index]);
    uint8_t *tmp_addr = (uint8_t *)&buffer[base_offset];
    uint8_t *aligned_addr = (uint8_t *)((uintptr_t)tmp_addr & ~(CACHE_LINE_SIZE - 1));
    int i = 0;
    while (is_loop) {
#ifdef WRITE 
        _mm512_mask_store_epi32((void *)aligned_addr, masks[test_index],
            _mm512_set1_epi32(i));
#else
        i = _mm512_reduce_add_epi32(
                _mm512_mask_load_epi32(_mm512_setzero_si512(), 
                    masks[test_index], (void *)aligned_addr));
#endif // write
        i++;
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
    time_list = calloc(ITER, sizeof(uint64_t));

    int page_size = PAGE_SIZE * 10;
    if (posix_memalign((void**)&buffer, PAGE_SIZE, page_size)) {
        perror("memalign");
        return 1;
    }

    for (int a_j = 0; a_j < NUM_MASK; a_j++) {
        uint64_t total = 0;
        int useless = 0;

        pthread_t attacker;
        is_loop = true;
        test_index = a_j;
        if (pthread_create(&attacker, NULL, attacker_thread, NULL) != 0) {
            perror("pthread_create");
            free((void*)buffer);
            return 1;
        }

        for (int i = 0; i < ITER; ++i) {
            // warm up: fill buffer to keep in cache
            for (int i = 0; i < page_size; i += CACHE_LINE_SIZE) {
                buffer[i] = i;
            }
            
            uint64_t start = rdtscp();
            for (int b_j = 0; b_j < B_SIZE; b_j++) {
#ifdef WRITE
                useless += buffer[base_offset + B_Offset[b_j]];
#else
                buffer[base_offset + B_Offset[b_j]] = start;
#endif
            }
            time_list[i] = rdtscp() - start;
            total += time_list[i];
        }
#ifdef WRITE
        printf("Write,Mask %x,", masks[a_j]);
#else
        printf("Read,Mask %x,", masks[a_j]);
#endif
        for (int i = 0; i < ITER; i++) {
            printf("%lu,", time_list[i]);
        }
        printf("\n");
        is_loop = false;
        pthread_join(attacker, NULL);
        fprintf(stderr, "Mask %x: Avg latency = %.2f cycles, Useless = %d\n", 
            masks[a_j], (double)total / ITER, useless);
    }

    free((void*)buffer);
    free(time_list);
    return 0;
}
