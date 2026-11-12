# Changelog

## Unreleased

- Added active payload-capability reporting to `HMS_Interface`; custom transports must implement `maxPayload()`.
- Changed `HMS_Messaging` to derive its payload ceiling from the active transport instead of assuming 1470 bytes.
- Versioned the messaging frame as v2 and added a per-session nonce so sequence reuse after sender restart is not mistaken for a duplicate; all communicating peers must be updated together.
- Added a Unity component-test app for messaging payload boundaries, duplicate suppression and session separation.
- Made the basic example fail safely on NVS recovery conditions instead of erasing the entire NVS partition.

## 2.1.0 — Component-owned topology configuration

- Added `HMS_EspNowNode`, a topology-aware facade inside `HMS_EspNow`.
- Moved role, topology, addressing, peer registration, unicast fan-out, broadcast behavior and receive filtering into the reusable component.
- Added component-level enums for RX-only, TX-only, bidirectional, five single-hop topologies and unicast/broadcast addressing.
- Local interface MAC is discovered by the component; removed application-maintained `SELF_MAC` duplication.
- Reduced the example mode selector to role, topology and addressing only; separated peer table from mode selection.
- Added a canonical basic-node example under the component, reused by the root project rather than maintaining separate TX/RX applications.
- Added component/API and production qualification documentation.
- Native ESP-IDF build and hardware qualification remain pending in an environment with the supported SDK/targets.

## 2.0.0 — Generic transport baseline

- Retained transport-neutral `HMS_Interface`, ESP-NOW transport and optional `HMS_Messaging` layers.
- Added native capability detection, bounded callback queues, lifecycle controls, diagnostics and compatibility documentation.
