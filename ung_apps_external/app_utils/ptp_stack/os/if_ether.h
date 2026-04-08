

#ifndef IF_ETHER_H
#define IF_ETHER_H

#define ETH_ALEN                6
#define ETH_TLEN                2
#define ETH_HLEN                14
#define ETH_ZLEN                60
#define ETH_DATA_LEN            1500
#define ETH_FRAME_LEN           1514
#define ETH_FCS_LEN             4

#define ETH_P_IP                0x0800
#define ETH_P_ARP               0x0806
#define ETH_P_8021Q             0x8100
#define ETH_P_IPV6              0x86DD
#define ETH_P_PAUSE             0x8808

#define ETH_P_PAE               0x888E
#define ETH_P_MVRP              0x88F5
#define ETH_P_1588              0x88F7
#define ETH_P_PRP               0x88FB
#define ETH_P_HSR               0x892F

#if 0
struct ethhdr {
    u8 dest[ETH_ALEN];
    u8 src[ETH_ALEN];
    u16 proto;
} __attribute__((packed));
#endif

#define VLAN_HLEN               4

#define VLAN_ETH_HLEN           18
#define VLAN_ETH_ZLEN           64
#define VLAN_ETH_DATA_LEN       1500
#define VLAN_ETH_FRAME_LEN      1518

#define VLAN_PRIO_MASK          0xe000
#define VLAN_PRIO_SHIFT         13
#define VLAN_CFI_MASK           0x1000
#ifndef VLAN_VID_MASK
#define VLAN_VID_MASK           0x0fff
#endif

struct vlan_hdr {
    u16 tci;
    u16 proto;
};

#if 0
#define IP_CE                   0x8000
#define IP_DF                   0x4000
#define IP_MF                   0x2000
#define IP_OFFSET               0x1FFF

#define IP_PROTO_ICMP           1
#define IP_PROTO_IGMP           2
#define IP_PROTO_TCP            6
#define IP_PROTO_UDP            17

struct iphdr {
    u8 ihl:4;
    u8 version:4;
    u8 tos;
    u16 tot_len;
    u16 id;
    u16 frag_off;
    u8 ttl;
    u8 protocol;
    u16 check;
    u32 saddr;
    u32 daddr;
};
#endif

#if 0
struct in6_addr {
    union {
        u8 addr8[16];
        u16 addr16[8];
        u32 addr32[4];
    };
};
#endif

struct ipv6hdr {
    u8 prio:4;
    u8 version:4;
    u8 flow_lbl[3];
    u16 len;
    u8 nexthdr;
    u8 hop_limit;
    struct in6_addr saddr;
    struct in6_addr daddr;
};

struct udphdr {
    u16 src;
    u16 dst;
    u16 len;
    u16 check;
};

#if 0
#define AF_INET          2
#define AF_INET6         10
#define AF_PACKET        17
#endif

#endif
