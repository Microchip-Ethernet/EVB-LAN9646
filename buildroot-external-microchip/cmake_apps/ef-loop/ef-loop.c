// Copyright (c) 2020 Microchip Technology Inc. and its subsidiaries.
// SPDX-License-Identifier: (GPL-2.0)

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <getopt.h>
#include <ev.h>
#include <fcntl.h>
#include <unistd.h>
#include <netinet/in.h>
#include <linux/if_packet.h>
#include <linux/filter.h>
#include <asm/byteorder.h>
#include <net/if.h>
#include <linux/if_ether.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <signal.h>
#include <inttypes.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <linux/if_packet.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <assert.h>
#include <pthread.h>

#ifndef likely
# define likely(x)		__builtin_expect(!!(x), 1)
#endif
#ifndef unlikely
# define unlikely(x)		__builtin_expect(!!(x), 0)
#endif

static const char *src_port = NULL;
static int src_ifindex = -1;
static const char *dst_port = NULL;
static int dst_ifindex = -1;
static ev_io packet_watcher;
static ev_timer timer;
static int src_fd = -1;
static int dst_fd = -1;
static int total_size = 0;
static int total_frames = 0;
static int interval = 1;
static pthread_mutex_t mutex;

static struct option long_options[] =
{
	{"src", required_argument, NULL, 's'},
	{"dst", required_argument, NULL, 'd'},
	{"interval", required_argument, NULL, 'i'},
	{"fast", no_argument, NULL, 'f'},
	{"help", no_argument, NULL, 'h'},
};

static void print_help(void)
{
	printf("options:\n"
	       "--src:            source port\n"
	       "--dst:            destination port\n"
	       "--interval:       interval of measurements\n"
	       "--fast:           try fast RX\n"
	       "--help:           help\n");
}

void packet_forward(unsigned char *buf, int buf_len, struct sockaddr_ll *sl)
{
	struct iovec iov[1] =
	{
		{ .iov_base = buf, .iov_len = buf_len }
	};
	int l;

	sl->sll_ifindex = dst_ifindex;

	struct msghdr msg =
	{
		.msg_name = sl,
		.msg_namelen = sizeof(*sl),
		.msg_iov = (struct iovec *)iov,
		.msg_iovlen = 1,
		.msg_control = NULL,
		.msg_controllen = 0,
		.msg_flags = 0,
	};

	l = sendmsg(dst_fd, &msg, 0);

	if (l < 0) {
		if(errno != EWOULDBLOCK)
			fprintf(stderr, "send failed: %m\n");
	} else if (l != buf_len)
		fprintf(stderr, "short write in sendto: %d instead of %d\n", l, buf_len);
}

static void packet_rcv(EV_P_ ev_io *w, int revents)
{
	int cc;
	unsigned char buf[10000];
	struct sockaddr_ll sl;
	socklen_t salen = sizeof sl;

	cc = recvfrom(src_fd, &buf, sizeof(buf), 0, (struct sockaddr *) &sl, &salen);
	if (cc <= 0) {
		fprintf(stderr, "recvfrom failed: %m\n");
		return;
	}

	if (sl.sll_ifindex != src_ifindex)
		return;

	pthread_mutex_lock(&mutex);
	total_frames += 1;
	total_size += cc;
	pthread_mutex_unlock(&mutex);

	if (dst_port)
		packet_forward(buf, cc, &sl);
}

int open_socket(const char *port, int *fd, int *port_ifindex, bool src)
{
	int s;

	s = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
	if(s < 0) {
		fprintf(stderr, "socket failed: %m\n");
		return -1;
	}

	if (setsockopt(s, SOL_SOCKET, SO_BINDTODEVICE, port, strlen(port) + 1) < 0) {
		fprintf(stderr, "setsockopt packet filter failed: %m\n");
	} else if (fcntl(s, F_SETFL, O_NONBLOCK) < 0) {
		fprintf(stderr, "fcntl set nonblock failed: %m\n");
	} else {
		*fd = s;
		if (src) {
			ev_io_init(&packet_watcher, packet_rcv, *fd, EV_READ);
			ev_io_start(EV_DEFAULT, &packet_watcher);
		}

		*port_ifindex = if_nametoindex(port);
		if (*port_ifindex)
			return 0;
	}

	close(s);
	return -1;
}

static void show_statistics(EV_P_ ev_timer *w, int revents)
{
	pthread_mutex_lock(&mutex);

	printf("time: %ld, nr_frames: %d, total_size: %dBytes, bytes/sec: %d, Mbit/sec: %d\n",
	       time(NULL), total_frames, total_size, total_size / interval, total_size * 8 / 1000000 / interval);

	total_frames = 0;
	total_size = 0;

	pthread_mutex_unlock(&mutex);
}

struct block_desc {
	uint32_t version;
	uint32_t offset_to_priv;
	struct tpacket_hdr_v1 h1;
};

struct ring {
	struct iovec *rd;
	uint8_t *map;
	struct tpacket_req3 req;
};

static pthread_t thread_id;
static sig_atomic_t sigint = 0;

static void bye(void)
{
	sigint = 1;
	ev_break(EV_DEFAULT, EVBREAK_ALL);
}

