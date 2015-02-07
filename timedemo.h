#pragma once
#ifndef TIMEDEMO_H
#define TIMEDEMO_H

#include <stdint.h>
#include <time.h>

#include "stats.h"

typedef struct {
    //performance optimization: stack stats instead of pointer
    stats_t stats;
    clock_t last_begin;
} timedemo_t;

extern timedemo_t *tdstats;

timedemo_t* TimeDemo_Init(void);
void        TimeDemo_Destroy(timedemo_t*);
void        TimeDemo_Reset(timedemo_t*);

void TimeDemo_BeginFrame(timedemo_t*);
void TimeDemo_EndFrame(timedemo_t*);

double TimeDemo_Variance(const timedemo_t*);
double TimeDemo_Mean(const timedemo_t*);

double TimeDemo_Min(const timedemo_t*);
double TimeDemo_Max(const timedemo_t*);

uint64_t TimeDemo_MinIndex(const timedemo_t*);
uint64_t TimeDemo_MaxIndex(const timedemo_t*);

#endif /* TIMEDEMO_H */
