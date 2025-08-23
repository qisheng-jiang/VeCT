#include <stdio.h> 
#include <assert.h> 
#include <unistd.h>

#include "../../../../include/exp_setup.h"

#define INPUT_SIZE TEST_INPUT_SIZE_RSORT

int in[INPUT_SIZE] = {0}; 
int output[INPUT_SIZE] = {0};

// A utility function to get maximum value in arr[] 
int getMax(int arr[], int n) 
{ 
    int mx = arr[0]; 
    for (int i = 0; i < n; i += SPLIT_VECTOR) {
        int v_0 = arr[i];
        int v_1 = arr[i+1];
        int v_2 = arr[i+2];
        int v_3 = arr[i+3];
        int v_4 = arr[i+4];
        int v_5 = arr[i+5];
        int v_6 = arr[i+6];
        int v_7 = arr[i+7];
        if (v_0 > mx) mx = v_0;
        if (v_1 > mx) mx = v_1;
        if (v_2 > mx) mx = v_2;
        if (v_3 > mx) mx = v_3;
        if (v_4 > mx) mx = v_4;
        if (v_5 > mx) mx = v_5;
        if (v_6 > mx) mx = v_6;
        if (v_7 > mx) mx = v_7;
    }
    return mx; 
} 
  

static int count[10] = {0};
// A function to do counting sort of arr[] according to 
// the digit represented by exp. 
void countSort(int arr[], int n, int exp) 
{
    int i;

    // volatile is used to prevent compiler optimizations that might replace the loop with a memset
    volatile int *vcount = (volatile int *)count;
    for (i = 0; i < 10; i++) vcount[i] = 0;

    // Store count of occurrences in count[] 
    for (i = 0; i < n; i += 1) {
        int v_0 = arr[i];
        // int v_1 = arr[i+1];
        // int v_2 = arr[i+2];
        // int v_3 = arr[i+3];
        // int v_4 = arr[i+4];
        // int v_5 = arr[i+5];
        // int v_6 = arr[i+6];
        // int v_7 = arr[i+7];

        v_0 = (v_0 / exp) % 10;
        // v_1 = (v_1 / exp) % 10;
        // v_2 = (v_2 / exp) % 10;
        // v_3 = (v_3 / exp) % 10;
        // v_4 = (v_4 / exp) % 10;
        // v_5 = (v_5 / exp) % 10;
        // v_6 = (v_6 / exp) % 10;
        // v_7 = (v_7 / exp) % 10;

        int c_0 = count[v_0];
        // int c_1 = count[v_1];
        // int c_2 = count[v_2];
        // int c_3 = count[v_3];
        // int c_4 = count[v_4];
        // int c_5 = count[v_5];
        // int c_6 = count[v_6];
        // int c_7 = count[v_7];

        count[v_0] = c_0 + 1;
        // count[v_1] = c_1 + 1;
        // count[v_2] = c_2 + 1;
        // count[v_3] = c_3 + 1;
        // count[v_4] = c_4 + 1;
        // count[v_5] = c_5 + 1;
        // count[v_6] = c_6 + 1;
        // count[v_7] = c_7 + 1;
    }
        
  
    // Change count[i] so that count[i] now contains actual 
    //  position of this digit in output[] 
    for (i = 1; i < 10; i++) 
        count[i] += count[i - 1]; 
  
    // Build the output array 
    for (i = n - 1; i >= 0; i--) 
    { 
        output[count[ (arr[i]/exp)%10 ] - 1] = arr[i]; 
        count[ (arr[i]/exp)%10 ]--; 
    } 
  
    // Copy the output array to arr[], so that arr[] now 
    // contains sorted numbers according to current digit 
    // for (i = 0; i < n; i++) 
    //     arr[i] = output[i]; 
} 
  
// The main function to that sorts arr[] of size n using  
// Radix Sort 
void radixsort(int arr[], int n) 
{ 
    // Find the maximum number to know number of digits 
    int m = getMax(arr, n); 
  
    // Do counting sort for every digit. Note that instead 
    // of passing digit number, exp is passed. exp is 10^i 
    // where i is current digit number 
    for (int exp = 1; m/exp > 0; exp *= 10) 
        countSort(arr, n, exp); 
} 

// Driver program to test above functions 
int main(int argc, char** argv) 
{ 
    srand(0);
    for (int i = 0; i < INPUT_SIZE; i++) {
      read(0, &in[i], 2);
    }
#if END_TO_END != 1
    uint64_t start, end;
    uint64_t cpu_time_used = 0;
    int temp = 0;
    for (uint64_t i = 0; i < WARMUP_COUNT + REPEAT_COUNT; i++) {
        for (int j = 0; j < INPUT_SIZE; j++) {
            in[j] += rand();
            in[j] &= 0xFFFFFFF;
            output[j] = 0;
        }
        clean_cache(in, INPUT_SIZE * sizeof(int));
        clean_cache(output, INPUT_SIZE * sizeof(int));
        _mm_mfence();
        start = __rdtscp(&temp);
#endif
        radixsort(in, INPUT_SIZE); 
#if END_TO_END != 1
        _mm_mfence();
        end = __rdtscp(&temp);
        cpu_time_used += ( i < WARMUP_COUNT ? 0 : (end - start));
    }
    fprintf(stderr, "Elapsed time in %s: %lu\n", __FILE__, cpu_time_used);
#endif
    write(1, output, INPUT_SIZE * sizeof(int));
    return 0; 
}
