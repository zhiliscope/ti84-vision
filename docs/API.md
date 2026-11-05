# Wi-Fi and HTTPS vision adapter

`main/wifi.c` connects in station mode using the configured SSID/password, bounded retries and a 20 s IP wait. It synchronizes time by SNTP before certificate validation (15 s initial wait). NVS initialization errors are returned; the code does not silently erase flash. Wi-Fi configuration is put in RAM. An initialization failure partway through setup may require a reboot before retrying; ordinary connectivity failures can be retried.

`main/vision.c` implements the documented [Anthropic Messages endpoint](https://platform.claude.com/docs/en/api/messages/create) and [base64 image source](https://platform.claude.com/docs/en/build-with-claude/vision), using ESP-IDF HTTP/TLS and cJSON. Configure endpoint, API version and an available vision-capable model explicitly; the project makes no assumption about your provider/account/model access.

```text
POST https://api.anthropic.com/v1/messages
Content-Type: application/json
x-api-key: <configured secret>
anthropic-version: <configured version>
```

Request JSON (illustrative JPEG data placeholder; firmware sends a real captured JPEG):

```json
{
  "model": "YOUR_AVAILABLE_VISION_MODEL",
  "max_tokens": 1024,
  "messages": [{
    "role": "user",
    "content": [
      {"type": "image", "source": {
        "type": "base64", "media_type": "image/jpeg", "data": "BASE64_JPEG"
      }},
      {"type": "text", "text": "Describe this image concisely."}
    ]
  }]
}
```

The decoder requires HTTP 200 and a JSON `content` array and joins its `type: text` blocks. Other block types are ignored; no text is an error. Unsupported characters become `?`, continuation bytes are skipped and newline is retained for the calculator's ASCII font. Maximum text is 8192 bytes plus NUL; exceeding it errors rather than silently truncating. The result is a single image/question, not a persistent conversation. Capture first; asking without an image errors.

HTTPS only; CA bundle and hostname verification stay enabled. Device time must be valid. Redirects are disabled so credentials cannot follow a redirect to another host. The configured endpoint itself is trusted by the owner and receives both the image and API key; a Messages-compatible proxy can be selected, but arbitrary providers need separate encoding/parsing code. Keys and Wi-Fi details live in ignored `firmware/config/local_config.h`; build products contain them and must remain private. No key, prompt, image, complete HTTP body or provider error body is logged or sent to TI. Status contains only counts, readiness and error names.

Requests use up to 256 KiB JPEG, bounded base64/JSON allocations, a 32 KiB HTTP response cap and a 90 s HTTPS phase deadline with 10 s socket operation timeouts. Reads close on overflow, incomplete/invalid JSON or transport failure. A blocking socket call can finish up to its timeout after the phase deadline; this is not a hard realtime cancellation guarantee. Wi-Fi/time synchronization precede the HTTPS phase, so use the 180 s TI wait for first use. ESP USB polling continues in another task. No automatic API retry occurs, including HTTP auth, rate limit and server failures.

Provider/model errors surface as sanitized HTTP status codes or ESP error names; inspect connectivity and configuration without printing secrets. Follow `TESTING.md` to distinguish authentication, rate limits, oversized bodies and TLS failures. No API request or Wi-Fi association was performed during this build session.
