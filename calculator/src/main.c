#include "wire.h"
#include <graphx.h>
#include <srldrvce.h>
#include <stdio.h>
#include <string.h>
#include <ti/getcsc.h>
#include <time.h>
static srl_device_t serial;
static uint8_t serial_buffer[1024];
static usb_device_t candidate;
static bool online, opening;
static unsigned connected_count, enabled_count;
static int last_error;
static tv_parser parser;
static tv_frame received;
static bool have_reply;
static uint16_t seq = 1;
static char status[160] = "Attach verified OTG cable; 6 = PING";
static char text[TV_TEXT_LIMIT + 1];
static size_t text_len, top_line;
static char prompt[TV_MAX_PAYLOAD + 1] = "Describe this image concisely.";
static bool dirty = true, view, settings, editing;
static unsigned timeout = 180;
static clock_t last_byte;
static void reset_connection(void) {
    online = false;
    parser.used = 0;
    have_reply = false;
    dirty = true;
}
static usb_error_t usb_event(usb_event_t event, void *data, usb_callback_data_t *ctx) {
    usb_error_t e = srl_UsbEventCallback(event, data, ctx);
    if (e != USB_SUCCESS)
        return e;
    if (event == USB_DEVICE_CONNECTED_EVENT && !(usb_GetRole() & USB_ROLE_DEVICE)) {
        ++connected_count;
        return usb_ResetDevice(data);
    }
    if (event == USB_DEVICE_ENABLED_EVENT && !(usb_GetRole() & USB_ROLE_DEVICE)) {
        ++enabled_count;
        if (!online && !opening && !candidate)
            candidate = usb_RefDevice(data);
    }
    if (event == USB_DEVICE_DISCONNECTED_EVENT) {
        if (data == candidate)
            candidate = usb_UnrefDevice(candidate);
        if (data == serial.dev) {
            srl_Close(&serial);
            reset_connection();
            strcpy(status, "USB disconnected");
        }
    }
    return USB_SUCCESS;
}
static void pump(void) {
    usb_error_t e = usb_HandleEvents();
    if (e != USB_SUCCESS) {
        last_error = e;
        snprintf(status, sizeof status, "USB event error %d", last_error);
        dirty = true;
    }
    if (candidate && !opening && !online) {
        /* Keep referenced until all synchronous setup calls finish. */
        usb_device_t d = candidate;
        candidate = NULL;
        opening = true;
        last_error =
            srl_Open(&serial, d, serial_buffer, sizeof serial_buffer, SRL_INTERFACE_ANY, 115200);
        if (!last_error && serial.dev) {
            /* srldrvce sets line coding but does not assert DTR/RTS. CDC control
             * interface is explicitly 0 in OUR ESP descriptor. No discovery API invented. */
            usb_control_setup_t setup = {
                .bmRequestType = 0x21, .bRequest = 0x22, .wValue = 3, .wIndex = 0, .wLength = 0};
            last_error = usb_DefaultControlTransfer(d, &setup, NULL, 3, NULL);
            online = last_error == USB_SUCCESS && serial.dev == d;
        }
        if (!online && serial.dev == d)
            srl_Close(&serial);
        usb_UnrefDevice(d);
        opening = false;
        snprintf(status, sizeof status, online ? "CDC open; 6 = PING" : "CDC open failed: %d",
                 last_error);
        dirty = true;
    }
    if (online) {
        uint8_t bytes[64];
        int n = srl_Read(&serial, bytes, sizeof bytes);
        if (n < 0) {
            last_error = n;
            strcpy(status, "Serial read failed; reconnect");
            srl_Close(&serial);
            reset_connection();
            return;
        }
        if (parser.used && (clock_t)(clock() - last_byte) > 2UL * CLOCKS_PER_SEC) {
            parser.used = 0;
            ++parser.errors;
        }
        if (n > 0)
            last_byte = clock();
        for (int i = 0; i < n; i++)
            if (tv_feed(&parser, bytes[i], &received))
                have_reply = true;
    }
}
static void ascii_status(const tv_frame *r) {
    size_t n = r->len;
    if (n >= sizeof status)
        n = sizeof status - 1;
    for (size_t i = 0; i < n; i++)
        status[i] = (r->data[i] >= 32 && r->data[i] <= 126) ? r->data[i] : '?';
    status[n] = 0;
    dirty = true;
}
static bool request(uint8_t type, const void *data, size_t n, tv_frame *result, unsigned seconds) {
    if (!online) {
        strcpy(status, "CDC offline; reconnect / check role");
        dirty = true;
        return false;
    }
    tv_frame q = {.type = type, .seq = seq++, .len = n};
    if (n)
        memcpy(q.data, data, n);
    uint8_t bytes[TV_MAX_FRAME];
    size_t len = tv_encode(bytes, &q), sent = 0;
    clock_t start = clock();
    have_reply = false;
    parser.used = 0;
    while (online && (clock_t)(clock() - start) < (clock_t)seconds * CLOCKS_PER_SEC) {
        pump();
        if (os_GetCSC() == sk_Clear) {
            strcpy(status, "Canceled locally; ESP may still finish");
            dirty = true;
            return false;
        }
        if (sent < len) {
            int count = srl_Write(&serial, bytes + sent, len - sent);
            if (count < 0) {
                last_error = count;
                break;
            }
            sent += (size_t)count;
        }
        if (have_reply) {
            have_reply = false;
            if (received.seq != q.seq)
                continue;
            if (received.type == TV_ERROR) {
                ascii_status(&received);
                return false;
            }
            if (received.type == (type | 0x80)) {
                *result = received;
                return true;
            }
        }
    }
    strcpy(status, "Timeout/disconnect; no automatic retry");
    dirty = true;
    return false;
}
static void line(int y, const char *s) {
    gfx_PrintStringXY(s, 0, y);
}
static void draw_status(void) {
    char buf[41];
    size_t n = strlen(status);
    for (size_t i = 0; i < n && i < 120; i += 40) {
        size_t k = n - i;
        if (k > 40)
            k = 40;
        memcpy(buf, status + i, k);
        buf[k] = 0;
        line(202 + (i / 40) * 10, buf);
    }
}
/* Fixed 40-column wrapping; explicit newlines and hard-wrapped long words.
 * ASCII keeps each cell exactly eight pixels wide. */
