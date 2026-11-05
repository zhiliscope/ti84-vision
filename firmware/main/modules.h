#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
esp_err_t tv_camera_capture(size_t *bytes);
const uint8_t *tv_camera_jpeg(size_t *len);
esp_err_t tv_vision_ask(const char *prompt, char *text, size_t capacity);

esp_err_t tv_wifi_connect(void);
int tv_vision_http_status(void);
