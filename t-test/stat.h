#ifndef STAT_H
#define STAT_H

#include <stdint.h>
#include <time.h>
#include <math.h>

#define NUM_ITERATIONS 510000uL
#define CACHE_LINE_SIZE 64
#define NUM_INT CACHE_LINE_SIZE / sizeof(int)
#define NUM_LINE 8
#define NUM_MASK 13 

#define INIT_VALUE 0xff

int compare_uint64(const void *a, const void *b) {
    return (*(uint64_t*)a > *(uint64_t*)b) - (*(uint64_t*)a < *(uint64_t*)b);
}

void clean_cache(void *addr, int64_t cache_size) {
    for (int64_t i = 0; i < cache_size; i++) {
        _mm_clflush(addr + i * CACHE_LINE_SIZE);
    }
    _mm_mfence();
}

#define PRINT_ONLY 1

void summary(uint64_t time_list[]) {
#if (PRINT_ONLY)
    for (int i = 0; i < NUM_ITERATIONS; i++) {
        printf("%lld", time_list[i]);
        if (i < NUM_ITERATIONS - 1) {
            printf(",");
        }
    }
    printf("\n");
#else
    double sum_time = 0.0;
    int counts = NUM_ITERATIONS;

    for (int i = 0; i < counts; i++) {
        sum_time += (double)time_list[i];
    }
  
    double avg_time = counts == 0 ? 0.0 : sum_time / (double)counts;
    
    double sum_sq_time = 0.0;
    for (int i = 0; i < counts; i++) {
        double i_d = (double)time_list[i] - avg_time;
        sum_sq_time += (i_d * i_d);
    }
    double std_time = counts == 0 ? 0.0 : sqrt(sum_sq_time / (double)counts);

    // Calculate medians
    double median_time = 0.0;
    if (counts > 0) {
        qsort(time_list, counts, sizeof(uint64_t), compare_uint64);
        median_time = (counts % 2 == 0) ? 
            (double)(time_list[counts / 2 - 1] + time_list[counts / 2]) / 2.0 : 
            (double)time_list[counts / 2];
    }

    // printf(",Median (cycles),Average,Standard Deviation\n");
    printf(",%.2f,%.2f,%.2f\n", median_time, avg_time, std_time);
#endif // PRINT_ONLY
}

#endif // STAT_H