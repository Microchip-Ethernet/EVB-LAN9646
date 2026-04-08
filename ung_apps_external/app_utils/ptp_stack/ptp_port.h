

#ifndef PTP_PORT_H
#define PTP_PORT_H

#include "ptp_bmca.h"
#include "ptp_filter.h"
#include "ptp_msg.h"

#define MAX_PTP_PORTS  13

#if (MAX_PTP_PORTS > 7)
#define MAX_PDELAY_RESP  4
#else
#define MAX_PDELAY_RESP  8
#endif

enum {
    EVENT_NONE,
    EVENT_FAULT_SET,
    EVENT_FAULT_CLEAR,
    EVENT_ENABLE,
    EVENT_DISABLE,
    EVENT_ANNOUNCE_TIMEOUT,
    EVENT_QUALIFICATION_TIMEOUT,
    EVENT_CLOCK_STABLE,
    EVENT_CLOCK_UNSTABLE,
    EVENT_READY_MASTER,
    EVENT_BECOME_MASTER,
    EVENT_BECOME_SLAVE,
    EVENT_BECOME_PASSIVE,
    EVENT_INIT_COMPLETE,
};

#define ALLOWED_LOST_RESPONSES  3

static u8 allowed_lost_responses = ALLOWED_LOST_RESPONSES;
static u8 allowed_pdelay_faults = 5;

struct nrate_info {
    double ratio;
    struct ptu tx1;
    struct ptu tx2;
    struct ptu rx1;
    struct ptu rx2;
    u8 ratio_valid;
#if 1
    double last_ratio;
    int last_diff;
#endif
};

struct ptp_pdelay_info {
    u8 index;
    s64 delay;
    s64 raw_delay;
    struct nrate_info nrate;
    struct ptp_filter filter;
    struct ptp_port_identity id;
};

struct ptp_multi_pdelay {
    struct ptp_msg_info rx_pdelay_resp[MAX_PDELAY_RESP];
    struct ptp_msg_info rx_pdelay_fup[MAX_PDELAY_RESP];
    struct ptp_pdelay_info pdelay_info[MAX_PDELAY_RESP];
};

struct ptp_clock;

struct ptp_port {
    u8 index;
    u8 peer_index;

    /* This portmap specifies which ports to send. */
    u16 portmap;
    u16 id;
    u8 state;
    u8 last_state;
    u8 next_state;

    u32 gptp:1;
    u32 gptp_auto:1;
    u32 gptp_slave:1;
    u32 p2p_two_step:1;
    u32 sync_two_step:1;
    u32 path_trace:1;
    u32 follow_up_tlv:1;
    u32 peer_resp:1;
    u32 peer_valid:1;
    u32 no_id_check:1;
    u32 no_id_replace:1;
    u32 report_announce:1;
    u32 report_mismatched:2;
    u32 report_pdelay:1;
    u32 report_ready:1;
    u32 report_ready_done:1;
    u32 report_resp:1;
    u32 report_signal:1;
    u32 report_sync:1;
    u32 as_capable:1;
    u32 as_capable_set:1;
    u32 no_capable:1;
    u32 no_link:1;
    u32 p2p:1;
    u32 use_delay:1;
    u32 use_one_way_resp:1;
    u32 allow_multi_resp:1;
    u32 mult_resp:1;
    u32 own_clock_resp:1;

    int neighborDelayThresh;

    struct ptp_port_identity peer_id;
    u8 peer_addr[6];

    struct ptp_port_ds ds;
    signed char initialPdelayReqInterval;
    signed char operPdelayReqInterval;
    signed char initialSyncInterval;
    signed char operSyncInterval;
    signed char pdelay_req_interval;
    signed char logSyncInterval;
    u16 rx_latency;
    u16 tx_latency;
    s16 asym_delay;
    u16 initialSyncTimeout;
    u16 waitPdelayReqInterval;
    u16 waitSyncInterval;

    struct foreign_dataset *best;
    struct foreign_dataset foreign_masters[MAX_FOREIGN_MASTERS];

    /* PTP messages for tracking. */
    struct ptp_msg_info tx_sync;
    struct ptp_msg_info tx_delay_req;
    struct ptp_msg_info tx_pdelay_resp;
    struct ptp_msg_info rx_delay_resp;
    struct ptp_msg_info rx_pdelay_fup;

