# Compatibility and target support

## What “generic ESP32” means

`HMS_EspNow` is written against ESP-IDF's public `esp_wifi` and `esp_now` APIs. It can be used on ESP32-family targets for which the selected ESP-IDF release supplies those APIs. It cannot make a Wi-Fi-less target ESP-NOW capable. A Wi-Fi companion chip would need its own host transport/integration.

Do not assume that every target supports every feature. Confirm in the target's ESP-IDF documentation and at runtime:

- Wi-Fi STA/AP interface availability.
- ESP-NOW API availability.
- Native ESP-NOW version from `esp_now_get_version()`.
- Native v1/v2 maximum payload macros.
- Maximum total/encrypted peer counts.
- Supported Wi-Fi channels, regulatory domain and simultaneous Wi-Fi/AP behavior.
- Encryption support and key limits.

## ESP-IDF API profiles

- The source uses the `esp_now_recv_info_t` receive callback API.
- ESP-IDF 5.0–5.4 send callbacks use the MAC-address callback signature.
- ESP-IDF 5.5+ uses the `esp_now_send_info_t` callback signature; a compile-time adapter selects the appropriate declaration.
- ESP-NOW v2 support starts with ESP-IDF 5.4 on supported targets. IDF 5.0–5.3 are treated as v1-only by the component.

Because Espressif can change public types and target-specific support, build against the exact IDF tag/commit used by the product. Do not rely on the current `latest` documentation for an older pinned SDK.

## Clean target build

```bash
idf.py set-target <target>
idf.py fullclean
idf.py build
```

Build each target in its own build directory or clean between target changes. Do not reuse generated `sdkconfig` across chips. Capture the SDK version, target, board revision, sdkconfig and compiler warnings in release evidence.
