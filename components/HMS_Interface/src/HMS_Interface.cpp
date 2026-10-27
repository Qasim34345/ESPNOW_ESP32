#include "HMS_Interface.h"
#include <utility>

void HMS_Interface::setReceiveCallback(ReceiveCallback cb) {
    onReceive = std::move(cb);
}

void HMS_Interface::setSendCallback(SendCallback cb) {
    onSend = std::move(cb);
}
