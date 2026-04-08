
#ifndef PTP_SERVO_H
#define PTP_SERVO_H

#include "ptp_ptu.h"


enum {
    SERVO_UNLOCKED,
    SERVO_JUMP,
    SERVO_JUMP_LONG,
    SERVO_LOCKING,
    SERVO_LOCKED,
};

struct ptp_servo {
    struct ptu last_offset;
    struct ptu last_ingress;
    struct ptu last_interval;
    struct ptu last_dbg_ingress;
    int kp;
    int ki;
    s64 drift;
    int freq_diff;
    int last_freq;
    u8 drift_valid;
};

void servo_adj_ingress(struct ptp_servo *s, struct ptu *offset);
int servo_sample(struct ptp_servo *s, struct ptu *offset,
    struct ptu *ingress, int *adj, int *state);

void init_ptp_servo(struct ptp_servo *s);

#endif

