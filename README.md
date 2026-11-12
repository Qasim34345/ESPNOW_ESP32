# ESPNOW_Generic — reusable ESP-NOW component + example application

This repository separates the reusable ESP-NOW implementation from the example application. The component owns transport behavior and exposes selectable communication modes; the example chooses modes, supplies its peer table, and demonstrates a periodic `Hello! World` message.

## Architecture

- `HMS_Interface`: transport-neutral interface and callback lifetime tracking.
- `HMS_EspNow`: ESP-IDF ESP-NOW transport, Wi-Fi policy, peer table, native capability detection, payload checks, bounded callback queues, worker task, statistics and lifecycle.
- `HMS_EspNowNode`: topology-aware facade exposing roles, topologies, addressing modes, local MAC discovery, peer registration, topology validation, send fan-out and receive filtering.
- `HMS_Messaging`: optional framing, sequence, receipt ACK, retry and duplicate-suppression layer for unicast application messages.
- `main/`: editable root application, including mode and peer-table settings.
- `components/HMS_EspNow/examples/basic_node`: standalone project that builds the same root application.

## Project layout

```text
ESPNOW_Generic/
├── CMakeLists.txt
├── sdkconfig.defaults
├── main/                         # Full, editable root application and board settings
├── components/
│   ├── HMS_Interface/
│   ├── HMS_EspNow/
│   │   ├── include/HMS_EspNow.h
│   │   ├── include/HMS_EspNowNode.h
│   │   ├── src/
│   │   └── examples/basic_node/
│   └── HMS_Messaging/
└── docs/
```

## Select a communication mode

Edit `main/demo_modes.h` to select:

```cpp
constexpr HMS_EspNowRole ROLE = HMS_EspNowRole::RX_ONLY;
constexpr HMS_EspNowTopology TOPOLOGY = HMS_EspNowTopology::ONE_WAY;
constexpr HMS_EspNowAddressing ADDRESSING = HMS_EspNowAddressing::UNICAST;
```

Edit `main/peer_table.h` for the MAC addresses of other nodes. The local interface MAC is read by the component; there is no `SELF_MAC` setting and no MAC spoofing. The radio configuration is in `main/app_main.cpp`. For the included two-board ESP32-S3 pair, the defaults target the second board (`DC:DA:0C:18:0C:10`) as `RX_ONLY`, with the first board (`DC:DA:0C:12:80:F8`) as its peer. To run the first board as TX, select `TX_ONLY` in `main/demo_modes.h` and change its peer entry to the second board's MAC.

| Topology | Required roles | Behavior |
|---|---|---|
| `ONE_WAY` | TX_ONLY → RX_ONLY | One node sends to one receiver |
| `TWO_WAY` | BIDIRECTIONAL on both | Two nodes send and receive |
| `ONE_TO_MANY` | TX_ONLY → RX_ONLY nodes | Sender fans out to peers or broadcasts |
| `MANY_TO_ONE` | TX_ONLY nodes → RX_ONLY coordinator | Senders target one coordinator by unicast, or broadcast with receiver-side sender filtering |
| `MANY_TO_MANY` | Typically BIDIRECTIONAL on participating nodes | Nodes send to configured peers or broadcast; TX_ONLY/RX_ONLY can be used for partial participants |

More detail and valid peer-table arrangements are in `components/HMS_EspNow/examples/README.md` and `docs/NETWORK_MODES.md`.

## Build

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p COMx flash monitor
```

Use a Wi-Fi-capable target supported by the ESP-IDF version you have selected. The project intentionally does not hard-code an ESP32 target. Both ends must use a compatible Wi-Fi interface and the same RF channel.

## Protocol and production boundaries

- ESP-NOW is single-hop. Mesh routing, multi-hop forwarding, route discovery and loop prevention are not provided.
- Broadcast cannot use per-peer CCMP encryption.
- Native TX status is MAC-layer status, not application delivery or command execution.
- `HMS_Messaging` receipt ACK means a valid frame was received; it does not mean the application executed the command.
- `HMS_Messaging` wire protocol v2 requires all communicating peers to be updated together.
- Native ESP-NOW v2 support and maximum payload depend on the exact ESP-IDF version and target. `V2` startup fails if native v2 capability is unavailable.
- ESP-NOW peer limits vary by SDK, target and encrypted-peer configuration.
- Production use requires secure per-device key provisioning, regulatory/channel review, target-specific memory/stack measurements, stress testing and a completed release checklist.

## Validation status

The workspace contains an earlier generated build for ESP-IDF v6.1-dirty / `esp32`, but that build predates the current source edits. This revision has not been built, flashed or hardware-qualified. See `docs/BUILD_STATUS.md`, `docs/COMPATIBILITY.md`, `docs/VALIDATION_PLAN.md` and `docs/RELEASE_CHECKLIST.md` before making a production claim.
