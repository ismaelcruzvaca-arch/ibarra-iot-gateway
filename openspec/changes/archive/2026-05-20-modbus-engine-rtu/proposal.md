# Proposal: Modbus RTU Engine

## Intent

Add a Modbus RTU polling engine to `edge_ops/modbus_bridge/` so the gateway can read input registers from industrial Modbus RTU slave devices over serial (RS-485). The engine must be testable in isolation via a mock serial port, enabling TDD before hardware integration.

## Scope

### In Scope
- `ISerialPort` abstract interface (`write`, `read`, `set_timeout`)
- `ModbusRTUMaster` class with `read_input_registers()` (Function 04)
- CRC-16/Modbus (polynomial 0xA001) frame assembly and validation
- Response parsing with CRC validation and exception codes
- `MockSerialPort` for TDD with Catch2 test suite
- CMakeLists.txt update — add engine sources to both library and test targets

### Out of Scope
- Modbus functions other than FC04 (FC01/FC02/FC03/FC05/FC06/FC15/FC16 deferred)
- TCP/RTU-over-TCP gateway mode
- Polling scheduler or multi-slave orchestration
- RS-485 direction control (RTS toggling) — deferred to integration layer

## Capabilities

### New Capabilities
- `modbus-rtu-engine`: Modbus RTU master protocol engine — serial abstraction, FC04 read, CRC-16, exception handling

### Modified Capabilities
None — this is additive to the existing infrastructure.

## Approach

Greenfield C++ classes within `edge_ops/modbus_bridge/`. `ISerialPort` is a pure virtual interface injected into `ModbusRTUMaster` via constructor. `read_input_registers()` assembles the RTU frame (slave addr + FC04 + start addr + quantity + CRC-16), sends via `ISerialPort::write()`, parses the response frame, validates CRC, and returns register values or throws on exception/CRC mismatch. All logic tested with `MockSerialPort` + Catch2.

## Affected Areas

| Area | Impact | Description |
|------|--------|-------------|
| `edge_ops/modbus_bridge/include/ISerialPort.hpp` | New | Abstract serial port interface |
| `edge_ops/modbus_bridge/include/ModbusRTUMaster.hpp` | New | RTU master class declaration |
| `edge_ops/modbus_bridge/src/ModbusRTUMaster.cpp` | New | RTU engine implementation (CRC, framing, parsing) |
| `edge_ops/modbus_bridge/tests/test_modbus_engine.cpp` | New | Catch2 tests with MockSerialPort |
| `edge_ops/modbus_bridge/CMakeLists.txt` | Modified | Add engine sources to lib + test targets |

## Risks

| Risk | Likelihood | Mitigation |
|------|------------|------------|
| Serial port timing (RS-485 inter-frame spacing) | Medium | `set_timeout()` exposed in interface; precise timing deferred to integration |
| CRC-16 bit order (LSB-first 0xA001 vs MSB-first 0x8005) | Low | Verified against Modbus specification worked example; unit tests cover known byte sequences |
| Exception code handling incomplete | Low | Spec covers all 4 standard Modbus exception codes (01–04) |

## Rollback Plan

Revert `CMakeLists.txt` to previous revision. Delete the four new files. No existing functionality is modified.

## Dependencies

- Existing Catch2 vendored header (already in `tests/catch2/`)
- No external serial library — `ISerialPort` abstracts the dependency

## Success Criteria

- [ ] All Catch2 `test_modbus_engine` tests pass (frame assembly, CRC, parsing, exceptions)
- [ ] `read_input_registers()` returns correct values for known-good response bytes
- [ ] CRC mismatch on response triggers explicit error
- [ ] Modbus exception codes 01–04 are parsed and surfaced
- [ ] Host-mode CMake build succeeds with new tests registered in CTest
