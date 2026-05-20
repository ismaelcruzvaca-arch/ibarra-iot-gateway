# Modbus RTU Engine Specification

## Purpose

Defines the Modbus RTU master protocol engine for reading input registers (Function Code 04) from industrial slave devices over RS-485 serial. Enables TDD via mock serial port abstraction before hardware integration.

## Requirements

### Requirement: ISerialPort Interface

The system MUST define a pure virtual `ISerialPort` interface with three methods: `write(const std::vector<uint8_t>&)`, `read(std::vector<uint8_t>&, size_t max_bytes)`, and `set_timeout(int ms)`.

#### Scenario: Interface contract enforces override

- GIVEN a class inheriting `ISerialPort`
- WHEN any of `write`, `read`, or `set_timeout` is not overridden
- THEN compilation MUST fail with an unimplemented pure virtual error

### Requirement: CRC-16/Modbus Calculation

The system MUST calculate CRC-16 using polynomial 0xA001 (reflected), initial value 0xFFFF, no final XOR, LSB-first, per the Modbus RTU specification.

#### Scenario: Known byte sequence CRC

- GIVEN the Modbus RTU frame body `01 04 00 00 00 02`
- WHEN CRC-16/Modbus is computed
- THEN the 16-bit result MUST be `0x71CB` (wire order: `CB 71`)

### Requirement: ModbusRTUMaster::read_input_registers

The system MUST provide `ModbusRTUMaster::read_input_registers(uint8_t slave_id, uint16_t start_addr, uint16_t quantity)` that assembles an FC04 request frame, sends it via `ISerialPort::write()`, reads the response, validates CRC, and returns the register values.

#### Scenario: Happy path — valid 3-register response

- GIVEN a `ModbusRTUMaster` connected to a mock that enqueues response `01 04 06 00 0A 00 14 00 1E CRCLO CRCHI`
- WHEN `read_input_registers(1, 0, 3)` is called
- THEN the result MUST equal `{10, 20, 30}`

#### Scenario: Request frame assembly correctness

- GIVEN slave ID=1, start_addr=0, quantity=2
- WHEN the request frame is assembled
- THEN the wire bytes MUST be `01 04 00 00 00 02 71 CB`

### Requirement: Response CRC Validation

The system MUST validate the trailing CRC-16 of every response frame before accepting the register payload.

#### Scenario: CRC mismatch raises error

- GIVEN a mock returning a response with an invalid CRC
- WHEN `read_input_registers()` is called
- THEN the operation MUST fail with a CRC validation error

#### Scenario: Valid CRC completes successfully

- GIVEN a mock returning a response with a correct CRC
- WHEN `read_input_registers()` is called
- THEN the operation MUST succeed and return parsed register values

### Requirement: Timeout Handling

The system MUST propagate serial port timeouts as recoverable errors.

#### Scenario: Serial read timeout

- GIVEN a mock whose `read()` blocks past the configured timeout
- WHEN `read_input_registers()` is called
- THEN the operation MUST fail with a timeout error

### Requirement: Modbus Exception Code Handling

The system MUST detect and surface Modbus exception codes 01–04 (illegal function, illegal data address, illegal data value, slave device failure) from response frames.

#### Scenario: Exception code 02 — Illegal data address

- GIVEN a mock returning response `01 84 02 CRCLO CRCHI`
- WHEN `read_input_registers()` is called
- THEN the operation MUST indicate "illegal data address" (exception code 0x02)

### Requirement: MockSerialPort for TDD

The system MUST provide a `MockSerialPort` that records written bytes and delivers pre-enqueued simulated responses, enabling Catch2 tests without hardware.

#### Scenario: Write interception

- GIVEN a `MockSerialPort`
- WHEN `ModbusRTUMaster` writes an FC04 request frame
- THEN the test MAY retrieve the exact written bytes for assertion

#### Scenario: Enqueued response injection

- GIVEN a `MockSerialPort` with a simulated valid-CRC response enqueued
- WHEN `ModbusRTUMaster::read_input_registers()` is called
- THEN the returned register values MUST match the simulated response payload
