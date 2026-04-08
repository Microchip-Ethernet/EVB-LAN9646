
#ifndef PTP_MSG_H
#define PTP_MSG_H

struct ptp_msg_info {
    uint valid:1;
    uint two_step:1;
    uint resp:1;
    s64 correction;
    u16 seqid;
    u8 domain;
    u8 msg;
    struct ptp_port_identity id;
    struct ptp_timestamp ptp_time;
    struct ptp_utime hw_time;
};

struct ptp_msg_save {
    u8 delayed:1;
    u8 tx:1;
    u8 udp:1;
    u8 index;
    u8 queue;
    u16 portmap;
    u16 vid;
    u32 sec;
    u32 nsec;
};

struct ptp_message {
    u8 refcnt;
    void *p;
    struct ptp_utime rx;
    struct ptp_msg_save save;
    struct ptp_msg *msg;
    u8 payload[14];
};

struct ptp_message *get_tx_msg(void);
void tx_msg_lock(void);
void tx_msg_unlock(void);
void tx_msg_init(void);

void ptp_msg_tx(struct ptp_msg *msg);

struct ptp_message *alloc_msg(struct ptp_msg *msg, int len,
    struct ptp_utime *rx);
void free_msg(struct ptp_message *m);
void msg_get(struct ptp_message *m);
void msg_put(struct ptp_message *m);
void msg_clear(struct ptp_message **m);
void msg_save(struct ptp_message **s, struct ptp_message *m);

void msg_info_clear(struct ptp_msg_info *info);
void msg_info_save(struct ptp_msg_info *info, struct ptp_msg *msg,
    struct ptp_utime *hw_time);
bool msg_info_valid(struct ptp_msg_info *info);

#endif

