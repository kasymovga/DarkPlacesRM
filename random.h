#pragma once
#ifndef DSLIB_RANDOM_H
#define DSLIB_RANDOM_H

#include <inttypes.h>
#include <stdint.h>

void Xrand_Init(int fake_random);
void Xrand_Shutdown(void);

/* return in in range [-2**31, 2**31-1] */

#define XRAND_MAX INT32_MAX
int32_t xrand(void);

#endif /* DSLIB_RANDOM_H */
