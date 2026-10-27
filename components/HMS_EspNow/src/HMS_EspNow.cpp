#include "HMS_EspNow.h"

#include <cstring>
#include "esp_log.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

namespace {
constexpr char TAG[] = "HMS_EspNow";
constexpr uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
}

HMS_EspNow* HMS_EspNow::_instance = nullptr;

HMS_EspNow::HMS_EspNow(const HMS_EspNowConfig& config) : _config(config) {}

HMS_EspNow::~HMS_EspNow() {
    end();
}

bool HMS_EspNow::setError(esp_err_t err) {
    _lastError = err;
    if (err != ESP_OK) ESP_LOGE(TAG, "%s", esp_err_to_name(err));
    return err == ESP_OK;
}

bool HMS_EspNow::validMac(const uint8_t* mac) const {
    if (!mac) return false;
    bool allZero = true;
    for (size_t i = 0; i < 6; ++i) allZero = allZero && (mac[i] == 0);
    return !allZero;
}

bool HMS_EspNow::prepareWifi() {
    wifi_mode_t currentMode;
    esp_err_t err = esp_wifi_get_mode(&currentMode);
    if (err == ESP_ERR_WIFI_NOT_INIT) {
        if (!_config.start_wifi_if_needed) return setError(err);
        wifi_init_config_t wifiConfig = WIFI_INIT_CONFIG_DEFAULT();
        err = esp_wifi_init(&wifiConfig);
        if (err != ESP_OK) return setError(err);
        _ownsWifiInit = true;

        err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
        if (err != ESP_OK) return setError(err);
        err = esp_wifi_set_mode(_config.wifi_mode);
        if (err != ESP_OK) return setError(err);
        err = esp_wifi_start();
        if (err != ESP_OK) return setError(err);
        _ownsWifiStart = true;
    } else if (err != ESP_OK) {
        return setError(err);
    } else {
        ESP_LOGI(TAG, "Using existing Wi-Fi subsystem (mode=%d)", (int)currentMode);
        if (currentMode != WIFI_MODE_STA && currentMode != WIFI_MODE_APSTA) {
            ESP_LOGE(TAG, "Existing Wi-Fi mode has no station interface; ESP-NOW peer interface is STA");
            return setError(ESP_ERR_INVALID_STATE);
        }
    }

    if (_config.channel != 0) {
        uint8_t primary = 0;
        wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
        err = esp_wifi_get_channel(&primary, &second);
        if (err == ESP_OK && primary == _config.channel) return true;

        // Only change channel when this driver started/owns the Wi-Fi interface.
        // Existing Wi-Fi owners may be connected to an AP; never disrupt them.
        if (!_ownsWifiStart) {
            if (err != ESP_OK) return setError(err);
            ESP_LOGW(TAG, "Existing Wi-Fi owner controls channel %u; requested channel %u not forced", primary, _config.channel);
            return true;
        }
        err = esp_wifi_set_channel(_config.channel, WIFI_SECOND_CHAN_NONE);
        if (err != ESP_OK) return setError(err);
    }
    return true;
}

