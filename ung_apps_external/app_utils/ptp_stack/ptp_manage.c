
#include "ptp_protocol.h"


static u16 ptp_manage_get_default_ds(struct ptp_clock *c, struct ptp_port *p,
    u8 *data)
{
    struct ptp_default_ds *ds = (struct ptp_default_ds *) data;

    memcpy(ds, &c->dds, sizeof(struct ptp_default_ds));
    return sizeof(struct ptp_default_ds);
}

static u16 ptp_manage_set_default_ds(struct ptp_clock *c, struct ptp_port *p,
    u8 *data)
{
    return 0;
}

static u16 ptp_manage_get_current_ds(struct ptp_clock *c, struct ptp_port *p,
    u8 *data)
{
    struct ptp_current_ds *ds = (struct ptp_current_ds *) data;

    memcpy(ds, &c->cur, sizeof(struct ptp_current_ds));
    return sizeof(struct ptp_current_ds);
}

static u16 ptp_manage_get_parent_ds(struct ptp_clock *c, struct ptp_port *p,
    u8 *data)
{
    struct ptp_parent_ds *ds = (struct ptp_parent_ds *) data;

    memcpy(ds, &c->pds, sizeof(struct ptp_parent_ds));
    return sizeof(struct ptp_parent_ds);
}

static u16 ptp_manage_get_priority1(struct ptp_clock *c, struct ptp_port *p,
    u8 *data)
{
    struct ptp_management_data *manage = (struct ptp_management_data *) data;

    manage->val = c->dds.priority1;
    manage->reserved = 0;
    return sizeof(struct ptp_management_data);
}

static u16 ptp_manage_set_priority1(struct ptp_clock *c, struct ptp_port *p,
    u8 *data)
{
    struct ptp_management_data *manage = (struct ptp_management_data *) data;

    return 0;
}

static u16 ptp_manage_get_priority2(struct ptp_clock *c, struct ptp_port *p,
    u8 *data)
{
    struct ptp_management_data *manage = (struct ptp_management_data *) data;

    manage->val = c->dds.priority2;
    manage->reserved = 0;
    return sizeof(struct ptp_management_data);
}

static u16 ptp_manage_set_priority2(struct ptp_clock *c, struct ptp_port *p,
    u8 *data)
{
    struct ptp_management_data *manage = (struct ptp_management_data *) data;

    return 0;
}

static u16 ptp_manage_get_domain(struct ptp_clock *c, struct ptp_port *p,
    u8 *data)
{
    struct ptp_management_data *manage = (struct ptp_management_data *) data;

    manage->val = c->dds.domainNumber;
    manage->reserved = 0;
    return sizeof(struct ptp_management_data);
}

static u16 ptp_manage_get_port_ds(struct ptp_clock *c, struct ptp_port *p,
    u8 *data)
{
    struct ptp_port_ds *ds = (struct ptp_port_ds *) data;

    memcpy(ds, &p->ds, sizeof(struct ptp_port_ds));
    if (!ds->delayMechanism)
        ds->delayMechanism = 0xFE;
    return sizeof(struct ptp_port_ds);
}

static u16 ptp_manage_get_ann_int(struct ptp_clock *c, struct ptp_port *p,
    u8 *data)
{
    struct ptp_management_data *manage = (struct ptp_management_data *) data;

    manage->val = p->ds.logAnnounceInterval;
    manage->reserved = 0;
    return sizeof(struct ptp_management_data);
}

static u16 ptp_manage_get_ann_timeout(struct ptp_clock *c, struct ptp_port *p,
    u8 *data)
{
    struct ptp_management_data *manage = (struct ptp_management_data *) data;

    manage->val = p->ds.announceReceiptTimeout;
    manage->reserved = 0;
    return sizeof(struct ptp_management_data);
}

static u16 ptp_manage_get_sync_int(struct ptp_clock *c, struct ptp_port *p,
    u8 *data)
{
    struct ptp_management_data *manage = (struct ptp_management_data *) data;

    manage->val = p->ds.logSyncInterval;
    manage->reserved = 0;
    return sizeof(struct ptp_management_data);
}

static u16 ptp_manage_get_version(struct ptp_clock *c, struct ptp_port *p,
    u8 *data)
{
    struct ptp_management_data *manage = (struct ptp_management_data *) data;

    manage->val = p->ds.versionNumber;
    manage->reserved = 0;
    return sizeof(struct ptp_management_data);
}

