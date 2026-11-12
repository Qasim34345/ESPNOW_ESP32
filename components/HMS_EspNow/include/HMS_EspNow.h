#pragma once

#include "HMS_Interface.h"
#include <esp_err.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_idf_version.h>
#include <stddef.h>
#include <stdint.h>
#include <atomic>
#include <mutex>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#if defined(ESP_NOW_MAX_DATA_LEN_V2)
static constexpr size_t HMS_ESPNOW_MAX_PAYLOAD_V2 = ESP_NOW_MAX_DATA_LEN_V2;
#else
// Older SDKs/targets may not expose v2. Never invent a 1470-byte capability.
static constexpr size_t HMS_ESPNOW_MAX_PAYLOAD_V2 = ESP_NOW_MAX_DATA_LEN;
#endif
static constexpr size_t HMS_ESPNOW_MAX_PAYLOAD_V1 = ESP_NOW_MAX_DATA_LEN;
static constexpr size_t HMS_ESPNOW_STORAGE_MAX_PAYLOAD = HMS_ESPNOW_MAX_PAYLOAD_V2;

enum class HMS_EspNowPayloadProfile : uint8_t { V1_COMPAT = 0, V2 = 1 };
enum class HMS_EspNowPeerPolicy : uint8_t { EXPLICIT_ONLY = 0, AUTO_ADD_UNENCRYPTED = 1 };
enum class HMS_EspNowWifiPolicy : uint8_t { USE_EXISTING = 0, MANAGE_IF_UNINITIALIZED = 1 };
enum class HMS_EspNowChannelPolicy : uint8_t { REQUIRE_MATCH = 0, USE_CURRENT = 1 };

struct HMS_EspNowConfig {
    HMS_EspNowWifiPolicy wifi_policy = HMS_EspNowWifiPolicy::MANAGE_IF_UNINITIALIZED;
    HMS_EspNowChannelPolicy channel_policy = HMS_EspNowChannelPolicy::REQUIRE_MATCH;
    wifi_mode_t wifi_mode = WIFI_MODE_STA;
    wifi_interface_t interface = WIFI_IF_STA;
    uint8_t channel = 0; // 0 = use current channel; 1..13 are broadly usable, 14 is region-specific
    HMS_EspNowPayloadProfile payload_profile = HMS_EspNowPayloadProfile::V1_COMPAT;
    HMS_EspNowPeerPolicy peer_policy = HMS_EspNowPeerPolicy::EXPLICIT_ONLY;
    uint16_t rx_queue_depth = 8;
    uint16_t tx_queue_depth = 16;
    uint16_t worker_stack_size = 6144;
    UBaseType_t worker_priority = 5;
    bool allow_broadcast = true;
};

struct HMS_EspNowStats {
    uint32_t rx_received = 0;
    uint32_t rx_delivered = 0;
    uint32_t rx_dropped_queue_full = 0;
    uint32_t rx_dropped_stopping = 0;
    uint32_t rx_dropped_invalid = 0;
    uint32_t tx_callbacks = 0;
    uint32_t tx_success = 0;
    uint32_t tx_failures = 0;
    uint32_t tx_dropped_queue_full = 0;
    uint32_t tx_dropped_stopping = 0;
    uint32_t tx_requests_accepted = 0;
    uint32_t tx_requests_rejected = 0;
};

struct HMS_EspNowCapabilities {
    uint32_t native_version = 0; // 1 or 2 as reported by esp_now_get_version()
    size_t v1_max_payload = HMS_ESPNOW_MAX_PAYLOAD_V1;
    size_t v2_max_payload = HMS_ESPNOW_MAX_PAYLOAD_V2;
    size_t effective_max_payload = 0;
    bool v2_supported = false;
};

/**
 * ESP-NOW transport for ESP-IDF targets which provide the ESP-NOW API.
 * One active instance is supported because ESP-IDF callback registration is global.
 * send()/broadcast() report local API acceptance; SendCallback reports MAC status only.
 */
