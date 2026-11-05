/* Non-secret fixtures, only included by COMPILE_CHECK. App refuses operation.
 * Keep the full network call graph linked without requiring real credentials.
 * .invalid cannot address a real API endpoint. */
#define TV_WIFI_SSID "COMPILE_ONLY"
#define TV_WIFI_PASSWORD "COMPILE_ONLY_NOT_A_PASSWORD"
#define TV_API_URL "https://compile-only.invalid/v1/messages"
#define TV_API_KEY "COMPILE_ONLY_NOT_A_KEY"
#define TV_API_MODEL "COMPILE_ONLY_NOT_A_MODEL"
#define TV_API_VERSION "2026-10-21"
