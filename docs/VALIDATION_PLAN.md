# Validation plan before production use

## Build matrix

- Build using the exact ESP-IDF version intended for the product (initial target: v6.1).
- Build classic ESP32 and ESP32-S3 at minimum if both are intended targets.
- Enable compiler warnings and address all warnings; test with the project's actual sdkconfig.

## Functional tests

1. Start/stop lifecycle, repeated begin/end, and failed partial initialization cleanup.
2. Two boards on the same channel: broadcast receive in both directions.
3. Unicast to a configured peer and auto-add behavior.
4. Invalid/null MAC, null payload, zero length, and payload > 250 bytes.
5. Queue saturation and observable event-drop behavior.
6. Wi-Fi already initialized by an external owner; verify component does not stop it or force channel changes.
7. Wi-Fi uninitialized; verify owned initialization is cleaned up after end.
8. Wrong channel and radio loss behavior.
9. Encrypted unicast with matching LMKs and expected failure with mismatched keys.
10. Repeated traffic under load, receiver reset, sender reset, and shutdown while traffic is active.

## Reliability/security tests still needed

- Application ACK, timeout, bounded retry, sequence IDs, duplicate suppression.
- Replay protection and key lifecycle.
- Payload schema/versioning and malformed packets.
- Rate/latency/packet-loss measurements under Wi-Fi coexistence.
- Power-save behavior on supported targets.

## Acceptance evidence

Keep build logs, target + ESP-IDF version, sdkconfig, board revision, test wiring, packet counts, failure counts, and observed RSSI. A successful archive check or source review is not a substitute for compiling and running these tests on hardware.
