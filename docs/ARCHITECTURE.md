# Architecture

```text
Product application
    │
    ├── selects role / topology / addressing
    ├── supplies peer table and product callbacks
    │
    ▼
HMS_EspNowNode (topology-aware node facade)
    │  - validates role/topology/addressing combinations
    │  - discovers selected local Wi-Fi MAC
    │  - provisions configured peers and optional CCMP keys
    │  - performs unicast fan-out or broadcast
    │  - filters receive senders when configured
    │
    ├──────── optional HMS_Messaging (independent transport client)
    │
    ▼
HMS_Interface (transport-neutral callback contract)
    │
    ▼
HMS_EspNow (native ESP-NOW transport)
    │  - Wi-Fi ownership/channel policy
    │  - native version and payload capability detection
    │  - peer table and send/receive validation
    │  - bounded native callback queues and worker
    │  - counters and orderly shutdown
    ▼
ESP-IDF esp_wifi / esp_now / FreeRTOS
```

## Layer responsibilities

### HMS_Interface

Defines a transport-neutral contract, including the active maximum payload capability, and dispatches callbacks outside its callback mutex. It tracks active callback invocations so owners can clear callbacks and wait for copied invocations to return. Callback payload pointers are borrowed for the callback duration.

### HMS_EspNow

Implements native ESP-NOW mechanics only. It owns Wi-Fi policy, interface/channel validation, native version query, peer registration, payload-size enforcement, callback adaptation, bounded event queues, statistics and teardown. One active instance is supported because ESP-IDF callback registration is process-global.

### HMS_EspNowNode

Provides the reusable application-facing options: role, topology, addressing and payload profile. It copies peer addresses and key material during construction, discovers the selected STA/AP MAC, validates the node's mode/peer-list combination, registers unicast peers, and implements `sendToPeers()` semantics. It does not implement mesh routing or application message reliability.

The node facade uses an immutable peer table after construction. For applications requiring runtime peer-table mutation, use the lower-level `HMS_EspNow::addPeer()` / `removePeer()` API and own the associated concurrency and topology policy.

### HMS_Messaging

Optional simple unicast frame/receipt-ACK layer. It has a single receive callback slot on the supplied `HMS_Interface`; do not attach it to a transport already owned by another callback-multiplexing client. ACK means well-formed frame receipt, not command execution. Blocking reliable sends are rejected from transport callbacks.

## Callback and queue flow

Native callbacks validate metadata, copy data into bounded queues, update counters and return. A worker task dispatches callbacks in task context. RX and TX queues are separate; TX servicing is bounded per worker pass. RX queue depth has a direct RAM cost because each queue item reserves the compile-time maximum payload storage.

## Lifecycle

- Call `begin()` from task context.
- `requestStop()` is callback-safe and asks the transport worker to exit; it does not complete resource teardown.
- Call `end()` from a separate application task, not from a callback. It unregisters callbacks, waits for native callbacks in flight, joins the worker, releases queues and deinitializes only Wi-Fi resources owned by this component.
- A user callback that never returns can prevent deterministic shutdown; the component cannot safely force-delete a task executing user code.
- `HMS_EspNowNode` copies the peer table and key bytes at construction; do not mutate the caller's source buffers expecting the node to change.

## TX semantics

A successful `send()`/`sendToPeers()` return indicates local API acceptance. The native send callback reports MAC-layer delivery status, not remote application receipt. Use a protocol with transaction IDs and a remote application ACK when command-level confirmation is required.
