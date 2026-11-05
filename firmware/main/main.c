#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "modules.h"
#include "sdkconfig.h"
#include "tinyusb.h"
#include "tusb.h"
#include "tusb_cdc_acm.h"
#include "wire.h"
#include <stdio.h>
#include <string.h>
/* Internal PHY; one CDC function, no USB-Serial-JTAG console. */
static const tusb_desc_device_t device_desc = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = 64,
    /* Espressif's example IDs for local experiments; not an allocated product ID. */
    .idVendor = 0x303a,
    .idProduct = 0x4001,
    .bcdDevice = 0x0100,
    .iManufacturer = 1,
    .iProduct = 2,
    .iSerialNumber = 3,
    .bNumConfigurations = 1};
static const uint8_t config_desc[] = {TUD_CONFIG_DESCRIPTOR(1, 2, 0,
                                                            TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN,
#if CONFIG_TV_SELF_POWERED
                                                            TUSB_DESC_CONFIG_ATT_SELF_POWERED,
#else
                                                            0,
#endif
                                                            100),
                                      TUD_CDC_DESCRIPTOR(0, 4, 0x81, 8, 0x02, 0x82, 64)};
static const char *strings[] = {(const char[]){0x09, 0x04}, "Prototype", "TI Vision CDC",
                                "TV-PROTOTYPE-1", "CDC"};
typedef struct {
    tv_frame f;
    uint32_t epoch;
} job;
static QueueHandle_t workq, doneq;
static char answer[TV_TEXT_LIMIT + 1];
static size_t answer_len;
static bool busy;
static uint32_t epoch;
static void reply(tv_frame *r, const tv_frame *q, uint8_t type, const char *text) {
    memset(r, 0, sizeof *r);
    r->type = type;
    r->seq = q->seq;
    r->len = strlen(text);
    if (r->len > TV_MAX_PAYLOAD)
        r->len = TV_MAX_PAYLOAD;
    memcpy(r->data, text, r->len);
}
static void worker(void *arg) {
    (void)arg;
    job j;
    for (;;)
        if (xQueueReceive(workq, &j, portMAX_DELAY)) {
            char detail[80];
            esp_err_t err;
            if (j.f.type == TV_CAPTURE) {
                size_t bytes = 0;
                err = tv_camera_capture(&bytes);
                snprintf(detail, sizeof detail, "CAPTURED %u bytes", (unsigned)bytes);
            } else {
                char prompt[TV_MAX_PAYLOAD + 1];
                memcpy(prompt, j.f.data, j.f.len);
                prompt[j.f.len] = 0;
                err = tv_vision_ask(prompt, answer, sizeof answer);
                snprintf(detail, sizeof detail, "READY %u bytes", (unsigned)strlen(answer));
            }
            tv_frame response;
            if (err == ESP_OK)
                reply(&response, &j.f, j.f.type | 0x80, detail);
            else {
                snprintf(detail, sizeof detail, "%s: %s",
                         j.f.type == TV_CAPTURE ? "CAMERA" : "VISION", esp_err_to_name(err));
                if (j.f.type == TV_ASK && tv_vision_http_status() && tv_vision_http_status() != 200)
                    snprintf(detail, sizeof detail, "VISION: HTTP %d", tv_vision_http_status());
                reply(&response, &j.f, TV_ERROR, detail);
            }
            j.f = response;
            xQueueSend(doneq, &j, portMAX_DELAY);
        }
}
static bool send_frame(const tv_frame *f) {
    uint8_t bytes[TV_MAX_FRAME];
    size_t n = tv_encode(bytes, f), off = 0;
    int64_t until = esp_timer_get_time() + 2000000;
    while (off < n && tud_mounted() && esp_timer_get_time() < until) {
        size_t put = tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0, bytes + off, n - off);
        off += put;
        esp_err_t e = tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, 0);
        if (e != ESP_OK && e != ESP_ERR_TIMEOUT && e != ESP_ERR_NOT_FINISHED)
            return false;
        if (off < n)
            vTaskDelay(pdMS_TO_TICKS(2));
    }
    return off == n;
}
static void process(const tv_frame *q) {
    tv_frame r;
    switch (q->type) {
    case TV_PING:
        reply(&r, q, q->type | 0x80, (q->len == 4 && !memcmp(q->data, "PING", 4)) ? "PONG" : "");
        if (!r.len)
            reply(&r, q, TV_ERROR, "PING payload must be PING");
        break;
    case TV_STATUS: {
        char s[120];
        snprintf(s, sizeof s, "USB mounted; worker %s; response %u bytes; camera %s; network %s",
                 busy ? "busy" : "idle", (unsigned)answer_len,
#if CONFIG_TV_ENABLE_CAMERA
                 "enabled",
#else
                 "disabled",
#endif
#if CONFIG_TV_ENABLE_NETWORK
                 "enabled"
#else
                 "disabled"
#endif
        );
        reply(&r, q, q->type | 0x80, s);
        break;
    }
    case TV_CAPTURE:
    case TV_ASK:
        if (busy) {
            reply(&r, q, TV_ERROR, "BUSY; wait for completion");
            break;
        }
        if ((q->type == TV_CAPTURE && q->len) ||
            (q->type == TV_ASK && (!q->len || memchr(q->data, 0, q->len)))) {
            reply(&r, q, TV_ERROR, "Invalid command payload");
            break;
        }
        busy = true;
        answer_len = 0;
        answer[0] = 0;
        {
            job j = {.f = *q, .epoch = epoch};
            if (xQueueSend(workq, &j, 0) == pdTRUE)
                return;
        }
        busy = false;
        reply(&r, q, TV_ERROR, "Worker queue full");
        break;
    case TV_RESULT: {
        if (busy) {
            reply(&r, q, TV_ERROR, "BUSY");
            break;
        }
        if (q->len != 2) {
            reply(&r, q, TV_ERROR, "Expected uint16 offset");
            break;
        }
        uint16_t off = tv_u16(q->data);
        if (off > answer_len) {
            reply(&r, q, TV_ERROR, "Offset out of range");
            break;
        }
        r = (tv_frame){.type = q->type | 0x80, .seq = q->seq};
        tv_put16(r.data, (uint16_t)answer_len);
        tv_put16(r.data + 2, off);
        size_t count = answer_len - off;
        if (count > TV_MAX_PAYLOAD - 4)
            count = TV_MAX_PAYLOAD - 4;
        memcpy(r.data + 4, answer + off, count);
        r.len = count + 4;
        break;
    }
    case TV_JPEG: {
        if (busy) {
            reply(&r, q, TV_ERROR, "BUSY");
            break;
        }
        if (q->len != 4) {
            reply(&r, q, TV_ERROR, "Expected uint32 offset");
            break;
        }
        size_t total = 0;
        const uint8_t *image = tv_camera_jpeg(&total);
        uint32_t offset = tv_u32(q->data);
        if (!image || !total) {
            reply(&r, q, TV_ERROR, "No captured JPEG");
            break;
        }
        if (offset > total) {
            reply(&r, q, TV_ERROR, "Offset out of range");
            break;
        }
        r = (tv_frame){.type = q->type | 0x80, .seq = q->seq};
        tv_put32(r.data, total);
        tv_put32(r.data + 4, offset);
        size_t count = total - offset;
        if (count > TV_MAX_PAYLOAD - 8)
            count = TV_MAX_PAYLOAD - 8;
        memcpy(r.data + 8, image + offset, count);
        r.len = count + 8;
        break;
    }
    default:
        reply(&r, q, TV_ERROR, "Unsupported command");
    }
    if (!send_frame(&r))
        ESP_LOGW("usb", "Reply enqueue failed");
}
/* Volatile gate keeps the complete USB/worker call graph in link validation,
 * even for gate-closed/compile-check binaries. Never changed by the program. */
