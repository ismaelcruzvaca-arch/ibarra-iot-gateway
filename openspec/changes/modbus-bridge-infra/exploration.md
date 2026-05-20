# Exploration: Modbus-HaLow Bridge Infrastructure for RV1106

## Current State

The ibarra-iot-gateway project is a multi-layered edge IoT system with four distinct layers:

1. **Edge Nodes (ESP32/C++)** — `edge_nodes/norvi_monitor/`
   - Arduino/PlatformIO firmware with FreeRTOS dual-core tasks
   - ADS1115 ADC with EMA filtering, ISR-based debounce, MQTTS (mTLS)
   - LittleFS offline spooling with binary `SpooledEvent` struct
   - JSON serialization via ArduinoJson (`PayloadSerializer.h`)
   - Unity test framework (`test/test_main.cpp`)
   - Header-only pattern for libraries (`EMAFilter.h`, `PayloadSerializer.h`)
   - `#ifdef ARDUINO` / `#else` dual compilation for host tests

2. **Edge Worker (Node.js)** — `services/edge_worker/`
   - MQTT → SQLite ingestion via `better-sqlite3`
   - Dependency injection: `createEdgeWorker({ mqttClient, db, config })`
   - Validates `node_health` field on every payload

3. **Cloud Sync (Node.js)** — `services/cloud_sync/`
   - SQLite → Hasura GraphQL upload with exponential backoff
   - Same DI pattern: `createCloudSync({ db, fetch, config })`

4. **Simulator (Python)** — `simulation/simulator.py`
   - Publishes 7 simulated machines to MQTT with Sparkplug B Lite payload

5. **Infrastructure** — Docker Compose (Mosquitto, Watchtower, shared volumes)

### Key Finding: NO Protobuf or CMake Exists

The entire project currently uses **JSON serialization** (ArduinoJson on ESP32, native JSON in Node.js/Python). There is **zero** protobuf usage, **zero** CMake configuration, and **zero** cross-compilation setup. The Modbus bridge module is entirely greenfield in these dimensions.

### SDD Artifact Patterns

The project uses `openspec/changes/{change-name}/` with:
- `proposal.md` — Quick Path, Details table, Risks & Tradeoffs
- `specs.md` — Immutable Constraints table, Given/When/Then TDD specs
- `tasks.md` — Phases with `- [ ]` / `- [x]` checklists
- `verify-report.md` — Static audit and test mapping

Five archived SDD changes exist: `norvi-concurrent-skeleton`, `norvi-network-ntp`, `norvi-ads1115-ema`, `norvi-deep-persistence`, `norvi-silicon-shield`.

### Coding Conventions

- **C++ headers**: `#ifndef` guard pattern, header-only for testable utilities
- **Enums**: `UPPER_SNAKE_CASE` for C++ enum values (e.g., `STATE_DISCONNECTED`)
- **Structs**: `PascalCase` (e.g., `ProductionEvent`, `SpooledEvent`)
- **Variables**: `camelCase` (e.g., `globalCycleCount`, `productionEventQueue`)
- **Tests**: Unity framework, `test_{descriptive_name}` naming, dual `setup()/main()` blocks
- **Conditional compilation**: `#ifdef ARDUINO` / `#else` for host-testable code
- **Payload**: JSON with `node_health` (string) + `metrics[]` array pattern

---

## Affected Areas

- `edge_ops/modbus_bridge/` — **NEW** directory (does not exist)
- `openspec/changes/modbus-bridge-infra/` — **NEW** SDD artifacts
- `.gitignore` — May need protobuf generated file exclusions
- The Modbus bridge does NOT alter any existing `edge_nodes/norvi_monitor/` code

---

## Approaches

### 1. Two PRs — Binary Contract + Toolchain (as proposed)

**PR #1 — Binary Contract** (`proto/telemetry.proto` + headers + tests):
- `proto/telemetry.proto` with `ModbusBridgePayload` and `OperatingStatus` enum
- Proto-generated C++ headers (or manual structs for host-testability)
- `tests/test_protobuf_contract.cpp` for serialization roundtrip tests
- Estimated: ~150-200 lines

**PR #2 — Cross-Compilation Toolchain + CMake** (`cmake/` + `CMakeLists.txt`):
- `cmake/rv1106_toolchain.cmake`
- Root `CMakeLists.txt` with C++17, protobuf_generate_cpp
- Test integration (CMakeLists for host tests)
- Estimated: ~140-180 lines

- **Pros**: Clean separation of data contract from build system. Each PR independently reviewable. The proto contract is pure definition. The CMake PR validates the proto compiles under cross-compilation.
- **Cons**: PR #2 references proto files from PR #1, so it must branch from PR #1's branch.
- **Effort**: Medium

### 2. Three PRs — Proto + Toolchain + CMakeLists Integration

Split PR #2 further: toolchain-only cmake (PR #2a) and CMakeLists integration with protobuf targets (PR #2b).

