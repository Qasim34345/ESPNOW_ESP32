#pragma once
#include <stddef.h>
#include <stdint.h>
#include <functional>
#include <mutex>
#include <condition_variable>

enum class HMS_Protocol : uint8_t { NONE = 0, ESPNOW = 1 };

/**
 * Transport-neutral contract. maxPayload() is zero until the transport is ready;
 * callback data is borrowed and valid only during dispatch.
 */
class HMS_Interface {
public:
    virtual ~HMS_Interface() = default;
    virtual HMS_Protocol protocol() const noexcept = 0;
    virtual bool begin() = 0;
    virtual void end() = 0;
    virtual size_t maxPayload() const noexcept = 0;
    virtual bool send(const uint8_t* peer_addr, const uint8_t* data, size_t len) = 0;
    virtual bool broadcast(const uint8_t* data, size_t len) = 0;

    using ReceiveCallback = std::function<void(const uint8_t*, const uint8_t*, size_t, int8_t)>;
    using SendCallback = std::function<void(const uint8_t*, bool)>;
    void setReceiveCallback(ReceiveCallback cb);
    void setSendCallback(SendCallback cb);
    // Clears the callback and waits for any already-copied invocation to return.
    // Must not be called from that callback itself.
    void clearReceiveCallbackAndWait();
    void clearSendCallbackAndWait();
    static bool inTransportCallbackContext() noexcept;

protected:
    void dispatchReceive(const uint8_t* mac, const uint8_t* data, size_t len, int8_t rssi);
    void dispatchSend(const uint8_t* mac, bool success);
private:
    std::mutex callbackMutex_;
    std::condition_variable callbackCv_;
    size_t receiveCallbacksInFlight_ = 0;
    size_t sendCallbacksInFlight_ = 0;
    ReceiveCallback onReceive_;
    SendCallback onSend_;
};
