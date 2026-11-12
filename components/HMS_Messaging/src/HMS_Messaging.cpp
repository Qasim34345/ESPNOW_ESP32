#include "HMS_Messaging.h"
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include "esp_random.h"
#include "esp_log.h"

namespace { constexpr char TAG[] = "HMS_Messaging"; }

HMS_Messaging::HMS_Messaging(HMS_Interface& transport)
    : transport_(transport) {
    ackSemaphore_ = xSemaphoreCreateBinary();
    transport_.setReceiveCallback([this](const uint8_t* mac, const uint8_t* data, size_t len, int8_t rssi) { onReceive(mac,data,len,rssi); });
}
HMS_Messaging::~HMS_Messaging() {
    // Wait for any currently executing blocking transaction before deleting its semaphore.
    std::lock_guard<std::mutex> sendLock(sendMutex_);
    transport_.clearReceiveCallbackAndWait();
    if (ackSemaphore_) vSemaphoreDelete(ackSemaphore_);
}
void HMS_Messaging::setMessageCallback(MessageCallback callback) {
    std::lock_guard<std::mutex> lock(callbackMutex_); messageCallback_ = std::move(callback);
}
size_t HMS_Messaging::maxApplicationPayload() const noexcept {
    const size_t transportMax = transport_.maxPayload();
    if (transportMax <= HEADER_SIZE) return 0;
    const size_t applicationMax = transportMax - HEADER_SIZE;
    return applicationMax > std::numeric_limits<uint16_t>::max()
        ? std::numeric_limits<uint16_t>::max()
        : applicationMax;
}
bool HMS_Messaging::isValidUnicast(const uint8_t* mac) {
    if (!mac) return false;
    bool zero=true, ff=true;
    for (size_t i=0;i<6;++i) { zero &= mac[i]==0; ff &= mac[i]==0xFF; }
    return !zero && !ff && (mac[0]&1U)==0;
}
uint32_t HMS_Messaging::readU32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24); }
uint16_t HMS_Messaging::readU16(const uint8_t* p) { return (uint16_t)p[0] | ((uint16_t)p[1]<<8); }
uint64_t HMS_Messaging::readU64(const uint8_t* p) {
    uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i) value |= static_cast<uint64_t>(p[i]) << (8 * i);
    return value;
}
void HMS_Messaging::writeU64(uint8_t* p, uint64_t v) {
    for (unsigned i = 0; i < 8; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i));
}
void HMS_Messaging::writeU32(uint8_t* p, uint32_t v) { for (unsigned i=0;i<4;++i) p[i]=(uint8_t)(v>>(8*i)); }
void HMS_Messaging::writeU16(uint8_t* p, uint16_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }

HMS_ReliableResult HMS_Messaging::sendReliable(const uint8_t* peer, const uint8_t* payload, size_t len, uint32_t timeoutMs, uint8_t retries) {
    if (!ackSemaphore_) return HMS_ReliableResult::NOT_STARTED;
    if (HMS_Interface::inTransportCallbackContext()) return HMS_ReliableResult::WOULD_BLOCK_IN_CALLBACK;
    if (transport_.maxPayload() == 0) return HMS_ReliableResult::NOT_STARTED;
    if (!isValidUnicast(peer) || !payload || len==0 || len>maxApplicationPayload() || timeoutMs==0) return HMS_ReliableResult::INVALID_ARGUMENT;
    std::lock_guard<std::mutex> sendLock(sendMutex_); // one outstanding reliable transaction per instance
    std::unique_ptr<uint8_t[]> frame(new (std::nothrow) uint8_t[HEADER_SIZE+len]);
    if (!frame) return HMS_ReliableResult::TX_REJECTED;
    frame[0]=MAGIC0; frame[1]=MAGIC1; frame[2]=VERSION; frame[3]=0;
    while (xSemaphoreTake(ackSemaphore_,0)==pdTRUE) {}
    uint64_t session;
    uint32_t seq;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        if (sessionId_ == 0) {
            sessionId_ = (static_cast<uint64_t>(esp_random()) << 32) | esp_random();
            if (sessionId_ == 0) sessionId_ = 1;
        }
        session = sessionId_;
        seq=nextSequence_++; if (nextSequence_==0) nextSequence_=1;
        pending_=true; ackReceived_=false; pendingSession_=session; pendingSequence_=seq; std::memcpy(pendingPeer_,peer,6);
    }
    writeU64(frame.get()+4,session);
    writeU32(frame.get()+12,seq);
    writeU16(frame.get()+16,static_cast<uint16_t>(len));
    std::memcpy(frame.get()+HEADER_SIZE,payload,len);
    for (uint16_t attempt=0; attempt<=static_cast<uint16_t>(retries); ++attempt) {
        if (!transport_.send(peer,frame.get(),HEADER_SIZE+len)) { std::lock_guard<std::mutex> lock(stateMutex_); pending_=false; return HMS_ReliableResult::TX_REJECTED; }
        TickType_t waitTicks = pdMS_TO_TICKS(timeoutMs);
        if (waitTicks == 0) waitTicks = 1;
        if (xSemaphoreTake(ackSemaphore_, waitTicks)==pdTRUE) {
            std::lock_guard<std::mutex> lock(stateMutex_);
            if (ackReceived_ && pendingSession_==session && pendingSequence_==seq && std::memcmp(pendingPeer_,peer,6)==0) { pending_=false; return HMS_ReliableResult::OK; }
        }
    }
    { std::lock_guard<std::mutex> lock(stateMutex_); pending_=false; }
    return HMS_ReliableResult::TIMEOUT;
}

