#define _GNU_SOURCE
#include "frame.h"
#include "rawsock.h"

#include <errno.h>
#include <linux/if_packet.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t g_stop = 0;

static void on_signal(int sig)
{
    (void)sig;
    g_stop = 1;
}

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static void usage(void)
{
    fprintf(stderr, "usage: receiver <iface> [--timeout SEC] [--idle SEC] [--verbose]\n"
                    "  --timeout SEC  stop after SEC seconds in total\n"
                    "  --idle SEC     stop after SEC seconds without a matching frame\n");
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        usage();
        return 2;
    }
    const char *ifname = argv[1];
    double timeout = 0.0, idle = 0.0;
    int verbose = 0;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--verbose") == 0) {
            verbose = 1;
        } else if (strcmp(argv[i], "--timeout") == 0 && i + 1 < argc) {
            timeout = atof(argv[++i]);
        } else if (strcmp(argv[i], "--idle") == 0 && i + 1 < argc) {
            idle = atof(argv[++i]);
        } else {
            usage();
            return 2;
        }
    }

    setvbuf(stdout, NULL, _IOLBF, 0);

    int ifindex = 0;
    uint8_t mac[FRAME_MAC_LEN];
    int fd = rawsock_open(ifname, FRAME_ETHERTYPE, &ifindex, mac);
    if (fd < 0) {
        return 1;
    }

    /* Big receive buffer so a fast burst is not dropped by the kernel. */
    int rcvbuf = 4 * 1024 * 1024;
    if (setsockopt(fd, SOL_SOCKET, SO_RCVBUFFORCE, &rcvbuf, sizeof rcvbuf) < 0) {
        setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof rcvbuf);
    }
    /* Wake up every 100 ms so the stop flag and the timers are checked. */
    struct timeval tv = {0, 100000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    char mac_text[18];
    mac_format(mac, mac_text);
    printf("READY iface=%s mac=%s\n", ifname, mac_text);

    static const uint8_t bcast[FRAME_MAC_LEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    static frame_t f;
    uint8_t buf[2048];

    unsigned long rx = 0, crc_err = 0, other_err = 0, gaps = 0, lost = 0, dup = 0, payload_bad = 0;
    uint32_t expected = 0;
    int have_seq = 0;

    double start = now_s();
    double last = start;

    while (!g_stop) {
        double t = now_s();
        if (timeout > 0.0 && t - start >= timeout) {
            break;
        }
        if (idle > 0.0 && t - last >= idle) {
            break;
        }

        struct sockaddr_ll from;
        socklen_t from_len = sizeof from;
        ssize_t n = recvfrom(fd, buf, sizeof buf, 0, (struct sockaddr *)&from, &from_len);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                continue;
            }
            perror("recvfrom");
            break;
        }
        if (from.sll_pkttype == PACKET_OUTGOING) {
            continue;   /* our own transmissions (only seen when sender and receiver share a host) */
        }
        if (n >= (ssize_t)FRAME_MAC_LEN && memcmp(buf, mac, FRAME_MAC_LEN) != 0 &&
            memcmp(buf, bcast, FRAME_MAC_LEN) != 0) {
            continue;   /* addressed to someone else */
        }

        last = now_s();
        int rc = frame_unpack(buf, (size_t)n, &f);
        if (rc != FRAME_OK) {
            if (rc == FRAME_ERR_CRC) {
                crc_err++;
            } else {
                other_err++;
            }
            printf("DROP reason=\"%s\"\n", frame_strerror(rc));
            continue;
        }

        if (!have_seq) {
            expected = f.seq;
            have_seq = 1;
        }
        if (f.seq == expected) {
            expected++;
        } else if (f.seq > expected) {
            gaps++;
            lost += f.seq - expected;
            printf("GAP expected=%u got=%u\n", expected, f.seq);
            expected = f.seq + 1;
        } else {
            dup++;
            printf("OUT_OF_ORDER expected=%u got=%u\n", expected, f.seq);
        }

        for (uint16_t j = 0; j < f.len; j++) {
            if (f.payload[j] != (uint8_t)(f.seq + j)) {
                payload_bad++;
                break;
            }
        }
        rx++;
        if (verbose) {
            printf("RX seq=%u len=%u\n", f.seq, (unsigned)f.len);
        }
    }

    printf("SUMMARY rx=%lu crc_err=%lu other_err=%lu gaps=%lu lost=%lu dup=%lu payload_bad=%lu\n",
           rx, crc_err, other_err, gaps, lost, dup, payload_bad);
    close(fd);
    return 0;
}
