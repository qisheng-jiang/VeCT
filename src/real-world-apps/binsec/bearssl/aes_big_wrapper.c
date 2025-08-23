#include "../__libsym__/sym.h"
#include "inc/bearssl.h"
#include <stdint.h>

#include "../../../include/exp_setup.h"

#define KEY_LEN 240 /* uint32_t skey[60]; => 60 * 4 */
#define N_ROUND 10
#define IV_LEN br_aes_big_BLOCK_SIZE /* 16 bytes */
#define DATA_LEN TEST_DATA_SIZE /* Must be a multiple of block size */

int main(int argc, char *argv[]){  
  br_aes_big_cbcenc_keys ctx = {0};
  ctx.vtable = &br_aes_big_cbcenc_vtable;
  ctx.num_rounds = N_ROUND;
  uint8_t iv[IV_LEN] = {0};
  uint8_t data[DATA_LEN] = {0};

  HIGH_INPUT(KEY_LEN, ctx.skey);
  HIGH_INPUT(DATA_LEN, data);

#if END_TO_END != 1
  uint64_t start, end;
  uint64_t cpu_time_used = 0;
  int temp = 0;
  for (uint64_t i = 0; i < WARMUP_COUNT + REPEAT_COUNT; i++) {
    clean_cache(data, DATA_LEN);
    clean_cache(ctx.skey, KEY_LEN);
    _mm_mfence();
    start = __rdtscp(&temp);
#endif
    br_aes_big_cbcenc_run(&ctx, iv, data, (size_t) DATA_LEN);
#if END_TO_END != 1
    _mm_mfence();
    end = __rdtscp(&temp);
    cpu_time_used += ( i < WARMUP_COUNT ? 0 : (end - start));
  }
  fprintf(stderr, "Elapsed time in %s: %lu\n", __FILE__, cpu_time_used);
#endif

  write(1, data, DATA_LEN);
  return 0;
}
