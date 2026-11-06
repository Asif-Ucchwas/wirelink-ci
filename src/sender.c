#define _GNU_SOURCE
#include "frame.h"
#include "rawsock.h"

#include <errno.h>
#include <linux/if_packet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static void usage(void)
{
    fprintf(stderr,
            "usage: sender <iface> <dst-mac> [--count N] [--size BYTES] [--seq START]\n"
            "              [--corrupt INDEX] [--ethertype HEX] [--interval-us US]\n"
            "  --corrupt INDEX   flip one bit of the CRC in the INDEX-th frame (0-based)\n"
            "  --ethertype HEX   overwrite the EtherType (used to test foreign traffic)\n");
}

static int parse_num(const char *text, int base, long max, long *out)
{
    char *end = NULL;
    errno = 0;
    long v = strtol(text, &end, base);
    if (errno != 0 || end == text || *end != '\0' || v < 0 || v > max) {
        return -1;
    }
    *out = v;
    return 0;
}

static void sleep_us(long us)
{
    struct timespec ts = {us / 1000000, (us % 1000000) * 1000};
    nanosleep(&ts, NULL);
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        usage();
        return 2;
    }
    const char *ifname = argv[1];
    uint8_t dst[FRAME_MAC_LEN];
    if (mac_parse(argv[2], dst) != 0) {
        fprintf(stderr, "bad destination MAC '%s'\n", argv[2]);
        return 2;
    }

    long count = 1, size = 100, seq0 = 0, corrupt = -1, ethertype = -1, interval_us = 0;
    for (int i = 3; i < argc; i += 2) {
        if (i + 1 >= argc) {
            usage();
            return 2;
        }
        const char *opt = argv[i];
        const char *val = argv[i + 1];
        int bad;
        if (strcmp(opt, "--count") == 0) {
            bad = parse_num(val, 10, 100000000L, &count);
        } else if (strcmp(opt, "--size") == 0) {
            bad = parse_num(val, 10, (long)FRAME_MAX_PAYLOAD, &size);
        } else if (strcmp(opt, "--seq") == 0) {
            bad = parse_num(val, 10, 4000000000L, &seq0);
        } else if (strcmp(opt, "--corrupt") == 0) {
            bad = parse_num(val, 10, 100000000L, &corrupt);
        } else if (strcmp(opt, "--ethertype") == 0) {
            bad = parse_num(val, 16, 0xFFFFL, &ethertype);
        } else if (strcmp(opt, "--interval-us") == 0) {
            bad = parse_num(val, 10, 10000000L, &interval_us);
        } else {
            bad = -1;
        }
        if (bad) {
            fprintf(stderr, "bad option or value: %s %s\n", opt, val);
            usage();
            return 2;
        }
    }

    int ifindex = 0;
    uint8_t own_mac[FRAME_MAC_LEN];
    int fd = rawsock_open(ifname, 0, &ifindex, own_mac);
    if (fd < 0) {
        return 1;
    }

    struct sockaddr_ll to;
    memset(&to, 0, sizeof to);
    to.sll_family = AF_PACKET;
    to.sll_ifindex = ifindex;
    to.sll_halen = FRAME_MAC_LEN;
    memcpy(to.sll_addr, dst, FRAME_MAC_LEN);

    static frame_t f;
    uint8_t buf[FRAME_MAX_LEN];
    memcpy(f.dst, dst, FRAME_MAC_LEN);
    memcpy(f.src, own_mac, FRAME_MAC_LEN);
    f.len = (uint16_t)size;

    long dropped = 0;
    for (long i = 0; i < count; i++) {
        f.seq = (uint32_t)(seq0 + i);
        for (long j = 0; j < size; j++) {
            f.payload[j] = (uint8_t)(f.seq + (uint32_t)j);
        }
        size_t n = 0;
        int rc = frame_pack(&f, buf, sizeof buf, &n);
        if (rc != FRAME_OK) {
            fprintf(stderr, "pack failed: %s\n", frame_strerror(rc));
            close(fd);
            return 1;
        }
        if (corrupt == i) {
            buf[n - 1] ^= 0x01;
        }
        if (ethertype >= 0) {
            buf[12] = (uint8_t)(ethertype >> 8);
            buf[13] = (uint8_t)ethertype;
        }

        /* A full transmit queue is retried briefly (5 ms). If it stays full, the frame is counted
         * as dropped and the sequence number moves on, so the receiver sees the gap. */
        int sent_ok = 0;
        for (int attempt = 0; attempt < 50; attempt++) {
            ssize_t sent = sendto(fd, buf, n, 0, (struct sockaddr *)&to, sizeof to);
            if (sent == (ssize_t)n) {
                sent_ok = 1;
                break;
            }
            if (sent < 0 && (errno == ENOBUFS || errno == EAGAIN)) {
                sleep_us(100);
                continue;
            }
            perror("sendto");
            close(fd);
            return 1;
        }
        if (!sent_ok) {
            dropped++;
        }
        if (interval_us > 0) {
            sleep_us(interval_us);
        }
    }

    printf("SENT count=%ld size=%ld dropped=%ld\n", count, size, dropped);
    close(fd);
    return 0;
}
