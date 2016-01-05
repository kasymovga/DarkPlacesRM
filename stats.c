#include <float.h>
#include <stdlib.h>

#include "stats.h"

stats_t* Stats_Init(void) {
    stats_t* res = calloc(1, sizeof(stats_t));

    Stats_Reset(res);
    return res;
}

void Stats_Reset(stats_t* s) {
    if(s) {
        s->S_i = 0.;
        s->M_i = 0.;
        s->min_value = DBL_MAX;
        s->max_value = DBL_MIN;
        s->min_index = UINT64_MAX;
        s->max_index = UINT64_MAX;
        s->count = 0;
    }
}

void Stats_Destroy(stats_t* s) {
    if(s) {
        free(s);
        s = NULL;
    }
}

void Stats_Add(stats_t* s, double value) {
    volatile double tmp, mk, sk;

    if(!s->count) {
        mk = value;
        sk = 0.;
    } else {
        tmp = (value - s->M_i);
        mk = s->M_i + tmp / (s->count + 1);
        sk = s->S_i + tmp * (value - mk);
    }

    s->M_i = mk;
    s->S_i = sk;
    if(value < s->min_value) {
        s->min_value = value;
        s->min_index = s->count;
    }
    if(value > s->max_value) {
        s->max_value = value;
        s->max_index = s->count;
    }
    s->count++;
}

double Stats_Variance(const stats_t* s) {
    if(s) {
        return s->S_i / (s->count - 1);
    } else {
        return 0.;
    }
}

double Stats_Mean(const stats_t* s) {
    if(s) {
        return s->M_i;
    } else {
        return 0.;
    }
}

double Stats_Min(const stats_t* s) {
    if(s) {
        return s->min_value;
    } else {
        return 0.;
    }
}

double Stats_Max(const stats_t* s) {
    if(s) {
        return s->max_value;
    } else {
        return 0.;
    }
}

uint64_t Stats_MinIndex(const stats_t* s) {
    if(s) {
        return s->min_index;
    } else {
        return UINT64_MAX;
    }
}

uint64_t Stats_MaxIndex(const stats_t* s) {
    if(s) {
        return s->max_index;
    } else {
        return UINT64_MAX;
    }
}
