#include "frame.h"

#include <stdio.h>
#include <string.h>

static int checks = 0;
static int failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        checks++;                                                          \
        if (!(cond)) {                                                     \
            failures++;                                                    \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
        }                                                                  \
    } while (0)

static void make_frame(frame_t *f, uint32_t seq, uint16_t len)
{
    memset(f, 0, sizeof *f);
    memset(f->dst, 0xAA, FRAME_MAC_LEN);
    memset(f->src, 0xBB, FRAME_MAC_LEN);
    f->seq = seq;
    f->len = len;
    for (uint16_t i = 0; i < len; i++) {
        f->payload[i] = (uint8_t)(i * 7 + seq);
    }
}

static void test_crc_known_answer(void)
{
    /* The standard CRC-32 check value for the ASCII string "123456789". */
    CHECK(frame_crc32((const uint8_t *)"123456789", 9) == 0xCBF43926u);
    CHECK(frame_crc32((const uint8_t *)"", 0) == 0x00000000u);
}

static void test_round_trip(void)
{
    const uint16_t sizes[] = {0, 1, 5, 46, 100, FRAME_MAX_PAYLOAD};
    for (size_t k = 0; k < sizeof sizes / sizeof sizes[0]; k++) {
        frame_t a, b;
        uint8_t buf[FRAME_MAX_LEN];
        size_t n = 0;

        make_frame(&a, 1000u + (uint32_t)k, sizes[k]);
        CHECK(frame_pack(&a, buf, sizeof buf, &n) == FRAME_OK);
        CHECK(n == FRAME_HDR_LEN + sizes[k] + FRAME_CRC_LEN);

        memset(&b, 0, sizeof b);
        CHECK(frame_unpack(buf, n, &b) == FRAME_OK);
        CHECK(memcmp(a.dst, b.dst, FRAME_MAC_LEN) == 0);
        CHECK(memcmp(a.src, b.src, FRAME_MAC_LEN) == 0);
        CHECK(a.seq == b.seq);
        CHECK(a.len == b.len);
        CHECK(memcmp(a.payload, b.payload, sizes[k]) == 0);
    }
}

static void test_corrupted_crc_rejected(void)
{
    frame_t a, b;
    uint8_t buf[FRAME_MAX_LEN];
    size_t n = 0;

    make_frame(&a, 7, 40);
    CHECK(frame_pack(&a, buf, sizeof buf, &n) == FRAME_OK);

    /* Flip one bit in each region in turn: MAC, seq, payload, CRC field. */
    const size_t spots[] = {0, 8, 14, FRAME_HDR_LEN + 10, n - 1};
    for (size_t k = 0; k < sizeof spots / sizeof spots[0]; k++) {
        buf[spots[k]] ^= 0x01;
        CHECK(frame_unpack(buf, n, &b) == FRAME_ERR_CRC);
        buf[spots[k]] ^= 0x01;
    }
    CHECK(frame_unpack(buf, n, &b) == FRAME_OK);   /* restored frame is valid again */
}

static void test_truncated_rejected(void)
{
    frame_t a, b;
    uint8_t buf[FRAME_MAX_LEN];
    size_t n = 0;

    make_frame(&a, 9, 30);
    CHECK(frame_pack(&a, buf, sizeof buf, &n) == FRAME_OK);

    /* Every shorter length must be rejected, never accepted or crash. */
    for (size_t cut = 0; cut < n; cut++) {
        CHECK(frame_unpack(buf, cut, &b) != FRAME_OK);
    }
    CHECK(frame_unpack(buf, n - 1, &b) == FRAME_ERR_TRUNCATED);
}

static void test_oversize_rejected(void)
{
    frame_t a, b;
    uint8_t buf[FRAME_MAX_LEN + 8];
    size_t n = 0;

    /* Packing a payload over the limit is refused. */
    make_frame(&a, 1, FRAME_MAX_PAYLOAD);
    a.len = FRAME_MAX_PAYLOAD + 1;
    CHECK(frame_pack(&a, buf, sizeof buf, &n) == FRAME_ERR_TOO_BIG);

    /* A buffer longer than the largest legal frame is refused. */
    memset(buf, 0, sizeof buf);
    CHECK(frame_unpack(buf, FRAME_MAX_LEN + 1, &b) == FRAME_ERR_TOO_BIG);

    /* A len field larger than allowed is refused, even with a matching EtherType. */
    make_frame(&a, 2, 10);
    CHECK(frame_pack(&a, buf, sizeof buf, &n) == FRAME_OK);
    buf[18] = 0xFF;
    buf[19] = 0xFF;
    CHECK(frame_unpack(buf, n, &b) == FRAME_ERR_LENGTH);

    /* An output buffer that is too small is refused. */
    make_frame(&a, 3, 100);
    CHECK(frame_pack(&a, buf, 50, &n) == FRAME_ERR_ARG);
}

static void test_wrong_ethertype_rejected(void)
{
    frame_t a, b;
    uint8_t buf[FRAME_MAX_LEN];
    size_t n = 0;

    make_frame(&a, 4, 20);
    CHECK(frame_pack(&a, buf, sizeof buf, &n) == FRAME_OK);
    buf[12] = 0x08;   /* 0x0800 = IPv4 */
    buf[13] = 0x00;
    CHECK(frame_unpack(buf, n, &b) == FRAME_ERR_ETHERTYPE);
}

static void test_padded_frame_accepted(void)
{
    /* The NIC pads short frames to 60 bytes; the len field must let us ignore the padding. */
    frame_t a, b;
    uint8_t buf[FRAME_MAX_LEN];
    size_t n = 0;

    make_frame(&a, 5, 5);
    CHECK(frame_pack(&a, buf, sizeof buf, &n) == FRAME_OK);
    CHECK(n < 60);
    memset(buf + n, 0, 60 - n);
    memset(&b, 0, sizeof b);
    CHECK(frame_unpack(buf, 60, &b) == FRAME_OK);
    CHECK(b.len == 5 && b.seq == 5);
}

static void test_null_arguments(void)
{
    frame_t a;
    uint8_t buf[FRAME_MAX_LEN];
    size_t n = 0;

    make_frame(&a, 6, 4);
    CHECK(frame_pack(NULL, buf, sizeof buf, &n) == FRAME_ERR_ARG);
    CHECK(frame_pack(&a, NULL, sizeof buf, &n) == FRAME_ERR_ARG);
    CHECK(frame_pack(&a, buf, sizeof buf, NULL) == FRAME_ERR_ARG);
    CHECK(frame_unpack(NULL, 30, &a) == FRAME_ERR_ARG);
    CHECK(frame_unpack(buf, 30, NULL) == FRAME_ERR_ARG);
}

int main(void)
{
    test_crc_known_answer();
    test_round_trip();
    test_corrupted_crc_rejected();
    test_truncated_rejected();
    test_oversize_rejected();
    test_wrong_ethertype_rejected();
    test_padded_frame_accepted();
    test_null_arguments();

    if (failures != 0) {
        printf("%d of %d checks FAILED\n", failures, checks);
        return 1;
    }
    printf("all frame tests passed (%d checks)\n", checks);
    return 0;
}
