#include "HMS_Interface.h"
#include <utility>

namespace { thread_local bool g_inTransportCallback = false; }

void HMS_Interface::setReceiveCallback(ReceiveCallback cb) {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    onReceive_ = std::move(cb);
}
void HMS_Interface::setSendCallback(SendCallback cb) {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    onSend_ = std::move(cb);
}
void HMS_Interface::clearReceiveCallbackAndWait() {
    std::unique_lock<std::mutex> lock(callbackMutex_);
    onReceive_ = {};
    callbackCv_.wait(lock, [this] { return receiveCallbacksInFlight_ == 0; });
}
void HMS_Interface::clearSendCallbackAndWait() {
    std::unique_lock<std::mutex> lock(callbackMutex_);
    onSend_ = {};
    callbackCv_.wait(lock, [this] { return sendCallbacksInFlight_ == 0; });
}
bool HMS_Interface::inTransportCallbackContext() noexcept { return g_inTransportCallback; }

void HMS_Interface::dispatchReceive(const uint8_t* mac, const uint8_t* data, size_t len, int8_t rssi) {
    ReceiveCallback cb;
    {
        std::lock_guard<std::mutex> lock(callbackMutex_);
        cb = onReceive_;
        if (cb) ++receiveCallbacksInFlight_;
    }
    if (!cb) return;
    const bool previous = g_inTransportCallback;
    g_inTransportCallback = true;
    cb(mac, data, len, rssi);
    g_inTransportCallback = previous;
    {
        std::lock_guard<std::mutex> lock(callbackMutex_);
        --receiveCallbacksInFlight_;
    }
    callbackCv_.notify_all();
}
void HMS_Interface::dispatchSend(const uint8_t* mac, bool success) {
    SendCallback cb;
    {
        std::lock_guard<std::mutex> lock(callbackMutex_);
        cb = onSend_;
        if (cb) ++sendCallbacksInFlight_;
    }
    if (!cb) return;
    const bool previous = g_inTransportCallback;
    g_inTransportCallback = true;
    cb(mac, success);
    g_inTransportCallback = previous;
    {
        std::lock_guard<std::mutex> lock(callbackMutex_);
        --sendCallbacksInFlight_;
    }
    callbackCv_.notify_all();
}
