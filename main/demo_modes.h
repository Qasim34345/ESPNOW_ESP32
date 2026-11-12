#pragma once
#include "HMS_EspNowNode.h"

// Defaults target the second ESP32-S3 as a one-way receiver.
namespace demo_modes {
constexpr HMS_EspNowRole ROLE = HMS_EspNowRole::RX_ONLY;
constexpr HMS_EspNowTopology TOPOLOGY = HMS_EspNowTopology::ONE_WAY;
constexpr HMS_EspNowAddressing ADDRESSING = HMS_EspNowAddressing::UNICAST;
}
