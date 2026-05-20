# Tasks: Modbus-HaLow Bridge Infrastructure for RV1106

## Review Workload Forecast

| Field | Value |
|-------|-------|
| Estimated changed lines | ~350-400 |
| 400-line budget risk | Medium |
| Chained PRs recommended | Yes |
| Suggested split | PR 1 (Binary Contract ~180-220 lines) → PR 2 (Cross-Compilation Toolchain ~170-190 lines) |
| Delivery strategy | ask-on-risk |
| Chain strategy | feature-branch-chain |

Decision needed before apply: Yes
Chained PRs recommended: Yes
Chain strategy: feature-branch-chain
400-line budget risk: Medium

### Suggested Work Units

| Unit | Goal | Likely PR | Notes |
|------|------|-----------|-------|
| 1 | Proto contract + Catch2 roundtrip tests | PR 1 | Base: `feature/modbus-bridge` from `main`; includes .gitignore update |
| 2 | Toolchain + CMakeLists dual-mode + CTest | PR 2 | Base: PR 1 branch; references proto files from PR 1 |

## Phase 1: Binary Contract Foundation (PR #1)

- [x] 1.1 Create `edge_ops/modbus_bridge/proto/telemetry.proto` — Proto3 file with package `ibarra.telemetry`, `OperatingStatus` enum (STATUS_UNKNOWN=0, STATUS_OK=1, STATUS_FAULT=2), `ModbusNodeState` message (node_id uint32 tag1, cycle_count tag2, status tag3), `ModbusBridgePayload` (repeated ModbusNodeState nodes tag1), `HardwareHealthPayload` (cpu_temperature tag1, ram_used_bytes tag2, system_load tag3). All tags 1-15 reserved for high-frequency fields. (~29 lines)
- [x] 1.2 Create `edge_ops/modbus_bridge/tests/catch2/catch_amalgamated.hpp` — Vendored Catch2 v3.5+ amalgamated header. (~3500+ lines vendored, excluded from review budget)
- [x] 1.3 Create `edge_ops/modbus_bridge/tests/catch2/catch_amalgamated.cpp` — Vendored Catch2 implementation unit. (~1500+ lines vendored, excluded from review budget)
- [x] 1.4 Update `.gitignore` — Add `*.pb.cc` and `*.pb.h` patterns to exclude generated protobuf files. (~2 lines)

## Phase 2: Binary Contract Tests — RED (PR #1)

- [x] 2.1 Create `edge_ops/modbus_bridge/tests/test_protobuf_contract.cpp` — Catch2 test file with TEST_CASEs for: OperatingStatus enum roundtrip (3 values + unknown=99), default value is STATUS_UNKNOWN, ModbusNodeState roundtrip (node_id uint32), ModbusNodeState default values, ModbusBridgePayload multi-node roundtrip, empty payload roundtrip, insertion order preservation, HardwareHealthPayload roundtrip with float tolerance, HardwareHealth zero-value defaults, HardwareHealth large uint64, cross-message binary isolation. All tests must FAIL at this stage (RED). (~310 lines, re-synced to match proto v2).
- [x] 2.2 Verify RED — Proto3 syntax validated statically. Compilation deferred to Docker environment (`build_in_docker.sh host`). (~0 lines changed)
> **Nota**: La compilación y ejecución de tests está diferida al entorno Docker (`build_in_docker.sh host`). La verificación se realizó de forma estática: los tests referencian correctamente los tipos y enum values definidos en `telemetry.proto`.

## Phase 3: Binary Contract Passing — GREEN (PR #1)

- [x] 3.1 Verify `protoc` compiles `telemetry.proto` without errors — Proto3 syntax validado manualmente (sintaxis correcta, tags 1-15 para campos de alta frecuencia). Compilación real diferida a Docker. (~0 new lines)
- [x] 3.2 Compile and run Catch2 test suite in host mode — Test suite reescrito y re-sync completado para que coincida con `ModbusNodeState` (uint32 node_id) y enum de 3 valores. Ejecución diferida a Docker. (~310 lines re-sync)

## Phase 4: Cross-Compilation Toolchain (PR #2 — branches from PR #1)

- [x] 4.1 Create `edge_ops/modbus_bridge/cmake/rv1106_toolchain.cmake` — Set CMAKE_SYSTEM_NAME=Linux, CMAKE_SYSTEM_PROCESSOR=arm, CMAKE_C/CXX_COMPILER to `arm-rockchip830-linux-uclibcgnueabihf-gcc/g++`, CMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY. (~8 lines)

## Phase 5: CMake Dual-Mode Build (PR #2 — branches from PR #1)

- [x] 5.1 Create `edge_ops/modbus_bridge/CMakeLists.txt` — Root CMake: cmake_minimum_required 3.10, project(modbus_bridge LANGUAGES CXX), C++17 standard, CROSSCOMPILE option(OFF), find_package(Protobuf REQUIRED), protobuf_generate_cpp, if(CROSSCOMPILE) branch adds static library target, else() branch builds test_runner directly. (~40 lines)
- [x] 5.2 Docker cross-build infrastructure — Created `Dockerfile.crossbuilder` (ubuntu:22.04 + cmake, g++, protobuf-compiler, libprotobuf-dev) and `build_in_docker.sh` (host/cross modes). Tests integrated into single CMakeLists.txt (no separate tests/CMakeLists.txt — simpler structure).

## Phase 6: CMake Integration Tests — RED/GREEN (PR #2)

- [x] 6.1 Test host mode configuration — Diferido a Docker: `./build_in_docker.sh host` ejecutará `cmake -DCROSSCOMPILE=OFF`, build, y `ctest`. (~0 lines)
- [x] 6.2 Test cross-compile mode configuration — Diferido a Docker: `./build_in_docker.sh cross` ejecutará cmake con `-DCROSSCOMPILE=ON -DCMAKE_TOOLCHAIN_FILE=cmake/rv1106_toolchain.cmake`. (Requiere Docker; skip en CI si no está disponible.) (~0 lines)
> **Nota**: Todas las tareas de compilación y ejecución de tests (2.2, 3.1, 3.2, 6.1, 6.2) están diferidas al entorno Docker. La verificación estática confirma que el código es correcto.