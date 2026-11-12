# Validation and release plan

The current source edits have not been compiled or exercised on physical boards. Do not represent this revision as hardware-qualified until these checks pass.

## Component test app

The Unity test app under `tests/component_tests` covers the active transport payload boundary, duplicate-frame suppression and session separation. Build, flash and run it on a supported ESP-IDF target:

```bash
idf.py -C tests/component_tests set-target esp32
idf.py -C tests/component_tests build
idf.py -C tests/component_tests -p <PORT> flash monitor
```

Expand this app with regression cases for each fixed issue and add a hardware test plan before release. The tests do not replace the target/SDK compatibility matrix below.

## Build matrix

- Build cleanly on each supported ESP-IDF major/minor release.
- Build each Wi-Fi-capable target selected for the product (for example classic ESP32, ESP32-S2, ESP32-S3, ESP32-C2/C3/C5/C6/C61 where the chosen IDF exposes ESP-NOW).
- Verify v1-only targets/SDKs fail a requested v2 startup cleanly and can use `V1_COMPAT`.
- Verify both callback API profiles (IDF 5.0–5.4 and IDF 5.5+) compile.
- Treat new compiler warnings as release blockers where practical.

## Topology facade validation

- Valid and invalid role/topology combinations for all five patterns.
- Empty, single-peer, duplicate-peer, multicast-MAC, self-MAC and over-limit peer tables.
- Unicast fan-out to multiple receivers; verify each peer receives once per send cycle.
- Broadcast with zero peers; verify multiple receivers and no encryption.
- `sendToPeer()` rejects unconfigured peers and broadcast mode.
- RX-only nodes never submit application TX; TX-only nodes do not deliver application RX callbacks; bidirectional nodes do both.

## Transport functional tests

1. One-way unicast with explicit peer registration.
2. Broadcast reception by multiple receivers; verify no false claim of per-receiver ACK.
3. Bidirectional exchange and application-level response.
4. One-to-many sequential unicast fan-out.
5. Many-to-one unicast and broadcast from several independently rebooted senders; verify broadcast source allow-list behavior.
6. Many-to-many single-hop peer fan-out.
7. V1 boundary: 250 accepted, 251 rejected.
8. V2 boundary: native maximum accepted, one byte over rejected.
9. Mixed v1/v2 pair with sender limited to 250 bytes.
10. Encrypted unicast with matching PMK/LMK; mismatched keys must fail.
11. Broadcast plus encryption configuration must be rejected by the application/test configuration.
12. STA/AP interface MAC and peer-interface matching.
13. Existing Wi-Fi channel match/mismatch; STA associated with AP; APSTA mode.

## Lifecycle and stress tests

- Repeated begin/end cycles.
- Fail each startup stage (Wi-Fi init/start, queue allocation, ESP-NOW init, callback registration, task creation) with fault injection where possible.
- Call `requestStop()` from a callback and `end()` later from an application task.
- Attempt `end()` from callback and verify it returns without self-joining.
- Callback calls `send()` while another task initiates shutdown; verify no deadlock and no use-after-free.
- Saturate RX/TX queues with slow callbacks and verify distinct drop counters.
- Concurrent send, add/remove peer, stats and shutdown calls.
- Peer reboot, sender reboot, Wi-Fi AP association/channel changes, RF interference and sustained traffic.
- Measure queue RAM, minimum heap, worker stack high-water mark, packet loss, latency distribution and shutdown duration.

## Security tests

- Ensure test keys are removed from production builds.
- Verify no PMK/LMK values are logged or stored in source control.
- Verify encrypted peer provisioning and key mismatch behavior.
- Test sender authorization and replay handling at the application layer; link encryption alone is not command authorization.

## Acceptance evidence

Archive clean build logs, IDF tag, target, sdkconfig, board revision, peer configuration, packet-loss/latency metrics, heap/stack measurements, soak-test duration, RF test conditions and known limitations. A source review or successful compile alone is not production qualification.
