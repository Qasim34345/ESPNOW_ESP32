# Build and qualification status

The workspace contains generated baseline build artifacts for ESP-IDF v6.1-dirty, target `esp32`. An incremental CMake build completed on that baseline and reported 35% free in its configured app partition. Those artifacts predate the current source edits and do **not** validate this revision.

No build was run after the current edits. An `idf.py build` attempt could not start because the configured Python environment/constraints paths were unavailable. This revision has not been flashed or tested on physical hardware. Do not treat generated artifacts, source inspection, or Unity test definitions as evidence that the current source compiles or that the device behavior is qualified.

Before production release:

1. Select and document the exact ESP-IDF tags/commits and Wi-Fi-capable targets to support.
2. Build the root project and the standalone `components/HMS_EspNow/examples/basic_node` project for each target; run the component test app in `tests/component_tests`.
3. Save the target, SDK commit, `sdkconfig`, compiler output, warnings and board revision.
4. Run the validation matrix in `VALIDATION_PLAN.md`: startup/shutdown cycles, callback teardown, queue saturation, channel mismatch, peer exhaustion, encrypted unicast, broadcast, v1/v2 interoperability and every topology.
5. Measure RAM/heap, worker stack high-water marks, throughput, packet loss and latency on hardware.
6. Provision unique production keys outside source control. Review regulatory channel restrictions and product threat model.
7. Complete `RELEASE_CHECKLIST.md` and record evidence for every claimed target/SDK combination.

The editable example is in `main/app_main.cpp`, with mode selection in `main/demo_modes.h` and peers in `main/peer_table.h`. The component discovers the local interface MAC at runtime.

Do not call this revision hardware-qualified or production-certified until the above steps pass. It is a reusable component architecture and implementation baseline pending target-specific qualification.
