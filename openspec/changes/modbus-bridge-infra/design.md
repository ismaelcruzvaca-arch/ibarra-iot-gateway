# Design: Modbus-HaLow Bridge Infrastructure for RV1106

## Technical Approach

Greenfield module at `edge_ops/modbus_bridge/` introducing proto3 binary serialization and a CMake dual-mode build. Two chained PRs: PR #1 delivers the proto contract + Catch2 roundtrip tests; PR #2 delivers the toolchain + CMakeLists integration. The module sits alongside `edge_nodes/norvi_monitor/` without modifying it — a parallel edge module for the RV1106 Linux target.

## Architecture Decisions

| Decision | Choice | Rejected | Rationale |
|----------|--------|----------|-----------|
| Serialization | Proto3 flat messages | JSON (ArduinoJson), flatbuffers | Proto3 gives compact binary, forward-compat, tag reservation for perf; flatbuffers overkill for this payload size |
| Tag allocation | 1–15 reserved for high-frequency | Random/explicit-only | Single-byte varint encoding for fields on every cycle — measurable bandwidth savings on constrained link |
| Test framework | Catch2 single-header | GoogleTest, Unity | Catch2 matches project's header-only ethos; no CMake integration complexity; single file vendoring; Unity is ESP32-centric |
| Build system | CMake with CROSSCOMPILE toggle | PlatformIO, Makefile | PlatformIO is ESP32-only; CMake is standard for Linux cross-compilation; dual-mode needs conditional target inclusion |
| Payload split | ModbusBridgePayload + HardwareHealthPayload | Single monolithic message | Different cadences (machine cycles vs health metrics), independent serialization boundaries, different producers |
| Branch chain | feature/modbus-bridge from main | Single PR, three PRs | Two PRs fit the 400-line review budget; PR #2 chains from PR #1 since it references proto files |

## Data Flow

```
telemetry.proto ──┬──► protoc ──► telemetry.pb.cc/.pb.h ──► modbus_bridge_lib (static)
                  │                                           │
                  │                          CROSSCOMPILE=ON  │  CROSSCOMPILE=OFF
                  │                          ─────────────────┼─────────────────
                  │                          Production target│ Test targets
                  │                          (armhf binary)  │ (Catch2 + CTest)
                  │                                           │
                  └──► tests/test_protobuf_contract.cpp ───────┘
                       (uses generated headers in host mode only)
```

## File Changes

| File | Action | Description |
|------|--------|-------------|
| `edge_ops/modbus_bridge/proto/telemetry.proto` | Create | Proto3 definitions: OperatingStatus (3 values), ModbusNodeState, ModbusBridgePayload, HardwareHealthPayload |
| `edge_ops/modbus_bridge/tests/test_protobuf_contract.cpp` | Create | Catch2 roundtrip tests for all messages and scenarios from spec |
| `edge_ops/modbus_bridge/tests/catch2/catch_amalgamated.hpp` | Create | Vendored Catch2 single-header (amalgamated) |
| `edge_ops/modbus_bridge/tests/catch2/catch_amalgamated.cpp` | Create | Vendored Catch2 implementation unit |
| `edge_ops/modbus_bridge/tests/CMakeLists.txt` | Create | Test executable target: links protobuf + catch2, registers with CTest |
| `edge_ops/modbus_bridge/cmake/rv1106_toolchain.cmake` | Create | Cross-compile toolchain: system name/processor, compiler paths, try-compile guard |
| `edge_ops/modbus_bridge/CMakeLists.txt` | Create | Root CMake: dual-mode build, protobuf_generate_cpp, C++17 standard, conditional tests |
| `.gitignore` | Modify | Add `*.pb.cc`, `*.pb.h` exclusions for generated protobuf files |

## Interfaces / Contracts

### telemetry.proto (PR #1)