bool HMS_EspNow::begin() {
    if (_isInitialized) return true;
    if (_instance && _instance != this) {
        ESP_LOGE(TAG, "Only one HMS_EspNow instance may be active at a time");
        return setError(ESP_ERR_INVALID_STATE);
    }
    if (_config.event_queue_depth == 0 || _config.worker_stack_size < 2048 ||
        (_config.wifi_mode != WIFI_MODE_STA && _config.wifi_mode != WIFI_MODE_APSTA)) {
        return setError(ESP_ERR_INVALID_ARG);
    }
    _stopping = false;
    _lastError = ESP_OK;

    if (!prepareWifi()) {
        end();
        return false;
    }

    QueueHandle_t queue = xQueueCreate(_config.event_queue_depth, sizeof(Event));
    if (!queue) {
        setError(ESP_ERR_NO_MEM);
        end();
        return false;
    }
    _eventQueue = queue;
    SemaphoreHandle_t stopped = xSemaphoreCreateBinary();
    if (!stopped) {
        setError(ESP_ERR_NO_MEM);
        end();
        return false;
    }
    _workerStopped = stopped;

    esp_err_t err = esp_now_init();
    if (err != ESP_OK) {
        setError(err);
        end();
        return false;
    }

    _instance = this;
    err = esp_now_register_recv_cb(&HMS_EspNow::onDataRecv);
    if (err != ESP_OK) {
        setError(err);
        end();
        return false;
    }
    _recvRegistered = true;
    err = esp_now_register_send_cb(&HMS_EspNow::onDataSent);
    if (err != ESP_OK) {
        setError(err);
        end();
        return false;
    }
    _sendRegistered = true;

    _isInitialized = true;
    TaskHandle_t workerTask = nullptr;
    BaseType_t taskResult = xTaskCreate(&HMS_EspNow::workerEntry, "hms_espnow_evt",
                                        _config.worker_stack_size, this,
                                        _config.worker_priority, &workerTask);
    if (taskResult != pdPASS) {
        setError(ESP_ERR_NO_MEM);
        _isInitialized = false;
        end();
        return false;
    }
    _workerTask = workerTask;
    ESP_LOGI(TAG, "ESP-NOW initialized (max payload=%u bytes)", (unsigned)HMS_ESPNOW_MAX_PAYLOAD);
    return true;
}

void HMS_EspNow::end() {
    if (!_isInitialized && !_eventQueue && !_workerStopped && !_ownsWifiInit && !_ownsWifiStart && !_recvRegistered && !_sendRegistered) {
        if (_instance == this) _instance = nullptr;
        return;
    }

    _stopping = true;
    if (_recvRegistered) {
        esp_now_unregister_recv_cb();
        _recvRegistered = false;
    }
    if (_sendRegistered) {
        esp_now_unregister_send_cb();
        _sendRegistered = false;
    }
    if (_isInitialized || _instance == this) {
        esp_err_t err = esp_now_deinit();
        if (err != ESP_OK && err != ESP_ERR_ESPNOW_NOT_INIT) ESP_LOGW(TAG, "esp_now_deinit: %s", esp_err_to_name(err));
    }
    _isInitialized = false;

    QueueHandle_t queue = static_cast<QueueHandle_t>(_eventQueue);
    SemaphoreHandle_t stopped = static_cast<SemaphoreHandle_t>(_workerStopped);
    if (_workerTask && queue && stopped) {
        Event event{};
        event.type = EventType::STOP;
        if (xQueueSend(queue, &event, portMAX_DELAY) == pdTRUE) {
            xSemaphoreTake(stopped, portMAX_DELAY);
        }
    }
    _workerTask = nullptr;
    if (_eventQueue) { vQueueDelete(static_cast<QueueHandle_t>(_eventQueue)); _eventQueue = nullptr; }
    if (_workerStopped) { vSemaphoreDelete(static_cast<SemaphoreHandle_t>(_workerStopped)); _workerStopped = nullptr; }
    if (_instance == this) _instance = nullptr;

    if (_ownsWifiStart) {
        esp_err_t err = esp_wifi_stop();
        if (err != ESP_OK) ESP_LOGW(TAG, "esp_wifi_stop: %s", esp_err_to_name(err));
        _ownsWifiStart = false;
    }
    if (_ownsWifiInit) {
        esp_err_t err = esp_wifi_deinit();
        if (err != ESP_OK) ESP_LOGW(TAG, "esp_wifi_deinit: %s", esp_err_to_name(err));
        _ownsWifiInit = false;
    }
}

bool HMS_EspNow::setPrimaryMasterKey(const uint8_t* pmk) {
    if (!_isInitialized || !pmk) return setError(ESP_ERR_INVALID_ARG);
    return setError(esp_now_set_pmk(pmk));
}

bool HMS_EspNow::addPeer(const uint8_t* peer_addr, bool encrypt, const uint8_t* lmk, uint8_t channel) {
    if (!_isInitialized || !validMac(peer_addr)) return setError(ESP_ERR_INVALID_ARG);
    if (encrypt && !lmk) return setError(ESP_ERR_INVALID_ARG);
    if (esp_now_is_peer_exist(peer_addr)) return true;

    esp_now_peer_info_t peer{};
    std::memcpy(peer.peer_addr, peer_addr, 6);
    peer.channel = channel; // 0 means current channel
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = encrypt;
    if (encrypt) std::memcpy(peer.lmk, lmk, ESP_NOW_KEY_LEN);
    return setError(esp_now_add_peer(&peer));
}

