#pragma once

#include <stddef.h>
#include <stdint.h>
#include <functional>

/** Logical transport choices exposed to applications. Only ESP-NOW is included
 * in this reference project; additional transports can implement this contract. */
enum class HMS_Protocol : uint8_t {
    NONE = 0,
    ESPNOW = 1,
    // Future: BLE, UART, Modbus, Wi-Fi/IP, etc.
};

class HMS_Interface {
public:
    virtual ~HMS_Interface() = default;

    virtual HMS_Protocol protocol() const noexcept = 0;
    virtual bool begin() = 0;
    virtual void end() = 0;
    virtual bool send(const uint8_t* peer_addr, const uint8_t* data, size_t len) = 0;
    virtual bool broadcast(const uint8_t* data, size_t len) = 0;

    // Callbacks run in the concrete implementation's application/task context,
    // not directly in the native Wi-Fi callback context. Data pointers are valid
    // only for the duration of the callback.
    using ReceiveCallback = std::function<void(const uint8_t* mac_addr,
                                               const uint8_t* data,
                                               size_t len,
                                               int8_t rssi)>;
    using SendCallback = std::function<void(const uint8_t* mac_addr, bool success)>;

    void setReceiveCallback(ReceiveCallback cb);
    void setSendCallback(SendCallback cb);

protected:
    ReceiveCallback onReceive;
    SendCallback onSend;
};