static size_t draw_text(void) {
    size_t row = 0, col = 0;
    char buf[41] = {0};
    for (size_t i = 0; i <= text_len; i++) {
        char c = text[i];
        if (!c || c == '\n' || col == 40) {
            buf[col] = 0;
            if (row >= top_line && row < top_line + 16)
                line(32 + (row - top_line) * 10, buf);
            ++row;
            col = 0;
            if (!c)
                break;
            if (c == '\n')
                continue;
        }
        buf[col++] = c;
    }
    return row;
}
static size_t total_lines;
static void draw(void) {
    gfx_FillScreen(255);
    gfx_SetTextFGColor(0);
    gfx_SetTextBGColor(255);
    line(0, "TI VISION  |  USB CDC prototype");
    if (editing) {
        line(20, "UP/DOWN choose character; ENTER append");
        line(30, "DEL erase; 2ND save; CLEAR cancel");
        for (size_t i = 0; i < strlen(prompt); i += 40) {
            char b[41];
            size_t n = strlen(prompt) - i;
            if (n > 40)
                n = 40;
            memcpy(b, prompt + i, n);
            b[n] = 0;
            line(60 + (i / 40) * 10, b);
        }
    } else if (settings) {
        line(25, "1 Edit question (max 240 characters)");
        line(40, "2 Reset to describe-image question");
        line(55, "3 Toggle request deadline 120/180 sec");
        line(80, "Wi-Fi / API keys: ESP local_config.h");
        line(95, "Keys never transferred to calculator");
        line(120, "CLEAR returns to menu");
    } else if (view) {
        line(15, "UP/DOWN line; LEFT/RIGHT page; CLEAR");
        total_lines = draw_text();
    } else {
        line(25, "1 Capture Image");
        line(40, "2 Ask About Image");
        line(55, "3 View Response");
        line(70, "4 Connection Status");
        line(85, "5 Settings");
        line(100, "6 PING / PONG USB test");
        line(112, "7 Run 100 PING tests (CLEAR stops)");
        line(125, online ? "CDC: open (PING checks round trip)" : "CDC: offline");
        line(150, "CLEAR exits to calculator");
    }
    draw_status();
    dirty = false;
}
static bool fetch_response(void) {
    tv_frame r;
    size_t off = 0, total = 0;
    text[0] = 0;
    text_len = 0;
    do {
        uint8_t payload[2];
        tv_put16(payload, (uint16_t)off);
        if (!request(TV_RESULT, payload, 2, &r, 5))
            return false;
        if (r.len < 4 || tv_u16(r.data + 2) != off)
            break;
        size_t t = tv_u16(r.data), count = r.len - 4;
        if (t > TV_TEXT_LIMIT || (off && t != total) || off + count > t || (!count && off < t))
            break;
        total = t;
        for (size_t i = 0; i < count; i++) {
            uint8_t c = r.data[4 + i];
            text[off + i] = (c == '\n' || (c >= 32 && c <= 126)) ? c : '?';
        }
        off += count;
        text[off] = 0;
        text_len = off;
        if (off == total)
            return true;
    } while (off < TV_TEXT_LIMIT);
    strcpy(status, "Invalid response chunk");
    dirty = true;
    return false;
}
int main(void) {
    gfx_Begin();
    gfx_SetMonospaceFont(8);
    last_error = usb_Init(usb_event, NULL, NULL, USB_DEFAULT_INIT_FLAGS);
    if (last_error) {
        snprintf(status, sizeof status, "USB init error %d", last_error);
        draw();
        while (os_GetCSC() != sk_Clear) {
        }
        usb_Cleanup();
        gfx_End();
        return 1;
    }
    tv_frame r;
    char chosen = 'a';
    char saved_prompt[sizeof prompt];
    for (;;) {
        pump();
        if (dirty)
            draw();
        uint8_t key = os_GetCSC();
        if (!key)
            continue;
        if (editing) {
            if (key == sk_Up)
                chosen = chosen == 126 ? 32 : chosen + 1;
            if (key == sk_Down)
                chosen = chosen == 32 ? 126 : chosen - 1;
            size_t n = strlen(prompt);
            if (key == sk_Del && n)
                prompt[n - 1] = 0;
            if (key == sk_Enter && n < TV_MAX_PAYLOAD) {
                prompt[n] = chosen;
                prompt[n + 1] = 0;
            }
            if (key == sk_Clear) {
                strcpy(prompt, saved_prompt);
                editing = false;
            }
            if (key == sk_2nd)
                editing = false;
            snprintf(status, sizeof status, "Selected: [%c]; question length %u", chosen,
                     (unsigned)strlen(prompt));
            dirty = true;
            continue;
        }
        if (settings) {
            if (key == sk_Clear)
                settings = false;
            if (key == sk_1) {
                strcpy(saved_prompt, prompt);
                editing = true;
            }
            if (key == sk_2)
                strcpy(prompt, "Describe this image concisely.");
            if (key == sk_3)
                timeout = timeout == 120 ? 180 : 120;
            snprintf(status, sizeof status, "Request deadline: %u sec; RAM settings", timeout);
            dirty = true;
            continue;
        }
        if (view) {
            if (key == sk_Clear)
                view = false;
            if (key == sk_Up && top_line)
                --top_line;
            if (key == sk_Down && top_line + 1 < total_lines)
                ++top_line;
            if (key == sk_Left)
                top_line = top_line >= 16 ? top_line - 16 : 0;
            if (key == sk_Right && top_line + 16 < total_lines)
                top_line += 16;
            dirty = true;
            continue;
        }
        if (key == sk_Clear)
            break;
        if (key == sk_1) {
            strcpy(status, "Capturing... CLEAR cancels wait");
            draw();
            if (request(TV_CAPTURE, NULL, 0, &r, 15))
                ascii_status(&r);
        }
        if (key == sk_2) {
            strcpy(status, "Asking... CLEAR cancels wait");
            draw();
            if (request(TV_ASK, prompt, strlen(prompt), &r, timeout) && fetch_response()) {
                view = true;
                top_line = 0;
                strcpy(status, "Response received");
            }
            dirty = true;
        }
        if (key == sk_3) {
            view = true;
            top_line = 0;
            dirty = true;
        }
        if (key == sk_4) {
            if (online && request(TV_STATUS, NULL, 0, &r, 5))
                ascii_status(&r);
            else {
                snprintf(status, sizeof status, "role=%02x conn=%u enabled=%u err=%d crc=%u",
                         usb_GetRole(), connected_count, enabled_count, last_error, parser.errors);
                dirty = true;
            }
        }
        if (key == sk_7) {
            unsigned passed = 0;
            for (; passed < 100; passed++) {
                if (!request(TV_PING, "PING", 4, &r, 5) || r.len != 4 || memcmp(r.data, "PONG", 4))
                    break;
                snprintf(status, sizeof status, "PING passes: %u / 100", passed + 1);
                draw();
            }
            snprintf(status, sizeof status, "PING test finished: %u / 100 passed", passed);
            dirty = true;
        }
        if (key == sk_5) {
            settings = true;
            dirty = true;
        }
        if (key == sk_6 && request(TV_PING, "PING", 4, &r, 5)) {
            if (r.len == 4 && !memcmp(r.data, "PONG", 4))
                strcpy(status, "PONG: USB round trip passed once");
            else
                strcpy(status, "Unexpected PING response");
            dirty = true;
        }
    }
    if (candidate)
        usb_UnrefDevice(candidate);
    if (online)
        srl_Close(&serial);
    usb_Cleanup();
    gfx_End();
    return 0;
}
