# HMS_EspNow examples

The editable application is in the repository root's `main/` directory. [`basic_node/`](basic_node/README.md) is a standalone project that builds that same source; there is no separate TX and RX implementation to keep in sync.

## Public mode options

The component exposes these enums in `HMS_EspNowNode.h`:

- `HMS_EspNowRole`: `RX_ONLY`, `TX_ONLY`, `BIDIRECTIONAL`
- `HMS_EspNowTopology`: `ONE_WAY`, `TWO_WAY`, `ONE_TO_MANY`, `MANY_TO_ONE`, `MANY_TO_MANY`
- `HMS_EspNowAddressing`: `UNICAST`, `BROADCAST`
- `HMS_EspNowPayloadProfile`: `V1_COMPAT`, `V2` (from `HMS_EspNow.h`)

`main/demo_modes.h` selects role, topology and addressing. `main/peer_table.h` contains the node-specific MAC list. These files forward to the root `main/` settings. The defaults target the second ESP32-S3 as RX with the first as its peer. Radio/security/queue configuration is supplied to `HMS_EspNowNodeConfig::transport` in `main/app_main.cpp`; product applications should populate it from their own platform configuration or board support layer.

## Topology/peer-table rules

| Pattern | TX node peer list | RX node peer list | Role |
|---|---|---|---|
| One-way unicast | Exactly one receiver | Exactly one sender | TX_ONLY / RX_ONLY |
| One-way broadcast | Empty list recommended | Empty list recommended | TX_ONLY / RX_ONLY |
| Two-way | The other node | The other node | BIDIRECTIONAL on both |
| One-to-many unicast | All receivers | Sender MAC(s) | TX_ONLY / RX_ONLY; BIDIRECTIONAL for return traffic |
| One-to-many broadcast | Empty list | Empty list or optional sender allow-list | TX_ONLY / RX_ONLY; BIDIRECTIONAL for return traffic |
| Many-to-one unicast | Exactly one coordinator | All sender MACs | TX_ONLY / RX_ONLY (BIDIRECTIONAL also allowed for a hybrid coordinator) |
| Many-to-one broadcast | No destination list required | Sender MAC allow-list recommended | TX_ONLY / RX_ONLY; enable `filter_broadcast_receivers` on coordinator |
| Many-to-many unicast | All intended neighbors | All intended neighbors | BIDIRECTIONAL is typical; TX_ONLY/RX_ONLY may be used for partial participants |
| Many-to-many broadcast | Empty list | Empty list or optional source allow-list | BIDIRECTIONAL is typical; TX_ONLY/RX_ONLY may be used for partial participants |

`sendToPeers()` performs one unicast submission per configured peer, or one broadcast submission in broadcast mode. `sendToPeer()` is for a configured unicast peer. The component validates role/topology combinations and rejects broadcast + encryption. Broadcast many-to-one is supported, but every node in radio range can hear the frames; configure a sender allow-list on the coordinator with `filter_broadcast_receivers=true` if required. The role controls local TX/RX capability; `BIDIRECTIONAL` is valid for star-pattern nodes where return traffic is required, except `ONE_WAY`, while `TWO_WAY` requires bidirectional nodes.

## Important boundaries

- These are single-hop patterns, not native ESP-NOW mesh routing.
- Broadcast is unencrypted; CCMP is per-peer unicast only.
- Native send callbacks report MAC-layer status, not remote application receipt or execution.
- `V2` is rejected if the running target/SDK does not report native v2 capability. The compile-time payload limit is never fabricated when the SDK lacks the v2 macro.
- Peer limits are SDK/target/configuration dependent. The component checks exposed SDK limits where available; `esp_now_add_peer()` remains authoritative.
- The node facade copies its peer table and key material at construction. The pointed-to peer/key buffers only need to remain valid during construction.
- Call `requestStop()` from a callback if needed; call `end()` later from an application task. Never destroy the node from its own callback.
- Callback data is borrowed and valid only for the callback duration. Copy it if it must outlive the callback.
- Do not use the example test setup as a production key-provisioning mechanism.
