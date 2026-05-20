# Design: Modbus RTU Engine

## Technical Approach

Greenfield C++ classes in `edge_ops/modbus_bridge/` implementing a Modbus RTU master for FC04 (Read Input Registers). `ISerialPort` decouples the protocol engine from physical UART. `ModbusRTUMaster` injects the serial port at construction, assembles RTU frames, calculates CRC-16, sends requests, parses responses, and validates CRC. A `MockSerialPort` enables Catch2 TDD. Errors surface as a lightweight exception hierarchy.

## Architecture Decisions

| Decision | Choice | Rejected | Rationale |
|---|---|---|---|
| Serial abstraction | `ISerialPort` pure virtual interface | Template-based policy | Runtime polymorphism is idiomatic C++, allows easy mocking, and keeps headers clean |
| Error handling | Custom `ModbusError` exception hierarchy | `std::expected`/error codes | Proposal explicitly specifies throwing on failures; exceptions are clear for a single-call API and map naturally to Catch2 test assertions |
| CRC-16 | Bitwise loop (no LUT) | 256-byte lookup table | Bitwise is auditable, trivial to verify against Modbus spec example, and fast enough for 8-byte frames |
| Mock design | Two-queue `MockSerialPort` (write log + response queue) | Global state / macro stubs | Queue-based mock supports multiple sequential transactions in one test and requires zero test-framework coupling |
| Namespace | `ibarra::modbus` | Global namespace | Matches existing `ibarra::telemetry` protobuf convention |

## Data Flow

```
read_input_registers(slave_id, start_addr, quantity)
  │
  ├─► Assemble request: [addr][0x04][start_hi][start_lo][qty_hi][qty_lo]
  │   Compute CRC-16 → append [CRClo][CRChi]
  │
  ├─► ISerialPort::write(request)
  │
  ├─► ISerialPort::read(response, 5 + 2*quantity + 2)
  │
  ├─► Validate trailing CRC-16
  │
  ├─► Detect exception frame (function code with MSB set)
  │   └─► throw ModbusResponseError(code)
  │
  └─► Parse payload bytes into vector<uint16_t> (big-endian)
```

## File Changes

| File | Action | Description |
|------|--------|-------------|
| `include/ISerialPort.hpp` | Create | Pure virtual serial port interface |
| `include/ModbusRTUMaster.hpp` | Create | RTU master class + exception types |
| `src/ModbusRTUMaster.cpp` | Create | Frame assembly, CRC-16, response parsing |
| `tests/test_modbus_engine.cpp` | Create | Catch2 tests with MockSerialPort |
| `CMakeLists.txt` | Modify | Add `ModbusRTUMaster.cpp` to lib + test targets; add `test_modbus_engine.cpp` to test target |

## Interfaces / Contracts

```cpp
namespace ibarra::modbus {

class ISerialPort {
public:
    virtual ~ISerialPort() = default;
    virtual void write(const std::vector<uint8_t>& data) = 0;
    virtual bool read(std::vector<uint8_t>& out, size_t max_bytes) = 0;
    virtual void set_timeout(int ms) = 0;
};

struct ModbusError : public std::runtime_error {
    using std::runtime_error::runtime_error;
};
struct ModbusCRCError : public ModbusError {
    using ModbusError::ModbusError;
};
struct ModbusTimeoutError : public ModbusError {
    using ModbusError::ModbusError;
};
struct ModbusResponseError : public ModbusError {
    uint8_t code;
    ModbusResponseError(uint8_t c)
        : ModbusError("Modbus exception"), code(c) {}
};

class ModbusRTUMaster {
public:
    explicit ModbusRTUMaster(ISerialPort& port);
    std::vector<uint16_t> read_input_registers(
        uint8_t slave_id,
        uint16_t start_addr,
        uint16_t quantity);
private:
    ISerialPort& port_;
    uint16_t crc16(const std::vector<uint8_t>& data);
};

} // namespace ibarra::modbus
```

## Testing Strategy

| Layer | What to Test | Approach |
|-------|-------------|----------|
| Unit | CRC-16 known value (`0x71CB`) | Direct call to private helper or free function |
| Unit | Request frame assembly | MockSerialPort write log inspection |
| Unit | Happy-path response parsing | Enqueue valid response, assert registers |
| Unit | CRC mismatch | Enqueue bad-CRC response, assert `ModbusCRCError` thrown |
| Unit | Timeout propagation | Mock returning false on read, assert `ModbusTimeoutError` |
| Unit | Exception codes 01–04 | Enqueue `0x81`/`0x82`/`0x83`/`0x84` frames, assert `ModbusResponseError::code` |
| Integration | CMake/CTest registration | Host build runs `test_runner` including new suite |

## Migration / Rollout

No migration required. Purely additive.

## Open Questions

None.
