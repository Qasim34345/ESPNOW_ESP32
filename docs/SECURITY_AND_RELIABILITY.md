# Security and reliability

## Link security

- ESP-NOW encrypted unicast uses PMK/LMK and CCMP link protection.
- Broadcast is not encrypted by the native peer encryption mechanism.
- Provision keys outside source control. Never log PMK/LMK. Plan rotation, revocation and device replacement. `HMS_EspNowNode` copies key bytes at construction and clears its internal key arrays at destruction; this is not a substitute for secure boot, flash encryption, secure provisioning or a platform key store.
- The example keys are public test constants and must never be used in a deployed product.
- Link encryption does not authorize commands, provide durable replay protection, or guarantee exactly-once application effects.

## Application command security

For commands that change physical state, validate source identity, command allow-list, payload length, version, ranges, freshness, replay window and authorization before execution. Prefer desired-state commands (`set valve=closed`) over non-idempotent toggles. Send a separate application execution-result message only after the defined operation completes.

## Reliability boundaries

- Native send acceptance is not over-the-air success.
- MAC TX success is not remote application processing.
- Broadcast has no per-recipient acknowledgement.
- `HMS_Messaging` ACK means frame receipt at its messaging layer, not command execution.
- Retry only idempotent operations or use transaction IDs and duplicate suppression.
- The duplicate cache is bounded and volatile. A random 64-bit sender-session identifier prevents ordinary sequence reuse after sender restart from colliding with the prior session, but this is not authentication or durable replay defense; validate authorization and freshness at the application layer.
- `HMS_Messaging` wire protocol v2 is not compatible with v1 peers; update both ends together.

## Memory and overload

RX and TX queues are bounded. RX queue items reserve the full compile-time payload storage size. A larger RX depth can consume substantial internal RAM. Measure heap, queue allocation, callback duration, stack high-water mark and packet-drop rates under realistic peak traffic before choosing queue depths.

## Features deliberately outside HMS_EspNow

Discovery, pairing, key vault, persistent peer table, fragmentation/reassembly, routing/mesh, OTA, application authorization and durable transaction handling belong in separate layers. ESP-NOW is single-hop; do not present peer-to-peer fan-out as native mesh routing.