static u16 ptp_manage_get_delay_mech(struct ptp_clock *c, struct ptp_port *p,
    u8 *data)
{
    struct ptp_management_data *manage = (struct ptp_management_data *) data;

    manage->val = p->ds.delayMechanism;
    if (!manage->val)
        manage->val = 0xFE;
    manage->reserved = 0;
    return sizeof(struct ptp_management_data);
}

static u16 ptp_manage_get_pdelay_int(struct ptp_clock *c, struct ptp_port *p,
    u8 *data)
{
    struct ptp_management_data *manage = (struct ptp_management_data *) data;

    manage->val = p->ds.logMinPdelayReqInterval;
    manage->reserved = 0;
    return sizeof(struct ptp_management_data);
}

static u16 ptp_manage_get_port_ds_np(struct ptp_clock *c, struct ptp_port *p,
    u8 *data)
{
    struct ptp_port_ds_gptp *manage = (struct ptp_port_ds_gptp *) data;

    manage->neighborPropDelayThresh = p->neighborDelayThresh;
    manage->asCapable = port_capable(p);
    return sizeof(struct ptp_port_ds_gptp);
}

void port_tx_manage_error(struct ptp_port *p, struct ptp_message *m,
    u8 action, u16 id, u16 err_id)
{
    struct ptp_message *manage;
    struct ptp_msg_management_base *b;
    struct ptp_management_error_tlv *error;
    struct ptp_msg *req = m->msg;
    struct ptp_msg *msg;
    void *tlv_end;
    u16 messageLength;
    u16 len;

    manage = clock_new_msg(p->c, MANAGEMENT_MSG, NULL);
    msg = manage->msg;

    msg->hdr.messageLength = sizeof(struct ptp_msg_hdr) +
        sizeof(struct ptp_msg_management_base);
    msg->hdr.sequenceId = req->hdr.sequenceId;
    if (!p->no_id_replace)
        msg->hdr.sourcePortIdentity.port = p->id;

    b = &msg->data.management.b;
    memcpy(b, &req->hdr.sourcePortIdentity,
        sizeof(struct ptp_port_identity));
#if 0
    b->startingBoundaryHops = req->data.management.b.startingBoundaryHops;
    b->boundaryHops = req->data.management.b.boundaryHops;
#endif
    b->actionField = action;

    error = msg->data.management.tlv.error;
    error->managementErrorId = err_id;
    error->managementId = id;
    tlv_end = error->data;
    len = sizeof(struct ptp_management_error_tlv) - 1;
    messageLength = msg->hdr.messageLength;
    tlv_end = ptp_add_tlv(&error->tlv, tlv_end, TLV_MANAGEMENT_ERROR_STATUS,
        len, &messageLength);
    msg->hdr.messageLength = messageLength;

    ptp_msg_tx(msg);
    ptp_tx_msg(manage, p->index, 1 << (p->index - 1), 7, p->c->transport,
               p->c->vid, 0, 0, NULL);
}  /* port_tx_manage_error */

void port_tx_manage(struct ptp_port *p, struct ptp_message *m,
    u8 action, u16 id,
    u16 (*set_tlv)(struct ptp_clock *c, struct ptp_port *p, u8 *data),
    struct ptp_clock *c, struct ptp_port *q)
{
    struct ptp_message *manage;
    struct ptp_management_tlv *tlv;
    struct ptp_msg_management_base *b;
    struct ptp_msg *req = m->msg;
    struct ptp_msg *msg;
    u16 tlv_len;
    void *tlv_end;
    u8 *data;
    u16 len;
    u16 messageLength;

    manage = clock_new_msg(p->c, MANAGEMENT_MSG, NULL);
    msg = manage->msg;

    msg->hdr.messageLength = sizeof(struct ptp_msg_hdr) +
        sizeof(struct ptp_msg_management_base);
    msg->hdr.sequenceId = req->hdr.sequenceId;

    if (!q)
        q = p;
    if (!p->no_id_replace)
        msg->hdr.sourcePortIdentity.port = q->id;

