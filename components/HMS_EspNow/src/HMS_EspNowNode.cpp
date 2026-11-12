#include "HMS_EspNowNode.h"
#include <cstring>
#include <utility>
#include "esp_mac.h"
#include "esp_log.h"

namespace {
constexpr char TAG[] = "HMS_EspNowNode";
bool validUnicastMac(const uint8_t* mac) {
    if (!mac) return false;
    bool zero = true, ff = true;
    for (size_t i=0;i<6;++i) { zero &= mac[i] == 0; ff &= mac[i] == 0xFF; }
    return !zero && !ff && (mac[0] & 1U) == 0;
}
bool sameMac(const uint8_t* a, const uint8_t* b) { return std::memcmp(a,b,6) == 0; }
}

HMS_EspNowNode::HMS_EspNowNode(const HMS_EspNowNodeConfig& config)
    : config_(config), transport_(config.transport) {
    if (config.peers && config.peer_count) {
        peers_.reserve(config.peer_count);
        for (size_t i=0;i<config.peer_count;++i) {
            std::array<uint8_t,6> mac{};
            std::memcpy(mac.data(), config.peers[i], 6);
            peers_.push_back(mac);
        }
    }
    if (config.pmk) { std::memcpy(pmk_.data(), config.pmk, pmk_.size()); hasPmk_ = true; }
    if (config.lmk) { std::memcpy(lmk_.data(), config.lmk, lmk_.size()); hasLmk_ = true; }
}
HMS_EspNowNode::~HMS_EspNowNode() {
    end();
    volatile uint8_t* pmk=pmk_.data();
    volatile uint8_t* lmk=lmk_.data();
    for (size_t i=0;i<pmk_.size();++i) { pmk[i]=0; lmk[i]=0; }
}

bool HMS_EspNowNode::isTransmitter() const noexcept { return config_.role != HMS_EspNowRole::RX_ONLY; }
bool HMS_EspNowNode::isReceiver() const noexcept { return config_.role != HMS_EspNowRole::TX_ONLY; }
bool HMS_EspNowNode::isStarted() const noexcept { return started_.load(std::memory_order_acquire); }
const uint8_t* HMS_EspNowNode::localMac() const noexcept { return localMac_.data(); }
size_t HMS_EspNowNode::peerCount() const noexcept { return peers_.size(); }
HMS_EspNowStats HMS_EspNowNode::stats() const { return transport_.stats(); }
HMS_EspNowCapabilities HMS_EspNowNode::capabilities() const noexcept { return transport_.capabilities(); }
esp_err_t HMS_EspNowNode::lastError() const noexcept { return transport_.lastError(); }
bool HMS_EspNowNode::isBroadcastMode() const noexcept { return config_.addressing == HMS_EspNowAddressing::BROADCAST; }

bool HMS_EspNowNode::validate() const {
    if (config_.role > HMS_EspNowRole::BIDIRECTIONAL || config_.topology > HMS_EspNowTopology::MANY_TO_MANY ||
        config_.addressing > HMS_EspNowAddressing::BROADCAST) return false;
    if (config_.peer_count != peers_.size() || (config_.peer_count && !config_.peers)) return false;
    if (config_.encrypt_unicast && (isBroadcastMode() || !hasPmk_ || !hasLmk_)) return false;
    if (isBroadcastMode() && !config_.transport.allow_broadcast) return false;
    for (size_t i=0;i<peers_.size();++i) {
        if (!validUnicastMac(peers_[i].data())) return false;
        for (size_t j=i+1;j<peers_.size();++j) if (sameMac(peers_[i].data(),peers_[j].data())) return false;
    }
    const size_t n=peers_.size();
#ifdef ESP_NOW_MAX_TOTAL_PEER_NUM
    if (n > ESP_NOW_MAX_TOTAL_PEER_NUM) return false;
#endif
#ifdef ESP_NOW_MAX_ENCRYPT_PEER_NUM
    if (config_.encrypt_unicast && n > ESP_NOW_MAX_ENCRYPT_PEER_NUM) return false;
#endif
    switch (config_.topology) {
        case HMS_EspNowTopology::ONE_WAY:
            if (config_.role == HMS_EspNowRole::BIDIRECTIONAL) return false;
            if (!isBroadcastMode() && n != 1) return false;
            break;
        case HMS_EspNowTopology::TWO_WAY:
            if (config_.role != HMS_EspNowRole::BIDIRECTIONAL) return false;
            if (!isBroadcastMode() && n != 1) return false;
            if (isBroadcastMode() && n > 1) return false;
            break;
        case HMS_EspNowTopology::ONE_TO_MANY:
            if (!isBroadcastMode() && n == 0) return false;
            break;
        case HMS_EspNowTopology::MANY_TO_ONE:
            if (!isBroadcastMode() && n == 0) return false;
            if (config_.role == HMS_EspNowRole::TX_ONLY && !isBroadcastMode() && n != 1) return false;
            if (config_.role == HMS_EspNowRole::RX_ONLY && isBroadcastMode() && config_.filter_broadcast_receivers && n == 0) return false;
            break;
        case HMS_EspNowTopology::MANY_TO_MANY:
            if (!isBroadcastMode() && n == 0) return false;
            break;
    }
    return true;
}

void HMS_EspNowNode::installTransportCallbacks() {
    transport_.setReceiveCallback([this](const uint8_t* mac,const uint8_t* data,size_t len,int8_t rssi) { handleReceive(mac,data,len,rssi); });
    transport_.setSendCallback([this](const uint8_t* mac,bool success) { handleSend(mac,success); });
}