- **Pros**: Each PR is ultra-focused. Toolchain is cmake-only, easily auditable. CMakeLists integration ties everything together.
- **Cons**: PR #2b might be too small to stand alone. More coordination overhead.
- **Effort**: Medium-High (more process, same code)

### 3. Single PR with Staged Commits

- **Pros**: Atomic, one review cycle.
- **Cons**: Exceeds 400-line review budget. Mixes contract definition with build system, poor cognitive focus for reviewers.
- **Effort**: Low (but poor review quality)

### Recommendation

**Two PRs as proposed are appropriate and well-sized.** Each comes in under the 400-line budget:

| PR | Content | Est. Lines |
|----|---------|-----------|
| PR #1 | `proto/telemetry.proto`, `include/` headers, `tests/test_protobuf_contract.cpp` | ~150-200 |
| PR #2 | `cmake/rv1106_toolchain.cmake`, root `CMakeLists.txt`, test CMakeLists | ~140-180 |

The second PR should branch from the first PR's branch (Feature Branch Chain pattern) since it references proto files.

---

## Risks

1. **Paradigm shift from JSON to Protobuf**: The project has exclusively used JSON serialization (ArduinoJson, native JSON). Introducing protobuf requires the team to learn proto3 semantics, `protoc` compilation, and C++ generated code patterns. This is the single highest-risk item.

2. **CMake dual-mode requirement**: All tests MUST run on x86_64 host, while production binaries cross-compile for armhf/uClibc on RV1106. The CMakeLists.txt must support both modes (host testing vs. cross-compilation). This requires careful `if(CROSSCOMPILE)` guards and separate test targets that don't use the cross-compiler.

3. **Toolchain binary specificity**: The compiler names `arm-rockchip830-linux-uclibcgnueabihf-gcc/g++` are very specific. `CMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY` is essential to prevent link failures during CMake configure on the host (which lacks the target's libc). This needs CI validation.

4. **Protobuf host dependency**: CI pipeline must install `protobuf-compiler` and `libprotobuf-dev`. Without `protoc`, `protobuf_generate_cpp` will fail. This is a new CI requirement not present in the current PlatformIO-only workflow.

5. **Proto3 field semantics**: Proto3 has no `required` fields and no default values for enums. The `OperatingStatus` enum must handle unknown values. Tag reservation (1-15) for high-frequency fields is correct proto3 practice but must be documented.

6. **Branch management**: Work should be on `feature/modbus-bridge` (new branch from `main`), independent from the existing `feature/norvi-firmware` branch. The two PRs should use Feature Branch Chain (PR #2 depends on PR #1).

7. **No existing test harness for CMake/C++**: Unlike the ESP32 code which has PlatformIO's `pio test`, the new CMake-based module needs its own test runner setup (likely CTest + GoogleTest or Catch2). This is additional infrastructure not present in the project.

---

## Design Considerations for Modbus Bridge on RV1106

1. **OperatingStatus enum design**: The requirement says "not rigid booleans" — this is architecturally sound. An enum like `UNKNOWN=0, STARTING=1, RUNNING=2, FAULT=3, STOPPED=4, MAINTENANCE=5` provides richer state than a boolean `is_running`. Proto3 enums default to 0 (first value), so `UNKNOWN=0` is the safe default.

2. **Separate machine cycles from hardware health**: The requirement to separate `ModbusBridgePayload` (NORVI states with cycle_count) from `HardwareHealthPayload` (cpu_temperature, ram_used_bytes, system_load) is correct — these are different telemetry cadences. Machine cycles come at production speed; health metrics are lower frequency.

3. **Flat message design (repeated fields)**: Using `repeated NorviState` inside `ModbusBridgePayload` avoids nested oneofs and keeps serialization simple and backward-compatible.

4. **Header separation (src/ vs include/)**: The project convention uses `src/` for headers and `include/` for secrets/platform configs. For the new module, `include/` should hold the public API headers and `src/` the implementation — matching standard CMake conventions and enabling clean `target_include_directories`.

5. **TDD on host**: The `#ifdef ARDUINO` / `#else` pattern from norvi_monitor is NOT applicable here since the Modbus bridge is NOT Arduino firmware. All tests are pure C++ (host-side). Use CTest + a modern test framework (Catch2 or GoogleTest).

---

## Ready for Proposal

**Yes.** The exploration is complete. The orchestrator should proceed to `sdd-propose` for the `modbus-bridge-infra` change. Key decisions for the proposal:

- Confirm two PRs (Binary Contract, then CMake Toolchain)
- Confirm proto3 syntax with tag reservation 1-15
- Confirm `OperatingStatus` enum values (propose concrete set)
- Confirm test framework for host C++ tests (recommend Catch2 for simplicity)
- Confirm branch strategy: `feature/modbus-bridge` from `main`, Feature Branch Chain for PR #2