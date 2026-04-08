
#include "avtpdu_1722.h"

static SOCKET eth_avtpdu;
static struct sockaddr_ll eth_avtpdu_addr;
static u8 *eth_avtpdu_buf;

static u8 avtpdu_dst[] = { 0x01, 0x1B, 0xC5, 0x0A, 0xC0, 0x00 };
static u8 avtpdu_src[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

static u8 avtpdu_id[8];
static u16 avtpdu_seqid = 0;

static u8 automotive_test_mode = 1;

static SOCKET create_avtpdu(struct ip_info *info)
{
	SOCKET sockfd;
	struct ethhdr *eh;

	sockfd = Socket(AF_PACKET, SOCK_RAW, htons(AVTPDU_PROTO));
	if (sockfd < 0)
		return sockfd;

	eth_avtpdu_addr.sll_family = PF_PACKET;
	eth_avtpdu_addr.sll_protocol = htons(AVTPDU_PROTO);
	eth_avtpdu_addr.sll_ifindex = info->if_idx;
	eth_avtpdu_addr.sll_hatype = ARPHRD_ETHER;
	eth_avtpdu_addr.sll_halen = ETH_ALEN;
	eth_avtpdu_addr.sll_pkttype = PACKET_MULTICAST;
	memcpy(eth_avtpdu_addr.sll_addr, avtpdu_dst, ETH_ALEN);
	eth_avtpdu_addr.sll_addr[6] = 0x00;
	eth_avtpdu_addr.sll_addr[7] = 0x00;
	eth_avtpdu_buf = malloc(sizeof(struct aed_test_status) + 20);
	memset(eth_avtpdu_buf, 0, sizeof(struct aed_test_status) + 20);
	memcpy(eth_avtpdu_buf, eth_avtpdu_addr.sll_addr, ETH_ALEN);
	memcpy(&eth_avtpdu_buf[ETH_ALEN], info->hwaddr, ETH_ALEN);
	memcpy(avtpdu_src, info->hwaddr, ETH_ALEN);
	eh = (struct ethhdr *) eth_avtpdu_buf;
	eh->h_proto = htons(AVTPDU_PROTO);
	memcpy(avtpdu_id, info->hwaddr, ETH_ALEN);
	avtpdu_id[6] = 0x93;
	avtpdu_id[7] = 0x83;

	return sockfd;
}

static void init_aed_test_status(u8 id[], struct aed_test_status *msg)
{
	struct aed_test_status_specific *specific;

	msg->subtype = IEEE_1722_1_AECP;
	msg->sv = 0;
	msg->version = 0;
	msg->ctrl_data = 1;
	msg->len.data_len.status = 0;
	msg->len.data_len.ctrl_data_len = AED_TEST_STATUS_LEN;
	msg->len.data = htons(msg->len.data);
	memcpy(&msg->target_entity_id, id, 8);
	msg->controller_entity_id = 0;
	msg->type.cmd.u = 1;
	msg->type.cmd.cmd_type = IEEE_1722_1_GET_COUNTERS;
	msg->type.data = htons(msg->type.data);
	msg->desc_type = htons(AED_DESC_TYPE_AVB_INTERFACE);
	msg->desc_index = htons(1);
	msg->counters_valid = htonl(7 << AED_COUNTER_MESSAGE_TIMESTAMP);
	specific = (struct aed_test_status_specific *)
		&msg->counters[AED_COUNTER_MESSAGE_TIMESTAMP];
	specific->message_timestamp = 0;
	specific->station_state = AED_STATE_ETHERNET_READY;
	memset(specific->station_state_specific_data, 0, 3);
}

static void update_aed_test_status(struct aed_test_status *msg, u16 seqid,
	u16 type, u16 index, u64 timestamp, u8 state)
{
	struct aed_test_status_specific *specific;

	memcpy(&eth_avtpdu_buf[ETH_ALEN], avtpdu_src, ETH_ALEN);
	inc_mac_addr((char *)&eth_avtpdu_buf[ETH_ALEN + 3], index + 1);
	msg->sequence_id = htons(seqid);
	msg->desc_type = htons(type);
	msg->desc_index = htons(index);
	specific = (struct aed_test_status_specific *)
		&msg->counters[AED_COUNTER_MESSAGE_TIMESTAMP];
	specific->message_timestamp = htobe64(timestamp);
	specific->station_state = state;
}

static void init_aed(void)
{
	struct aed_test_status *msg = (struct aed_test_status *)
		&eth_avtpdu_buf[14];
#if 0
	u8 *data = eth_avtpdu_buf;
	int i;
#endif

	init_aed_test_status(avtpdu_id, msg);
#if 0
	for (i = 0; i < 0x40; i++) {
printf("%02x ", data[i]);
		if ((i % 16) == 15)
printf("\n");
	}
#endif
}

static void send_aed(struct aed_test_status *msg, u16 portmap)
{
	int len = sizeof(struct aed_test_status) + 14;
	struct ptp_msg_hdr hdr;
#ifdef LINUX_PTP
	int rc;
#endif

	memset(&hdr, 0, sizeof(struct ptp_msg_hdr));
	hdr.messageType = 5;
	hdr.domainNumber = 0;
	hdr.sequenceId = msg->sequence_id;
	memcpy(&hdr.sourcePortIdentity.clockIdentity, avtpdu_id, 8);
#ifdef LINUX_PTP
	rc = set_msg_info(dev[1].fd, &hdr, portmap, 0, 0);
#endif
	Sendto(eth_avtpdu, eth_avtpdu_buf, len, 0, (SA *)&eth_avtpdu_addr,
		sizeof(eth_avtpdu_addr));
}

static void test_aed(u8 type, u8 index)
{
	struct aed_test_status *msg = (struct aed_test_status *)
		&eth_avtpdu_buf[14];

	if (type == 0)
		update_aed_test_status(msg, avtpdu_seqid++,
			AED_DESC_TYPE_AVB_INTERFACE, index, 0,
			AED_STATE_ETHERNET_READY);
	else if (type == 1) {
		u64 timestamp = 1234567890;

		update_aed_test_status(msg, avtpdu_seqid++,
			AED_DESC_TYPE_AVB_INTERFACE, index, timestamp,
			AED_STATE_AVB_SYNC);
	}
	send_aed(msg, 1 << (index - 1));
}