static volatile bool operation_allowed =
#if CONFIG_TV_COMPILE_CHECK || !CONFIG_TV_POWER_APPROVED
    false;
#else
    true;
#endif
void app_main(void) {
    if (!operation_allowed) {
#if CONFIG_TV_COMPILE_CHECK
        ESP_LOGE("gate", "COMPILE CHECK ONLY: no USB/camera/Wi-Fi operation");
#else
        ESP_LOGE("gate", "USB disabled: verify power/adapter per HARDWARE.md, then menuconfig");
#endif
        return;
    }
#if CONFIG_TV_SELF_POWERED
    int pin = CONFIG_TV_VBUS_GPIO;
    if (!GPIO_IS_VALID_GPIO(pin) || pin == 19 || pin == 20 || pin == 43 || pin == 44 ||
        (pin >= 26 && pin <= 37) || pin == 0 || pin == 3 || pin == 45 || pin == 46) {
        ESP_LOGE("gate", "VBUS sense pin conflicts or is unsafe");
        return;
    }
#endif
    workq = xQueueCreate(1, sizeof(job));
    doneq = xQueueCreate(1, sizeof(job));
    assert(workq && doneq);
    assert(xTaskCreate(worker, "vision_worker", 12288, NULL, 4, NULL) == pdPASS);
    tinyusb_config_t usb = {
        .device_descriptor = &device_desc,
        .string_descriptor = strings,
        .string_descriptor_count = 5,
        .external_phy = false,
        .configuration_descriptor = config_desc,
#if CONFIG_TV_SELF_POWERED
        .self_powered = true,
        .vbus_monitor_io = CONFIG_TV_VBUS_GPIO,
#endif
    };
    ESP_ERROR_CHECK(tinyusb_driver_install(&usb));
    tinyusb_config_cdcacm_t cdc = {
        .usb_dev = TINYUSB_USBDEV_0, .cdc_port = TINYUSB_CDC_ACM_0, .rx_unread_buf_sz = 512};
    ESP_ERROR_CHECK(tusb_cdc_acm_init(&cdc));
    ESP_LOGI("usb", "CDC ready; camera/Wi-Fi are opt-in; console stays UART0");
    tv_parser parser = {0};
    tv_frame frame;
    bool mounted = false;
    int64_t last_rx = 0;
    for (;;) {
        bool now = tud_mounted();
        if (now != mounted) {
            mounted = now;
            ++epoch;
            memset(&parser, 0, sizeof parser);
            answer_len = 0;
            ESP_LOGI("usb", "%s", now ? "mounted" : "unmounted");
        }
        job j;
        if (xQueueReceive(doneq, &j, 0)) {
            busy = false;
            if (j.epoch == epoch && mounted) {
                answer_len = strlen(answer);
                send_frame(&j.f);
            } else {
                answer_len = 0;
                answer[0] = 0;
            }
        }
        if (parser.used && esp_timer_get_time() - last_rx > 2000000) {
            parser.used = 0;
            ++parser.errors;
        }
        uint8_t buf[64];
        size_t got = 0;
        if (mounted && tinyusb_cdcacm_read(TINYUSB_CDC_ACM_0, buf, sizeof buf, &got) == ESP_OK &&
            got) {
            last_rx = esp_timer_get_time();
            for (size_t i = 0; i < got; i++)
                if (tv_feed(&parser, buf[i], &frame))
                    process(&frame);
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}
