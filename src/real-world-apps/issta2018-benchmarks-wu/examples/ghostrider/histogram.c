#include <stdio.h> 
#include <assert.h> 
#include <unistd.h>

#include "../../../../include/exp_setup.h"

#define INPUT_SIZE TEST_INPUT_SIZE

void histogram(int a[], int c[]) {
    int i;
    for(i=0;i<INPUT_SIZE;i+=SPLIT_VECTOR) {
        c[i]=0;
        c[i+1]=0;
        c[i+2]=0;
        c[i+3]=0;
        c[i+4]=0;
        c[i+5]=0;
        c[i+6]=0;
        c[i+7]=0;
    }
    for(i=0;i<INPUT_SIZE;i+=SPLIT_VECTOR) {
        int v_0 =a[i];
        int v_1 =a[i+1];
        int v_2 =a[i+2];
        int v_3 =a[i+3];
        int v_4 =a[i+4];
        int v_5 =a[i+5];
        int v_6 =a[i+6];
        int v_7 =a[i+7];

        int t_0, t_1, t_2, t_3, t_4, t_5, t_6, t_7;
        if(v_0>0) t_0=v_0%INPUT_SIZE; else t_0=(0-v_0)%INPUT_SIZE;
        if(v_1>0) t_1=v_1%INPUT_SIZE; else t_1=(0-v_1)%INPUT_SIZE;
        if(v_2>0) t_2=v_2%INPUT_SIZE; else t_2=(0-v_2)%INPUT_SIZE;
        if(v_3>0) t_3=v_3%INPUT_SIZE; else t_3=(0-v_3)%INPUT_SIZE;
        if(v_4>0) t_4=v_4%INPUT_SIZE; else t_4=(0-v_4)%INPUT_SIZE;
        if(v_5>0) t_5=v_5%INPUT_SIZE; else t_5=(0-v_5)%INPUT_SIZE;
        if(v_6>0) t_6=v_6%INPUT_SIZE; else t_6=(0-v_6)%INPUT_SIZE;
        if(v_7>0) t_7=v_7%INPUT_SIZE; else t_7=(0-v_7)%INPUT_SIZE;

        c[t_0]=c[t_0]+1; 
        c[t_1]=c[t_1]+1; 
        c[t_2]=c[t_2]+1; 
        c[t_3]=c[t_3]+1; 
        c[t_4]=c[t_4]+1; 
        c[t_5]=c[t_5]+1; 
        c[t_6]=c[t_6]+1; 
        c[t_7]=c[t_7]+1; 
    } 
}
  
int in[INPUT_SIZE] = {0}; 
int out[INPUT_SIZE] = {0}; 
// Driver program to test above function
int main(int argc, char** argv) 
{ 
    for (int i = 0; i < INPUT_SIZE; i++) {
      read(0, &in[i], 2);
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
        histogram(in, out); 
#if END_TO_END != 1
        _mm_mfence();
        end = __rdtscp(&temp);
        cpu_time_used += ( i < WARMUP_COUNT ? 0 : (end - start));
    }
    fprintf(stderr, "Elapsed time in %s: %lu\n", __FILE__, cpu_time_used);
#endif
    write(1, out, INPUT_SIZE*4);
    return 0; 
}
