# Delta Specs: modbus-engine-rtu

This change introduces one **new** capability (no existing specs are modified).

## ADDED Capabilities

### Capability: modbus-rtu-engine

> Full spec: `specs/modbus-rtu-engine/spec.md`

Modbus RTU master protocol engine — serial abstraction (`ISerialPort`), FC04 read input registers, CRC-16/Modbus (polynomial 0xA001), response parsing, timeout and exception code handling. TDD-enabled via `MockSerialPort` + Catch2.
