#include "wire.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint32_t rng = 0x31cafe;
static uint32_t random32(void) {
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}
int main(void) {
    assert(tv_crc((const uint8_t *)"123456789", 9) == 0x29b1);
    tv_frame ping = {.type = 1, .seq = 1, .len = 4};
    memcpy(ping.data, "PING", 4);
    const uint8_t golden[] = {0x54, 0x56, 1, 1, 1, 0, 4, 0, 0x50, 0x49, 0x4e, 0x47, 0xca, 0xc4};
    uint8_t encoded[TV_MAX_FRAME];
    assert(tv_encode(encoded, &ping) == sizeof golden);
    assert(!memcmp(encoded, golden, sizeof golden));
    unsigned runs = 0;
    for (size_t n = 0; n <= TV_MAX_PAYLOAD; n++) {
        tv_frame f = {.type = TV_ASK, .seq = (uint16_t)random32(), .len = n}, out;
        for (size_t i = 0; i < n; i++)
            f.data[i] = (uint8_t)random32();
        uint8_t wire[TV_MAX_FRAME];
        size_t size = tv_encode(wire, &f);
        for (size_t split = 0; split <= size; split++) {
            tv_parser p = {0};
            unsigned found = 0;
            /* Caller chunk boundaries must not change a byte stream. */
            for (size_t i = 0; i < split; i++)
                found += tv_feed(&p, wire[i], &out);
            for (size_t i = split; i < size; i++)
                found += tv_feed(&p, wire[i], &out);
            assert(found == 1 && out.type == f.type && out.seq == f.seq && out.len == f.len);
            assert(!memcmp(out.data, f.data, n));
            ++runs;
        }
        /* Each bit in payload/CRC: a single corrupted bit must never pass. */
        for (size_t at = 8; at < size; at++)
            for (unsigned bit = 0; bit < 8; bit++) {
                tv_parser p = {0};
                wire[at] ^= 1u << bit;
                unsigned found = 0;
                for (size_t i = 0; i < size; i++)
                    found += tv_feed(&p, wire[i], &out);
                assert(found == 0);
                wire[at] ^= 1u << bit;
                ++runs;
            }
        tv_parser p = {0};
        const uint8_t garbage[] = {0, 'T', 'T', 'V', 9, 0, 0, 0, 255, 255};
        for (size_t i = 0; i < sizeof garbage; i++)
            tv_feed(&p, garbage[i], &out);
        unsigned found = 0;
        for (size_t i = 0; i < size; i++)
            found += tv_feed(&p, wire[i], &out);
        assert(found == 1);
    }
    tv_frame invalid = {.len = TV_MAX_PAYLOAD + 1};
    uint8_t bytes[TV_MAX_FRAME];
    assert(tv_encode(bytes, &invalid) == 0);
    tv_parser p = {0};
    tv_frame out;
    for (unsigned i = 0; i < 200000; i++) {
        tv_feed(&p, (uint8_t)random32(), &out);
        assert(p.used <= TV_MAX_FRAME);
    }
    printf("PASS: %u fragmentation/corruption cases; CRC known vector; noise/bounds\n", runs);
}
