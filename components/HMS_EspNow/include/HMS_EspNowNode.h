#pragma once

#include "HMS_EspNow.h"
#include <array>
#include <cstddef>
#include <functional>
#include <vector>

/** Application intent. The component translates these modes into ESP-NOW peer/send behavior. */
enum class HMS_EspNowRole : uint8_t { RX_ONLY, TX_ONLY, BIDIRECTIONAL };
enum class HMS_EspNowTopology : uint8_t { ONE_WAY, TWO_WAY, ONE_TO_MANY, MANY_TO_ONE, MANY_TO_MANY };
enum class HMS_EspNowAddressing : uint8_t { UNICAST, BROADCAST };

/** Node-level configuration. Peer addresses are copied by the constructor. */
struct HMS_EspNowNodeConfig {
    HMS_EspNowRole role = HMS_EspNowRole::RX_ONLY;
    HMS_EspNowTopology topology = HMS_EspNowTopology::ONE_WAY;
    HMS_EspNowAddressing addressing = HMS_EspNowAddressing::UNICAST;
    const uint8_t (*peers)[6] = nullptr; // Other nodes; sender list on RX nodes, destination list on TX nodes.
    size_t peer_count = 0;
    bool encrypt_unicast = false;
    const uint8_t* pmk = nullptr; // 16 bytes; copied during construction.
    const uint8_t* lmk = nullptr; // 16 bytes; shared by this node and each configured encrypted peer.
    bool filter_unicast_receivers = true;
    bool filter_broadcast_receivers = false; // Optional source allow-list for broadcast RX.
    HMS_EspNowConfig transport{};
};

/**
 * Topology-aware ESP-NOW node facade. It owns a transport instance and manages local
 * interface identity, peer registration, topology validation, unicast fan-out and
 * receive filtering. It does not implement multi-hop routing or mesh forwarding.
 *
 * sendToPeers() means one unicast submission per configured peer, or one broadcast
 * submission in BROADCAST mode. Return value indicates local API acceptance only.
 * The send callback reports ESP-NOW MAC-layer status, not application execution.
 */
class HMS_EspNowNode final {
public:
    using ReceiveCallback = HMS_Interface::ReceiveCallback;
    using SendCallback = HMS_Interface::SendCallback;

    explicit HMS_EspNowNode(const HMS_EspNowNodeConfig& config);
    ~HMS_EspNowNode();
    HMS_EspNowNode(const HMS_EspNowNode&) = delete;
    HMS_EspNowNode& operator=(const HMS_EspNowNode&) = delete;

    esp_err_t begin();
    void requestStop(); // Safe from a user callback; call end() later from task context.
    void end();         // Do not call from a receive/send callback.

    esp_err_t sendToPeers(const uint8_t* data, size_t len);
    esp_err_t sendToPeer(const uint8_t* peer, const uint8_t* data, size_t len);
    bool isTransmitter() const noexcept;
    bool isReceiver() const noexcept;
    bool isStarted() const noexcept;
    const uint8_t* localMac() const noexcept; // Valid after begin() succeeds.
    size_t peerCount() const noexcept;
    HMS_EspNowStats stats() const;
    HMS_EspNowCapabilities capabilities() const noexcept;
    esp_err_t lastError() const noexcept;
    void setReceiveCallback(ReceiveCallback callback);
    void setSendCallback(SendCallback callback);

private:
    bool validate() const;
    bool peerConfigured(const uint8_t* mac) const;
    bool isBroadcastMode() const noexcept;
    void installTransportCallbacks();
    void handleReceive(const uint8_t* mac, const uint8_t* data, size_t len, int8_t rssi);
    void handleSend(const uint8_t* mac, bool success);

    HMS_EspNowNodeConfig config_;
    std::vector<std::array<uint8_t, 6>> peers_;
    std::array<uint8_t, 16> pmk_{};
    std::array<uint8_t, 16> lmk_{};
    bool hasPmk_ = false;
    bool hasLmk_ = false;
    std::array<uint8_t, 6> localMac_{};
    HMS_EspNow transport_;
    mutable std::mutex callbackMutex_;
    ReceiveCallback receiveCallback_;
    SendCallback sendCallback_;
    std::atomic<bool> started_{false};
};
