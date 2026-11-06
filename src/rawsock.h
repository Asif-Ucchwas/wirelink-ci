#ifndef RAWSOCK_H
#define RAWSOCK_H

#include <stdint.h>

/* Open an AF_PACKET raw socket bound to one interface.
 * ethertype != 0: the socket also RECEIVES frames of that EtherType (kernel-side filter).
 * ethertype == 0: send-only socket.
 * On success returns the fd and fills *ifindex and mac[6] (the interface's own MAC).
 * On failure prints the reason to stderr and returns -1. Needs root or CAP_NET_RAW. */
int rawsock_open(const char *ifname, uint16_t ethertype, int *ifindex, uint8_t mac[6]);

/* Parse "aa:bb:cc:dd:ee:ff". Returns 0 on success, -1 on bad text. */
int mac_parse(const char *text, uint8_t mac[6]);

/* Write "aa:bb:cc:dd:ee:ff" plus NUL into out (18 bytes). */
void mac_format(const uint8_t mac[6], char out[18]);

#endif