bool HMS_EspNow::removePeer(const uint8_t* peer_addr) {
    if (!_isInitialized || !validMac(peer_addr)) return setError(ESP_ERR_INVALID_ARG);
    if (!esp_now_is_peer_exist(peer_addr)) return setError(ESP_ERR_ESPNOW_NOT_FOUND);
    return setError(esp_now_del_peer(peer_addr));
}

bool HMS_EspNow::peerExists(const uint8_t* peer_addr) const {
    return _isInitialized && validMac(peer_addr) && esp_now_is_peer_exist(peer_addr);
}

bool HMS_EspNow::send(const uint8_t* peer_addr, const uint8_t* data, size_t len) {
    if (!_isInitialized || !validMac(peer_addr) || !data || len == 0 || len > HMS_ESPNOW_MAX_PAYLOAD) {
        return setError(ESP_ERR_INVALID_ARG);
    }
    if (!peerExists(peer_addr) && !addPeer(peer_addr)) return false;
    return setError(esp_now_send(peer_addr, data, len));
}

bool HMS_EspNow::broadcast(const uint8_t* data, size_t len) {
    if (!_isInitialized || !data || len == 0 || len > HMS_ESPNOW_MAX_PAYLOAD) {
        return setError(ESP_ERR_INVALID_ARG);
    }
    if (!peerExists(BROADCAST_MAC) && !addPeer(BROADCAST_MAC)) return false;
    return send(BROADCAST_MAC, data, len);
}

bool HMS_EspNow::enqueueEvent(const Event& event) {
    if (_stopping || !_eventQueue) return false;
    if (xQueueSend(static_cast<QueueHandle_t>(_eventQueue), &event, 0) != pdTRUE) {
        ESP_LOGW(TAG, "ESP-NOW event queue full; dropping event type=%u", (unsigned)event.type);
        return false;
    }
    return true;
}

void HMS_EspNow::onDataRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
    HMS_EspNow* self = _instance;
    if (!self || self->_stopping || !info || !info->src_addr || !data || len <= 0 ||
        len > static_cast<int>(HMS_ESPNOW_MAX_PAYLOAD)) return;
    Event event{};
    event.type = EventType::RX;
    std::memcpy(event.mac, info->src_addr, 6);
    event.rssi = (info->rx_ctrl) ? info->rx_ctrl->rssi : 0;
    event.len = static_cast<uint16_t>(len);
    std::memcpy(event.data, data, static_cast<size_t>(len));
    self->enqueueEvent(event);
}

void HMS_EspNow::onDataSent(const wifi_tx_info_t* info, esp_now_send_status_t status) {
    HMS_EspNow* self = _instance;
    if (!self || self->_stopping || !info || !info->des_addr) return;
    Event event{};
    event.type = EventType::TX;
    std::memcpy(event.mac, info->des_addr, 6);
    event.success = (status == ESP_NOW_SEND_SUCCESS);
    self->enqueueEvent(event);
}

void HMS_EspNow::workerEntry(void* arg) {
    auto* self = static_cast<HMS_EspNow*>(arg);
    self->workerLoop();
    vTaskDelete(nullptr);
}

void HMS_EspNow::workerLoop() {
    QueueHandle_t queue = static_cast<QueueHandle_t>(_eventQueue);
    for (;;) {
        Event event{};
        if (xQueueReceive(queue, &event, portMAX_DELAY) != pdTRUE) continue;
        if (event.type == EventType::STOP) break;
        if (_stopping) continue;
        if (event.type == EventType::RX && onReceive) {
            onReceive(event.mac, event.data, event.len, event.rssi);
        } else if (event.type == EventType::TX && onSend) {
            onSend(event.mac, event.success);
        }
    }
    if (_workerStopped) xSemaphoreGive(static_cast<SemaphoreHandle_t>(_workerStopped));
}
