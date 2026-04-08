

#ifndef PTP_FORWARD_H
#define PTP_FORWARD_H

#include "ptp_msg.h"
#include "ptp_clock.h"

void ptp_check_residence(struct ptp_clock *c);
void ptp_fwd_fup(struct ptp_port *p, struct ptp_message *m);
void ptp_forward(struct ptp_port *q, struct ptp_message *m);
void ptp_save_sync(struct ptp_clock *c, struct ptp_message *m);
void ptp_save_fup(struct ptp_clock *c, struct ptp_message *m);

void ptp_set_sync_interval(struct ptp_clock *c);
void ptp_setup_sync_interval(struct ptp_clock *c);
void ptp_change_master_interval(struct ptp_port *p, int interval);
void ptp_change_slave_interval(struct ptp_clock *c, int interval, bool now);

#endif