esp_err_t HMS_EspNowNode::begin() {
    if (started_.load(std::memory_order_acquire)) return ESP_OK;
    if (!validate()) {
        ESP_LOGE(TAG, "Invalid node configuration: role=%u topology=%u addressing=%u peer_count=%u",
                 (unsigned)config_.role, (unsigned)config_.topology, (unsigned)config_.addressing,
                 (unsigned)peers_.size());
        return ESP_ERR_INVALID_ARG;
    }
    const esp_mac_type_t macType = config_.transport.interface == WIFI_IF_AP ? ESP_MAC_WIFI_SOFTAP : ESP_MAC_WIFI_STA;
    esp_err_t err = esp_read_mac(localMac_.data(), macType);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read local %s MAC: %s",
                 config_.transport.interface == WIFI_IF_AP ? "AP" : "STA", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "Local %s MAC %02X:%02X:%02X:%02X:%02X:%02X",
             config_.transport.interface == WIFI_IF_AP ? "AP" : "STA",
             localMac_[0], localMac_[1], localMac_[2], localMac_[3], localMac_[4], localMac_[5]);
    for (size_t i = 0; i < peers_.size(); ++i) {
        if (sameMac(peers_[i].data(), localMac_.data())) {
            ESP_LOGE(TAG, "Peer table entry %u is this device's local MAC; list only remote nodes",
                     (unsigned)i);
            return ESP_ERR_INVALID_ARG;
        }
    }
    if (config_.encrypt_unicast && !isBroadcastMode()) {
        err = transport_.beginDetailed();
        if (err != ESP_OK) return err;
        if (!transport_.setPrimaryMasterKey(pmk_.data())) { err=transport_.lastError(); end(); return err; }
    } else {
        err = transport_.beginDetailed();
        if (err != ESP_OK) return err;
    }
    if (!isBroadcastMode()) {
        for (const auto& peer : peers_) {
            if (!transport_.addPeer(peer.data(),config_.encrypt_unicast,config_.encrypt_unicast?lmk_.data():nullptr,config_.transport.channel)) {
                err=transport_.lastError(); end(); return err;
            }
        }
    }
    installTransportCallbacks();
    started_.store(true,std::memory_order_release);
    ESP_LOGI(TAG,"Node started: role=%u topology=%u addressing=%u peers=%u channel=%u payload_max=%u",
             (unsigned)config_.role,(unsigned)config_.topology,(unsigned)config_.addressing,
             (unsigned)peers_.size(),(unsigned)config_.transport.channel,(unsigned)transport_.maxPayload());
    return ESP_OK;
}

void HMS_EspNowNode::requestStop() { transport_.requestStop(); }
void HMS_EspNowNode::end() {
    if (!started_.exchange(false,std::memory_order_acq_rel) && !transport_.isInitialized()) return;
    if (HMS_Interface::inTransportCallbackContext()) { transport_.requestStop(); ESP_LOGE(TAG,"end() called from callback; deferred stop requested, call end() from task context"); return; }
    transport_.clearReceiveCallbackAndWait();
    transport_.clearSendCallbackAndWait();
    transport_.end();
}

bool HMS_EspNowNode::peerConfigured(const uint8_t* mac) const {
    if (!mac) return false;
    for (const auto& peer:peers_) if (sameMac(peer.data(),mac)) return true;
    return false;
}
esp_err_t HMS_EspNowNode::sendToPeer(const uint8_t* peer,const uint8_t* data,size_t len) {
    if (!started_.load(std::memory_order_acquire) || !isTransmitter() || isBroadcastMode() || !peer || !peerConfigured(peer)) return ESP_ERR_INVALID_STATE;
    return transport_.sendDetailed(peer,data,len);
}
esp_err_t HMS_EspNowNode::sendToPeers(const uint8_t* data,size_t len) {
    if (!started_.load(std::memory_order_acquire) || !isTransmitter() || !data || len==0) return ESP_ERR_INVALID_STATE;
    if (isBroadcastMode()) return transport_.broadcastDetailed(data,len);
    esp_err_t result=ESP_OK;
    for (const auto& peer:peers_) {
        esp_err_t err=transport_.sendDetailed(peer.data(),data,len);
        if (err!=ESP_OK && result==ESP_OK) result=err;
        if (peers_.size()>1) vTaskDelay(pdMS_TO_TICKS(5));
    }
    return result;
}

void HMS_EspNowNode::handleReceive(const uint8_t* mac,const uint8_t* data,size_t len,int8_t rssi) {
    if (!isReceiver() || !mac || !data) return;
    if (isBroadcastMode() && config_.filter_broadcast_receivers && !peerConfigured(mac)) return;
    if (!isBroadcastMode() && config_.filter_unicast_receivers && !peerConfigured(mac)) return;
    ReceiveCallback cb;
    { std::lock_guard<std::mutex> lock(callbackMutex_); cb=receiveCallback_; }
    if (cb) cb(mac,data,len,rssi);
}
void HMS_EspNowNode::handleSend(const uint8_t* mac,bool success) {
    SendCallback cb;
    { std::lock_guard<std::mutex> lock(callbackMutex_); cb=sendCallback_; }
    if (cb) cb(mac,success);
}
void HMS_EspNowNode::setReceiveCallback(ReceiveCallback cb) { std::lock_guard<std::mutex> lock(callbackMutex_); receiveCallback_=std::move(cb); }
void HMS_EspNowNode::setSendCallback(SendCallback cb) { std::lock_guard<std::mutex> lock(callbackMutex_); sendCallback_=std::move(cb); }