    b = &msg->data.management.b;
    memcpy(b, &req->hdr.sourcePortIdentity,
        sizeof(struct ptp_port_identity));
#if 0
    b->startingBoundaryHops = req->data.management.b.startingBoundaryHops;
    b->boundaryHops = req->data.management.b.boundaryHops;
#endif
    b->actionField = action;

    tlv = msg->data.management.tlv.normal;
    tlv->managementId = id;

    data = tlv->dataField;
    if (set_tlv)
        tlv_len = set_tlv(c, q, data);
    else
        tlv_len = 0;

    tlv_end = (data + tlv_len);
    len = sizeof(struct ptp_management_tlv) - 1 + tlv_len;
    messageLength = msg->hdr.messageLength;
    tlv_end = ptp_add_tlv(&tlv->tlv, tlv_end, TLV_MANAGEMENT,
        len, &messageLength);
    msg->hdr.messageLength = messageLength;

    ptp_msg_tx(msg);
    ptp_tx_msg(manage, p->index, 1 << (p->index - 1), 7, p->c->transport,
               p->c->vid, 0, 0, NULL);
}  /* port_tx_manage */

void handle_management(struct ptp_port *p, struct ptp_message *m)
{
    struct ptp_msg *msg = m->msg;
    struct ptp_msg_management_base *b = &msg->data.management.b;
    u16 len;

    /* Make sure payload is there. */
    len = msg->hdr.messageLength - sizeof(struct ptp_msg_hdr);
    if (len <= sizeof(struct ptp_msg_management_base))
        return;

    if (!same_clock_id(&p->c->id, &b->targetPortIdentity.clockIdentity) &&
        !broadcast_clock_id(&b->targetPortIdentity.clockIdentity)) {
        return;
    }

    ptp_save_rx_msg(m);
}

