#include "HMS_EspNow.h"
#include <cstring>
#include <mutex>
#include "esp_err.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

namespace {
constexpr char TAG[] = "HMS_EspNow";
constexpr uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
std::mutex g_instanceMutex;
HMS_EspNow* g_instance = nullptr;
std::atomic<uint32_t> g_callbacksInFlight{0};
}

HMS_EspNow::HMS_EspNow(const HMS_EspNowConfig& config) : config_(config) {}
HMS_EspNow::~HMS_EspNow() { end(); }

HMS_EspNow* HMS_EspNow::instanceSnapshot() {
    std::lock_guard<std::mutex> lock(g_instanceMutex);
    HMS_EspNow* instance = g_instance;
    if (instance) g_callbacksInFlight.fetch_add(1, std::memory_order_acq_rel);
    return instance;
}

bool HMS_EspNow::setError(esp_err_t err) {
    { std::lock_guard<std::mutex> lock(errorMutex_); lastError_ = err; }
    if (err != ESP_OK) ESP_LOGE(TAG, "operation failed: %s (0x%x)", esp_err_to_name(err), (unsigned)err);
    return err == ESP_OK;
}
esp_err_t HMS_EspNow::lastError() const noexcept {
    std::lock_guard<std::mutex> lock(errorMutex_);
    return lastError_;
}
bool HMS_EspNow::isInitialized() const noexcept {
    std::lock_guard<std::recursive_mutex> lock(apiMutex_);
    return initialized_;
}
size_t HMS_EspNow::configuredMaxPayload() const noexcept {
    return effectiveMaxPayload_.load(std::memory_order_acquire);
}
size_t HMS_EspNow::maxPayload() const noexcept { return configuredMaxPayload(); }
HMS_EspNowCapabilities HMS_EspNow::capabilities() const noexcept {
    HMS_EspNowCapabilities c{};
    c.native_version = nativeVersion_.load(std::memory_order_acquire);
    c.v1_max_payload = HMS_ESPNOW_MAX_PAYLOAD_V1;
    c.v2_max_payload = HMS_ESPNOW_MAX_PAYLOAD_V2;
    c.v2_supported = c.native_version >= 2;
    c.effective_max_payload = effectiveMaxPayload_.load(std::memory_order_acquire);
    return c;
}

bool HMS_EspNow::validMac(const uint8_t* mac, bool allowBroadcast) const {
    if (!mac) return false;
    bool allZero = true, allFF = true;
    for (size_t i = 0; i < 6; ++i) { allZero &= mac[i] == 0; allFF &= mac[i] == 0xFF; }
    if (allZero) return false;
    if (allFF) return allowBroadcast && config_.allow_broadcast;
    return (mac[0] & 0x01U) == 0;
}

bool HMS_EspNow::prepareWifi() {
    wifi_mode_t currentMode = WIFI_MODE_NULL;
    esp_err_t err = esp_wifi_get_mode(&currentMode);
    if (err == ESP_ERR_WIFI_NOT_INIT) {
        if (config_.wifi_policy != HMS_EspNowWifiPolicy::MANAGE_IF_UNINITIALIZED) return setError(err);
        wifi_init_config_t wifiConfig = WIFI_INIT_CONFIG_DEFAULT();
        err = esp_wifi_init(&wifiConfig); if (err != ESP_OK) return setError(err);
        ownsWifiInit_ = true;
        err = esp_wifi_set_storage(WIFI_STORAGE_RAM); if (err != ESP_OK) return setError(err);
        err = esp_wifi_set_mode(config_.wifi_mode); if (err != ESP_OK) return setError(err);
        err = esp_wifi_start(); if (err != ESP_OK) return setError(err);
        ownsWifiStart_ = true;
    } else if (err != ESP_OK) return setError(err);
    else {
        const bool hasSta = currentMode == WIFI_MODE_STA || currentMode == WIFI_MODE_APSTA;
        const bool hasAp = currentMode == WIFI_MODE_AP || currentMode == WIFI_MODE_APSTA;
        if ((config_.interface == WIFI_IF_STA && !hasSta) || (config_.interface == WIFI_IF_AP && !hasAp)) return setError(ESP_ERR_INVALID_STATE);
    }

    if (config_.channel != 0) {
        uint8_t currentChannel = 0; wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
        err = esp_wifi_get_channel(&currentChannel, &second);
        if (err != ESP_OK) return setError(err);
        if (currentChannel != config_.channel) {
            if (ownsWifiStart_) {
                err = esp_wifi_set_channel(config_.channel, WIFI_SECOND_CHAN_NONE);
                if (err != ESP_OK) return setError(err);
            } else if (config_.channel_policy == HMS_EspNowChannelPolicy::REQUIRE_MATCH) {
                ESP_LOGE(TAG, "Wi-Fi channel mismatch: active=%u requested=%u", currentChannel, config_.channel);
                return setError(ESP_ERR_INVALID_STATE);
            } else {
                ESP_LOGW(TAG, "Using externally managed Wi-Fi channel %u (requested %u)", currentChannel, config_.channel);
            }
        }
    }
    return true;
}

