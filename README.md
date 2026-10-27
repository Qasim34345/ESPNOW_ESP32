# ESPNOW_ESP32 — HMS Interface + ESP-NOW example project

This ESP-IDF project integrates two initially supplied components into one runnable reference application:

- `HMS_Interface`: abstract transport contract and protocol identifier.
- `HMS_EspNow`: ESP-NOW transport implementation of that contract.
- `main`: two-board broadcast/receive demonstration.

The goal is a component-first architecture: product applications depend on the interface contract, while concrete transports implement it. ESP-NOW is the only concrete transport in this project. Future transports can implement `HMS_Interface` without changing application-level code, provided the application selects the appropriate implementation.

## Target and toolchain

Designed for ESP-IDF v6.1 and the ESP32 family. The supplied native callback signatures match the ESP-IDF v6.1 API style. This archive has not been built in the actual ESP-IDF environment or verified on hardware; treat it as an implementation candidate until compiled and tested against the exact target.

## Build and flash

```bash
idf.py set-target esp32
idf.py build
idf.py -p COMx flash monitor
```

Replace `esp32` with your target (for example, `esp32s3`) and `COMx` with the board's serial port. Build and flash the same project to two boards. Both boards broadcast a heartbeat every five seconds and receive packets from the other board, printing sender MAC, RSSI, length, and payload bytes.

The app uses a fixed channel (channel 1) only when this driver initializes Wi-Fi itself. If Wi-Fi was already initialized by another owner, the component does not force its channel or stop that Wi-Fi subsystem. For two-board testing, ensure both devices actually operate on the same channel.

## Components

```text
ESPNOW_ESP32/
├── CMakeLists.txt
├── sdkconfig.defaults
├── README.md
├── components/
│   ├── HMS_Interface/
│   │   ├── CMakeLists.txt
│   │   ├── include/HMS_Interface.h
│   │   └── src/HMS_Interface.cpp
│   └── HMS_EspNow/
│       ├── CMakeLists.txt
│       ├── include/HMS_EspNow.h
│       └── src/HMS_EspNow.cpp
├── main/
│   ├── CMakeLists.txt
│   └── app_main.cpp
└── docs/
    ├── ARCHITECTURE.md
    ├── API_AND_LIFECYCLE.md
    └── VALIDATION_PLAN.md
```

## Important behavior and limitations

- Native payload limit is conservatively set to 250 bytes for broad ESP-NOW v1.0 compatibility.
- `send()` returning `true` means the native send request was accepted, not that the remote application processed it. The asynchronous callback reports MAC-layer send status.
- Broadcast packets are not protected by per-peer CCMP and are not application-acknowledged.
- Receive/send callbacks are copied into a bounded FreeRTOS queue and invoked by a worker task, not directly from the native Wi-Fi callback. Events can be dropped if the queue is full; this is logged.
- The current implementation supports one active `HMS_EspNow` instance because native ESP-NOW callback registration is global.
- Peer entries are transient and are cleared by ESP-NOW deinitialization.
- Optional encrypted unicast peer configuration is exposed by `addPeer()`, but the demo does not enable it or provision keys.
- No application-level retry/ACK, discovery/pairing, fragmentation, mesh routing, OTA, or persistent peer storage is implemented yet.
- The component can initialize/start Wi-Fi if it finds Wi-Fi uninitialized. If Wi-Fi is already initialized, it piggybacks and leaves ownership with the existing application. Ensure that externally owned Wi-Fi is started and configured before using this component.
- Do not call `end()` from inside a receive/send callback: it waits for the worker task to stop. Set callbacks before `begin()` and avoid changing callbacks concurrently with delivery.

## Reference architecture and next steps

See `docs/ARCHITECTURE.md`, `docs/API_AND_LIFECYCLE.md`, and `docs/VALIDATION_PLAN.md`. This is a starting point for development, not a production-qualified release. Compile with the exact ESP-IDF version and run the validation plan before product use.
