
#include "ptp_protocol.h"

static bool ptp_signal_interval(struct ptp_clock *c, struct ptp_port *p,
    void *data)
{
    struct ptp_organization_ext_tlv *org = (struct ptp_organization_ext_tlv *)
        data;
    struct IEEE_802_1AS_data_2 *interval = (struct IEEE_802_1AS_data_2 *)
        org->dataField;
    int linkdelay;
    int timesync;
    int ann;

    if (org->organizationId[0] == 0x00 &&
        org->organizationId[1] == 0x80 &&
        org->organizationId[2] == 0xC2 &&
        org->organizationSubType[0] == 0 &&
        org->organizationSubType[1] == 0 &&
        org->organizationSubType[2] == 2) {

        /* Report once when not supposed to receive this message. */
        if (p->report_signal) {
            p->report_signal = 0;
            exception_log(0, 0, "Signaling received at p-%u", p->id);
        }

        /* Apply only when acting as AED master. */
        if (!port_is_aed_master(p))
            return true;

        linkdelay = interval->linkDelayInterval;
        timesync = interval->timeSyncInterval;
        ann = interval->announceInterval;
        if (linkdelay != 127 && linkdelay != 126) {

        }
        if (timesync != 127 && timesync != 126) {
            if (timesync != p->logSyncInterval) {
                ptp_change_master_interval(p, timesync);
            }
        }
        if (ann != 127 && ann != 126) {

        }
        return true;
    }
    return false;
}  /* ptp_signal_interval */

void handle_signaling(struct ptp_port *p, struct ptp_message *m)
{
    struct ptp_msg *msg = m->msg;
    struct ptp_msg_signaling_base *b = &msg->data.signaling.b;
    u16 len;

    /* Make sure payload is there. */
    len = msg->hdr.messageLength - sizeof(struct ptp_msg_hdr);
    if (len <= sizeof(struct ptp_msg_signaling_base))
        return;

    if (!same_clock_id(&p->c->id, &b->targetPortIdentity.clockIdentity) &&
        !broadcast_clock_id(&b->targetPortIdentity.clockIdentity)) {
        return;
    }

    ptp_save_rx_msg(m);
}

void proc_signaling(struct ptp_port *p, struct ptp_message *m)
{
    struct ptp_msg *msg = m->msg;
    struct ptp_msg_signaling_base *b = &msg->data.signaling.b;
    struct ptp_clock *c = NULL;
    struct ptp_tlv *tlv;
    u8 *data;
    u16 len;

    if (same_clock_id(&p->c->id, &b->targetPortIdentity.clockIdentity) ||
        broadcast_clock_id(&b->targetPortIdentity.clockIdentity)) {
        c = p->c;
    }

    len = msg->hdr.messageLength - sizeof(struct ptp_msg_hdr);
    len -= sizeof(struct ptp_msg_signaling_base);
    data = (u8 *)(b + 1);
    tlv = ptp_next_tlv(&data, &len);
    while (tlv) {
        switch (tlv->tlvType) {
        case TLV_ORGANIZATION_EXTENSION:
            ptp_signal_interval(c, p, tlv);
            break;
        case TLV_REQUEST_UNICAST_TRANSMISSION:
            break;
        case TLV_GRANT_UNICAST_TRANSMISSION:
            break;
        case TLV_CANCEL_UNICAST_TRANSMISSION:
            break;
        case TLV_ACKNOWLEDGE_CANCEL_UNICAST_TRANSMISSION:
            break;
        }
        tlv = ptp_next_tlv(&data, &len);
    }
}
