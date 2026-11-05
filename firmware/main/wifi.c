#include "modules.h"
#include "sdkconfig.h"
#if CONFIG_TV_ENABLE_NETWORK || CONFIG_TV_COMPILE_CHECK
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"
#include <stdatomic.h>
#include <string.h>
#include <time.h>
#if CONFIG_TV_COMPILE_CHECK
#include "compile_check_config.h"
#elif __has_include("local_config.h")
#include "local_config.h"
#else
#include "local_config.example.h"
#endif
static EventGroupHandle_t wifi_events;
static atomic_uint retries;
static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg;
    (void)data;
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP)
        xEventGroupSetBits(wifi_events, 1);
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(wifi_events, 1);
        if (atomic_fetch_add(&retries, 1) < 5)
            esp_wifi_connect();
    }
}
esp_err_t tv_wifi_connect(void) {
    if (!*TV_WIFI_SSID || strlen(TV_WIFI_SSID) > 32 || strlen(TV_WIFI_PASSWORD) > 63)
        return ESP_ERR_INVALID_ARG;
    if (!wifi_events) {
        esp_err_t e = nvs_flash_init();
        /* Do not silently erase NVS on errors. */
        if (e != ESP_OK)
            return e;
        if ((e = esp_netif_init()) != ESP_OK)
            return e;
        if ((e = esp_event_loop_create_default()) != ESP_OK)
            return e;
        wifi_events = xEventGroupCreate();
        if (!wifi_events)
            return ESP_ERR_NO_MEM;
        if (!esp_netif_create_default_wifi_sta())
            return ESP_FAIL;
        wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
        if ((e = esp_wifi_init(&init)) != ESP_OK)
            return e;
        if ((e = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL)) !=
            ESP_OK)
            return e;
        if ((e = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL)) !=
            ESP_OK)
            return e;
        wifi_config_t c = {0};
        memcpy(c.sta.ssid, TV_WIFI_SSID, strlen(TV_WIFI_SSID));
        memcpy(c.sta.password, TV_WIFI_PASSWORD, strlen(TV_WIFI_PASSWORD));
        if ((e = esp_wifi_set_storage(WIFI_STORAGE_RAM)) != ESP_OK)
            return e;
        if ((e = esp_wifi_set_mode(WIFI_MODE_STA)) != ESP_OK)
            return e;
        if ((e = esp_wifi_set_config(WIFI_IF_STA, &c)) != ESP_OK)
            return e;
        if ((e = esp_wifi_start()) != ESP_OK)
            return e;
    }
    atomic_store(&retries, 0);
    if (!(xEventGroupGetBits(wifi_events) & 1))
        esp_wifi_connect();
    if (!(xEventGroupWaitBits(wifi_events, 1, pdFALSE, pdTRUE, pdMS_TO_TICKS(20000)) & 1))
        return ESP_ERR_TIMEOUT;
    if (time(NULL) < 1700000000) {
        if (!esp_sntp_enabled()) {
            esp_sntp_config_t s = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
            esp_err_t e = esp_netif_sntp_init(&s);
            if (e != ESP_OK)
                return e;
        }
        if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(15000)) != ESP_OK)
            return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}
#else
esp_err_t tv_wifi_connect(void) {
    return ESP_ERR_NOT_SUPPORTED;
}
#endif
