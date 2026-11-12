# Release checklist

- [ ] Pin exact ESP-IDF tag/commit and supported target matrix.
- [ ] Clean build for every target and SDK combination.
- [ ] Confirm runtime ESP-NOW version and effective payload on each hardware SKU.
- [ ] Verify v1 and v2 boundary behavior and mixed-version interoperability.
- [ ] Verify interface MAC, channel and Wi-Fi coexistence policy.
- [ ] Verify peer capacity and encrypted peer capacity on target.
- [ ] Provision unique PMK/LMK securely; remove test keys.
- [ ] Run encrypted unicast and key mismatch tests.
- [ ] Run repeated begin/end, failure injection and callback shutdown tests.
- [ ] Saturate queues and confirm drop metrics.
- [ ] Measure heap, internal RAM, stack high-water marks and callback execution time.
- [ ] Run one-way, two-way, one-to-many, many-to-one and many-to-many single-hop tests.
- [ ] Run RF stress, AP coexistence, peer reboot, sender reboot and long-duration soak.
- [ ] Define product loss/latency/recovery acceptance limits and archive evidence.
- [ ] Review command authorization, replay protection and idempotency separately from link encryption.