class HMS_EspNow final : public HMS_Interface {
public:
    explicit HMS_EspNow(const HMS_EspNowConfig& config = HMS_EspNowConfig());
    ~HMS_EspNow() override;
    HMS_EspNow(const HMS_EspNow&) = delete;
    HMS_EspNow& operator=(const HMS_EspNow&) = delete;

    HMS_Protocol protocol() const noexcept override { return HMS_Protocol::ESPNOW; }
    bool begin() override;
    esp_err_t beginDetailed();
    void requestStop(); // callback-safe no-op before the worker starts; call end() later from a task
    void end() override; // application-task context only; waits for bounded callbacks to finish

    bool send(const uint8_t* peer_addr, const uint8_t* data, size_t len) override;
    esp_err_t sendDetailed(const uint8_t* peer_addr, const uint8_t* data, size_t len);
    bool broadcast(const uint8_t* data, size_t len) override;
    esp_err_t broadcastDetailed(const uint8_t* data, size_t len);
    size_t maxPayload() const noexcept override;

    bool setPrimaryMasterKey(const uint8_t* pmk);
    bool addPeer(const uint8_t* peer_addr, bool encrypt = false, const uint8_t* lmk = nullptr, uint8_t channel = 0);
    esp_err_t addPeerDetailed(const uint8_t* peer_addr, bool encrypt = false, const uint8_t* lmk = nullptr, uint8_t channel = 0);
    bool removePeer(const uint8_t* peer_addr);
    esp_err_t removePeerDetailed(const uint8_t* peer_addr);
    bool peerExists(const uint8_t* peer_addr) const;

    bool isInitialized() const noexcept;
    HMS_EspNowCapabilities capabilities() const noexcept;
    esp_err_t lastError() const noexcept;
    HMS_EspNowStats stats() const;
    void resetStats();

private:
    enum class EventType : uint8_t { RX, TX };
    struct RxEvent { uint8_t mac[6]; int8_t rssi; uint16_t len; uint8_t data[HMS_ESPNOW_STORAGE_MAX_PAYLOAD]; };
    struct TxEvent { uint8_t mac[6]; bool success; };

    static void onDataRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len);
#if defined(ESP_IDF_VERSION) && ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)
    static void onDataSent(const esp_now_send_info_t* info, esp_now_send_status_t status);
#else
    static void onDataSent(const uint8_t* mac, esp_now_send_status_t status);
#endif
    static HMS_EspNow* instanceSnapshot();
    bool prepareWifi();
    bool validMac(const uint8_t* mac, bool allowBroadcast = false) const;
    bool setError(esp_err_t err);
    void workerLoop();
    static void workerEntry(void* arg);
    void enqueueRx(const RxEvent& event);
    void enqueueTx(const TxEvent& event);
    void releaseOwnedWifi();
    esp_err_t cleanupFailedBegin(esp_err_t cause);
    size_t configuredMaxPayload() const noexcept;

    HMS_EspNowConfig config_;
    mutable std::recursive_mutex apiMutex_;
    mutable std::mutex endMutex_;
    mutable std::mutex errorMutex_;
    esp_err_t lastError_ = ESP_OK;
    bool initialized_ = false;
    std::atomic<bool> stopping_{false};
    std::atomic<bool> stopRequested_{false};
    bool ownsWifiInit_ = false, ownsWifiStart_ = false, nowInitialized_ = false;
    bool recvRegistered_ = false, sendRegistered_ = false;
    QueueHandle_t rxQueue_ = nullptr, txQueue_ = nullptr;
    SemaphoreHandle_t workerStopped_ = nullptr;
    std::atomic<TaskHandle_t> workerTask_{nullptr};
    std::atomic<uint32_t> nativeVersion_{0};
    std::atomic<size_t> effectiveMaxPayload_{0};
    std::atomic<uint32_t> rxReceived_{0}, rxDelivered_{0}, rxDroppedQueueFull_{0}, rxDroppedStopping_{0}, rxDroppedInvalid_{0};
    std::atomic<uint32_t> txCallbacks_{0}, txSuccess_{0}, txFailures_{0}, txDroppedQueueFull_{0}, txDroppedStopping_{0}, txRequestsAccepted_{0}, txRequestsRejected_{0};
};
