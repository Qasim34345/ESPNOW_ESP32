# API and lifecycle notes

## Typical use

```cpp
HMS_EspNow transport;
transport.setReceiveCallback(on_receive);
transport.setSendCallback(on_send);
if (!transport.begin()) {
    // inspect transport.lastError()
}
transport.broadcast(bytes, length);
transport.send(peer_mac, bytes, length);
transport.end();
```

## Lifecycle ownership

- If Wi-Fi is uninitialized and `start_wifi_if_needed` is true, the component initializes and starts Wi-Fi. It later stops/deinitializes only the resources it created.
- If Wi-Fi is already initialized, the component piggybacks. It does not assume ownership and does not change the channel if an external Wi-Fi owner is present.
- In the externally managed case, the application is responsible for ensuring Wi-Fi is started, the correct interface is enabled, and all peers share a compatible channel.
- Native ESP-NOW callbacks are global; the implementation therefore rejects a second simultaneously active `HMS_EspNow` instance.

## Send semantics

`send()` and `broadcast()` return whether the native send request was accepted. The later send callback reports native MAC-layer success/failure. It does not prove that the receiving application processed the payload.

## Threading and buffer lifetime

The native callbacks only validate and copy event data into a bounded queue. User callbacks run in the worker task. Receive data and MAC pointers passed to user callbacks point to event-owned memory and are valid only for the callback duration; copy them if they must be retained.

Set callbacks before `begin()` and do not mutate them concurrently with callback delivery. Do not call `end()` from a user callback because shutdown waits for the worker task.

## Security

`setPrimaryMasterKey(pmk)` configures the 16-byte PMK; call it after `begin()` and before adding encrypted peers. `addPeer(mac, true, lmk)` configures encrypted unicast using a 16-byte LMK. The application must provision suitable keys and configure matching peer keys on both devices. Broadcast is not encrypted by per-peer CCMP. Never log key material.
