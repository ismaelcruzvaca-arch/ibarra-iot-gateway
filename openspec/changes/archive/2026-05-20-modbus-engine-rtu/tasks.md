# Tasks: Modbus RTU Engine

## Review Workload Forecast

| Field | Value |
|-------|-------|
| Estimated changed lines | ~250–300 |
| 400-line budget risk | Low |
| Chained PRs recommended | No |
| Suggested split | Single PR |
| Delivery strategy | single-pr |

Decision needed before apply: Yes
Chained PRs recommended: No
Chain strategy: size-exception
400-line budget risk: Low

### Suggested Work Units

| Unit | Goal | Likely PR | Notes |
|------|------|-----------|-------|
| 1 | Full Modbus RTU engine: ISerialPort + MockSerialPort + CRC-16 + ModbusRTUMaster + CMake | PR 1 | Single commit on `feature/modbus-bridge`; all tests included |

## Phase 1: ISerialPort Interface

- [x] 1.1 RED — Add test to `tests/test_modbus_engine.cpp` that inherits `ISerialPort` without overriding all pure virtuals (compile failure expected)
- [x] 1.2 GREEN — Create `include/ISerialPort.hpp` with `write()`, `read()`, `set_timeout()` pure virtuals in `ibarra::modbus`
- [x] 1.3 TRIANGULATE — Add compile-test verifying a complete override subclass compiles
- [x] 1.4 REFACTOR — Ensure include guards, proper dtor, namespace consistency

## Phase 2: MockSerialPort

- [x] 2.1 RED — Write test creating `MockSerialPort`, calling `write()` and `read()`, asserting enqueued response returned — won't compile
- [x] 2.2 GREEN — Implement `MockSerialPort` (in test file) with `write_log` vector + `response_queue` deque, `read()` pops front into `out`
- [x] 2.3 TRIANGULATE — Add multi-transaction and empty-queue edge case tests
- [x] 2.4 REFACTOR — Extract `MockSerialPort` to reusable helper section, clean up test scaffolding

## Phase 3: CRC-16/Modbus

- [x] 3.1 RED — Write CRC test asserting `0x71CB` for frame `01 04 00 00 00 02` — won't link
- [x] 3.2 GREEN — Implement `crc16()` bitwise loop with poly `0xA001`, init `0xFFFF`, no final XOR, LSB-first in `ModbusRTUMaster.cpp`
- [x] 3.3 TRIANGULATE — Add CRC tests for empty input, single byte, max frame size
- [x] 3.4 REFACTOR — Extract CRC to a local utility function, add doc comment referencing Modbus spec

## Phase 4: ModbusRTUMaster

- [x] 4.1 RED — Write happy-path test: enqueue valid 3-register response `01 04 06 00 0A 00 14 00 1E CRCLO CRCHI`, assert `{10, 20, 30}`
- [x] 4.2 GREEN — Implement `read_input_registers()`: frame assembly, CRC append, `write()`, `read()`, CRC validation, big-endian parsing
- [x] 4.3 TRIANGULATE — Add tests: exception codes 01–04, CRC mismatch (`ModbusCRCError`), timeout propagation (`ModbusTimeoutError`), request frame byte assertion
- [x] 4.4 REFACTOR — Update `CMakeLists.txt` (add `src/ModbusRTUMaster.cpp` to library, `tests/test_modbus_engine.cpp` to test target), final cleanup
