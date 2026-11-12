# ESP-NOW roles, addressing and network patterns

## Functional roles are not protocol modes

ESP-NOW does not define a permanent controller/peripheral role. Any node can transmit and receive when its Wi-Fi interface and ESP-NOW stack are initialized. A product may assign application roles such as controller, sensor, actuator, collector or gateway, but these are application responsibilities. A bidirectional node is both sender and receiver.

## Addressing

### Unicast

A frame is addressed to one peer MAC. Register the peer before sending when using `EXPLICIT_ONLY`. Encrypted unicast requires a configured PMK and matching per-peer LMK at both ends. Peer-table limits are hardware/SDK-defined; inspect `ESP_NOW_MAX_TOTAL_PEER_NUM` and `ESP_NOW_MAX_ENCRYPT_PEER_NUM` for the selected build.

### Broadcast

The destination is `FF:FF:FF:FF:FF:FF`. The broadcast peer must be added as an unencrypted peer by the local stack. Broadcast is not encrypted with ESP-NOW's peer CCMP mechanism. A sender does not receive a per-recipient list of delivery acknowledgements. Do not send secrets or privileged commands in plaintext broadcast frames.

## Common single-hop topologies

1. **One-way:** one sender to one receiver.
2. **Two-way:** two nodes each send and receive.
3. **One-to-many:** one sender broadcasts or sends sequential unicast copies to several peers.
4. **Many-to-one:** several independent senders transmit to one collector. Encrypted collectors need each sender provisioned as a peer.
5. **Many-to-many:** multiple nodes have peer relationships and send to one another. Every destination still needs to be reachable in one hop and within peer-table limits.

These are application deployment patterns, not five separate ESP-NOW operating modes. ESP-NOW is not a mesh-routing protocol. A multi-hop mesh requires routing, forwarding, TTL/hop limits, duplicate suppression, route maintenance, congestion control and recovery above this component.

## Interface and channel

The local ESP-NOW interface (STA or AP) must match the peer's configured interface. STA and AP MAC addresses are different; use the MAC corresponding to the interface selected by `HMS_EspNowConfig::interface`. The example uses Wi-Fi STA MAC addresses.

Peer channel 0 means use the current local Wi-Fi channel. A nonzero peer channel must match the channel on which the local Wi-Fi interface operates. When another application owns Wi-Fi, this component defaults to requiring a requested channel match; set `channel=0` to accept the current channel, or select `USE_CURRENT` policy to intentionally use an externally managed channel.

Channels and permitted transmit power are subject to the regulatory domain and board configuration. Channel 14 is not universally permitted. AP association can control the station channel; do not force a conflicting channel while connected to an AP.

## Version and payload interoperability

- v1 payload limit: 250 bytes.
- v2 payload limit: 1470 bytes on stacks exposing the v2 capability (some early IDF 5.4 releases used a different constant; use the SDK macro rather than a hardcoded value).
- A v2 node can receive v1 frames. A v1 node cannot reliably consume v2 frames larger than 250 bytes; depending on stack behavior, oversized v2 data may be truncated or discarded.
- Mixed-version networks should limit all application frames to 250 bytes.
- The component calls `esp_now_get_version()` on IDF 5.4+ after ESP-NOW initialization. If a v2 profile is requested but the native stack reports v1, startup fails with `ESP_ERR_NOT_SUPPORTED`.
- Targets/SDKs without the v2 macro allocate RX storage for the v1 maximum only; the code never substitutes a fictitious 1470-byte constant.

## Not implemented by this transport

Automatic discovery, pairing, key provisioning, persistent peer storage, channel hopping, fragmentation/reassembly, multi-hop routing, OTA, and application authorization are intentionally outside `HMS_EspNow`. Add these as separate, explicitly scoped layers rather than silently changing transport semantics.

## Component-facing API

`HMS_EspNowNode.h` exposes `HMS_EspNowRole`, `HMS_EspNowTopology` and `HMS_EspNowAddressing`. Construct `HMS_EspNowNodeConfig`, set these selections, supply the other nodes' MAC addresses (except where broadcast removes the need for a destination list), and configure the nested `HMS_EspNowConfig` radio/queue/payload options. `HMS_EspNowNode::begin()` validates the combination and provisions the configured unicast peers. `sendToPeers()` implements fan-out/broadcast; receive callbacks are delivered only for RX-capable roles and, by default, configured unicast senders.

The example's `demo_modes.h` is intentionally only the three mode selections. Its peer table and transport tuning are separate from the mode selector. See `components/HMS_EspNow/examples/basic_node/`.
