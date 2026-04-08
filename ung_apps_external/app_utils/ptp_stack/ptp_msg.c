
#include "ptp_msg.h"

static void ptp_hdr_rx(struct ptp_msg_hdr *hdr)
{
    hdr->correctionField = betoh64(hdr->correctionField);
    hdr->messageLength = ntohs(hdr->messageLength);
    hdr->sourcePortIdentity.port = ntohs(hdr->sourcePortIdentity.port);
    hdr->sequenceId = ntohs(hdr->sequenceId);
}

static void ptp_hdr_tx(struct ptp_msg_hdr *hdr)
{
    hdr->correctionField = htobe64(hdr->correctionField);
    hdr->messageLength = htons(hdr->messageLength);
    hdr->sourcePortIdentity.port = htons(hdr->sourcePortIdentity.port);
    hdr->sequenceId = htons(hdr->sequenceId);
}

static void ptp_port_identity_rx(struct ptp_port_identity *id)
{
    id->port = ntohs(id->port);
}

static void ptp_port_identity_tx(struct ptp_port_identity *id)
{
    id->port = htons(id->port);
}

static void ptp_timestamp_rx(struct ptp_timestamp *ts)
{
    ts->sec.hi = ntohs(ts->sec.hi);
    ts->sec.lo = ntohl(ts->sec.lo);
    ts->nsec = ntohl(ts->nsec);
}

static void ptp_timestamp_tx(struct ptp_timestamp *ts)
{
    ts->sec.hi = htons(ts->sec.hi);
    ts->sec.lo = htonl(ts->sec.lo);
    ts->nsec = htonl(ts->nsec);
}

static void ptp_tlv_manage_rx(struct ptp_tlv *tlv)
{
    struct ptp_management_tlv *manage =
        (struct ptp_management_tlv *) tlv;

    manage->managementId = ntohs(manage->managementId);
    switch (manage->managementId) {
    case M_DEFAULT_DATA_SET:
        break;
    case M_CURRENT_DATA_SET:
        break;
    case M_PARENT_DATA_SET:
        break;
    case M_PORT_DATA_SET:
        break;
    default:
        break;
    }
}

static void ptp_tlv_manage_tx(struct ptp_tlv *tlv)
{
    struct ptp_management_tlv *manage =
        (struct ptp_management_tlv *) tlv;

    switch (manage->managementId) {
    case M_DEFAULT_DATA_SET:
    {
        struct ptp_default_ds *ds = (struct ptp_default_ds *)
            manage->dataField;

        ds->numberPorts = htons(ds->numberPorts);
        ds->clockQuality.offsetScaledLogVariance =
            htons(ds->clockQuality.offsetScaledLogVariance);
        break;
    }
    case M_CURRENT_DATA_SET:
    {
        struct ptp_current_ds *ds = (struct ptp_current_ds *)
            manage->dataField;

        ds->stepsRemoved = htons(ds->stepsRemoved);
        ds->offsetFromMaster = htobe64(ds->offsetFromMaster);
        ds->meanPathDelay = htobe64(ds->meanPathDelay);
        break;
    }
    case M_PARENT_DATA_SET:
    {
        struct ptp_parent_ds *ds = (struct ptp_parent_ds *)
            manage->dataField;

        ds->parentPortIdentity.port = htons(ds->parentPortIdentity.port);
        ds->observedParentOffsetScaledLogVariance =
            htons(ds->observedParentOffsetScaledLogVariance);
        ds->observedParentClockPhaseChangeRate =
            htonl(ds->observedParentClockPhaseChangeRate);
        ds->grandmasterClockQuality.offsetScaledLogVariance =
            htons(ds->grandmasterClockQuality.offsetScaledLogVariance);
        break;
    }
    case M_PORT_DATA_SET:
    {
        struct ptp_port_ds *ds = (struct ptp_port_ds *)
            manage->dataField;

        ds->portIdentity.port = htons(ds->portIdentity.port);
        ds->peerMeanPathDelay = htobe64(ds->peerMeanPathDelay);
        break;
    }
    case M_PORT_DATA_SET_NP:
    {
        struct ptp_port_ds_gptp *ds = (struct ptp_port_ds_gptp *)
            manage->dataField;

        ds->neighborPropDelayThresh = htonl(ds->neighborPropDelayThresh);
        ds->asCapable = htonl(ds->asCapable);
        break;
    }
    default:
        break;
    }
    manage->managementId = htons(manage->managementId);
}

