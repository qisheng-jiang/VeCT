#include <stdio.h>
#include <assert.h>
#include <unistd.h>

#include "../../../../include/exp_setup.h"

#define INPUT_SIZE TEST_INPUT_SIZE

void heappop(int heap[], int dummy[]) {
    // Min-heap pop implementation (non-recursive)
    int size = INPUT_SIZE;
    if (size == 0) return;

    heap[0] = heap[size - 1];
    size--;

    int i = 0;
    while (2 * i + 1 < size) {
        int smallest = i;
        int left = 2 * i + 1;
        int right = 2 * i + 2;

        if (left < size && heap[left] < heap[smallest]) {
            smallest = left;
        }
        if (right < size && heap[right] < heap[smallest]) {
            smallest = right;
        }
        if (smallest == i) break;

        int tmp = heap[i];
        heap[i] = heap[smallest];
        heap[smallest] = tmp;
        i = smallest;
    }

    // copy heap to dummy for output
    for (int j = 0; j < INPUT_SIZE; j += SPLIT_VECTOR) {
        dummy[j] = heap[j];
        dummy[j + 1] = heap[j + 1];
        dummy[j + 2] = heap[j + 2];
        dummy[j + 3] = heap[j + 3];
        dummy[j + 4] = heap[j + 4];
        dummy[j + 5] = heap[j + 5];
        dummy[j + 6] = heap[j + 6];
        dummy[j + 7] = heap[j + 7];
    }
}

int in[INPUT_SIZE] = {0};
int out[INPUT_SIZE] = {0};

int main(int argc, char** argv) {
    srand(0);
    for (int i = 0; i < INPUT_SIZE; i++) {
        read(0, &in[i], 4);
    }

#if END_TO_END != 1
    uint64_t start, end;
    uint64_t cpu_time_used = 0;
    int temp = 0;
    for (uint64_t i = 0; i < WARMUP_COUNT + REPEAT_COUNT; i++) {
        for (int j = 0; j < INPUT_SIZE; j++) {
            in[j] += rand();
            out[j] = 0;
        }
        clean_cache(in, INPUT_SIZE * sizeof(int));
        clean_cache(out, INPUT_SIZE * sizeof(int));
        _mm_mfence();
        start = __rdtscp(&temp);
#endif
        heappop(in, out);
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