    struct ptp_clock *c;
    struct ptp_pdelay_info pdelay_info;

    u16 seqid_ann;
    u16 seqid_delay_req;
    u16 seqid_sync;
    u16 seqid_manage;
    u16 seqid_signal;
#ifdef LINUX_PTP
#if 1
    u16 last_seqid_rx_ann;
    u16 last_seqid_rx_sync;
    u16 last_seqid_rx_fup;
    u16 last_seqid_rx_delay_req;
    u16 last_seqid_rx_delay_resp;
    u16 last_seqid_rx_pdelay_resp_fup;
    u16 last_seqid_tx_ann;
    u16 last_seqid_tx_sync;
    u16 last_seqid_tx_fup;
    u16 last_seqid_tx_delay_req;
    u16 last_seqid_tx_delay_resp;
    u16 last_seqid_tx_pdelay_resp_fup;
#endif
#endif
    u16 as_capable_err;
    u8 resp_missing;
    u8 resp_cnt;
    u8 resp_fup_cnt;
    u8 mult_resp_cnt;
    u8 mult_resp_fup_cnt;
    u16 cnt_delay_req;
    u16 cnt_sync_rx;
    u8 cnt_delay_resp;
    u8 cnt_sync_tx;
    u8 cnt_fup_tx;
    u8 max_sync_tx;
    u8 max_delay_req_tx;
    int delay_interval;
    int last_fault;
    struct ksz_schedule_work delay_req_work;
    struct ksz_timer_info delay_timer_info;
    struct ksz_schedule_work ann_timeout_work;
    struct ksz_timer_info ann_timeout_timer_info;
    struct ksz_schedule_work qual_timeout_work;
    struct ksz_timer_info qual_timeout_timer_info;
    struct ksz_schedule_work fault_timeout_work;
    struct ksz_timer_info fault_timeout_timer_info;

    struct ptp_multi_pdelay *multi_pdelay;
    unsigned long last_tx_sync_jiffies;
};

struct ptp_port *get_ptp_hw_port(u8 port);
struct ptp_port *get_ptp_port(u8 port);

int host_event(struct ptp_port *p, int event);
int port_event(struct ptp_port *p, int event);
void port_set_state(struct ptp_port *p, int state);

void port_check_capable(struct ptp_port *p);
int port_capable(struct ptp_port *p);

int port_state(struct ptp_port *p);
bool port_is_aed(struct ptp_port *p);
bool port_is_aed_master(struct ptp_port *p);
struct bmc_dataset *port_best_ds(struct ptp_port *p);

/* In ptp_sync. */
void port_path_delay(struct ptp_port *p, struct ptp_utime *egress,
    struct ptp_timestamp *receive, s64 corr_resp);
void port_assign_peer_delay(struct ptp_port *p, struct ptp_pdelay_info *info);
void port_peer_delay(struct ptp_port *p, struct ptp_pdelay_info *info,
    struct ptp_utime *egress, struct ptp_utime *ingress,
    struct ptp_timestamp *receive, s64 corr_resp,
    struct ptp_timestamp *transmit, s64 corr_fup);
void port_sync(struct ptp_port *p, struct ptp_utime *ingress,
    struct ptp_timestamp *origin, s64 cor_sync, s64 cor_fup, u16 seqid);

void port_forward_sync(struct ptp_port *p, struct ptp_message *m);
void port_forward_fup(struct ptp_port *p, struct ptp_message *m);

void port_set_announce_timeout(struct ptp_port *p);
void port_stop_announce_timeout(struct ptp_port *p);

void port_set_qualification_timeout(struct ptp_port *p);
void port_stop_qualification_timeout(struct ptp_port *p);

void port_set_fault_timeout(struct ptp_port *p, int fault, int ms);
void port_stop_fault_timeout(struct ptp_port *p);

void port_init(struct ptp_port *p);

void exit_ptp_port_profile(struct ptp_port *p);
void exit_ptp_port_hw(struct ptp_port *p);
void init_ptp_port_profile(struct ptp_port *p, u8 delay_mechanism,
    int two_step, int delay);
void init_ptp_port_hw(struct ptp_port *p, u16 id, u8 index);

#endif
