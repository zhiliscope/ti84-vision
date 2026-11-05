#include "wire.h"
#include <string.h>
uint16_t tv_crc(const uint8_t *p, size_t n) {
    uint16_t crc = 0xffff;
    while (n--) {
        crc ^= (uint16_t)*p++ << 8;
        for (unsigned i = 0; i < 8; i++)
            crc = (uint16_t)((crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1);
    }
    return crc;
}
size_t tv_encode(uint8_t *p, const tv_frame *f) {
    if (f->len > TV_MAX_PAYLOAD)
        return 0;
    p[0] = 'T';
    p[1] = 'V';
    p[2] = 1;
    p[3] = f->type;
    tv_put16(p + 4, f->seq);
    tv_put16(p + 6, f->len);
    memcpy(p + 8, f->data, f->len);
    tv_put16(p + 8 + f->len, tv_crc(p + 2, 6 + f->len));
    return f->len + 10u;
}
static void drop(tv_parser *p) {
    --p->used;
    memmove(p->bytes, p->bytes + 1, p->used);
}
int tv_feed(tv_parser *p, uint8_t b, tv_frame *out) {
    if (p->used == sizeof p->bytes) {
        drop(p);
        ++p->errors;
    }
    p->bytes[p->used++] = b;
    while (p->used) {
        if (p->bytes[0] != 'T') {
            drop(p);
            continue;
        }
        if (p->used < 2)
            return 0;
        if (p->bytes[1] != 'V') {
            drop(p);
            continue;
        }
        if (p->used < 8)
            return 0;
        uint16_t n = tv_u16(p->bytes + 6);
        if (p->bytes[2] != 1 || n > TV_MAX_PAYLOAD) {
            ++p->errors;
            drop(p);
            continue;
        }
        if (p->used < n + 10u)
            return 0;
        if (tv_crc(p->bytes + 2, n + 6u) != tv_u16(p->bytes + 8 + n)) {
            ++p->errors;
            drop(p);
            continue;
        }
        out->type = p->bytes[3];
        out->seq = tv_u16(p->bytes + 4);
        out->len = n;
        memcpy(out->data, p->bytes + 8, n);
        p->used -= n + 10u;
        memmove(p->bytes, p->bytes + n + 10u, p->used);
        return 1;
    }
    return 0;
}