static void ptp_tlv_manage_error_rx(struct ptp_tlv *tlv)
{
    struct ptp_management_error_tlv *error =
        (struct ptp_management_error_tlv *) tlv;

    error->managementErrorId = ntohs(error->managementErrorId);
    error->managementId = ntohs(error->managementId);
    error->reserved1 = ntohl(error->reserved1);   
}

static void ptp_tlv_manage_error_tx(struct ptp_tlv *tlv)
{
    struct ptp_management_error_tlv *error =
        (struct ptp_management_error_tlv *) tlv;

    error->managementErrorId = htons(error->managementErrorId);
    error->managementId = htons(error->managementId);
    error->reserved1 = htonl(error->reserved1);   
}

static void ptp_tlv_org_ext_rx(struct ptp_tlv *tlv)
{
    struct ptp_organization_ext_tlv *org =
        (struct ptp_organization_ext_tlv *) tlv;

    if (org->organizationId[0] == 0x00 &&
        org->organizationId[1] == 0x80 &&
        org->organizationId[2] == 0xC2) {
        if (org->organizationSubType[0] == 0 &&
            org->organizationSubType[1] == 0 &&
            org->organizationSubType[2] == 1) {
            struct IEEE_802_1AS_data_1 *data =
                (struct IEEE_802_1AS_data_1 *) org->dataField;

            data->cumulativeScaledRateOffset =
                ntohl(data->cumulativeScaledRateOffset);
            data->gmTimeBaseIndicator =
                ntohs(data->gmTimeBaseIndicator);

            /* Assumption is the higher part is either 0xFFFF for a negative
             * number or zero or a small number that just goes over the limit
             * of the 64-bit lower part number.
             */
            data->lastGmPhaseChange.hi =
                ntohs(data->lastGmPhaseChange.hi);
            data->lastGmPhaseChange.lo =
                betoh64(data->lastGmPhaseChange.lo);
            data->lastGmPhaseChange.frac =
                ntohs(data->lastGmPhaseChange.frac);
            data->scaledLastGmFreqChange =
                ntohl(data->scaledLastGmFreqChange);
        } else if (org->organizationSubType[0] == 0 &&
            org->organizationSubType[1] == 0 &&
            org->organizationSubType[2] == 2) {
            struct IEEE_802_1AS_data_2 *data =
                (struct IEEE_802_1AS_data_2 *) org->dataField;

            data->reserved = ntohs(data->reserved);
        }
    } else if (org->organizationId[0] == 0x1C &&
               org->organizationId[1] == 0x12 &&
               org->organizationId[2] == 0x9D) {
        if (org->organizationSubType[0] == 0 &&
            org->organizationSubType[1] == 0 &&
            org->organizationSubType[2] == 1) {
            struct IEEE_C37_238_data *data =
                (struct IEEE_C37_238_data *) org->dataField;

            data->grandmasterID = ntohs(data->grandmasterID);
            data->grandmasterTimeInaccuracy = ntohl(data->grandmasterTimeInaccuracy);
            data->networkTimeInaccuracy = ntohl(data->networkTimeInaccuracy);
            data->reserved = ntohs(data->reserved);
        }
    } else if (org->organizationId[0] == 0x00 &&
               org->organizationId[1] == 0x10 &&
               org->organizationId[2] == 0xA1) {
        if (org->organizationSubType[0] == 0 &&
            org->organizationSubType[1] == 0 &&
            org->organizationSubType[2] == 1) {
            struct MICROCHIP_802_1AS_data_1 *data =
                (struct MICROCHIP_802_1AS_data_1 *) org->dataField;
            int i = tlv->lengthField + sizeof(struct ptp_tlv);

            i -= sizeof(struct ptp_organization_ext_tlv) - 1;
            while (i >= sizeof(struct MICROCHIP_802_1AS_data_1)) {
                data->respondingPortIdentity.port =
                    ntohs(data->respondingPortIdentity.port);
                data->peerDelay = ntohl(data->peerDelay);
                data->peerRatio = ntohl(data->peerRatio);
                data++;
                i -= sizeof(struct MICROCHIP_802_1AS_data_1);
            }
        }
    }
}

