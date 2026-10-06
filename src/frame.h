#ifndef FRAME_H
#define FRAME_H

#include <stddef.h>
#include <stdint.h>

/* Wire layout (all multi-byte fields big-endian):
 *   dst MAC (6) | src MAC (6) | EtherType (2) | seq (4) | len (2) | payload (len) | CRC-32 (4)
 * The CRC covers every byte before it. */

#define FRAME_ETHERTYPE    0x88B5u   /* IEEE local experimental EtherType */
#define FRAME_MAC_LEN      6u
#define FRAME_HDR_LEN      20u       /* 6 + 6 + 2 + 4 + 2 */
#define FRAME_CRC_LEN      4u
#define FRAME_MAX_PAYLOAD  1490u     /* 1500-byte Ethernet payload - seq - len - CRC */
#define FRAME_MAX_LEN      (FRAME_HDR_LEN + FRAME_MAX_PAYLOAD + FRAME_CRC_LEN)

typedef struct {
    uint8_t  dst[FRAME_MAC_LEN];
    uint8_t  src[FRAME_MAC_LEN];
    uint32_t seq;
    uint16_t len;
    uint8_t  payload[FRAME_MAX_PAYLOAD];
} frame_t;

enum {
    FRAME_OK            =  0,
    FRAME_ERR_ARG       = -1,   /* NULL pointer or output buffer too small */
    FRAME_ERR_TOO_BIG   = -2,   /* payload or whole frame over the maximum */
    FRAME_ERR_TRUNCATED = -3,   /* fewer bytes than the header says */
    FRAME_ERR_ETHERTYPE = -4,   /* not our EtherType */
    FRAME_ERR_LENGTH    = -5,   /* len field larger than allowed */
    FRAME_ERR_CRC       = -6    /* CRC mismatch */
};

uint32_t frame_crc32(const uint8_t *data, size_t n);
int frame_pack(const frame_t *f, uint8_t *buf, size_t buf_size, size_t *out_len);
int frame_unpack(const uint8_t *buf, size_t n, frame_t *f);
const char *frame_strerror(int status);

#endif
