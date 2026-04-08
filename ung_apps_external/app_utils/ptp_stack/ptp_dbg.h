

#ifndef PTP_DBG_H
#define PTP_DBG_H

#ifdef DEBUG_MSG
#if 0
#define DBG_PTP_ALLOC
#define DBG_PTP_FWD
#define DBG_PTP_PORT
#define DBG_PTP_TIMEOUT
#define DBG_PTP_TIMER
#define DBG_PRESP_MISSING
#define DBG_PTP_DELAY
#define DBG_PTP_PEER
#define DBG_PTP_RATIO
#define DBG_PTP_SERVO
#endif
#if 1
#define NOTIFY_PTP_EVENT
#endif
#endif

#endif