static void ptp_tlv_org_ext_tx(struct ptp_tlv *tlv)
{
    struct ptp_organization_ext_tlv *org =
        (struct ptp_organization_ext_tlv *) tlv;

    if (org->organizationId[0] == 0x00 &&
        org->organizationId[1] == 0x80 &&
        org->organizationId[2] == 0xC2) {
        if (org->organizationSubType[0] == 0 &&
            org->organizationSubType[1] == 0 &&
            org->organizationSubType[2] == 1) {
            struct IEEE_802_1AS_data_1 *data =
                (struct IEEE_802_1AS_data_1 *) org->dataField;

            data->cumulativeScaledRateOffset =
                htonl(data->cumulativeScaledRateOffset);
            data->gmTimeBaseIndicator =
                htons(data->gmTimeBaseIndicator);
            data->lastGmPhaseChange.hi =
                htons(data->lastGmPhaseChange.hi);
            data->lastGmPhaseChange.lo =
                htobe64(data->lastGmPhaseChange.lo);
            data->lastGmPhaseChange.frac =
                htons(data->lastGmPhaseChange.frac);
            data->scaledLastGmFreqChange =
                htonl(data->scaledLastGmFreqChange);
        } else if (org->organizationSubType[0] == 0 &&
            org->organizationSubType[1] == 0 &&
            org->organizationSubType[2] == 2) {
            struct IEEE_802_1AS_data_2 *data =
                (struct IEEE_802_1AS_data_2 *) org->dataField;

            data->reserved = htons(data->reserved);
        }
    } else if (org->organizationId[0] == 0x1C &&
               org->organizationId[1] == 0x12 &&
               org->organizationId[2] == 0x9D) {
        if (org->organizationSubType[0] == 0 &&
            org->organizationSubType[1] == 0 &&
            org->organizationSubType[2] == 1) {
            struct IEEE_C37_238_data *data =
                (struct IEEE_C37_238_data *) org->dataField;

            data->grandmasterID = htons(data->grandmasterID);
            data->grandmasterTimeInaccuracy =
                htonl(data->grandmasterTimeInaccuracy);
            data->networkTimeInaccuracy =
                htonl(data->networkTimeInaccuracy);
            data->reserved = htons(data->reserved);
        }
    } else if (org->organizationId[0] == 0x00 &&
               org->organizationId[1] == 0x10 &&
               org->organizationId[2] == 0xA1) {
        if (org->organizationSubType[0] == 0 &&
            org->organizationSubType[1] == 0 &&
            org->organizationSubType[2] == 1) {
            struct MICROCHIP_802_1AS_data_1 *data =
                (struct MICROCHIP_802_1AS_data_1 *) org->dataField;
            int i = tlv->lengthField + sizeof(struct ptp_tlv);

            i -= sizeof(struct ptp_organization_ext_tlv) - 1;
            while (i >= sizeof(struct MICROCHIP_802_1AS_data_1)) {
                data->respondingPortIdentity.port =
                    htons(data->respondingPortIdentity.port);
                data->peerDelay = htonl(data->peerDelay);
                data->peerRatio = htonl(data->peerRatio);
                data++;
                i -= sizeof(struct MICROCHIP_802_1AS_data_1);
            }
        }
    }
}

static void ptp_tlv_rx(u8 *data, u16 len)
{
    struct ptp_tlv *tlv;
    u16 size;

    do {
        tlv = (struct ptp_tlv *) data;
        tlv->tlvType = ntohs(tlv->tlvType);
        tlv->lengthField = ntohs(tlv->lengthField);
        size = tlv->lengthField + sizeof(struct ptp_tlv);
        if (len >= size)
            len -= size;
        else {
            break;
        }
        switch (tlv->tlvType) {
        case TLV_MANAGEMENT:
            ptp_tlv_manage_rx(tlv);
            break;
        case TLV_MANAGEMENT_ERROR_STATUS:
            ptp_tlv_manage_error_rx(tlv);
            break;
        case TLV_ORGANIZATION_EXTENSION:
            ptp_tlv_org_ext_rx(tlv);
            break;
        default:
            break;
        }
        data += size;
    } while (len >= sizeof(struct ptp_tlv));
}  /* ptp_tlv_rx */

