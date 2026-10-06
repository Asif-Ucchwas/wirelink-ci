#include "frame.h"

#include <string.h>

/* Standard CRC-32 (IEEE 802.3): reflected, poly 0xEDB88320, init and final XOR 0xFFFFFFFF.
 * Bit-by-bit on purpose: short, easy to check, speed does not matter here. */
uint32_t frame_crc32(const uint8_t *data, size_t n)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            if (crc & 1u) {
                crc = (crc >> 1) ^ 0xEDB88320u;
            } else {
                crc >>= 1;
            }
        }
    }
    return ~crc;
}

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint16_t get16(const uint8_t *p)
{
    return (uint16_t)(((unsigned)p[0] << 8) | p[1]);
}

static uint32_t get32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}

int frame_pack(const frame_t *f, uint8_t *buf, size_t buf_size, size_t *out_len)
{
    if (!f || !buf || !out_len) {
        return FRAME_ERR_ARG;
    }
    if (f->len > FRAME_MAX_PAYLOAD) {
        return FRAME_ERR_TOO_BIG;
    }
    size_t body = FRAME_HDR_LEN + f->len;
    size_t total = body + FRAME_CRC_LEN;
    if (buf_size < total) {
        return FRAME_ERR_ARG;
    }

    memcpy(buf, f->dst, FRAME_MAC_LEN);
    memcpy(buf + 6, f->src, FRAME_MAC_LEN);
    put16(buf + 12, (uint16_t)FRAME_ETHERTYPE);
    put32(buf + 14, f->seq);
    put16(buf + 18, f->len);
    memcpy(buf + FRAME_HDR_LEN, f->payload, f->len);
    put32(buf + body, frame_crc32(buf, body));

    *out_len = total;
    return FRAME_OK;
}

/* A received buffer may be LONGER than the frame (the NIC pads short frames up to 60 bytes),
 * so the len field decides where the payload ends and the CRC starts. */
int frame_unpack(const uint8_t *buf, size_t n, frame_t *f)
{
    if (!buf || !f) {
        return FRAME_ERR_ARG;
    }
    if (n > FRAME_MAX_LEN) {
        return FRAME_ERR_TOO_BIG;
    }
    if (n < FRAME_HDR_LEN + FRAME_CRC_LEN) {
        return FRAME_ERR_TRUNCATED;
    }
    if (get16(buf + 12) != FRAME_ETHERTYPE) {
        return FRAME_ERR_ETHERTYPE;
    }
    uint16_t len = get16(buf + 18);
    if (len > FRAME_MAX_PAYLOAD) {
        return FRAME_ERR_LENGTH;
    }
    size_t body = FRAME_HDR_LEN + len;
    if (n < body + FRAME_CRC_LEN) {
        return FRAME_ERR_TRUNCATED;
    }
    if (get32(buf + body) != frame_crc32(buf, body)) {
        return FRAME_ERR_CRC;
    }

    memcpy(f->dst, buf, FRAME_MAC_LEN);
    memcpy(f->src, buf + 6, FRAME_MAC_LEN);
    f->seq = get32(buf + 14);
    f->len = len;
    memcpy(f->payload, buf + FRAME_HDR_LEN, len);
    return FRAME_OK;
}

const char *frame_strerror(int status)
{
    switch (status) {
    case FRAME_OK:            return "ok";
    case FRAME_ERR_ARG:       return "bad argument or buffer too small";
    case FRAME_ERR_TOO_BIG:   return "too big";
    case FRAME_ERR_TRUNCATED: return "truncated";
    case FRAME_ERR_ETHERTYPE: return "wrong EtherType";
    case FRAME_ERR_LENGTH:    return "bad length field";
    case FRAME_ERR_CRC:       return "CRC mismatch";
    default:                  return "unknown error";
    }
}
