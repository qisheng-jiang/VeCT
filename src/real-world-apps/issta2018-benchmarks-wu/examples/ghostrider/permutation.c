#include <stdio.h>
#include <assert.h>
#include <unistd.h>

#include "../../../../include/exp_setup.h"

#define INPUT_SIZE TEST_INPUT_SIZE

void permutation(int b[], int a[]) {
    int tmp_0, tmp_1, tmp_2, tmp_3, tmp_4, tmp_5, tmp_6, tmp_7, tmp_8, tmp_9;
    for (int i = 0; i < INPUT_SIZE; i += 10) {
        tmp_0 = b[i];
        tmp_1 = b[i + 1];
        tmp_2 = b[i + 2];
        tmp_3 = b[i + 3];
        tmp_4 = b[i + 4];
        tmp_5 = b[i + 5];
        tmp_6 = b[i + 6];
        tmp_7 = b[i + 7];
        tmp_8 = b[i + 8];
        tmp_9 = b[i + 9];
        a[tmp_0] = i;
        a[tmp_1] = i + 1;
        a[tmp_2] = i + 2;
        a[tmp_3] = i + 3;
        a[tmp_4] = i + 4;
        a[tmp_5] = i + 5;
        a[tmp_6] = i + 6;
        a[tmp_7] = i + 7;
        a[tmp_8] = i + 8;
        a[tmp_9] = i + 9;
    }
}

int in[INPUT_SIZE] = {0};
int out[INPUT_SIZE] = {0};

int main(int argc, char** argv) {
    for (int i = 0; i < INPUT_SIZE; i++) {
        read(0, &in[i], 4);
        if (in[i] < 0) in[i] = -in[i];
        in[i] = in[i] % INPUT_SIZE;
    }

#if END_TO_END != 1
    uint64_t start, end;
    uint64_t cpu_time_used = 0;
    int temp = 0;
    for (uint64_t i = 0; i < WARMUP_COUNT + REPEAT_COUNT; i++) {
        clean_cache(in, INPUT_SIZE * sizeof(int));
        clean_cache(out, INPUT_SIZE * sizeof(int));
        _mm_mfence();
        start = __rdtscp(&temp);
#endif
        permutation(in, out);
#if END_TO_END != 1
        _mm_mfence();
        end = __rdtscp(&temp);
        cpu_time_used += (i < WARMUP_COUNT ? 0 : (end - start));
    }
    fprintf(stderr, "Elapsed time in %s: %lu\n", __FILE__, cpu_time_used);
#endif

    write(1, out, INPUT_SIZE * sizeof(int));
    return 0;
}
