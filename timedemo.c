#include "timedemo.h"

#include <float.h>
#include <stdlib.h>

timedemo_t* TimeDemo_Init(void) {
    timedemo_t* res = malloc(sizeof(timedemo_t));
    if(res) {
        Stats_Reset(&res->stats);
    }

    return res;
}

void TimeDemo_Destroy(timedemo_t* t) {
    if(t) {
        free(t);
        t = NULL;
    }
}

void TimeDemo_Reset(timedemo_t* t) {
    Stats_Reset(&t->stats);
    t->last_begin = 0;
}


void TimeDemo_BeginFrame(timedemo_t* t) {
    if(t) {
        t->last_begin = clock();
    }
}

void TimeDemo_EndFrame(timedemo_t* t) {
    clock_t clock_delta = clock();
    double time_delta;

    if(t) {
        clock_delta -= t->last_begin;
        time_delta = (double)(clock_delta) / CLOCKS_PER_SEC;

        Stats_Add(&t->stats, time_delta);
    }
}


double TimeDemo_Variance(const timedemo_t* t) {
    if(t) {
        return Stats_Variance(&t->stats);
    } else {
        return 0.;
    }
}

double TimeDemo_Mean(const timedemo_t* t) {
    if(t) {
        return Stats_Mean(&t->stats);
    } else {
        return 0.;
    }
}


double TimeDemo_Min(const timedemo_t* t) {
    if(t) {
        return Stats_Min(&t->stats);
    } else {
        return 0.;
    }
}

double TimeDemo_Max(const timedemo_t* t) {
    if(t) {
        return Stats_Max(&t->stats);
    } else {
        return 0.;
    }
}


uint64_t TimeDemo_MinIndex(const timedemo_t* t) {
    if(t) {
        return Stats_MinIndex(&t->stats);
    } else {
        return UINT64_MAX;
    }
}

uint64_t TimeDemo_MaxIndex(const timedemo_t* t) {
    if(t) {
        return Stats_MaxIndex(&t->stats);
    } else {
        return UINT64_MAX;
    }
}
