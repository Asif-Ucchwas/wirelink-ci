#define _GNU_SOURCE
#include "rawsock.h"

#include <arpa/inet.h>
#include <linux/if_packet.h>
#include <net/if.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

int rawsock_open(const char *ifname, uint16_t ethertype, int *ifindex, uint8_t mac[6])
{
    size_t name_len = strlen(ifname);
    if (name_len == 0 || name_len >= IFNAMSIZ) {
        fprintf(stderr, "bad interface name '%s'\n", ifname);
        return -1;
    }

    int fd = socket(AF_PACKET, SOCK_RAW, htons(ethertype));
    if (fd < 0) {
        perror("socket(AF_PACKET) (need root or CAP_NET_RAW)");
        return -1;
    }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof ifr);
    memcpy(ifr.ifr_name, ifname, name_len);

    if (ioctl(fd, SIOCGIFINDEX, &ifr) < 0) {
        fprintf(stderr, "no such interface '%s'\n", ifname);
        close(fd);
        return -1;
    }
    *ifindex = ifr.ifr_ifindex;

    if (ioctl(fd, SIOCGIFHWADDR, &ifr) < 0) {
        perror("SIOCGIFHWADDR");
        close(fd);
        return -1;
    }
    memcpy(mac, ifr.ifr_hwaddr.sa_data, 6);

    struct sockaddr_ll sll;
    memset(&sll, 0, sizeof sll);
    sll.sll_family = AF_PACKET;
    sll.sll_protocol = htons(ethertype);
    sll.sll_ifindex = *ifindex;
    if (bind(fd, (struct sockaddr *)&sll, sizeof sll) < 0) {
        perror("bind");
        close(fd);
        return -1;
    }
    return fd;
}

int mac_parse(const char *text, uint8_t mac[6])
{
    unsigned v[6];
    char extra;
    if (sscanf(text, "%x:%x:%x:%x:%x:%x%c",
               &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &extra) != 6) {
        return -1;
    }
    for (int i = 0; i < 6; i++) {
        if (v[i] > 0xFFu) {
            return -1;
        }
        mac[i] = (uint8_t)v[i];
    }
    return 0;
}

void mac_format(const uint8_t mac[6], char out[18])
{
    snprintf(out, 18, "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}