static void ptp_tlv_tx(u8 *data, u16 len)
{
    struct ptp_tlv *tlv;
    u16 size;

    do {
        tlv = (struct ptp_tlv *) data;
        size = tlv->lengthField + sizeof(struct ptp_tlv);
        if (len >= size)
            len -= size;
        else
            break;
        switch (tlv->tlvType) {
        case TLV_MANAGEMENT:
            ptp_tlv_manage_tx(tlv);
            break;
        case TLV_MANAGEMENT_ERROR_STATUS:
            ptp_tlv_manage_error_tx(tlv);
            break;
        case TLV_ORGANIZATION_EXTENSION:
            ptp_tlv_org_ext_tx(tlv);
            break;
        default:
            break;
        }
        tlv->tlvType = htons(tlv->tlvType);
        tlv->lengthField = htons(tlv->lengthField);
        data += size;
    } while (len >= sizeof(struct ptp_tlv));
}  /* ptp_tlv_tx */

static void ptp_msg_rx(struct ptp_msg *msg)
{
    struct ptp_clock_quality *quality;
    u8 *data;
    u16 len;
    u16 size;

    ptp_hdr_rx(&msg->hdr);
    len = msg->hdr.messageLength;

    /* This message contains just the PTP header for report timestamp only. */
    if (len == sizeof(struct ptp_msg_hdr))
        return;
    switch (msg->hdr.messageType) {
    case SYNC_MSG:
        ptp_timestamp_rx(&msg->data.sync.originTimestamp);
        size = sizeof(struct ptp_msg_sync);
        break;
    case FOLLOW_UP_MSG:
        ptp_timestamp_rx(&msg->data.follow_up.preciseOriginTimestamp);
        size = sizeof(struct ptp_msg_follow_up);
        break;
    case DELAY_RESP_MSG:
        ptp_port_identity_rx(&msg->data.delay_resp.requestingPortIdentity);
        ptp_timestamp_rx(&msg->data.delay_resp.receiveTimestamp);
        size = sizeof(struct ptp_msg_delay_resp);
        break;
    case PDELAY_REQ_MSG:
        ptp_port_identity_rx(&msg->data.pdelay_req.reserved);
        ptp_timestamp_rx(&msg->data.pdelay_req.originTimestamp);
        size = sizeof(struct ptp_msg_pdelay_req);
        break;
    case PDELAY_RESP_MSG:
        ptp_port_identity_rx(&msg->data.pdelay_resp.requestingPortIdentity);
        ptp_timestamp_rx(&msg->data.pdelay_resp.requestReceiptTimestamp);
        size = sizeof(struct ptp_msg_pdelay_resp);
        break;
    case PDELAY_RESP_FOLLOW_UP_MSG:
        ptp_timestamp_rx(&msg->data.pdelay_resp_follow_up.
            responseOriginTimestamp);
        ptp_port_identity_rx(&msg->data.pdelay_resp_follow_up.
            requestingPortIdentity);
        size = sizeof(struct ptp_msg_pdelay_resp_follow_up);
        break;
    case ANNOUNCE_MSG:
        ptp_timestamp_rx(&msg->data.announce.originTimestamp);
        quality = &msg->data.announce.grandmasterClockQuality;
        msg->data.announce.currentUtcOffset =
            ntohs(msg->data.announce.currentUtcOffset),
        quality->offsetScaledLogVariance =
            ntohs(quality->offsetScaledLogVariance);
        msg->data.announce.stepsRemoved = ntohs(msg->data.announce.stepsRemoved);
        size = sizeof(struct ptp_msg_announce);
        break;
    case SIGNALING_MSG:
        msg->data.signaling.b.targetPortIdentity.port =
            ntohs(msg->data.signaling.b.targetPortIdentity.port);
        size = sizeof(struct ptp_msg_signaling_base);
        break;
    case MANAGEMENT_MSG:
        msg->data.management.b.targetPortIdentity.port =
            ntohs(msg->data.management.b.targetPortIdentity.port);
        size = sizeof(struct ptp_msg_management_base);
        break;
    default:
        size = len - sizeof(struct ptp_msg_hdr);
        break;
    }
    data = &msg->data.data[size];
    size += sizeof(struct ptp_msg_hdr);
    if (len >= size) {
        len -= size;
        if (len >= sizeof(struct ptp_tlv)) {
            ptp_tlv_rx(data, len);
        }
    }
}  /* ptp_msg_rx */