esp_err_t HMS_EspNow::cleanupFailedBegin(esp_err_t cause) {
    if (sendRegistered_) { (void)esp_now_unregister_send_cb(); sendRegistered_ = false; }
    if (recvRegistered_) { (void)esp_now_unregister_recv_cb(); recvRegistered_ = false; }
    {
        std::lock_guard<std::mutex> lock(g_instanceMutex);
        if (g_instance == this) g_instance = nullptr;
    }
    while (g_callbacksInFlight.load(std::memory_order_acquire) != 0) vTaskDelay(1);
    if (nowInitialized_) { (void)esp_now_deinit(); nowInitialized_ = false; }
    if (rxQueue_) { vQueueDelete(rxQueue_); rxQueue_ = nullptr; }
    if (txQueue_) { vQueueDelete(txQueue_); txQueue_ = nullptr; }
    if (workerStopped_) { vSemaphoreDelete(workerStopped_); workerStopped_ = nullptr; }
    releaseOwnedWifi();
    initialized_ = false;
    nativeVersion_ = 0;
    effectiveMaxPayload_ = 0;
    stopping_ = false;
    stopRequested_ = false;
    setError(cause);
    return cause;
}

bool HMS_EspNow::begin() {
    std::lock_guard<std::recursive_mutex> lock(apiMutex_);
    if (initialized_) return setError(ESP_OK);
    if (stopping_.load()) return setError(ESP_ERR_INVALID_STATE);
    if (config_.rx_queue_depth == 0 || config_.tx_queue_depth == 0 || config_.worker_stack_size < 3072 ||
        (config_.wifi_policy != HMS_EspNowWifiPolicy::USE_EXISTING && config_.wifi_policy != HMS_EspNowWifiPolicy::MANAGE_IF_UNINITIALIZED) ||
        (config_.channel_policy != HMS_EspNowChannelPolicy::REQUIRE_MATCH && config_.channel_policy != HMS_EspNowChannelPolicy::USE_CURRENT) ||
        (config_.wifi_mode != WIFI_MODE_STA && config_.wifi_mode != WIFI_MODE_AP && config_.wifi_mode != WIFI_MODE_APSTA) ||
        (config_.interface == WIFI_IF_AP && config_.wifi_mode == WIFI_MODE_STA) ||
        (config_.interface != WIFI_IF_STA && config_.interface != WIFI_IF_AP) || config_.channel > 14 ||
        (config_.payload_profile != HMS_EspNowPayloadProfile::V1_COMPAT && config_.payload_profile != HMS_EspNowPayloadProfile::V2) ||
        (config_.peer_policy != HMS_EspNowPeerPolicy::EXPLICIT_ONLY && config_.peer_policy != HMS_EspNowPeerPolicy::AUTO_ADD_UNENCRYPTED))
        return setError(ESP_ERR_INVALID_ARG);

    {
        std::lock_guard<std::mutex> instanceLock(g_instanceMutex);
        if (g_instance && g_instance != this) return setError(ESP_ERR_INVALID_STATE);
        g_instance = this;
    }
    stopping_ = false; stopRequested_ = false;
    if (!prepareWifi()) return cleanupFailedBegin(lastError());

    rxQueue_ = xQueueCreate(config_.rx_queue_depth, sizeof(RxEvent));
    txQueue_ = xQueueCreate(config_.tx_queue_depth, sizeof(TxEvent));
    workerStopped_ = xSemaphoreCreateBinary();
    if (!rxQueue_ || !txQueue_ || !workerStopped_) return cleanupFailedBegin(ESP_ERR_NO_MEM);

    esp_err_t err = esp_now_init();
    if (err != ESP_OK) return cleanupFailedBegin(err);
    nowInitialized_ = true;

    uint32_t version = 1;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 4, 0)
    err = esp_now_get_version(&version);
    if (err != ESP_OK) return cleanupFailedBegin(err);
