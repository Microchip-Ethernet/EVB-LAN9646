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

#include "ptp_bmca.h"
#include "ptp_clock.h"
#include "ptp_config.h"
#ifdef LINUX_PTP
#include "config_store.h"
#else
#include "config_store/config_store_gptp_api.h"
#endif


/* 1 for gPTP; 2 for gPTP Automotive */
static u8 ptp_stack_gptp = 1;

/* Specifify slave port in gPTP Automotive. */
static u8 ptp_stack_slave_port = 0;

static u8 ptp_stack_gm_capable = 1;
static u8 ptp_stack_2step = 1;
static u8 ptp_stack_e2e = 0;
static u8 ptp_stack_p2p = 1;

int ptp_config_get_gptp_mode(void)
{
    return ptp_stack_gptp;
}

u8 ptp_config_get_slave_port(void)
{
    return ptp_stack_slave_port;
}

bool ptp_config_using_p2p(void)
{
    if (ptp_stack_gptp > 0)
        return true;
    else
        return ptp_stack_p2p != 0;
}

bool ptp_config_using_e2e(void)
{
    if (ptp_config_using_p2p())
        return false;
    else
        return ptp_stack_e2e != 0;
}

bool ptp_config_using_two_step(void)
{
    if (ptp_stack_gptp > 0)
        return true;
    else
        return ptp_stack_2step != 0;
}

void ptp_init_config_ds(u8 index, struct ptp_default_ds *dds)
{
    u16 offsetScaledLogVariance = 0;

    memset(dds, 0, sizeof(struct ptp_default_ds));
    dds->clockQuality.clockClass = 248;
    dds->clockQuality.clockAccuracy = 0x21;
    dds->clockQuality.offsetScaledLogVariance = 0x436A;

    /* Get saved configuration if any. */
    gptp_profile_get_clock_prop(index,
        &dds->clockQuality.clockClass,
        &dds->clockQuality.clockAccuracy,
        &offsetScaledLogVariance);
    dds->clockQuality.offsetScaledLogVariance = offsetScaledLogVariance;

    dds->priority1 = 128;
    dds->priority2 = 128;
    if (ptp_stack_gptp) {
        dds->priority1 = 246;
        dds->priority2 = 248;
    }

    /* Get saved configuration if any. */
    gptp_profile_get_clock_prio(index,
        &dds->priority1, &dds->priority2);
    
    if (ptp_stack_gptp && 255 == dds->priority1) {
        ptp_stack_gm_capable = 0;
    }

    if (!ptp_stack_gm_capable)
        dds->flags |= DDS_SLAVE_ONLY;
    if (ptp_stack_2step)
        dds->flags |= DDS_TWO_STEP_FLAG;

    /* No grandmaster clock. */
    if (dds->flags & DDS_SLAVE_ONLY)
        dds->clockQuality.clockClass = 255;
}

void ptp_init_port_ds(struct ptp_port *p)
{
    struct ptp_port_ds *ds = &p->ds;
    u8 syncReceiptTimeout;
    u8 index = p->c->cfg_index;

    ds->portState = PTP_S_DISABLED;
    ds->logMinDelayReqInterval = 0;
    ds->peerMeanPathDelay = 0;
    ds->logAnnounceInterval = 1;
    ds->announceReceiptTimeout = 3;
    ds->logSyncInterval = 0;
    ds->logMinPdelayReqInterval = 0;
    if (p->gptp) {
        ds->logAnnounceInterval = 0;
        ds->logSyncInterval = -3;
    }
    ds->versionNumber = 2;

#ifdef LINUX_PTP
    index = p->index;
#endif
    /* Get saved configuration if any. */
    gptp_port_profile_get_sync(index,
        &ds->logSyncInterval,
        &syncReceiptTimeout);
    gptp_port_profile_get_announce(index,
        &ds->logAnnounceInterval,
        &ds->announceReceiptTimeout);
    gptp_port_profile_get_delay(index,
        &ds->logMinDelayReqInterval);
    gptp_port_profile_get_pdelay(index,
        &ds->logMinPdelayReqInterval);
}

void ptp_update_parent_ds(struct ptp_clock *c, struct foreign_dataset *f)
{
    struct ptp_default_ds *dds = &c->dds;
    struct ptp_parent_ds *pds = &c->pds;

    /* There is a master better than this clock. */
    if (f) {
        c->cur.stepsRemoved = f->dataset.steps_removed + 1;

        memcpy(&pds->parentPortIdentity, &f->dataset.tx,
            sizeof(struct ptp_port_identity));
        memcpy(&pds->grandmasterIdentity, &f->dataset.id,
            sizeof(struct ptp_clock_identity));
        memcpy(&pds->grandmasterClockQuality, &f->dataset.quality,
            sizeof(struct ptp_clock_quality));
        pds->grandmasterPriority1 = f->dataset.prio_1;
        pds->grandmasterPriority2 = f->dataset.prio_2;
    } else {
        memset(&c->cur, 0, sizeof(struct ptp_current_ds));

        memcpy(&pds->parentPortIdentity.clockIdentity,
            &dds->clockIdentity, sizeof(struct ptp_clock_identity));
        pds->parentPortIdentity.port = 0;
        memcpy(&pds->grandmasterIdentity, &dds->clockIdentity,
            sizeof(struct ptp_clock_identity));
        memcpy(&pds->grandmasterClockQuality, &dds->clockQuality,
            sizeof(struct ptp_clock_quality));
        pds->grandmasterPriority1 = dds->priority1;
        pds->grandmasterPriority2 = dds->priority2;
    }
}  /* ptp_update_parent_ds */