void ptp_msg_tx(struct ptp_msg *msg)
{
    struct ptp_clock_quality *quality;
    u8 *data;
    u16 len = msg->hdr.messageLength;
    u16 size;

    ptp_hdr_tx(&msg->hdr);
    switch (msg->hdr.messageType) {
    case SYNC_MSG:
        ptp_timestamp_tx(&msg->data.sync.originTimestamp);
        size = sizeof(struct ptp_msg_sync);
        break;
    case FOLLOW_UP_MSG:
        ptp_timestamp_tx(&msg->data.follow_up.preciseOriginTimestamp);
        size = sizeof(struct ptp_msg_follow_up);
        break;
    case DELAY_RESP_MSG:
        ptp_port_identity_tx(&msg->data.delay_resp.requestingPortIdentity);
        ptp_timestamp_tx(&msg->data.delay_resp.receiveTimestamp);
        size = sizeof(struct ptp_msg_delay_resp);
        break;
    case PDELAY_REQ_MSG:
        ptp_timestamp_tx(&msg->data.pdelay_req.originTimestamp);
        size = sizeof(struct ptp_msg_pdelay_req);
        break;
    case PDELAY_RESP_MSG:
        ptp_port_identity_tx(&msg->data.pdelay_resp.requestingPortIdentity);
        ptp_timestamp_tx(&msg->data.pdelay_resp.requestReceiptTimestamp);
        size = sizeof(struct ptp_msg_pdelay_resp);
        break;
    case PDELAY_RESP_FOLLOW_UP_MSG:
        ptp_timestamp_tx(&msg->data.pdelay_resp_follow_up.
            responseOriginTimestamp);
        ptp_port_identity_tx(&msg->data.pdelay_resp_follow_up.
            requestingPortIdentity);
        size = sizeof(struct ptp_msg_pdelay_resp_follow_up);
        break;
    case ANNOUNCE_MSG:
        ptp_timestamp_tx(&msg->data.announce.originTimestamp);
        quality = &msg->data.announce.grandmasterClockQuality;
        msg->data.announce.currentUtcOffset =
            htons(msg->data.announce.currentUtcOffset),
        quality->offsetScaledLogVariance =
            htons(quality->offsetScaledLogVariance);
        msg->data.announce.stepsRemoved = htons(msg->data.announce.stepsRemoved);
        size = sizeof(struct ptp_msg_announce);
        break;
    case SIGNALING_MSG:
        msg->data.signaling.b.targetPortIdentity.port =
            htons(msg->data.signaling.b.targetPortIdentity.port);
        size = sizeof(struct ptp_msg_signaling_base);
        break;
    case MANAGEMENT_MSG:
        msg->data.management.b.targetPortIdentity.port =
            htons(msg->data.management.b.targetPortIdentity.port);
        size = sizeof(struct ptp_msg_management_base);
        break;
    default:
        size = len - sizeof(struct ptp_msg_hdr);
        break;
    }
    data = &msg->data.data[size];
    size += sizeof(struct ptp_msg_hdr);
    if (len >= size) {
        len -= size;
        if (len >= sizeof(struct ptp_tlv)) {
            ptp_tlv_tx(data, len);
        }
    }
}  /* ptp_msg_tx */

bool msg_info_valid(struct ptp_msg_info *info)
{
    return info->valid;
}

bool msg_info_ts_valid(struct ptp_msg_info *info)
{
    return (info->hw_time.sec != 0);
}

void msg_info_clear(struct ptp_msg_info *info)
{
    info->valid = 0;
    info->resp = 0;
    info->hw_time.sec = 0;
}