void proc_management(struct ptp_port *p, struct ptp_message *m)
{
    struct ptp_msg *msg = m->msg;
    struct ptp_msg_management_base *b = &msg->data.management.b;
    struct ptp_management_tlv *manage = msg->data.management.tlv.normal;
    struct ptp_management_error_tlv *error = msg->data.management.tlv.error;
    struct ptp_tlv *tlv;
    struct ptp_clock *c = NULL;
    struct ptp_port *q = NULL;
    u8 *data;
    u8 resp;
    u16 len;
    u16 size;
    u16 target;
    int cnt;
    int no_id;
    u16 (*func)(struct ptp_clock *c, struct ptp_port *p, u8 *data);

    if (same_clock_id(&p->c->id, &b->targetPortIdentity.clockIdentity) ||
        broadcast_clock_id(&b->targetPortIdentity.clockIdentity)) {
        c = p->c;
    }

    resp = MANAGEMENT_ACKNOWLEDGE;
    switch (b->actionField) {
    case MANAGEMENT_GET:
        resp = MANAGEMENT_RESPONSE;
        break;
    case MANAGEMENT_SET:
        resp = MANAGEMENT_RESPONSE;
        break;
    case MANAGEMENT_RESPONSE:
        break;
    case MANAGEMENT_COMMAND:
        resp = MANAGEMENT_ACKNOWLEDGE;
        break;
    case MANAGEMENT_ACKNOWLEDGE:
        break;
    default:
        break;
    }

    len = msg->hdr.messageLength - sizeof(struct ptp_msg_hdr);
    len -= sizeof(struct ptp_msg_management_base);
    data = (u8 *)(b + 1);
    tlv = ptp_next_tlv(&data, &len);
    while (tlv) {
        switch (tlv->tlvType) {
        case TLV_MANAGEMENT:
            func = NULL;
            size = 0;
            no_id = 0;
            switch (manage->managementId) {
            case M_DEFAULT_DATA_SET:
                size = sizeof(struct ptp_default_ds);
                if (b->actionField == MANAGEMENT_GET)
                    func = ptp_manage_get_default_ds;
                else if (b->actionField == MANAGEMENT_SET)
                    func = ptp_manage_set_default_ds;
                break;
            case M_CURRENT_DATA_SET:
                size = sizeof(struct ptp_current_ds);
                if (b->actionField == MANAGEMENT_GET)
                    func = ptp_manage_get_current_ds;
                break;
            case M_PARENT_DATA_SET:
                size = sizeof(struct ptp_parent_ds);
                if (b->actionField == MANAGEMENT_GET)
                    func = ptp_manage_get_parent_ds;
                break;
            case M_PRIORITY1:
                size = sizeof(struct ptp_management_data);
                if (b->actionField == MANAGEMENT_GET)
                    func = ptp_manage_get_priority1;
                else if (b->actionField == MANAGEMENT_SET)
                    func = ptp_manage_set_priority1;
                break;
            case M_PRIORITY2:
                size = sizeof(struct ptp_management_data);
                if (b->actionField == MANAGEMENT_GET)
                    func = ptp_manage_get_priority2;
                else if (b->actionField == MANAGEMENT_SET)
                    func = ptp_manage_set_priority2;
                break;
            case M_DOMAIN:
                if (b->actionField == MANAGEMENT_GET)
                    func = ptp_manage_get_domain;
                break;
            case M_SLAVE_ONLY:
                break;
            case M_TIME:
                break;
            default:
                no_id++;
                break;
            }

            /* Found match for clock specific information. */
            if (func) {
                cnt = 1;
                q = p;

                /* Match all ports. */
                target = 0xFFFF;
            } else {
                cnt = c->cnt;
                q = c->ptp_ports[1];
                target = b->targetPortIdentity.port;

                /* Check for port specific inforamtion. */
                switch (manage->managementId) {
                case M_PORT_DATA_SET:
                    if (b->actionField == MANAGEMENT_GET)
                        func = ptp_manage_get_port_ds;
                    break;
                case M_LOG_ANNOUNCE_INTERVAL:
                    if (b->actionField == MANAGEMENT_GET)
                        func = ptp_manage_get_ann_int;
                    break;
                case M_ANNOUNCE_RECEIPT_TIMEOUT:
                    if (b->actionField == MANAGEMENT_GET)
                        func = ptp_manage_get_ann_timeout;
                    break;
                case M_LOG_SYNC_INTERVAL:
                    if (b->actionField == MANAGEMENT_GET)
                        func = ptp_manage_get_sync_int;
                    break;
                case M_VERSION_NUMBER:
                    if (b->actionField == MANAGEMENT_GET)
                        func = ptp_manage_get_version;
                    break;
                case M_DELAY_MECHANISM:
                    if (b->actionField == MANAGEMENT_GET)
                        func = ptp_manage_get_delay_mech;
                    break;
                case M_LOG_MIN_PDELAY_REQ_INTERVAL:
                    if (b->actionField == MANAGEMENT_GET)
                        func = ptp_manage_get_pdelay_int;
                    break;
                case M_PORT_DATA_SET_NP:
                    if (b->actionField == MANAGEMENT_GET)
                        func = ptp_manage_get_port_ds_np;
                    break;
                case M_ENABLE_PORT:
                    break;
                case M_DISABLE_PORT:
                    break;
                default:
                    no_id++;
                    break;
                }
            }
            tx_msg_lock();
            if (b->actionField == MANAGEMENT_SET &&
                tlv->lengthField < size + sizeof(u16)) {
                port_tx_manage_error(p, m, resp,
                    manage->managementId, M_WRONG_LENGTH);
            } else if (!func) {
                if (no_id == 2)
                    port_tx_manage_error(p, m, resp,
                        manage->managementId, M_NO_SUCH_ID);
                else
                    port_tx_manage_error(p, m, resp,
                        manage->managementId, M_NOT_SUPPORTED);
            } else {
                if (b->actionField == MANAGEMENT_GET) {
                    int i;
                    int proc = 0;

                    i = 1;
                    do {
                        if (q->id == target || target == 0xFFFF) {
                            port_tx_manage(p, m, resp, manage->managementId,
                                func, c, q);
                            proc++;
                        }
                        i++;
                        q = c->ptp_ports[i];
                    } while (i <= cnt);
                    if (!proc)
                        port_tx_manage_error(p, m, resp,
                            manage->managementId, M_WRONG_VALUE);
                } else {
                    u16 ret;

                    ret = func(c, q, manage->dataField);
                    if (ret)
                        port_tx_manage_error(p, m, resp,
                            manage->managementId, ret);
                    else
                        port_tx_manage(p, m, resp, manage->managementId,
                            NULL, NULL, NULL);
                }
            }
            tx_msg_unlock();
            break;
        case TLV_MANAGEMENT_ERROR_STATUS:
            break;
        default:
            break;
        }
        tlv = ptp_next_tlv(&data, &len);
    }
}