void HMS_Messaging::sendAck(const uint8_t* mac, uint64_t session, uint32_t sequence) {
    uint8_t ack[HEADER_SIZE]{};
    ack[0]=MAGIC0; ack[1]=MAGIC1; ack[2]=VERSION; ack[3]=FLAG_ACK;
    writeU64(ack+4,session); writeU32(ack+12,sequence); writeU16(ack+16,0);
    // The transport must already have this unicast peer registered.
    (void)transport_.send(mac,ack,sizeof(ack));
}

bool HMS_Messaging::isDuplicate(const uint8_t* mac, uint64_t session, uint32_t sequence) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    PeerCache* entry=nullptr;
    for (auto& p:peers_) if (p.used && std::memcmp(p.mac,mac,6)==0) { entry=&p; break; }
    if (!entry) {
        for (auto& p:peers_) if (!p.used) { entry=&p; break; }
        if (!entry) entry=&peers_[sequence % peers_.size()];
        *entry=PeerCache{}; entry->used=true; std::memcpy(entry->mac,mac,6);
    }
    for (size_t i = 0; i < SEQ_CACHE_COUNT; ++i)
        if (entry->session[i] == session && entry->seq[i] == sequence) return true;
    const uint8_t slot = entry->next++ % SEQ_CACHE_COUNT;
    entry->session[slot] = session;
    entry->seq[slot] = sequence;
    return false;
}

void HMS_Messaging::onReceive(const uint8_t* mac, const uint8_t* data, size_t len, int8_t) {
    if (!mac || !data || len<HEADER_SIZE || data[0]!=MAGIC0 || data[1]!=MAGIC1 || data[2]!=VERSION) return;
    const uint8_t flags=data[3];
    const uint64_t session=readU64(data+4);
    const uint32_t seq=readU32(data+12);
    const uint16_t payloadLen=readU16(data+16);
    if (len != HEADER_SIZE + payloadLen || session==0 || seq==0 || (flags & ~FLAG_ACK)!=0) return;
    if (flags & FLAG_ACK) {
        if (payloadLen!=0) return;
        bool matched=false;
        { std::lock_guard<std::mutex> lock(stateMutex_); if (pending_ && pendingSession_==session && pendingSequence_==seq && std::memcmp(pendingPeer_,mac,6)==0) { ackReceived_=true; matched=true; } }
        if (matched && ackSemaphore_) xSemaphoreGive(ackSemaphore_);
        return;
    }
    if (!isValidUnicast(mac)) return;
    sendAck(mac,session,seq); // ACK duplicates too, so a lost ACK can be recovered by sender retry.
    if (isDuplicate(mac,session,seq)) return;
    MessageCallback cb;
    { std::lock_guard<std::mutex> lock(callbackMutex_); cb=messageCallback_; }
    if (cb) cb(mac,data+HEADER_SIZE,payloadLen);
}