void msg_info_save(struct ptp_msg_info *info, struct ptp_msg *msg,
    struct ptp_utime *hw_time)
{
    info->valid = 1;
    info->two_step = msg->hdr.flagField.flag.twoStepFlag;
    info->correction = msg->hdr.correctionField;
    info->seqid = msg->hdr.sequenceId;
    info->domain = msg->hdr.domainNumber;
    info->msg = msg->hdr.messageType;
    memcpy(&info->id, &msg->hdr.sourcePortIdentity,
        sizeof(struct ptp_port_identity));
    switch (msg->hdr.messageType) {
    case SYNC_MSG:
        memcpy(&info->ptp_time, &msg->data.sync.originTimestamp,
            sizeof(struct ptp_timestamp));
        break;
    case FOLLOW_UP_MSG:
        memcpy(&info->ptp_time, &msg->data.follow_up.preciseOriginTimestamp,
            sizeof(struct ptp_timestamp));
        break;
    case DELAY_RESP_MSG:
        memcpy(&info->ptp_time, &msg->data.delay_resp.receiveTimestamp,
            sizeof(struct ptp_timestamp));
        break;
    case PDELAY_RESP_MSG:
        memcpy(&info->ptp_time,
            &msg->data.pdelay_resp.requestReceiptTimestamp,
            sizeof(struct ptp_timestamp));

        /* tx_pdelay_resp uses id to store requestingPortIdentity. */
        if (hw_time)
            break;
        memcpy(&info->id, &msg->data.pdelay_resp.requestingPortIdentity,
            sizeof(struct ptp_port_identity));
        break;
    case PDELAY_RESP_FOLLOW_UP_MSG:
        memcpy(&info->ptp_time,
            &msg->data.pdelay_resp_follow_up.responseOriginTimestamp,
            sizeof(struct ptp_timestamp));
        break;
    }
    memset(&info->hw_time, 0, sizeof(struct ptp_utime));
    if (hw_time)
        memcpy(&info->hw_time, hw_time, sizeof(struct ptp_utime));
}  /* msg_info_save */

static int alloc_msg_cnt;

struct ptp_message *alloc_msg(struct ptp_msg *msg, int len,
    struct ptp_utime *rx)
{
    struct ptp_message *m;

    m = mem_info_alloc(len + sizeof(struct ptp_message));
    if (m) {
        m->refcnt = 1;
        m->msg = (struct ptp_msg *) &m->payload[14];

        /* Indicate no source MAC address. */
        m->payload[6] = 0xFF;
        if (msg) {
            memcpy(m->msg, msg, len);
            ptp_msg_rx(m->msg);
        }
        if (rx)
            memcpy(&m->rx, rx, sizeof(struct ptp_utime));
        else
            memset(&m->rx, 0, sizeof(struct ptp_utime));
        ++alloc_msg_cnt;
#ifdef DBG_PTP_ALLOC
        if (msg && rx)
dbg_msg("alloc: %u %x r"NL, alloc_msg_cnt, msg->hdr.messageType);
        else if (msg)
dbg_msg("alloc: %u %x"NL, alloc_msg_cnt, msg->hdr.messageType);
        else
dbg_msg("alloc: %u"NL, alloc_msg_cnt);
#endif
    }
    return m;
}  /* alloc_msg */

void free_msg(struct ptp_message *m)
{
#ifdef DBG_PTP_ALLOC
    if (m && m->msg)
dbg_msg("free: %u %x"NL, alloc_msg_cnt, m->msg->hdr.messageType);
    else
dbg_msg("free: %u"NL, alloc_msg_cnt);
#endif
    mem_info_free(m);
    --alloc_msg_cnt;
}

void msg_get(struct ptp_message *m)
{
    m->refcnt++;
}

void msg_put(struct ptp_message *m)
{
    m->refcnt--;
    if (!m->refcnt) {
        free_msg(m);
    }
}

void msg_clear(struct ptp_message **m)
{
    if (*m)
        msg_put(*m);
    *m = NULL;
}

void msg_save(struct ptp_message **s, struct ptp_message *m)
{
    if (*s)
        msg_put(*s);
    msg_get(m);
    *s = m;
}

#ifdef LINUX_PTP
static pthread_mutex_t rx_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t tx_lock = PTHREAD_MUTEX_INITIALIZER;
#else
static struct mutex rx_lock;
static struct mutex tx_lock;
#endif
static struct ptp_message *tx_msg;

struct ptp_message *get_tx_msg(void)
{
    return tx_msg;
}

void tx_msg_lock(void)
{
    mutex_lock(&tx_lock);
}

void tx_msg_unlock(void)
{
    mutex_unlock(&tx_lock);
}

void tx_msg_init(void)
{
    mutex_init(&rx_lock);
    mutex_init(&tx_lock);
    tx_msg = alloc_msg(NULL, 1500, NULL);
}
