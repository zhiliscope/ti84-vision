#include "driver/gpio.h"
#include "esp_camera.h"
#include "esp_heap_caps.h"
#include "modules.h"
#include "sdkconfig.h"
#include <stdlib.h>
#include <string.h>
#if CONFIG_TV_COMPILE_CHECK
#include "reference_freenove_UNVERIFIED.h"
#elif __has_include("board_pins.h")
#include "board_pins.h"
#else
#include "board_pins.example.h"
#endif
static uint8_t *jpeg;
static size_t jpeg_len;
const uint8_t *tv_camera_jpeg(size_t *n) {
    *n = jpeg_len;
    return jpeg;
}
#if CONFIG_TV_ENABLE_CAMERA || CONFIG_TV_COMPILE_CHECK
static bool initialized;
static bool safe_pin(int n) {
    /* N16R8 flash/OPI PSRAM, USB, UART0, straps; conservative exclusions. */
    if (!GPIO_IS_VALID_GPIO(n) || n == 0 || n == 3 || n == 19 || n == 20 || (n >= 26 && n <= 37) ||
        n == 43 || n == 44 || n == 45 || n == 46)
        return false;
#if CONFIG_TV_SELF_POWERED
    if (n == CONFIG_TV_VBUS_GPIO)
        return false;
#endif
    return true;
}
static bool pins_valid(void) {
    const int pins[] = {TV_XCLK, TV_SDA, TV_SCL, TV_D0,    TV_D1,   TV_D2,   TV_D3,   TV_D4,
                        TV_D5,   TV_D6,  TV_D7,  TV_VSYNC, TV_HREF, TV_PCLK, TV_PWDN, TV_RESET};
    static volatile bool verified = TV_BOARD_PINS_VERIFIED;
    if (!verified)
        return false;
    for (unsigned i = 0; i < sizeof pins / sizeof *pins; i++) {
        if (i >= 14 && pins[i] == -1)
            continue;
        if (!safe_pin(pins[i]))
            return false;
        for (unsigned j = 0; j < i; j++)
            if (pins[j] == pins[i])
                return false;
    }
    return true;
}
#endif
esp_err_t tv_camera_capture(size_t *bytes) {
    *bytes = 0;
#if !CONFIG_TV_ENABLE_CAMERA && !CONFIG_TV_COMPILE_CHECK
    return ESP_ERR_NOT_SUPPORTED;
#else
    free(jpeg);
    jpeg = NULL;
    jpeg_len = 0; /* failed capture must not reuse an old image */
    if (!pins_valid())
        return ESP_ERR_INVALID_ARG;
    if (!heap_caps_get_total_size(MALLOC_CAP_SPIRAM))
        return ESP_ERR_NO_MEM;
    if (!initialized) {
        camera_config_t c = {.pin_pwdn = TV_PWDN,
                             .pin_reset = TV_RESET,
                             .pin_xclk = TV_XCLK,
                             .pin_sccb_sda = TV_SDA,
                             .pin_sccb_scl = TV_SCL,
                             .pin_d0 = TV_D0,
                             .pin_d1 = TV_D1,
                             .pin_d2 = TV_D2,
                             .pin_d3 = TV_D3,
                             .pin_d4 = TV_D4,
                             .pin_d5 = TV_D5,
                             .pin_d6 = TV_D6,
                             .pin_d7 = TV_D7,
                             .pin_vsync = TV_VSYNC,
                             .pin_href = TV_HREF,
                             .pin_pclk = TV_PCLK,
                             .xclk_freq_hz = 20000000,
                             .ledc_timer = LEDC_TIMER_0,
                             .ledc_channel = LEDC_CHANNEL_0,
                             .pixel_format = PIXFORMAT_JPEG,
                             .frame_size = FRAMESIZE_VGA,
                             .jpeg_quality = 12,
                             .fb_count = 1,
                             .fb_location = CAMERA_FB_IN_PSRAM,
                             .grab_mode = CAMERA_GRAB_WHEN_EMPTY};
        esp_err_t e = esp_camera_init(&c);
        if (e != ESP_OK)
            return e;
        initialized = true;
    }
    /* Discard queued frame, then request a fresh capture. */
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb)
        return ESP_FAIL;
    esp_camera_fb_return(fb);
    fb = esp_camera_fb_get();
    if (!fb)
        return ESP_FAIL;
    esp_err_t e = ESP_FAIL;
    if (fb->format == PIXFORMAT_JPEG && fb->len >= 4 && fb->len <= 256 * 1024 &&
        fb->buf[0] == 0xff && fb->buf[1] == 0xd8 && fb->buf[fb->len - 2] == 0xff &&
        fb->buf[fb->len - 1] == 0xd9) {
        uint8_t *copy = heap_caps_malloc(fb->len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (copy) {
            memcpy(copy, fb->buf, fb->len);
            free(jpeg);
            jpeg = copy;
            *bytes = jpeg_len = fb->len;
            e = ESP_OK;
        } else
            e = ESP_ERR_NO_MEM;
    }
    esp_camera_fb_return(fb);
    return e;
#endif
}
