

#ifndef PTP_FILTER_H
#define PTP_FILTER_H

#include "ptp_ptu.h"


struct mmedian_data {
    int order;
    s64 delays;
};

struct mmedian {
    int len;
    int cnt;
    int index;
    struct mmedian_data *data;
};

struct ptp_filter {
    struct ptu t1;
    struct ptu t2;
    struct ptu t3;
    struct ptu t4;
    double clock_ratio;
    s64 delay;
    u8 delay_valid;

    struct mmedian median;
};

s64 filter_sample(struct mmedian *m, s64 delay);
void filter_set_clock_ratio(struct ptp_filter *f, double clock_ratio);
void filter_set_t1_t2(struct ptp_filter *f, struct ptu *t1, struct ptu *t2);
void filter_set_t3_t4(struct ptp_filter *f, struct ptu *t3, struct ptu *t4);
void filter_set_delay(struct ptp_filter *f, double delay);
int filter_update_delay(struct ptp_filter *f, s64 *delay, s64 *raw);
int filter_update_offset(struct ptp_filter *f, struct ptu *offset);
void filter_reset(struct ptp_filter *f, int full);

void exit_ptp_filter(struct ptp_filter *f);
void init_ptp_filter(struct ptp_filter *f);

#endif

