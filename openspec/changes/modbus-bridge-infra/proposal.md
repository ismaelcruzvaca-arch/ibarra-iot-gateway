# Proposal: Modbus-HaLow Bridge Infrastructure for RV1106

This proposal establishes the protobuf binary contract and cross-compilation toolchain for a Modbus-HaLow bridge targeting the RV1106 (armhf/uClibc). It introduces protobuf3 serialization to replace JSON for the new bridge module and creates a dual-mode CMake build that compiles host-side tests on x86_64 while cross-compiling production binaries for the RV1106 target.

## Quick Path

1. **Define the Binary Contract**: Create `proto/telemetry.proto` with `ModbusBridgePayload`, `HardwareHealthPayload`, and `OperatingStatus` enum. Reserve tags 1-15 for high-frequency fields.
2. **Write Contract Tests**: TDD — write `tests/test_protobuf_contract.cpp` with Catch2 before any implementation logic. Roundtrip serialization tests must pass.
3. **Build the Cross-Compilation Toolchain**: Create `cmake/rv1106_toolchain.cmake` with `arm-rockchip830-linux-uclibcgnueabihf-gcc/g++`, `CMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY`, and C++17 standard.
4. **Wire Root CMakeLists.txt**: Dual-mode build — host test targets (Catch2 + CTest) and cross-compile production targets with `protobuf_generate_cpp`.
5. **Write Toolchain Tests**: TDD — verify CMake configure succeeds in both modes and proto sources compile.

## Details

| Topic | Decision |
|-------|----------|
| **Serialization format** | Proto3 — flat messages with `repeated` fields, no `oneof` nesting |
| **Tag reservation** | Tags 1-15 reserved for high-frequency fields (single-byte varint encoding) |
| **OperatingStatus enum** | `UNKNOWN=0, STARTING=1, RUNNING=2, FAULT=3, STOPPED=4, MAINTENANCE=5` |
| **Payload separation** | `ModbusBridgePayload` (NORVI states + cycle counts) separate from `HardwareHealthPayload` (CPU temp, RAM, load) — different telemetry cadences |
| **Target triple** | `arm-rockchip830-linux-uclibcgnueabihf` (RV1106 SDK toolchain) |
| **C++ standard** | C++17 (required by modern protobuf runtime) |
| **Test framework** | Catch2 (single header, no CMake integration complexity, matches project's header-only ethos) |
| **Branch strategy** | `feature/modbus-bridge` from `main`; Feature Branch Chain for PR #2 |
| **Build mode guard** | `CROSSCOMPILE` cache variable toggles host-test vs cross-compile targets |

## Capabilities

### New Capabilities
- `protobuf-contract`: Proto3 message definitions, enum, and tag reservation contract for bridge telemetry
- `cross-compilation-toolchain`: CMake dual-mode build system for RV1106 armhf/uClibc with host-side test targets

### Modified Capabilities
None — this is greenfield; no existing specs are altered.

## Approach

Two independent PRs via Feature Branch Chain:

**PR #1 — Binary Contract** (~150-200 lines): `proto/telemetry.proto`, `include/` public headers, `tests/test_protobuf_contract.cpp` with Catch2 roundtrip tests.

**PR #2 — Cross-Compilation Toolchain** (~140-180 lines): `cmake/rv1106_toolchain.cmake`, root `CMakeLists.txt` with `protobuf_generate_cpp` integration and CTest, branching from PR #1's branch since it references proto files.

## Affected Areas

| Area | Impact | Description |
|------|--------|-------------|
| `edge_ops/modbus_bridge/proto/` | New | Proto3 definitions |
| `edge_ops/modbus_bridge/include/` | New | Public C++ headers |
| `edge_ops/modbus_bridge/tests/` | New | Catch2 test suite |
| `edge_ops/modbus_bridge/cmake/` | New | RV1106 toolchain file |
| `edge_ops/modbus_bridge/CMakeLists.txt` | New | Root build with dual-mode |
| `.gitignore` | Modified | Add `*.pb.cc`, `*.pb.h` exclusions |

## Risks

| Risk | Likelihood | Mitigation |
|------|------------|------------|
| JSON → Protobuf paradigm shift for the team | High | Document proto3 semantics PR #1; provide inline comments on tag reservation and enum defaults |
| CMake dual-mode complexity (host test vs cross-compile) | Medium | `CROSSCOMPILE` cache variable gates mode; host mode never invokes cross-compiler |
| `protoc` CI dependency (not in current PlatformIO workflow) | Medium | Install `protobuf-compiler` + `libprotobuf-dev` in CI; document in toolchain README |
| Toolchain binary names are SDK-specific and rigid | Low | Hardcode in toolchain file per RV1106 SDK convention; `CMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY` prevents link failures |
| Proto3 no `required` fields — silent zero-values | Medium | Reserve tags 1-15 for high-frequency; explicit `UNKNOWN=0` default; tests verify zero-value handling |

## Rollback Plan

Delete `edge_ops/modbus_bridge/` directory. Remove `.gitignore` protobuf exclusions. No existing code is modified — rollback is clean deletion.

## Dependencies

- `protobuf-compiler` and `libprotobuf-dev` on CI host
- RV1106 SDK cross-compiler toolchain (`arm-rockchip830-linux-uclibcgnueabihf-gcc`)
- Catch2 single-header (vendored in `tests/`)

## Success Criteria

- [ ] `protoc` compiles `telemetry.proto` without errors
- [ ] Catch2 roundtrip tests pass: serialize → deserialize → field equality
- [ ] CMake configure succeeds in host mode (tests compile and run)
- [ ] CMake configure succeeds in cross-compile mode (production target builds)
- [ ] Each PR stays under 400 changed lines
- [ ] No modifications to `edge_nodes/norvi_monitor/` code