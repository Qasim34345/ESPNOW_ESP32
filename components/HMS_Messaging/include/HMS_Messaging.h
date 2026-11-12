#pragma once
#include "HMS_Interface.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

enum class HMS_ReliableResult : uint8_t { OK, INVALID_ARGUMENT, TX_REJECTED, TIMEOUT, NOT_STARTED, WOULD_BLOCK_IN_CALLBACK };

/** Optional application protocol layer. Owns the transport receive callback while alive.
 * Wire format v2: magic[2], version[1], flags[1], random session LE[8], sequence LE[4],
 * payload length LE[2], payload. All peers must use the same protocol version.
 * Reliable unicast only; ACK means well-formed frame receipt, not application execution.
 * It is not a mesh or replay defense. Duplicate history is volatile. Destroy outside callbacks.
 */
class HMS_Messaging {
public:
    using MessageCallback = std::function<void(const uint8_t* peer, const uint8_t* payload, size_t len)>;
    explicit HMS_Messaging(HMS_Interface& transport);
    ~HMS_Messaging();
    HMS_Messaging(const HMS_Messaging&) = delete;
    HMS_Messaging& operator=(const HMS_Messaging&) = delete;

    void setMessageCallback(MessageCallback callback);
    // Blocking reliable sends are rejected from transport callbacks to avoid starving ACK reception.
    HMS_ReliableResult sendReliable(const uint8_t* peer, const uint8_t* payload, size_t len,
                                    uint32_t ackTimeoutMs = 300, uint8_t retries = 3);
    size_t maxApplicationPayload() const noexcept;

private:
    static constexpr uint8_t MAGIC0 = 0x48, MAGIC1 = 0x4D, VERSION = 2, FLAG_ACK = 0x01;
    static constexpr size_t HEADER_SIZE = 18, PEER_CACHE_COUNT = 8, SEQ_CACHE_COUNT = 8;
    struct PeerCache {
        bool used=false;
        uint8_t mac[6]{};
        uint64_t session[SEQ_CACHE_COUNT]{};
        uint32_t seq[SEQ_CACHE_COUNT]{};
        uint8_t next=0;
    };
    void onReceive(const uint8_t* mac, const uint8_t* data, size_t len, int8_t rssi);
    bool isDuplicate(const uint8_t* mac, uint64_t session, uint32_t sequence);
    static bool isValidUnicast(const uint8_t* mac);
    static uint64_t readU64(const uint8_t* p);
    static uint32_t readU32(const uint8_t* p);
    static uint16_t readU16(const uint8_t* p);
    static void writeU64(uint8_t* p, uint64_t v);
    static void writeU32(uint8_t* p, uint32_t v);
    static void writeU16(uint8_t* p, uint16_t v);
    void sendAck(const uint8_t* mac, uint64_t session, uint32_t sequence);

    HMS_Interface& transport_;
    mutable std::mutex callbackMutex_;
    MessageCallback messageCallback_;
    std::mutex sendMutex_;
    std::mutex stateMutex_;
    SemaphoreHandle_t ackSemaphore_ = nullptr;
    bool pending_ = false, ackReceived_ = false;
    uint8_t pendingPeer_[6]{};
    uint64_t sessionId_ = 0, pendingSession_ = 0;
    uint32_t pendingSequence_ = 0, nextSequence_ = 1;
    std::array<PeerCache, PEER_CACHE_COUNT> peers_{};
};