```protobuf
syntax = "proto3";

package ibarra.telemetry;

// Operating status enum — STATUS_UNKNOWN=0 is the proto3 safe default.
enum OperatingStatus {
  STATUS_UNKNOWN = 0;
  STATUS_OK      = 1;
  STATUS_FAULT   = 2;
}

// Single Modbus node state — one per monitored device.
// Tags 1-15 reserved for high-frequency fields (single-byte varint).
message ModbusNodeState {
  uint32          node_id     = 1;  // Unique node identifier
  uint32          cycle_count = 2;  // Production cycle counter
  OperatingStatus status      = 3;  // Current operating state
}

// Bridge telemetry payload — machine cycle data at production cadence.
// Separated from hardware health due to different producers and cadences.
message ModbusBridgePayload {
  repeated ModbusNodeState nodes = 1;  // Collection of Modbus node states
}

// Hardware health payload — system metrics at lower cadence.
// Independent serialization boundary from ModbusBridgePayload.
message HardwareHealthPayload {
  float  cpu_temperature = 1;  // Degrees Celsius
  uint64 ram_used_bytes  = 2;  // Bytes of RAM in use
  float  system_load     = 3;  // Load average (1-minute)
}
```

### CMakeLists.txt (PR #2) — Key Structure

```cmake
cmake_minimum_required(VERSION 3.16)
project(modbus_bridge LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# CROSSCOMPILE toggle: OFF = host tests, ON = armhf production
option(CROSSCOMPILE "Cross-compile for RV1106" OFF)

# Proto generation (both modes need the .proto, only CROSSCOMPILE=ON
# generates and compiles the static library target)
find_package(Protobuf REQUIRED)
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS proto/telemetry.proto)

if(CROSSCOMPILE)
  # Production target: static library with generated protobuf sources
  add_library(modbus_bridge_lib STATIC ${PROTO_SRCS})
  target_include_directories(modbus_bridge_lib PUBLIC
    ${CMAKE_CURRENT_BINARY_DIR}  # Generated .pb.h location
    include)
  target_link_libraries(modbus_bridge_lib PUBLIC protobuf::libprotobuf)
else()
  # Host mode: test targets only
  add_subdirectory(tests)
endif()
```

### rv1106_toolchain.cmake (PR #2)

```cmake
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_C_COMPILER arm-rockchip830-linux-uclibcgnueabihf-gcc)
set(CMAKE_CXX_COMPILER arm-rockchip830-linux-uclibcgnueabihf-g++)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
```

## Testing Strategy

| Layer | What | Approach |
|-------|------|----------|
| Unit | OperatingStatus enum roundtrip (all 3 values + unknown=99) | Catch2 `TEST_CASE` — serialize ModbusNodeState, deserialize, assert equality |
| Unit | ModbusNodeState default values (node_id=0, cycle_count=0, status=STATUS_UNKNOWN) | Catch2 — construct default, verify proto3 zero-value defaults |
| Unit | ModbusBridgePayload with 0 and N nodes, insertion order | Catch2 — roundtrip, assert size and order |
| Unit | HardwareHealthPayload roundtrip including float tolerance, large uint64 | Catch2 — `Approx()` for floats, exact for uint64 |
| Integration | Host-mode CMake configure + build + CTest discovery | CMake `add_test()` + `Catch2RegisterTests` — verifies dual-mode toggle |
| Integration | Cross-compile CMake configure produces production target | CMake configure check — compiler found, STATIC_LIBRARY guard works |
| Integration | Missing cross-compiler emits FATAL_ERROR | CMake configure test — unset compiler, expect abort |

## Branch Strategy

```
main
 └── feature/modbus-bridge          ← PR #1: proto contract + tests
      └── feature/modbus-bridge-cmake ← PR #2: toolchain + CMakeLists (targets PR #1 branch)
```

## Open Questions

- [ ] Confirm vendored Catch2 version (recommend v3.5+ amalgamated) and commit policy (vendor full or submodule)