#include "modules.h"
#include "sdkconfig.h"
#include "wire.h"
#if CONFIG_TV_ENABLE_NETWORK || CONFIG_TV_COMPILE_CHECK
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#include "mbedtls/base64.h"
#include <stdlib.h>
#include <string.h>
#if CONFIG_TV_COMPILE_CHECK
#include "compile_check_config.h"
#elif __has_include("local_config.h")
#include "local_config.h"
#else
#include "local_config.example.h"
#endif
/* Intentional display conversion. No terminal/control escape sequences. */
static bool append_ascii(char *dst, size_t cap, size_t *used, const char *src) {
    while (*src) {
        unsigned char c = (unsigned char)*src++;
        if ((c & 0xc0) == 0x80)
            continue;
        if (*used + 1 >= cap)
            return false;
        dst[(*used)++] = (c == '\n' || (c >= 32 && c <= 126)) ? (char)c : '?';
    }
    dst[*used] = 0;
    return true;
}
#endif
static int last_http_status;
int tv_vision_http_status(void) {
    return last_http_status;
}
esp_err_t tv_vision_ask(const char *prompt, char *text, size_t cap) {
    last_http_status = 0;
    if (cap)
        text[0] = 0;
#if !CONFIG_TV_ENABLE_NETWORK && !CONFIG_TV_COMPILE_CHECK
    (void)prompt;
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (cap < 2 || !*TV_API_KEY || !*TV_API_MODEL || strncmp(TV_API_URL, "https://", 8))
        return ESP_ERR_INVALID_ARG;
    if (strchr(TV_API_KEY, '\r') || strchr(TV_API_KEY, '\n'))
        return ESP_ERR_INVALID_ARG;
    size_t n;
    const uint8_t *jpeg = tv_camera_jpeg(&n);
    if (!jpeg || !n)
        return ESP_ERR_INVALID_STATE;
    esp_err_t result = tv_wifi_connect();
    if (result != ESP_OK)
        return result;
    size_t b64cap = ((n + 2) / 3) * 4 + 1, b64len = 0;
    unsigned char *b64 = malloc(b64cap);
    char *body = NULL;
    cJSON *root = NULL, *parsed = NULL;
    esp_http_client_handle_t client = NULL;
    char *response = calloc(1, 32769);
    size_t response_len = 0;
    int64_t deadline = esp_timer_get_time() + 90000000;
    if (!b64 || !response) {
        result = ESP_ERR_NO_MEM;
        goto done;
    }
    if (mbedtls_base64_encode(b64, b64cap, &b64len, jpeg, n)) {
        result = ESP_FAIL;
        goto done;
    }
    b64[b64len] = 0;
    /* Constant skeleton is checked by parser; dynamic strings are JSON escaped. */
    root = cJSON_Parse(
        "{\"max_tokens\":1024,\"messages\":[{\"role\":\"user\",\"content\":[{\"type\":\"image\","
        "\"source\":{\"type\":\"base64\",\"media_type\":\"image/jpeg\"}},{\"type\":\"text\"}]}]}");
    if (!root || !cJSON_AddStringToObject(root, "model", TV_API_MODEL)) {
        result = ESP_ERR_NO_MEM;
        goto done;
    }
    cJSON *content = cJSON_GetObjectItem(
        cJSON_GetArrayItem(cJSON_GetObjectItem(root, "messages"), 0), "content");
    cJSON *source = cJSON_GetObjectItem(cJSON_GetArrayItem(content, 0), "source");
    if (!cJSON_AddStringToObject(source, "data", (char *)b64) ||
        !cJSON_AddStringToObject(cJSON_GetArrayItem(content, 1), "text", prompt)) {
        result = ESP_ERR_NO_MEM;
        goto done;
    }
    body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    root = NULL;
    free(b64);
    b64 = NULL;
    if (!body) {
        result = ESP_ERR_NO_MEM;
        goto done;
    }
    esp_http_client_config_t cfg = {.url = TV_API_URL,
                                    .method = HTTP_METHOD_POST,
                                    .timeout_ms = 10000,
                                    .crt_bundle_attach = esp_crt_bundle_attach,
                                    .disable_auto_redirect = true};
    client = esp_http_client_init(&cfg);
    if (!client) {
        result = ESP_ERR_NO_MEM;
        goto done;
    }
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "x-api-key", TV_API_KEY);
    esp_http_client_set_header(client, "anthropic-version", TV_API_VERSION);
    size_t body_len = strlen(body), sent = 0;
    result = esp_http_client_open(client, (int)body_len);
    if (result != ESP_OK)
        goto done;
    while (sent < body_len) {
        if (esp_timer_get_time() > deadline) {
            result = ESP_ERR_TIMEOUT;
            goto done;
        }
        int wrote = esp_http_client_write(client, body + sent, (int)(body_len - sent));
        if (wrote <= 0) {
            result = ESP_FAIL;
            goto done;
        }
        sent += (size_t)wrote;
    }
    int64_t announced = esp_http_client_fetch_headers(client);
    if (announced < 0) {
        result = ESP_FAIL;
        goto done;
    }
    last_http_status = esp_http_client_get_status_code(client);
    if (last_http_status != 200) {
        result = ESP_ERR_INVALID_RESPONSE;
        goto done;
    }
    if (announced > 32768) {
        result = ESP_ERR_INVALID_SIZE;
        goto done;
    }
    for (;;) {
        if (esp_timer_get_time() > deadline) {
            result = ESP_ERR_TIMEOUT;
            goto done;
        }
        /* fetch_headers may already have cached the entire body. Always drain
         * read(), even when is_complete_data_received() is already true. */
        char chunk[512];
        int count = esp_http_client_read(client, chunk, sizeof chunk);
        if (count == 0 && esp_http_client_is_complete_data_received(client))
            break;
        if (count <= 0) {
            result = ESP_ERR_INVALID_RESPONSE;
            goto done;
        }
        if (response_len + (size_t)count > 32768) {
            result = ESP_ERR_INVALID_SIZE;
            goto done;
        }
        memcpy(response + response_len, chunk, count);
        response_len += (size_t)count;
    }
    response[response_len] = 0;
    parsed = cJSON_ParseWithLength(response, response_len);
    if (!parsed) {
        result = ESP_ERR_INVALID_RESPONSE;
        goto done;
    }
    cJSON *blocks = cJSON_GetObjectItemCaseSensitive(parsed, "content"), *block;
    if (!cJSON_IsArray(blocks)) {
        result = ESP_ERR_INVALID_RESPONSE;
        goto done;
    }
    size_t used = 0;
    cJSON_ArrayForEach(block, blocks) {
        cJSON *kind = cJSON_GetObjectItemCaseSensitive(block, "type");
        cJSON *value = cJSON_GetObjectItemCaseSensitive(block, "text");
        if (cJSON_IsString(kind) && !strcmp(kind->valuestring, "text") && cJSON_IsString(value)) {
            if (used && !append_ascii(text, cap, &used, "\n")) {
                result = ESP_ERR_INVALID_SIZE;
                goto done;
            }
            if (!append_ascii(text, cap, &used, value->valuestring)) {
                result = ESP_ERR_INVALID_SIZE;
                goto done;
            }
        }
    }
    result = used ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
done:
    if (client)
        esp_http_client_cleanup(client);
    cJSON_Delete(root);
    cJSON_Delete(parsed);
    free(body);
    free(b64);
    free(response);
    if (result != ESP_OK)
        text[0] = 0;
    return result;
#endif
}
