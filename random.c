#include "random.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Generator 1 -- use for low-spatial normal-distributed numbers at high rate.
 * NOT cryptographically secure.
 *
 * Based upon a paper by S. Vigna [1]
 *
 * http://arxiv.org/pdf/1404.0390v1.pdf
 */

typedef struct {
    uint64_t state[2];
} xorplus128_t;

/* initialize generator with `dlen` octets stored in `data`.
 * if data is NULL, then the generator will use clock() and time() instead.
 *
 * if the length is < 16 octets, then it'll also use clock() and time() to
 * improve the initial state.
 */
static void srand_xorplus128(xorplus128_t* s, const uint8_t* data, const size_t dlen);

/* return integer in range [0, 2**32-1] */
static uint64_t rand_xorplus128(xorplus128_t* s);


void srand_xorplus128(xorplus128_t* s, const uint8_t* data, const size_t dlen) {
    s->state[0] = 0;
    s->state[1] = 0;
    if(data && 0 < dlen) {
        const size_t len = (dlen >= 16) ? 16 : dlen;
        memcpy(s->state, data, len);
        switch(len/8) {
        case 0: s->state[0] ^= (uint64_t)clock();
                //no 'break' is intentional
        case 1: s->state[1] ^= (uint64_t)time(NULL);
        default: /* do nothing */
                break;
        }
    }
    if(0 == s->state[0] && 0 == s->state[1]) {
        s->state[0] = (uint64_t)clock();
        s->state[1] = (uint64_t)time(NULL);
    }
}

uint64_t rand_xorplus128(xorplus128_t* s) {
    uint64_t x = s->state[0];
    const uint64_t y = s->state[1];
    s->state[0] = y;
    x ^= x << 23;
    s->state[1] = (x ^ y ^ (x >> 17) ^ (y >> 26));
    return (s->state[1] + y);
}

static xorplus128_t xp_rand_state;
static void*  xp_rand_mutex;
static int xp_rand_flags;
static uint32_t xp_rand_next;

#include "qtypes.h"
#include "thread.h"

void Xrand_Init(int fake_random) {
    uint8_t buf[16];
    uint64_t tmp, tmp2;
    if(fake_random) {
        srand_xorplus128(&xp_rand_state, (const uint8_t*)"Lorem Ipsum dolor sit amet", 16);
    } else {
        tmp = clock();
        memcpy(buf, &tmp, 8);
        tmp2 = time(NULL);
        if(0 == tmp && tmp == tmp2) {
            tmp2 += 1;
        }
        memcpy(buf, &tmp2, 8);
        srand_xorplus128(&xp_rand_state, buf, 16);
    }
    xp_rand_mutex = Thread_CreateMutex();
    xp_rand_flags = 0;
}

void Xrand_Shutdown(void) {
    Thread_DestroyMutex(&xp_rand_mutex);
}

int32_t xrand(void) {
    uint64_t res;
    uint32_t now;
    Thread_LockMutex(xp_rand_mutex);

    if(0 == xp_rand_flags) {
        res = rand_xorplus128(&xp_rand_state);
        now = res & UINT64_C(0xffffffff);
        xp_rand_next = (res >> 32) & UINT64_C(0xffffffff);
        xp_rand_flags = 1;
    } else {
        now = xp_rand_next;
        xp_rand_flags = 0;
    }

    Thread_UnlockMutex(xp_rand_mutex);
    return now & UINT32_C(0x7fffffff);
}

