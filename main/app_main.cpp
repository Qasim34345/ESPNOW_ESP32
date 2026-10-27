#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include "HMS_EspNow.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
constexpr char TAG[] = "ESPNOW_ESP32_DEMO";
std::unique_ptr<HMS_Interface> g_transport;
uint32_t g_sequence = 0;

// Application-level protocol selector. Add another concrete component here when
// a future transport implements HMS_Interface; keep the interface component free
// of dependencies on concrete transports.
std::unique_ptr<HMS_Interface> createTransport(HMS_Protocol selected) {
    switch (selected) {
        case HMS_Protocol::ESPNOW:
            return std::unique_ptr<HMS_Interface>(new (std::nothrow) HMS_EspNow());
        case HMS_Protocol::NONE:
        default:
            return nullptr;
    }
}

void receiveCallback(const uint8_t* mac, const uint8_t* data, size_t len, int8_t rssi) {
    char macText[18];
    std::snprintf(macText, sizeof(macText), "%02X:%02X:%02X:%02X:%02X:%02X",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    // Print as bounded bytes; received data is not assumed to be a C string.
    ESP_LOGI(TAG, "RX from %s | RSSI=%d dBm | len=%u", macText, (int)rssi, (unsigned)len);
    ESP_LOG_BUFFER_HEXDUMP(TAG, data, len, ESP_LOG_INFO);
}

void sendCallback(const uint8_t* mac, bool success) {
    char macText[18];
    std::snprintf(macText, sizeof(macText), "%02X:%02X:%02X:%02X:%02X:%02X",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    ESP_LOGI(TAG, "MAC TX result to %s: %s", macText, success ? "SUCCESS" : "FAILED");
}
}

extern "C" void app_main(void) {
    // This is where the application chooses its transport. Currently only
    // ESP-NOW is implemented; HMS_Interface is ready for future transports.
    g_transport = createTransport(HMS_Protocol::ESPNOW);
    if (!g_transport) {
        ESP_LOGE(TAG, "Requested transport is not available");
        return;
    }

    // Set callbacks before begin(), so the worker cannot race callback setup.
    g_transport->setReceiveCallback(receiveCallback);
    g_transport->setSendCallback(sendCallback);

    if (!g_transport->begin()) {
        ESP_LOGE(TAG, "Selected transport startup failed");
        return;
    }

    uint8_t ownMac[6]{};
    if (esp_wifi_get_mac(WIFI_IF_STA, ownMac) == ESP_OK) {
        ESP_LOGI(TAG, "Station MAC: %02X:%02X:%02X:%02X:%02X:%02X",
                 ownMac[0], ownMac[1], ownMac[2], ownMac[3], ownMac[4], ownMac[5]);
    }
    ESP_LOGI(TAG, "Ready. Flash this same project to two ESP32 boards on the same channel.");
    ESP_LOGI(TAG, "Broadcast heartbeat every 5 seconds. Broadcast is not encrypted and has no application ACK.");

    for (;;) {
        char message[96];
        const int len = std::snprintf(message, sizeof(message),
                                      "ESPNOW_ESP32 demo seq=%lu uptime_ms=%llu",
                                      static_cast<unsigned long>(++g_sequence),
                                      static_cast<unsigned long long>(esp_timer_get_time() / 1000));
        if (len > 0 && static_cast<size_t>(len) < sizeof(message)) {
            if (!g_transport->broadcast(reinterpret_cast<const uint8_t*>(message), static_cast<size_t>(len))) {
                ESP_LOGW(TAG, "Broadcast request rejected: %s", "see transport log");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