#endif
    nativeVersion_.store(version, std::memory_order_release);
    ESP_LOGI(TAG, "Native ESP-NOW version=%lu; SDK v1 max=%u; SDK v2 max=%u",
             (unsigned long)version, (unsigned)HMS_ESPNOW_MAX_PAYLOAD_V1, (unsigned)HMS_ESPNOW_MAX_PAYLOAD_V2);
    const bool nativeV2 = version >= 2;
    const size_t nativeMax = nativeV2 ? HMS_ESPNOW_MAX_PAYLOAD_V2 : HMS_ESPNOW_MAX_PAYLOAD_V1;
    if (config_.payload_profile == HMS_EspNowPayloadProfile::V2 && !nativeV2)
        return cleanupFailedBegin(ESP_ERR_NOT_SUPPORTED);
    effectiveMaxPayload_.store(config_.payload_profile == HMS_EspNowPayloadProfile::V2 ? nativeMax : HMS_ESPNOW_MAX_PAYLOAD_V1,
                               std::memory_order_release);

    err = esp_now_register_recv_cb(&HMS_EspNow::onDataRecv);
    if (err != ESP_OK) return cleanupFailedBegin(err);
    recvRegistered_ = true;
    err = esp_now_register_send_cb(&HMS_EspNow::onDataSent);
    if (err != ESP_OK) return cleanupFailedBegin(err);
    sendRegistered_ = true;

    TaskHandle_t task = nullptr;
    if (xTaskCreate(&HMS_EspNow::workerEntry, "hms_espnow_evt", config_.worker_stack_size, this, config_.worker_priority, &task) != pdPASS)
        return cleanupFailedBegin(ESP_ERR_NO_MEM);
    workerTask_.store(task, std::memory_order_release);
    initialized_ = true;
    ESP_LOGI(TAG, "Started; native ESP-NOW v%lu; profile=%s; effective max=%u; RXQ=%u TXQ=%u",
             (unsigned long)version, config_.payload_profile == HMS_EspNowPayloadProfile::V2 ? "v2" : "v1-compatible",
             (unsigned)maxPayload(), config_.rx_queue_depth, config_.tx_queue_depth);
    return setError(ESP_OK);
}

esp_err_t HMS_EspNow::beginDetailed() {
    std::lock_guard<std::recursive_mutex> lock(apiMutex_);
    return begin() ? ESP_OK : lastError();
}

void HMS_EspNow::requestStop() {
    if (!workerTask_.load(std::memory_order_acquire)) return;
    stopping_.store(true, std::memory_order_release);
    stopRequested_.store(true, std::memory_order_release);
}

void HMS_EspNow::end() {
    if (workerTask_.load(std::memory_order_acquire) && xTaskGetCurrentTaskHandle() == workerTask_.load(std::memory_order_acquire)) {
        requestStop();
        setError(ESP_ERR_INVALID_STATE);
        ESP_LOGE(TAG, "end() called from event worker; requestStop() recorded. Call end() from an application task.");
        return;
    }
    std::lock_guard<std::mutex> endLock(endMutex_);
    std::unique_lock<std::recursive_mutex> lock(apiMutex_);
    if (!initialized_ && !nowInitialized_ && !ownsWifiInit_ && !ownsWifiStart_) return;

    stopping_.store(true, std::memory_order_release);
    if (recvRegistered_) { (void)esp_now_unregister_recv_cb(); recvRegistered_ = false; }
    if (sendRegistered_) { (void)esp_now_unregister_send_cb(); sendRegistered_ = false; }
    {
        std::lock_guard<std::mutex> g(g_instanceMutex);
        if (g_instance == this) g_instance = nullptr;
    }
    while (g_callbacksInFlight.load(std::memory_order_acquire) != 0) vTaskDelay(1);
    if (nowInitialized_) {
        esp_err_t e = esp_now_deinit();
        if (e != ESP_OK && e != ESP_ERR_ESPNOW_NOT_INIT) ESP_LOGW(TAG, "esp_now_deinit: %s", esp_err_to_name(e));
        nowInitialized_ = false;
    }

    stopRequested_.store(true, std::memory_order_release);
    initialized_ = false;
    // Do not block on a full RX queue. Worker polls with a bounded 20 ms wait.
    // Release API mutex while joining: an in-flight user callback may call an API method,
    // which will now fail fast because stopping_ is set instead of deadlocking teardown.
    lock.unlock();
    if (workerTask_.load(std::memory_order_acquire) && workerStopped_)
        (void)xSemaphoreTake(workerStopped_, portMAX_DELAY);
    lock.lock();
    workerTask_.store(nullptr, std::memory_order_release);

    if (rxQueue_) { vQueueDelete(rxQueue_); rxQueue_ = nullptr; }
    if (txQueue_) { vQueueDelete(txQueue_); txQueue_ = nullptr; }
    if (workerStopped_) { vSemaphoreDelete(workerStopped_); workerStopped_ = nullptr; }
    releaseOwnedWifi();
    nativeVersion_ = 0;
    effectiveMaxPayload_ = 0;
    stopRequested_ = false;
    stopping_ = false;
    setError(ESP_OK);
}

