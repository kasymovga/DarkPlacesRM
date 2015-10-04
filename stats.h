#pragma once
#ifndef STATS_H
#define STATS_H

#include <stdint.h>

/* Provides an API for computing the Sample Variance and Mean, as well as
 * Min/Max value (and position in the data) in O(n) time and O(1) space.
 *
 * The algorithm is online, i.e. one can provide data piece by piece.
 *
 * Based upon a 1963 paper as cited in Knuth's TAOCP, as referenced by
 * http://www.johndcook.com/blog/standard_deviation/
 */

typedef struct {
    double M_i, S_i;
    double min_value, max_value;
    uint64_t count, min_index, max_index;
} stats_t;

stats_t* Stats_Init(void);
void Stats_Reset(stats_t*);
void Stats_Destroy(stats_t*);

void Stats_Add(stats_t*, double);

double Stats_Variance(const stats_t*);
double Stats_Mean(const stats_t*);

double Stats_Min(const stats_t*);
double Stats_Max(const stats_t*);

uint64_t Stats_MinIndex(const stats_t*);
uint64_t Stats_MaxIndex(const stats_t*);

#endif /* STATS_H */
