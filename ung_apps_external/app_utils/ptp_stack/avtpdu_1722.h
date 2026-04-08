
#ifndef AVTPDU_1722_H
#define AVTPDU_1722_H


#define AVTPDU_PROTO			0x22F0

struct avtpdu_stream_hdr {
	u8 subtype;
#ifdef __BIG_ENDIAN_BITFIELD
	u8 h_specific:1;
	u8 version:3;
	u8 mr:1;
	u8 f_s_d:2;
	u8 tv:1;
#else
	u8 tv:1;
	u8 f_s_d:2;
	u8 mr:1;
	u8 version:3;
	u8 h_specific:1;
#endif
	u8 sequence_num;
#ifdef __BIG_ENDIAN_BITFIELD
	u8 format_specific_data_1:7;
	u8 tu:1;
#else
	u8 tu:1;
	u8 format_specific_data_1:7;
#endif
	union {
		u8 id[8];
		struct {
			u8 mac_addr[6];
			u16 unique_id;
		} __packed addr;
	} __packed stream_id;
	u32 avtp_timestamp;
	u32 format_specific_data_2;
	u16 stream_data_len;
	u16 format_specific_data_3;
} __packed;

struct avtpdu_ctrl_hdr {
	u8 subtype;
#ifdef __BIG_ENDIAN_BITFIELD
	u8 h_specific:1;
	u8 version:3;
	u8 format_specific_data:4;
	u16 specific:5;
	u16 ctrl_data_len:11;
#else
	u8 format_specific_data:4;
	u8 version:3;
	u8 h_specific:1;
	union {
		struct {
			u16 ctrl_data_len:11;
			u16 specific:5;
		} __packed data_len;
		u16 data;
	} __packed len;
#endif
	union {
		u8 id[8];
		struct {
			u8 mac_addr[6];
			u16 unique_id;
		} __packed addr;
	} __packed stream_id;
} __packed;

struct iec_61883 {
	struct avtpdu_stream_hdr hdr;
};

#define IEEE_1722_1_AECP		0xFB

#define IEEE_1722_1_GET_COUNTERS	0x0029

#define AED_TEST_STATUS_LEN		148

#define AED_DESC_TYPE_STREAM_INPUT	0x0005
#define AED_DESC_TYPE_STREAM_OUTPUT	0x0006
#define AED_DESC_TYPE_AVB_INTERFACE	0x0009
#define AED_DESC_TYPE_AVB_INTERFACE	0x0009

#define AED_STATE_RESERVED_0		0x00
#define AED_STATE_ETHERNET_READY	0x01
#define AED_STATE_AVB_SYNC		0x02
#define AED_STATE_AVB_MEDIA_READY	0x03
#define AED_STATE_RESERVED_4		0x04
#define AED_STATE_USER_DEFINED		0x20
#define AED_STATE_EXPERIMENTAL		0xF0

#define AED_COUNTER_LINK_UP		0
#define AED_COUNTER_LINK_DOWN		1
#define AED_COUNTER_FRAMES_TX		2
#define AED_COUNTER_FRAMES_RX		3
#define AED_COUNTER_RX_CRC_ERR		4
#define AED_COUNTER_GPTP_GM_CHANGED	5
#define AED_COUNTER_MESSAGE_TIMESTAMP	24

struct aed_test_status {
	u8 subtype;
#ifdef __BIG_ENDIAN_BITFIELD
	u8 sv:1;
	u8 version:3;
	u8 ctrl_data:4;
	u16 status:5;
	u16 ctrl_data_len:11;
#else
	u8 ctrl_data:4;
	u8 version:3;
	u8 sv:1;
	union {
		struct {
			u16 ctrl_data_len:11;
			u16 status:5;
		} __packed data_len;
		u16 data;
	} __packed len;
#endif
	union {
		u8 id[8];
		struct {
			u8 mac_addr[6];
			u16 unique_id;
		} __packed addr;
	} __packed target_entity_id;
	u64 controller_entity_id;
	u16 sequence_id;
#ifdef __BIG_ENDIAN_BITFIELD
	u16 u:1;
	u16 cmd_type:15;
#else
	union {
		struct {
			u16 cmd_type:15;
			u16 u:1;
		} __packed cmd;
		u16 data;
	} __packed type;
#endif
	u16 desc_type;
	u16 desc_index;
	u32 counters_valid;
	u32 counters[32];
} __packed;

struct aed_test_status_specific {
	u64 message_timestamp;
	u8 station_state;
	u8 station_state_specific_data[3];
} __packed;

#endif
