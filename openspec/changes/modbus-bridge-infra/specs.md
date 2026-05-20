# Delta Specs: modbus-bridge-infra

This change introduces two **new** capabilities (no existing specs are modified).

## ADDED Capabilities

### Capability: protobuf-contract

> Full spec: `specs/protobuf-contract/spec.md`

Proto3 binary contract for bridge telemetry — `ModbusBridgePayload`, `HardwareHealthPayload`, `OperatingStatus` enum, tag reservation, and Catch2 roundtrip TDD scenarios.

### Capability: cross-compilation-toolchain

> Full spec: `specs/cross-compilation-toolchain/spec.md`

CMake dual-mode build system for RV1106 armhf/uClibc — toolchain file, CROSSCOMPILE toggle, protobuf code generation, and CTest integration.