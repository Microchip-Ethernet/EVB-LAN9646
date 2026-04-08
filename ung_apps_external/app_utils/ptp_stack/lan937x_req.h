/*
    Copyright (C) 2009-2019. Microchip Technology Inc. and its
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

#ifndef LAN937X_REQ_H
#define LAN937X_REQ_H

#include <stdio.h>

enum {
	DEV_IOC_OK,
	DEV_IOC_INVALID_SIZE,
	DEV_IOC_INVALID_CMD,
	DEV_IOC_INVALID_LEN,

	DEV_IOC_LAST
};

enum {
	DEV_CMD_INFO,
	DEV_CMD_GET,
	DEV_CMD_PUT,

	DEV_CMD_LAST
};

enum {
	DEV_INFO_INIT,
	DEV_INFO_EXIT,
	DEV_INFO_QUIT,
	DEV_INFO_NOTIFY,
	DEV_INFO_PORT,

	DEV_INFO_LAST
};

struct ksz_request {
	int size;
	int cmd;
	int subcmd;
	int output;
	int result;
	union {
		u8 data[1];
		int num[1];
	} param;
};

/* Some compilers in different OS cannot have zero number in array. */
#define SIZEOF_ksz_request	(sizeof(struct ksz_request) - sizeof(int))

/* Not used in the driver. */

#ifndef MAX_REQUEST_SIZE
#define MAX_REQUEST_SIZE	20
#endif

struct ksz_request_actual {
	int size;
	int cmd;
	int subcmd;
	int output;
	int result;
	union {
		u8 data[MAX_REQUEST_SIZE];
		int num[MAX_REQUEST_SIZE / sizeof(int)];
	} param;
};

#define DEV_IOC_MAGIC			0x92

#define DEV_IOC_MAX			1


struct ksz_read_msg {
	u16 len;
	u8 data[0];
} __packed;


enum {
	DEV_MOD_BASE,
	DEV_MOD_PTP,
	DEV_MOD_MRP,
	DEV_MOD_DLR,
	DEV_MOD_HSR,
};

struct ksz_resp_msg {
	u16 module;
	u16 cmd;
	union {
		u32 data[1];
	} resp;
} __packed;

#define PARAM_DATA_SIZE			80

void get_user_data(int *kernel, int *user);
void put_user_data(int *kernel, int *user);
int read_user_data(void *kernel, void *user, size_t size);
int write_user_data(void *kernel, void *user, size_t size);
int chk_ioctl_size(int len, int size, int additional, int *req_size,
	int *result, void *param, u8 *data);

#endif
