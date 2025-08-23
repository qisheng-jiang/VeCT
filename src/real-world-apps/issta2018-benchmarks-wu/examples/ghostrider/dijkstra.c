#include <stdio.h> 
#include <stdlib.h> 
#include <assert.h> 
#include <unistd.h>

#include "../../../../include/exp_setup.h"

// num edges -> INPUT_SIZE * INPUT_SIZE
#define INPUT_SIZE TEST_INPUT_SIZE_DJIKSTRA 

int secret = 0;
__attribute_noinline__ int dijkstra(int n, int s, int t, int e[]) {
    int vis[INPUT_SIZE] = {0};
    int dis[INPUT_SIZE] = {0};
    int __attribute__((annotate("secret"))) bestj = -1;
    vis[s] = 1;
    for(int i=0; i<n; i += SPLIT_VECTOR) {
        dis[i] = e[s * INPUT_SIZE + i];
        dis[i + 1] = e[s * INPUT_SIZE + i + 1];
        dis[i + 2] = e[s * INPUT_SIZE + i + 2];
        dis[i + 3] = e[s * INPUT_SIZE + i + 3];
        dis[i + 4] = e[s * INPUT_SIZE + i + 4];
        dis[i + 5] = e[s * INPUT_SIZE + i + 5];
        dis[i + 6] = e[s * INPUT_SIZE + i + 6];
        dis[i + 7] = e[s * INPUT_SIZE + i + 7];
    }
    for(int i=0; i<n; ++i) {
        for(int j=0; j<n; j += SPLIT_VECTOR) {
            int v_0 = vis[j];
            int v_1 = vis[j+1];
            int v_2 = vis[j+2];
            int v_3 = vis[j+3];
            int v_4 = vis[j+4];
            int v_5 = vis[j+5];
            int v_6 = vis[j+6];
            int v_7 = vis[j+7];
            int d_0 = dis[j];
            int d_1 = dis[j+1];
            int d_2 = dis[j+2];
            int d_3 = dis[j+3];
            int d_4 = dis[j+4];
            int d_5 = dis[j+5];
            int d_6 = dis[j+6];
            int d_7 = dis[j+7];

            if(!v_0 && (bestj < 0 || d_0 < dis[bestj])) {
                bestj = 0 + secret; // fix implicit flow here
                vis[bestj] = 1;
            }
            if(!v_1 && (bestj < 0 || d_1 < dis[bestj])) {
                bestj = 1 + secret; // fix implicit flow here
                vis[bestj] = 1;
            }
            if(!v_2 && (bestj < 0 || d_2 < dis[bestj])) {
                bestj = 2 + secret; // fix implicit flow here
                vis[bestj] = 1;
            }
            if(!v_3 && (bestj < 0 || d_3 < dis[bestj])) {
                bestj = 3 + secret; // fix implicit flow here
                vis[bestj] = 1;
            }
            if(!v_4 && (bestj < 0 || d_4 < dis[bestj])) {
                bestj = 4 + secret; // fix implicit flow here
                vis[bestj] = 1;
            }
            if(!v_5 && (bestj < 0 || d_5 < dis[bestj])) {
                bestj = 5 + secret; // fix implicit flow here
                vis[bestj] = 1;
            }
            if(!v_6 && (bestj < 0 || d_6 < dis[bestj])) {
                bestj = 6 + secret; // fix implicit flow here
                vis[bestj] = 1;
            }
            if(!v_7 && (bestj < 0 || d_7 < dis[bestj])) {
                bestj = 7 + secret; // fix implicit flow here
                vis[bestj] = 1;
            }
        }
        for(int j=0; j<n; j += SPLIT_VECTOR) {
            int v_0 = vis[j];
            int v_1 = vis[j+1];
            int v_2 = vis[j+2];
            int v_3 = vis[j+3];
            int v_4 = vis[j+4];
            int v_5 = vis[j+5];
            int v_6 = vis[j+6];
            int v_7 = vis[j+7];
            int d_0 = dis[j];
            int d_1 = dis[j+1];
            int d_2 = dis[j+2];
            int d_3 = dis[j+3];
            int d_4 = dis[j+4];
            int d_5 = dis[j+5];
            int d_6 = dis[j+6];
            int d_7 = dis[j+7];
            int e_0 = e[bestj * INPUT_SIZE + 0];
            int e_1 = e[bestj * INPUT_SIZE + 1];
            int e_2 = e[bestj * INPUT_SIZE + 2];
            int e_3 = e[bestj * INPUT_SIZE + 3];
            int e_4 = e[bestj * INPUT_SIZE + 4];
            int e_5 = e[bestj * INPUT_SIZE + 5];
            int e_6 = e[bestj * INPUT_SIZE + 6];
            int e_7 = e[bestj * INPUT_SIZE + 7];
     
            if(!v_0 && (dis[bestj] + e_0 < d_0)) dis[j] = dis[bestj] + e_0;
            if(!v_1 && (dis[bestj] + e_1 < d_1)) dis[j+1] = dis[bestj] + e_1;
            if(!v_2 && (dis[bestj] + e_2 < d_2)) dis[j+2] = dis[bestj] + e_2;
            if(!v_3 && (dis[bestj] + e_3 < d_3)) dis[j+3] = dis[bestj] + e_3;
            if(!v_4 && (dis[bestj] + e_4 < d_4)) dis[j+4] = dis[bestj] + e_4;
            if(!v_5 && (dis[bestj] + e_5 < d_5)) dis[j+5] = dis[bestj] + e_5;
            if(!v_6 && (dis[bestj] + e_6 < d_6)) dis[j+6] = dis[bestj] + e_6;
            if(!v_7 && (dis[bestj] + e_7 < d_7)) dis[j+7] = dis[bestj] + e_7;
        }
    }
    return dis[t];
}

int in[INPUT_SIZE * INPUT_SIZE] = {0}; 
// Driver program to test above function
int main(int argc, char** argv) 
{ 
    read(0, &secret, 1);
    secret -= (unsigned char)secret;
    for (int i = 0; i < INPUT_SIZE; i++) {
        for (int j = 0; j < INPUT_SIZE; j++) {
            read(0, &in[i * INPUT_SIZE + j], 2);
        }
    }
    int res = 0;
#if END_TO_END != 1
    uint64_t start, end;
    uint64_t cpu_time_used = 0;
    int temp = 0;
    for (uint64_t i = 0; i < WARMUP_COUNT + REPEAT_COUNT; i++) {
        clean_cache(in, INPUT_SIZE * INPUT_SIZE * sizeof(int));
        _mm_mfence();
        start = __rdtscp(&temp);
#endif
        res = dijkstra(INPUT_SIZE, 0, INPUT_SIZE-1, in); 
#if END_TO_END != 1
        _mm_mfence();
        end = __rdtscp(&temp);
        cpu_time_used += ( i < WARMUP_COUNT ? 0 : (end - start));
    }
    fprintf(stderr, "Elapsed time in %s: %lu\n", __FILE__, cpu_time_used);
#endif
    write(1, &res, sizeof(res));
    return 0; 
}
