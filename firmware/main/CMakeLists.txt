idf_component_register(SRCS "main.c" "camera.c" "vision.c" "wifi.c" "../../common/wire.c"
    INCLUDE_DIRS "." "../config" "../../common"
    REQUIRES esp_tinyusb esp32-camera esp_http_client esp_wifi esp_netif nvs_flash esp_event mbedtls json esp_timer)
