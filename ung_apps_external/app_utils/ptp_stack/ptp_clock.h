/*
    Copyright (C) 2020-2026 Microchip Technology Inc. and its
    subsidiaries (Microchip).  All rights reserved.

    You are permitted to use the software and its derivatives with Microchip
    products. See the license agreement accompanying this software, if any,
    for additional info regarding your rights and obligations.

    SOFTWARE AND DOCUMENTATION ARE PROVIDED "AS IS" WITHOUT WARRANTY OF ANY
    KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION, ANY
    WARRANTY OF MERCHANTABILITY, TITLE, NON-INFRINGEMENT AND FITNESS FOR A
    PARTICULAR PURPOSE. IN NO EVENT SHALL MICROCHIP, OR ITS LICENSORS BE
    LIABLE OR OBLIGATED UNDER CONTRACT, NEGLIGENCE, STRICT LIABILITY,
    CONTRIBUTION, BREACH OF WARRANTY, OR OTHER LEGAL EQUITABLE THEORY FOR
    ANY DIRECT OR INDIRECT DAMAGES OR EXPENSES INCLUDING BUT NOT LIMITED TO
    ANY INCIDENTAL, SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES, OR OTHER
    SIMILAR COSTS. TO THE FULLEST EXTENT ALLOWED BY LAW, MICROCHIP AND ITS
    LICENSORS LIABILITY WILL NOT EXCEED THE AMOUNT OF FEES, IF ANY, THAT YOU
    PAID DIRECTLY TO MICROCHIP TO USE THIS SOFTWARE. MICROCHIP PROVIDES THIS
    SOFTWARE CONDITIONALLY UPON YOUR ACCEPTANCE OF THESE TERMS.
*/

#ifndef PTP_CLOCK_H
#define PTP_CLOCK_H

#include "ptp_bmca.h"
#include "ptp_filter.h"
#include "ptp_port.h"
#include "ptp_servo.h"

#define MAX_PTP_CLOCKS  3

struct ptp_clock {
    u8 cfg_index;
    u8 cnt;
    u8 index;
    u8 domain;
    u8 delay_mechanism;
    u8 specific;
    u8 transport;
    u16 ports;
    u16 vid;
    struct ptp_port host_port;
    struct ptp_port *ptp_ports[MAX_PTP_PORTS + 1];

    struct ptp_port *slave_port;
    u16 live_ports;

    /* This portmap specifies which ports to send. */
    u16 portmap;

    signed char logAnnounceInterval;
    signed char logSyncInterval;
    u8 announceReceiptTimeout;
    u8 syncReceiptTimeout;
    u8 fupReceiptTimeout;
    int rx_fup_seqid;
    int rx_sync_seqid;
    u16 tx_fup_seqid;
    u16 tx_sync_seqid;

    /* PTP clock characteristics. */
    struct ptp_clock_identity id;

    /* Used in Announce. */
    struct ptp_clock_identity master_clock;
    s16 utc_offset;
    s64 residence;
    u8 time_source;
    struct bmc_dataset dataset;
    struct foreign_dataset foreign;
    struct foreign_dataset *best;
    struct ptp_clock_identity best_id;
    struct ptp_default_ds dds;
    struct ptp_current_ds cur;
    struct ptp_parent_ds pds;

    int servo_state;
#if 1
    double ratio;
#endif
    struct ptu offset;
    struct ptp_servo servo;
    struct ptp_utime first_sync_tx;
    struct ptp_utime last_sync_rx;

    /* PTP messages for forwarding. */
    struct ptp_message *last_ann;
    struct ptp_message *last_sync;
    struct ptp_message *last_fup;
    struct ptp_message *last_rx_fup;
    struct ptp_message *base_sync;
    struct ptp_message *base_fup;

    struct ptp_msg_info rx_sync;
    struct ptp_msg_info rx_fup;

    struct ptp_msg ann_template;
    struct ptp_msg sync_template;
    struct ptp_msg fup_template;
    struct ptp_msg delay_req_template;
    struct ptp_msg delay_resp_template;
    struct ptp_msg pdelay_req_template;
    struct ptp_msg manage_template;
    struct ptp_msg signal_template;

    u8 ann_max;
    u8 sync_max;
    u8 ann_cnt;
    u8 sync_cnt;
    u16 time_cnt;

    uint no_announce:1;
    uint sync_timeout:1;
    uint sync_timeout_ready:1;
    uint setup:1;
    uint stop:1;
    uint tx_sync:1;
    uint miss_sync:1;
    uint miss_fup:1;
    uint signaling:1;
    uint first_sync:1;
    uint report_sync:1;
    uint report_link:1;
    uint req_interval:1;
    uint set_interval_force:1;

    unsigned long last_sync_jiffies;
    unsigned long last_sync_time_jiffies;
    unsigned long oper_sync_jiffies;
    unsigned long prev_sync_jiffies;
    unsigned long set_sync_timeout_jiffies;

    struct ksz_schedule_work update_interval_work;
    struct ksz_schedule_work ethernet_ready_work;

    struct ksz_schedule_work sync_work;
    struct ksz_schedule_work interval_work;
    struct ksz_schedule_work sync_timeout_work;
    struct ksz_schedule_work fup_timeout_work;
    struct ksz_schedule_work signaling_work;
    struct ksz_timer_info sync_timer_info;
    struct ksz_timer_info interval_timer_info;
    struct ksz_timer_info sync_timeout_info;
    struct ksz_timer_info fup_timeout_info;
    struct ksz_timer_info signaling_timer_info;
};

void clock_enable_port(struct ptp_clock *c, struct ptp_port *p, int enable);

struct ptp_message *clock_get_msg(struct ptp_clock *c, struct ptp_msg *msg,
    struct ptp_message *m);
struct ptp_message *clock_new_msg(struct ptp_clock *c, u8 type,
    struct ptp_message *m);

u8 clock_class(struct ptp_clock *c);
u16 clock_steps(struct ptp_clock *c);
struct bmc_dataset *clock_default_ds(struct ptp_clock *c);
struct bmc_dataset *clock_best_ds(struct ptp_clock *c);
struct ptp_port *clock_best_port(struct ptp_clock *c);
struct ptp_port *clock_host_port(struct ptp_clock *c);
struct ptp_port *clock_slave_port(struct ptp_clock *c);
void clock_set_slave_port(struct ptp_clock *c, struct ptp_port *p);

void clock_set_initial_sync_timeout(struct ptp_clock *c, int delay);
void clock_set_sync_timeout(struct ptp_clock *c);
void clock_stop_sync_timeout(struct ptp_clock *c);
void clock_set_fup_timeout(struct ptp_clock *c);
void clock_stop_fup_timeout(struct ptp_clock *c);
void clock_set_sync_interval(struct ptp_clock *c);
void clock_stop_sync_interval(struct ptp_clock *c);

void clock_change_sync_timer(struct ptp_clock *c, int interval);
void clock_start_sync_timer(struct ptp_clock *c);
void clock_stop_sync_timer(struct ptp_clock *c);

void clock_start_signaling_timer(struct ptp_clock *c, bool oper);
void clock_stop_signaling_timer(struct ptp_clock *c);

struct ptp_clock *get_ptp_clock(u8 domain);

#endif