void HMS_EspNow::releaseOwnedWifi() {
    if (ownsWifiStart_) { esp_err_t e = esp_wifi_stop(); if (e != ESP_OK && e != ESP_ERR_WIFI_NOT_INIT) ESP_LOGW(TAG, "esp_wifi_stop: %s", esp_err_to_name(e)); ownsWifiStart_ = false; }
    if (ownsWifiInit_) { esp_err_t e = esp_wifi_deinit(); if (e != ESP_OK && e != ESP_ERR_WIFI_NOT_INIT) ESP_LOGW(TAG, "esp_wifi_deinit: %s", esp_err_to_name(e)); ownsWifiInit_ = false; }
}

bool HMS_EspNow::setPrimaryMasterKey(const uint8_t* pmk) {
    std::lock_guard<std::recursive_mutex> lock(apiMutex_);
    if (!initialized_ || stopping_.load() || !pmk) return setError(ESP_ERR_INVALID_ARG);
    return setError(esp_now_set_pmk(pmk));
}

bool HMS_EspNow::addPeer(const uint8_t* mac, bool encrypt, const uint8_t* lmk, uint8_t channel) {
    std::lock_guard<std::recursive_mutex> lock(apiMutex_);
    if (!initialized_ || stopping_.load() || !validMac(mac, true) || (encrypt && !lmk) || channel > 14) return setError(ESP_ERR_INVALID_ARG);
    bool isBroadcast = true; for (size_t i = 0; i < 6; ++i) isBroadcast &= mac[i] == 0xFF;
    if (isBroadcast && encrypt) return setError(ESP_ERR_INVALID_ARG);
    esp_now_peer_info_t peer{};
    esp_err_t lookup = esp_now_get_peer(mac, &peer);
    if (lookup == ESP_OK) {
        if (peer.encrypt != encrypt || peer.channel != channel || peer.ifidx != config_.interface ||
            (encrypt && std::memcmp(peer.lmk, lmk, ESP_NOW_KEY_LEN) != 0)) return setError(ESP_ERR_INVALID_STATE);
        return setError(ESP_OK);
    }
    if (lookup != ESP_ERR_ESPNOW_NOT_FOUND) return setError(lookup);
    std::memcpy(peer.peer_addr, mac, 6); peer.channel = channel; peer.ifidx = config_.interface; peer.encrypt = encrypt;
    if (encrypt) std::memcpy(peer.lmk, lmk, ESP_NOW_KEY_LEN);
    return setError(esp_now_add_peer(&peer));
}
esp_err_t HMS_EspNow::addPeerDetailed(const uint8_t* mac, bool encrypt, const uint8_t* lmk, uint8_t channel) {
    std::lock_guard<std::recursive_mutex> lock(apiMutex_);
    return addPeer(mac, encrypt, lmk, channel) ? ESP_OK : lastError();
}

bool HMS_EspNow::removePeer(const uint8_t* mac) {
    std::lock_guard<std::recursive_mutex> lock(apiMutex_);
    if (!initialized_ || stopping_.load() || !validMac(mac, true)) return setError(ESP_ERR_INVALID_ARG);
    return setError(esp_now_del_peer(mac));
}
esp_err_t HMS_EspNow::removePeerDetailed(const uint8_t* mac) {
    std::lock_guard<std::recursive_mutex> lock(apiMutex_);
    return removePeer(mac) ? ESP_OK : lastError();
}
bool HMS_EspNow::peerExists(const uint8_t* mac) const {
    std::lock_guard<std::recursive_mutex> lock(apiMutex_);
    return initialized_ && !stopping_.load() && validMac(mac, true) && esp_now_is_peer_exist(mac);
}

