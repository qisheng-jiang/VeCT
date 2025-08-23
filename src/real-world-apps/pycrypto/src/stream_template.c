/* -*- C -*- */

/*
 *  stream_template.c : Generic framework for stream ciphers
 *
 * Written by Andrew Kuchling and others
 *
 * ===================================================================
 * The contents of this file are dedicated to the public domain.  To
 * the extent that dedication to the public domain is not available,
 * everyone is granted a worldwide, perpetual, royalty-free,
 * non-exclusive license to exercise all rights associated with the
 * contents of this file for any purpose whatsoever.
 * No rights are reserved.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 * ===================================================================
 */

#include <unistd.h>
#include "pycrypto_common.h"
/* Cipher operation modes */

#include "../../../include/exp_setup.h"

#define MODE_ECB 1
#define MODE_CBC 2
#define MODE_CFB 3
#define MODE_PGP 4
#define MODE_OFB 5
#define MODE_CTR 6

#define _STR(x) #x
#define _XSTR(x) _STR(x)
#define _PASTE(x,y) x##y
#define _PASTE2(x,y) _PASTE(x,y)
#ifdef IS_PY3K
#define _MODULE_NAME _PASTE2(PyInit_,MODULE_NAME)
#else
#define _MODULE_NAME _PASTE2(init,MODULE_NAME)
#endif
#define _MODULE_STRING _XSTR(MODULE_NAME)

// #ifndef STREAM_SIZE
// 	#define STREAM_SIZE 32
// #endif

#define STREAM_SIZE TEST_DATA_SIZE

#ifndef KEY_LEN
	#define KEY_LEN 32
#endif

int main(int argc, char* argv[]) {
	stream_state st = {0};
	unsigned char key[KEY_LEN] = {0};
	unsigned char in[STREAM_SIZE] = {0};

	read(0, key, KEY_LEN);
	read(0, in, STREAM_SIZE);
#if END_TO_END != 1
  	uint64_t start, end;
  	uint64_t cpu_time_used = 0;
  	int temp = 0;
  	for (uint64_t i = 0; i < WARMUP_COUNT + REPEAT_COUNT; i++) {
		clean_cache(key, KEY_LEN);
		clean_cache(in, STREAM_SIZE);
		_mm_mfence();
		start = __rdtscp(&temp);
#endif
	stream_init(&st, key, KEY_LEN);
	stream_encrypt(&st, in, STREAM_SIZE);
#if END_TO_END != 1
		_mm_mfence();
		end = __rdtscp(&temp);
		cpu_time_used += ( i < WARMUP_COUNT ? 0 : (end - start));
	}
  	fprintf(stderr, "Elapsed time in %s: %lu\n", __FILE__, cpu_time_used);
#endif
	write(1, in, STREAM_SIZE);

  return 0;
}