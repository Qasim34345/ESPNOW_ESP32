# API and lifecycle contract

## Basic startup

```cpp
HMS_EspNowConfig cfg{};
cfg.wifi_policy = HMS_EspNowWifiPolicy::MANAGE_IF_UNINITIALIZED;
cfg.channel_policy = HMS_EspNowChannelPolicy::REQUIRE_MATCH;
cfg.wifi_mode = WIFI_MODE_STA;
cfg.interface = WIFI_IF_STA;
cfg.channel = 1;
cfg.payload_profile = HMS_EspNowPayloadProfile::V2; // Explicitly opt into v2 after runtime capability check
cfg.peer_policy = HMS_EspNowPeerPolicy::EXPLICIT_ONLY;

static HMS_EspNow radio(cfg);
radio.setReceiveCallback([](const uint8_t* mac, const uint8_t* data, size_t len, int8_t rssi) {
    // Copy data if it must outlive this callback. Keep this callback short.
});
const esp_err_t err = radio.beginDetailed();
if (err != ESP_OK) { /* handle esp_err_to_name(err) */ }
```

Initialize NVS before Wi-Fi when the selected Wi-Fi configuration uses NVS. The basic example fails startup on NVS recovery conditions rather than erasing the partition. Products must define an application-specific migration or recovery policy; do not erase NVS casually because it may contain unrelated settings or credentials.

The application owns the ESP event loop and network-interface initialization. Initialize `esp_netif` and the default event loop before starting Wi-Fi; the basic example does this and tolerates the already-initialized state.

## Payload capability

After `begin()`, inspect `radio.capabilities()`:

- `native_version`: native stack version (1 or 2).
- `v1_max_payload`: compile-time SDK v1 maximum.
- `v2_max_payload`: SDK v2 maximum where exposed; otherwise safely falls back to v1 storage size.
- `effective_max_payload`: selected profile's usable transport payload.
- `v2_supported`: native version query reported v2.

A requested v2 profile fails startup with `ESP_ERR_NOT_SUPPORTED` if the native stack reports v1. `maxPayload()` is zero before successful initialization and after teardown.

## Peer management

- Default `EXPLICIT_ONLY` requires `addPeer()` before unicast `send()`.
- `AUTO_ADD_UNENCRYPTED` is available only as an opt-in convenience policy. Do not use it as an authorization mechanism.
- Add the peer using the correct STA/AP interface and channel. `channel=0` on a peer means use the current local channel.
- Encrypted unicast requires `setPrimaryMasterKey()` and a 16-byte LMK for each encrypted peer. Provision unique keys securely in real deployments.
- Broadcast is a special all-FF peer and is always unencrypted.
- Peer capacity and encrypted-peer capacity are SDK/target-defined; inspect the native constants and handle `ESP_ERR_ESPNOW_FULL`.

## Wi-Fi ownership and channel policy

- `MANAGE_IF_UNINITIALIZED`: initialize/start Wi-Fi only if not already initialized.
- `USE_EXISTING`: fail if Wi-Fi has not already been initialized.
- `REQUIRE_MATCH`: if an external Wi-Fi owner is active and the requested nonzero channel differs, fail startup rather than silently continue.
- `USE_CURRENT`: accept the channel chosen by the external Wi-Fi owner. Setting `channel=0` also means no fixed channel requirement.

The component never changes the channel of an externally owned Wi-Fi interface. For AP/APSTA use, initialize and configure the AP through the owning application before starting this component. If STA is associated with an AP, the AP controls the station channel. Use the STA MAC when configured for `WIFI_IF_STA`; AP MAC is different.

## Send and callback semantics

- `send()` / `broadcast()` returning true means local API acceptance.
- `SendCallback` is the native MAC-layer result; it is not an application ACK.
- RX buffers are borrowed and valid only for the duration of callback dispatch.
- Callbacks run in the component worker task, not in the native Wi-Fi callback context. Still keep them bounded and avoid long flash operations.
- Do not call blocking `end()` from a callback. Use `requestStop()` and arrange for an application task to call `end()`.
- One active `HMS_EspNow` instance is supported.

## Detailed errors and diagnostics

Prefer the `*Detailed()` methods when a specific call's `esp_err_t` is needed. `lastError()` is a synchronized last-operation diagnostic but can be overwritten by another task after a call. `stats()` returns a snapshot; counters wrap naturally at 32 bits. Queue-full and stopping drops are counted separately; TX callback totals, MAC successes and MAC failures are tracked independently.

## HMS_Messaging caveats

`HMS_Messaging` owns the transport receive callback slot while alive. Do not install a competing callback. Its maximum application payload is derived from the transport's active `maxPayload()` and is zero until the transport is ready. The current wire format is version 2; all communicating peers must use the same version. Its ACK means the remote messaging layer accepted a well-formed frame; it does not mean the application callback completed successfully. `sendReliable()` blocks, so it returns `WOULD_BLOCK_IN_CALLBACK` when called from a transport callback. Queue the work to another task. Destroy the wrapper outside its own callback; its destructor clears the callback and waits for in-flight dispatch to finish.

Custom `HMS_Interface` implementations must report their active frame limit through `maxPayload()`. Return zero before the transport is ready and after it stops; `HMS_Messaging` uses that value to reject payloads that the transport cannot carry.

## Topology-aware node facade

`HMS_EspNowNode` is the preferred application-facing API when the product wants the component to own topology behavior. Its public selectors are:

- `HMS_EspNowRole::{RX_ONLY, TX_ONLY, BIDIRECTIONAL}`
- `HMS_EspNowTopology::{ONE_WAY, TWO_WAY, ONE_TO_MANY, MANY_TO_ONE, MANY_TO_MANY}`
- `HMS_EspNowAddressing::{UNICAST, BROADCAST}`
- `HMS_EspNowPayloadProfile::{V1_COMPAT, V2}` in the nested transport configuration.

The caller supplies a peer table containing other nodes' unicast MAC addresses. The component copies that table and the optional PMK/LMK during construction. The local MAC is read from the configured STA or AP interface by the component. `begin()` checks the role/topology/addressing/peer-table combination, starts the transport, applies the PMK if requested, and registers the unicast peers.

`sendToPeers(data,len)` sends to every configured unicast peer or issues one broadcast. `sendToPeer(peer,data,len)` sends to one configured unicast peer. Both return local ESP-NOW API acceptance/errors, not remote application confirmation. RX callbacks are delivered only for receiver-capable roles. In unicast mode, `filter_unicast_receivers=true` (default) accepts only source MACs in the configured peer table.

`HMS_EspNowNode::requestStop()` may be called from a callback. `end()` must be called later from a normal application task. Do not destroy the node from a callback. `localMac()` is valid after `begin()` succeeds.

The node facade uses an immutable peer list after construction. If runtime peer-table changes are required, use the lower-level `HMS_EspNow` peer APIs and implement the product's topology and concurrency policy explicitly.
