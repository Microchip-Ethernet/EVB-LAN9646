
#ifndef PTP_PROTOCOL_H
#define PTP_PROTOCOL_H

#include "ptp_msg.h"
#include "ptp_clock.h"


static
void ptp_tx_msg(struct ptp_message *m, u8 index, u16 portmap, u8 queue,
    u8 udp, u16 vid, u32 sec, u32 nsec, u8 *dest);

static
void ptp_save_rx_msg(struct ptp_message *m);

static
void ptp_save_tx_msg(struct ptp_message *m, u8 index, u16 portmap, u8 queue,
    u8 udp, u16 vid, u32 sec, u32 nsec);

void ptp_init_announce(struct ptp_clock *c, struct ptp_msg *msg);
void ptp_init_sync(struct ptp_clock *c, struct ptp_msg *msg);
void ptp_init_follow_up(struct ptp_clock *c, struct ptp_msg *msg);
void ptp_init_delay_req(struct ptp_clock *c, struct ptp_msg *msg);
void ptp_init_delay_resp(struct ptp_clock *c, struct ptp_msg *msg);
void ptp_init_pdelay_req(struct ptp_clock *c, struct ptp_msg *msg);
void ptp_init_manage(struct ptp_clock *c, struct ptp_msg *msg);
void ptp_init_signal(struct ptp_clock *c, struct ptp_msg *msg);

int broadcast_clock_id(struct ptp_clock_identity *n);
int same_clock_id(struct ptp_clock_identity *h,
    struct ptp_clock_identity *n);
int same_port_id(struct ptp_port_identity *h,
    struct ptp_port_identity *n);

void ptp_utime_to_timestamp(u16 sec, struct ptp_utime *u,
    struct ptp_timestamp *t);
int ptp_interval_to_cnt(int interval, int seconds);
int ptp_interval_to_ms(int interval);
int ptp_random_ms(int ms, int percent);

struct ptp_tlv *ptp_next_tlv(u8 **data, u16 *len);
void *ptp_add_tlv(struct ptp_tlv *tlv, void *tlv_end, u16 type, u16 len,
    u16 *size);

void port_tx_delay_req(struct ptp_port *p);
void port_tx_pdelay_req(struct ptp_port *p);

void port_fwd_announce(struct ptp_port *p, struct ptp_message *m);
void port_tx_announce(struct ptp_port *p);
void port_fwd_sync(struct ptp_port *p, struct ptp_message *m);
void port_fwd_fup(struct ptp_port *p, struct ptp_message *m, s64 residence);
void port_tx_sync(struct ptp_port *p);

void port_tx_signal_interval(struct ptp_port *p, int delay, int sync, int ann,
    struct ptp_message *m);

void port_announce_timeout(struct ptp_port *p);

void handle_state_decision(struct ptp_clock *c);

#endif

