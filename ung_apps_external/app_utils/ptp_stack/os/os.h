/*
    Copyright (C) 2019-2022 Microchip Technology Inc. and its
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

#ifndef __OS_H
#define __OS_H

#if 0
#include <atmel_start.h>
#endif
#ifdef USE_CYCLONETCP
#include <core/net.h>
#endif
#if 0
#include <timers.h>
#include <string.h>
#include <errno.h>
#include "hton.h"

#if( configSUPPORT_STATIC_ALLOCATION == 1 )
#include <os_platform/os_platform_api.h>
#include <mem_pool/pvt/mem_pool.h>
#else
#define HAVE_READLINE
#endif
#endif

#if 1
#include "wnp.h"
#include <stdbool.h>

#define mutex_lock    Pthread_mutex_lock
#define mutex_unlock  Pthread_mutex_unlock
#define mutex_init(x)
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define NL                              "\n"
#define PER_CHAR                        "%%%%"

typedef unsigned char u8;
typedef uint16_t u16;
typedef int16_t s16;
typedef uint u32;
typedef int s32;
typedef int64_t s64;
typedef uint64_t u64;

#ifndef __packed
#define __packed __attribute__((packed))
#endif

typedef int TimerHandle_t;
typedef unsigned long TickType_t;

#define pdTRUE  true
#define pdFALSE  false


#define MAX_SYSFS_BUF_SIZE              (100 - 20)

#include "sw_support.h"
#include "if_ether.h"


#ifndef BIT
#define BIT(x)                          (1 << (x))
#endif

#define betoh64                         be64toh

#define jiffies                         get_sys_time()
#define msecs_to_jiffies(x)             (x)

/*
 * Check at compile time that something is of a particular type.
 * Always evaluates to 1 so you may use it easily in comparisons.
 */
#define typecheck(type,x)               \
({  type __dummy;                       \
    typeof(x) __dummy2;                 \
    (void)(&__dummy == &__dummy2);      \
    1;                                  \
})

/*
 * Check at compile time that 'function' is a certain type, or is a pointer
 * to that type (needs to use typedef for the function type.)
 */
#define typecheck_fn(type,function)     \
({  typeof(type) __tmp = function;      \
    (void)__tmp;                        \
})

#define time_after(a,b)                 \
    (typecheck(unsigned long, a) &&     \
     typecheck(unsigned long, b) &&     \
     ((long)((b) - (a)) < 0))
#define time_before(a,b)                time_after(b,a)

#define time_after_eq(a,b)              \
    (typecheck(unsigned long, a) &&     \
     typecheck(unsigned long, b) &&     \
     ((long)((a) - (b)) >= 0))
#define time_before_eq(a,b)             time_after_eq(b,a)

#define time_delta(a,b)                 \
    ((unsigned long)((b) - (a)) * 1)

static inline void time_save(unsigned long *t, unsigned long m)
{
    *t = jiffies;

    /* The time can be set 0 or 1 for other indications. */
    if (*t < m)
        *t = m;
}

#define delay_micro(x)
#define delay_milli(x)                  vTaskDelay(pdMS_TO_TICKS(x))


#define spinlock_t int
#define spin_lock(x)
#define spin_unlock(x)


#define HZ                              (configTICK_RATE_HZ)

typedef int phy_interface_t;

enum {
    PHY_INTERFACE_MODE_MII,
    PHY_INTERFACE_MODE_GMII,
    PHY_INTERFACE_MODE_RMII,
    PHY_INTERFACE_MODE_RGMII,
    PHY_INTERFACE_MODE_RGMII_RXID,
    PHY_INTERFACE_MODE_RGMII_TXID,
    PHY_INTERFACE_MODE_RGMII_ID,
    PHY_INTERFACE_MODE_SGMII,
};


struct phy_device;

struct phy_driver {
    u32 phy_id;
    uint phy_id_mask;
    char *name;
    int (*config_init)(struct phy_device *phydev);
    int (*config_aneg)(struct phy_device *phydev);
    int (*read_status)(struct phy_device *phydev);
    int (*ack_interrupt)(struct phy_device *phydev);
    int (*config_intr)(struct phy_device *phydev);
    int (*did_interrupt)(struct phy_device *phydev);
    int (*match_phy_device)(struct phy_device *phydev);
};

struct phy_device {
    void *dev;
    struct phy_driver *drv;
    u32 phy_id;
    u32 dev_flags;
    u8 port;
    int addr;
    int speed;
    int duplex;
    int pause;
    int asym_pause;
    int link;
    int autoneg;
#if 0
    struct mutex lock;
#endif
};

int phydev_read(struct phy_device *phydev, u16 reg);
int phydev_write(struct phy_device *phydev, u16 reg, u16 val);


bool check_keypress(int waiting);
#if 0
char *getline(char *line, size_t len);
#endif

#if 1
/* Decide whether to use LAN9371-Dual board if not already set. */
#ifndef SETUP_LAN938X_DUAL
#define SETUP_LAN938X_DUAL
#endif
#endif

#if 1
/* This is used to switch using new register framework access. */
#define USE_REG_ACC
#endif

#ifdef DEBUG_MSG
#if 0
#define MEASURE_BOOT_TIME
#define BOOT_NSEC_CNT  16

extern uint boot_nsecs[];
extern int boot_nsec_index;
#endif
#endif

void ptp_get_clk(unsigned int *, unsigned int *);

#if 1
#define T1S_MASTER
#else
#define T1S_CLIENT  1
#endif

/* T1S PHY support. */
#if 1
#ifdef SETUP_LAN938X_DUAL
#define MAX_T1S_PHY  3
#else
#define MAX_T1S_PHY  1
#endif
#endif

#ifdef MAX_T1S_PHY

#ifdef SETUP_LAN938X_DUAL
#define MAX_CHIP_ID_CNT  2
#else
#define MAX_CHIP_ID_CNT  1
#endif

#if 0
/* Use PMCH_RX signal */
#define USE_PMCH_RX
#endif

#if (MAX_T1S_PHY > 1)
#if 1
/* Second T1S is connected to LED_2. */
#define USE_T1S_W2
#endif
#endif

#if 1
/* Use PMCH_TX operation */
#define USE_PMCH_TX
#else
#if 1
/* Required because of transmit timestamp interrupt problem. */
#define VERIFY_PMCH_TX
#endif
#endif

#if 1
#define USE_T1S_PLCA
#endif
#endif

#ifdef __cplusplus
}
#endif

#endif