bool HMS_EspNow::send(const uint8_t* mac, const uint8_t* data, size_t len) {
    std::lock_guard<std::recursive_mutex> lock(apiMutex_);
    if (!initialized_ || stopping_.load() || !validMac(mac, false) || !data || len == 0 || len > configuredMaxPayload()) {
        setError(ESP_ERR_INVALID_ARG); ++txRequestsRejected_; return false;
    }
    if (!esp_now_is_peer_exist(mac)) {
        if (config_.peer_policy != HMS_EspNowPeerPolicy::AUTO_ADD_UNENCRYPTED) {
            setError(ESP_ERR_ESPNOW_NOT_FOUND); ++txRequestsRejected_; return false;
        }
        esp_now_peer_info_t peer{}; std::memcpy(peer.peer_addr, mac, 6); peer.channel = 0; peer.ifidx = config_.interface; peer.encrypt = false;
        esp_err_t addErr = esp_now_add_peer(&peer);
        if (addErr != ESP_OK) { setError(addErr); ++txRequestsRejected_; return false; }
    }
    esp_err_t err = esp_now_send(mac, data, len);
    if (err == ESP_OK) ++txRequestsAccepted_; else ++txRequestsRejected_;
    return setError(err);
}
esp_err_t HMS_EspNow::sendDetailed(const uint8_t* mac, const uint8_t* data, size_t len) {
    std::lock_guard<std::recursive_mutex> lock(apiMutex_);
    return send(mac, data, len) ? ESP_OK : lastError();
}

bool HMS_EspNow::broadcast(const uint8_t* data, size_t len) {
    std::lock_guard<std::recursive_mutex> lock(apiMutex_);
    if (!initialized_ || stopping_.load() || !config_.allow_broadcast || !data || len == 0 || len > configuredMaxPayload()) {
        setError(ESP_ERR_INVALID_ARG); ++txRequestsRejected_; return false;
    }
    esp_now_peer_info_t existing{};
    esp_err_t lookup = esp_now_get_peer(BROADCAST_MAC, &existing);
    if (lookup == ESP_OK) {
        if (existing.encrypt || existing.ifidx != config_.interface) { setError(ESP_ERR_INVALID_STATE); ++txRequestsRejected_; return false; }
    } else if (lookup == ESP_ERR_ESPNOW_NOT_FOUND) {
        esp_now_peer_info_t peer{}; std::memcpy(peer.peer_addr, BROADCAST_MAC, 6); peer.channel = 0; peer.ifidx = config_.interface; peer.encrypt = false;
        esp_err_t e = esp_now_add_peer(&peer); if (e != ESP_OK) { setError(e); ++txRequestsRejected_; return false; }
    } else { setError(lookup); ++txRequestsRejected_; return false; }
    esp_err_t err = esp_now_send(BROADCAST_MAC, data, len);
    if (err == ESP_OK) ++txRequestsAccepted_; else ++txRequestsRejected_;
    return setError(err);
}
esp_err_t HMS_EspNow::broadcastDetailed(const uint8_t* data, size_t len) {
    std::lock_guard<std::recursive_mutex> lock(apiMutex_);
    return broadcast(data, len) ? ESP_OK : lastError();
}

void HMS_EspNow::enqueueRx(const RxEvent& event) {
    if (stopping_.load(std::memory_order_acquire)) { ++rxDroppedStopping_; return; }
    if (!rxQueue_ || xQueueSend(rxQueue_, &event, 0) != pdTRUE) { ++rxDroppedQueueFull_; return; }
}
void HMS_EspNow::enqueueTx(const TxEvent& event) {
    if (stopping_.load(std::memory_order_acquire)) { ++txDroppedStopping_; return; }
    if (!txQueue_ || xQueueSend(txQueue_, &event, 0) != pdTRUE) { ++txDroppedQueueFull_; return; }
}

