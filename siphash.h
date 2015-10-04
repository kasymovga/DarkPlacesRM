#pragma once
#ifndef SIPHASH_H
#define SIPHASH_H

#include <stdint.h>

int siphash(uint64_t* out, const uint8_t* in, uint64_t inlen, const uint8_t* k);

#endif
