#pragma once

#include "HMS_Interface.h"
#include <esp_err.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <stdint.h>
#include <stddef.h>

/** Native ESP-NOW compatibility profile. v1.0 payload size is deliberately used
 * as the default cross-version baseline. */
static constexpr size_t HMS_ESPNOW_MAX_PAYLOAD = 250;

struct HMS_EspNowConfig {
    // If Wi-Fi is not initialized, the driver initializes and starts it. If Wi-Fi
    // is already initialized, the driver piggybacks and does not own its lifecycle.
    bool start_wifi_if_needed = true;
    wifi_mode_t wifi_mode = WIFI_MODE_STA;

    // For a standalone deployment, configure a fixed channel. Set to 0 to leave
    // channel selection to the existing Wi-Fi owner. Do not force a channel on a
    // station already associated with an AP unless the application intends that.
    uint8_t channel = 1;

    // Bounded queue for events copied out of native Wi-Fi callbacks.
    uint8_t event_queue_depth = 8;
    uint8_t worker_priority = 5;
    uint16_t worker_stack_size = 4096;
};

class HMS_EspNow final : public HMS_Interface {
public:
    explicit HMS_EspNow(const HMS_EspNowConfig& config = HMS_EspNowConfig());
    ~HMS_EspNow() override;

    HMS_EspNow(const HMS_EspNow&) = delete;
    HMS_EspNow& operator=(const HMS_EspNow&) = delete;

    HMS_Protocol protocol() const noexcept override { return HMS_Protocol::ESPNOW; }
    bool begin() override;
    void end() override;

    // Return true means the native send request was accepted, not that the remote
    // application processed it. See the asynchronous send callback.
    bool send(const uint8_t* peer_addr, const uint8_t* data, size_t len) override;
    bool broadcast(const uint8_t* data, size_t len) override;

    // Set the 16-byte PMK before adding encrypted peers. Never log this key.
    bool setPrimaryMasterKey(const uint8_t* pmk);
    bool addPeer(const uint8_t* peer_addr, bool encrypt = false,
                 const uint8_t* lmk = nullptr, uint8_t channel = 0);
    bool removePeer(const uint8_t* peer_addr);
    bool peerExists(const uint8_t* peer_addr) const;
    bool isInitialized() const noexcept { return _isInitialized; }
    esp_err_t lastError() const noexcept { return _lastError; }

private:
    enum class EventType : uint8_t { RX, TX, STOP };
    struct Event {
        EventType type;
        uint8_t mac[6];
        int8_t rssi;
        bool success;
        uint16_t len;
        uint8_t data[HMS_ESPNOW_MAX_PAYLOAD];
    };

    static void onDataRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len);
    static void onDataSent(const wifi_tx_info_t* info, esp_now_send_status_t status);
    static HMS_EspNow* _instance;

    bool prepareWifi();
    bool enqueueEvent(const Event& event);
    static void workerEntry(void* arg);
    void workerLoop();
    bool validMac(const uint8_t* mac) const;
    bool setError(esp_err_t err);

    HMS_EspNowConfig _config;
    bool _isInitialized = false;
    bool _ownsWifiInit = false;
    bool _ownsWifiStart = false;
    bool _recvRegistered = false;
    bool _sendRegistered = false;
    bool _stopping = false;
    esp_err_t _lastError = ESP_OK;
    void* _eventQueue = nullptr;
    void* _workerStopped = nullptr;
    void* _workerTask = nullptr;
};