void HMS_EspNow::onDataRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
    HMS_EspNow* self = instanceSnapshot(); if (!self) return;
    if (!self->stopping_.load(std::memory_order_acquire)) {
        if (info && info->src_addr && data && len > 0 && static_cast<size_t>(len) <= self->configuredMaxPayload()) {
            RxEvent event{}; std::memcpy(event.mac, info->src_addr, 6);
            event.rssi = info->rx_ctrl ? info->rx_ctrl->rssi : 0;
            event.len = static_cast<uint16_t>(len); std::memcpy(event.data, data, static_cast<size_t>(len));
            ++self->rxReceived_; self->enqueueRx(event);
        } else ++self->rxDroppedInvalid_;
    } else ++self->rxDroppedStopping_;
    g_callbacksInFlight.fetch_sub(1, std::memory_order_acq_rel);
}

#if defined(ESP_IDF_VERSION) && ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)
void HMS_EspNow::onDataSent(const esp_now_send_info_t* info, esp_now_send_status_t status) {
    const uint8_t* mac = (info != nullptr) ? info->des_addr : nullptr;
#else
void HMS_EspNow::onDataSent(const uint8_t* mac, esp_now_send_status_t status) {
#endif
    HMS_EspNow* self = instanceSnapshot(); if (!self) return;
    if (!self->stopping_.load(std::memory_order_acquire) && mac) {
        TxEvent event{}; std::memcpy(event.mac, mac, 6); event.success = status == ESP_NOW_SEND_SUCCESS;
        ++self->txCallbacks_;
        if (event.success) ++self->txSuccess_; else ++self->txFailures_;
        self->enqueueTx(event);
    } else if (self->stopping_.load(std::memory_order_acquire)) ++self->txDroppedStopping_;
    g_callbacksInFlight.fetch_sub(1, std::memory_order_acq_rel);
}

void HMS_EspNow::workerEntry(void* arg) {
    auto* self = static_cast<HMS_EspNow*>(arg);
    while (self->workerTask_.load(std::memory_order_acquire) != xTaskGetCurrentTaskHandle()) vTaskDelay(1);
    self->workerLoop();
    vTaskDelete(nullptr);
}
void HMS_EspNow::workerLoop() {
    for (;;) {
        if (stopRequested_.load(std::memory_order_acquire)) break;
        RxEvent rx{};
        TickType_t receiveWait = pdMS_TO_TICKS(20);
        if (receiveWait == 0) receiveWait = 1;
        if (rxQueue_ && xQueueReceive(rxQueue_, &rx, receiveWait) == pdTRUE) {
            if (!stopRequested_.load() && !stopping_.load()) { dispatchReceive(rx.mac, rx.data, rx.len, rx.rssi); ++rxDelivered_; }
        }
        // Bound TX batch work so a continuously full TX queue cannot starve RX.
        TxEvent tx{};
        for (unsigned n = 0; n < 8 && txQueue_ && xQueueReceive(txQueue_, &tx, 0) == pdTRUE; ++n) {
            if (!stopRequested_.load() && !stopping_.load()) dispatchSend(tx.mac, tx.success);
        }
    }
    if (workerStopped_) xSemaphoreGive(workerStopped_);
}

HMS_EspNowStats HMS_EspNow::stats() const {
    HMS_EspNowStats s{};
    s.rx_received = rxReceived_.load(); s.rx_delivered = rxDelivered_.load();
    s.rx_dropped_queue_full = rxDroppedQueueFull_.load(); s.rx_dropped_stopping = rxDroppedStopping_.load();
    s.rx_dropped_invalid = rxDroppedInvalid_.load(); s.tx_callbacks = txCallbacks_.load();
    s.tx_success = txSuccess_.load(); s.tx_failures = txFailures_.load();
    s.tx_dropped_queue_full = txDroppedQueueFull_.load(); s.tx_dropped_stopping = txDroppedStopping_.load();
    s.tx_requests_accepted = txRequestsAccepted_.load(); s.tx_requests_rejected = txRequestsRejected_.load();
    return s;
}
void HMS_EspNow::resetStats() {
    rxReceived_ = 0; rxDelivered_ = 0; rxDroppedQueueFull_ = 0; rxDroppedStopping_ = 0; rxDroppedInvalid_ = 0;
    txCallbacks_ = 0; txSuccess_ = 0; txFailures_ = 0; txDroppedQueueFull_ = 0; txDroppedStopping_ = 0; txRequestsAccepted_ = 0; txRequestsRejected_ = 0;
}
