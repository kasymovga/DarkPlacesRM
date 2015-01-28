#pragma once
#ifndef DSLIB_RANDOM_H
#define DSLIB_RANDOM_H

/* Generator 1 -- use for low-spatial normal-distributed numbers at high rate.
 * NOT cryptographically secure.
 *
 * Based upon a paper by S. Vigna [1]
 *
 * http://arxiv.org/pdf/1404.0390v1.pdf
 */

#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>

typedef struct {
    uint64_t state[2];
} xorplus128_t;

/* initialize generator with `dlen` octets stored in `data`.
 * if data is NULL, then the generator will use clock() and time() instead.
 *
 * if the length is < 16 octets, then it'll also use clock() and time() to
 * improve the initial state.
 */
void srand_xorplus128(xorplus128_t* s, const uint8_t* data, const size_t dlen);

/* return integer in range [0, 2**32-1] */
uint64_t rand_xorplus128(xorplus128_t* s);

/* return fpoint in range [0, 1.0) */
double frand_xorplus128(xorplus128_t* s);

/* return fpoint in range [0, 1.0] */
double frandi_xorplus128(xorplus128_t* s);

void Xrand_Init(int fake_random);
void Xrand_Shutdown(void);

/* return in in range [-2**31, 2**31-1] */

#define XRAND_MAX INT32_MAX
int32_t xrand(void);

#endif /* DSLIB_RANDOM_H */