static int setup_socket(struct ring *ring, const char *netdev)
{
	int err, i, fd, v = TPACKET_V3;
	struct sockaddr_ll ll;
	unsigned int blocksiz = 1 << 22, framesiz = 1 << 11;
	unsigned int blocknum = 64;

	fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
	if (fd < 0) {
		perror("socket");
		exit(1);
	}

	err = setsockopt(fd, SOL_PACKET, PACKET_VERSION, &v, sizeof(v));
	if (err < 0) {
		perror("setsockopt");
		exit(1);
	}

	memset(&ring->req, 0, sizeof(ring->req));
	ring->req.tp_block_size = blocksiz;
	ring->req.tp_frame_size = framesiz;
	ring->req.tp_block_nr = blocknum;
	ring->req.tp_frame_nr = (blocksiz * blocknum) / framesiz;
	ring->req.tp_retire_blk_tov = 60;
	ring->req.tp_feature_req_word = TP_FT_REQ_FILL_RXHASH;

	err = setsockopt(fd, SOL_PACKET, PACKET_RX_RING, &ring->req,
			 sizeof(ring->req));
	if (err < 0) {
		perror("setsockopt");
		exit(1);
	}

	ring->map = mmap(NULL, ring->req.tp_block_size * ring->req.tp_block_nr,
			 PROT_READ | PROT_WRITE, MAP_SHARED | MAP_LOCKED, fd, 0);
	if (ring->map == MAP_FAILED) {
		perror("mmap");
		exit(1);
	}

	ring->rd = malloc(ring->req.tp_block_nr * sizeof(*ring->rd));
	assert(ring->rd);
	for (i = 0; i < ring->req.tp_block_nr; ++i) {
		ring->rd[i].iov_base = ring->map + (i * ring->req.tp_block_size);
		ring->rd[i].iov_len = ring->req.tp_block_size;
	}

	memset(&ll, 0, sizeof(ll));
	ll.sll_family = PF_PACKET;
	ll.sll_protocol = htons(ETH_P_ALL);
	ll.sll_ifindex = if_nametoindex(netdev);
	ll.sll_hatype = 0;
	ll.sll_pkttype = 0;
	ll.sll_halen = 0;

	err = bind(fd, (struct sockaddr *) &ll, sizeof(ll));
	if (err < 0) {
		perror("bind");
		exit(1);
	}

	return fd;
}

static void walk_block(struct block_desc *pbd, const int block_num)
{
	int num_pkts = pbd->h1.num_pkts, i;
	unsigned long bytes = 0;
	struct tpacket3_hdr *ppd;

	ppd = (struct tpacket3_hdr *) ((uint8_t *) pbd +
				       pbd->h1.offset_to_first_pkt);
	for (i = 0; i < num_pkts; ++i) {
		bytes += ppd->tp_snaplen;

		ppd = (struct tpacket3_hdr *) ((uint8_t *) ppd +
					       ppd->tp_next_offset);
	}

	pthread_mutex_lock(&mutex);
	total_frames += num_pkts;
	total_size += bytes;
	pthread_mutex_unlock(&mutex);
}

static void flush_block(struct block_desc *pbd)
{
	pbd->h1.block_status = TP_STATUS_KERNEL;
}

static void teardown_socket(struct ring *ring, int fd)
{
	munmap(ring->map, ring->req.tp_block_size * ring->req.tp_block_nr);
	free(ring->rd);
	close(fd);
}

static void *work_thread(void *vargp)
{
	const char *name = (const char*)vargp;
	int fd;
	struct ring ring;
	struct pollfd pfd;
	unsigned int block_num = 0, blocks = 64;
	struct block_desc *pbd;

	memset(&ring, 0, sizeof(ring));
	fd = setup_socket(&ring, name);
	assert(fd > 0);

	memset(&pfd, 0, sizeof(pfd));
	pfd.fd = fd;
	pfd.events = POLLIN | POLLERR;
	pfd.revents = 0;

	while (likely(!sigint)) {
		pbd = (struct block_desc *) ring.rd[block_num].iov_base;

		if ((pbd->h1.block_status & TP_STATUS_USER) == 0) {
			poll(&pfd, 1, -1);
			continue;
		}

		walk_block(pbd, block_num);
		flush_block(pbd);
		block_num = (block_num + 1) % blocks;
	}

	fflush(stdout);
	teardown_socket(&ring, fd);

	return NULL;
}

static void setup_tpacket(const char *name)
{
	pthread_create(&thread_id, NULL, work_thread, (void*)name);
}

int main(int argc, char *argv[])
{
	bool use_tpacket = false;
	int ret;
	int ch;

	while ((ch = getopt_long(argc, argv, "s:d:i:hf", long_options, NULL)) != -1) {
		switch (ch) {
		case 's':
			src_port = optarg;
			break;
		case 'd':
			dst_port = optarg;
			break;
		case 'i':
			interval = atoi(optarg);
			break;
		case 'h':
			print_help();
			return 0;
		case 'f':
			use_tpacket = true;
			break;
		}
	}

	if (!src_port) {
		printf("Source port is missing\n");
		print_help();
		return -1;
	}

	if (use_tpacket == true) {
		setup_tpacket(src_port);
		goto poll;
	}

	ret = open_socket(src_port, &src_fd, &src_ifindex, true);
	if (ret) {
		printf("Could not open socket: %s\n", src_port);
		return -1;
	}

	if (dst_port) {
		if (strcmp(dst_port, src_port) == 0) {
			dst_fd = src_fd;
			dst_ifindex = src_ifindex;
		} else {
			ret = open_socket(dst_port, &dst_fd, &dst_ifindex, false);
			if (ret) {
				printf("Could not open socket: %s\n", dst_port);
				return -1;
			}
		}
	}

	ret = atexit(bye);
	if (ret) {
		printf("Could not set atexit\n");
		return -1;
	}

poll:
	ev_init(&timer, show_statistics);
	timer.repeat = interval;
	ev_timer_again(EV_DEFAULT, &timer);

	ev_run(EV_DEFAULT, 0);
	pthread_join(thread_id, NULL);

	return 0;
}
