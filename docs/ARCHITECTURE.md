# Architecture

## Responsibility split

### HMS_Interface

- Declares `HMS_Protocol` and the transport-neutral abstract contract.
- Exposes `begin`, `end`, unicast `send`, `broadcast`, and asynchronous receive/send callbacks.
- Does not depend on a concrete transport, avoiding a circular component dependency.

### HMS_EspNow

- Implements the abstract interface using ESP-IDF's native ESP-NOW API.
- Initializes Wi-Fi only if it finds Wi-Fi uninitialized; records ownership so it only stops/deinitializes resources it created.
- Adds unicast peers on demand and adds the broadcast peer when needed.
- Validates MAC/data pointers and enforces a 250-byte payload ceiling.
- Copies native callback data into a bounded event queue; invokes application callbacks from a worker task.
- Provides optional encrypted-unicast peer setup via an LMK, but key provisioning remains the application's responsibility.

### Example application

- Configures callbacks, starts the transport, and broadcasts a sequence-numbered heartbeat.
- Prints incoming payload bytes safely without assuming they are null-terminated strings.
- Is deliberately generic: no product-specific payload schema or peer MAC is embedded.

## Protocol selection

The interface contract is the seam for protocol selection. The application chooses a concrete implementation (`HMS_EspNow` today). Do not add a factory inside `HMS_Interface` that directly constructs `HMS_EspNow`, because that would make the abstract component depend on the concrete component and create a dependency cycle. A future app-level factory can map a configuration enum to concrete drivers.

## Deliberate initial constraints

- One active native ESP-NOW driver instance at a time.
- Queue depth and worker stack are configurable.
- The example uses fixed-channel standalone behavior only when it owns Wi-Fi startup.
- Native send callback status is not an application delivery ACK.
