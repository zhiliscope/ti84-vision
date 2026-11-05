#ifndef TV_WIRE_H
#define TV_WIRE_H
#include <stddef.h>
#include <stdint.h>
#define TV_MAX_PAYLOAD 240u
#define TV_MAX_FRAME (TV_MAX_PAYLOAD + 10u)
#define TV_TEXT_LIMIT 8192u
enum {
    TV_PING = 1,
    TV_STATUS = 2,
    TV_CAPTURE = 3,
    TV_ASK = 4,
    TV_RESULT = 5,
    TV_JPEG = 6,
    TV_ERROR = 255
};
typedef struct {
    uint8_t type;
    uint16_t seq, len;
    uint8_t data[TV_MAX_PAYLOAD];
} tv_frame;
typedef struct {
    uint8_t bytes[TV_MAX_FRAME];
    size_t used;
    unsigned errors;
} tv_parser;
uint16_t tv_crc(const uint8_t *data, size_t len);
size_t tv_encode(uint8_t *dst, const tv_frame *frame);
/* Returns 1 for a valid complete frame; 0 otherwise. Never allocates. */
int tv_feed(tv_parser *parser, uint8_t byte, tv_frame *out);
static inline uint16_t tv_u16(const uint8_t *p) {
    return (uint16_t)(p[0] | (uint16_t)p[1] << 8);
}
static inline void tv_put16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}
static inline uint32_t tv_u32(const uint8_t *p) {
    return (uint32_t)tv_u16(p) | (uint32_t)tv_u16(p + 2) << 16;
}
static inline void tv_put32(uint8_t *p, uint32_t v) {
    tv_put16(p, (uint16_t)v);
    tv_put16(p + 2, (uint16_t)(v >> 16));
}
#endif
