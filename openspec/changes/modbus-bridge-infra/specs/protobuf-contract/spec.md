# Protobuf Contract Specification

## Purpose

Defines the proto3 binary serialization contract for the Modbus-HaLow bridge telemetry module targeting the RV1106 platform.

## Immutable Constraints

| Area | Constraint | Status |
|------|-----------|--------|
| **Syntax** | Proto3 syntax MUST be used. `optional` and `required` keywords SHALL NOT appear. | **IMMUTABLE** |
| **Tag Reservation** | Tags 1–15 MUST be reserved for high-frequency fields (single-byte varint encoding). Low-frequency fields MUST use tags ≥ 16. | **IMMUTABLE** |
| **Enum Default** | The `OperatingStatus` enum zero-value MUST be `STATUS_UNKNOWN=0`. No custom default logic is permitted. | **IMMUTABLE** |
| **Payload Separation** | Bridge telemetry (`ModbusBridgePayload`) and hardware health (`HardwareHealthPayload`) MUST be separate messages — different cadences, different producers. | **IMMUTABLE** |
| **No Nesting** | Messages MUST be flat with `repeated` for collections. `oneof` SHALL NOT be used. | **IMMUTABLE** |
| **TDD** | Roundtrip tests MUST be written with Catch2 BEFORE any production serialization logic is wired. | **IMMUTABLE** |

## Requirements

### Requirement: OperatingStatus Enum

The system MUST define a proto3 enum `OperatingStatus` with exactly three values: `STATUS_UNKNOWN=0`, `STATUS_OK=1`, `STATUS_FAULT=2`.

#### Scenario: Enum roundtrip preserves all values

- GIVEN a proto3 `OperatingStatus` enum definition compiled by `protoc`
- WHEN each enum value (0 through 2) is set on a `ModbusNodeState` message, serialized, and deserialized
- THEN the deserialized enum value MUST equal the original value

#### Scenario: Default value is STATUS_UNKNOWN

- GIVEN a freshly constructed proto3 message containing an `OperatingStatus` field
- WHEN no explicit value is assigned to that field
- THEN the field MUST read as `OperatingStatus::STATUS_UNKNOWN` (value 0)

#### Scenario: Unrecognized enum value preserves numeric

- GIVEN a serialized `ModbusNodeState` where the status field is set to an integer not in the enum (e.g., 99)
- WHEN the message is deserialized
- THEN the field MUST preserve the numeric value 99 without error (proto3 open-enum semantics)

### Requirement: ModbusNodeState Message

The system MUST define a proto3 message `ModbusNodeState` with three fields: `node_id` (uint32, tag 1), `cycle_count` (uint32, tag 2), `status` (OperatingStatus, tag 3).

#### Scenario: ModbusNodeState serialization roundtrip

- GIVEN a `ModbusNodeState` with `node_id = 1`, `cycle_count = 42`, `status = STATUS_OK`
- WHEN the message is serialized to binary and then deserialized
- THEN all three fields MUST match the original values exactly

#### Scenario: ModbusNodeState with default values

- GIVEN a `ModbusNodeState` with only `node_id = 2` set
- WHEN the message is serialized and deserialized
- THEN `cycle_count` MUST be 0 and `status` MUST be `STATUS_UNKNOWN` (proto3 defaults)

### Requirement: ModbusBridgePayload Message

The system MUST define a proto3 message `ModbusBridgePayload` containing a single `repeated ModbusNodeState nodes` field (tag 1).

#### Scenario: Payload with multiple nodes roundtrips

- GIVEN a `ModbusBridgePayload` with three `ModbusNodeState` entries having distinct `node_id` values
- WHEN serialized and deserialized
- THEN the `nodes` repeated field MUST contain exactly three entries with matching `node_id` values in insertion order

#### Scenario: Empty payload roundtrips

- GIVEN a `ModbusBridgePayload` with no `nodes` entries
- WHEN serialized and deserialized
- THEN the `nodes` field MUST be empty (size 0)

#### Scenario: Payload preserves insertion order

- GIVEN a `ModbusBridgePayload` with nodes added in order `node_id = [3, 1, 2]`
- WHEN serialized and deserialized
- THEN iterating `nodes` MUST yield the same insertion order: `3`, `1`, `2`

### Requirement: HardwareHealthPayload Message

The system MUST define a proto3 message `HardwareHealthPayload` with: `cpu_temperature` (float, tag 1), `ram_used_bytes` (uint64, tag 2), `system_load` (float, tag 3).

#### Scenario: HardwareHealth roundtrip with realistic values

- GIVEN a `HardwareHealthPayload` with `cpu_temperature = 45.5`, `ram_used_bytes = 134217728`, `system_load = 2.37`
- WHEN serialized and deserialized
- THEN `cpu_temperature` MUST be within 0.01 of 45.5, `ram_used_bytes` MUST equal 134217728, and `system_load` MUST be within 0.01 of 2.37

#### Scenario: HardwareHealth zero-value defaults

- GIVEN a `HardwareHealthPayload` with no fields explicitly set
- WHEN serialized and deserialized
- THEN `cpu_temperature` MUST be 0.0f, `ram_used_bytes` MUST be 0, and `system_load` MUST be 0.0f

#### Scenario: HardwareHealth preserves large uint64

- GIVEN a `HardwareHealthPayload` with `ram_used_bytes = 4294967296` (4 GiB)
- WHEN serialized and deserialized
- THEN `ram_used_bytes` MUST equal 4294967296 without truncation

### Requirement: Tag Reservation for High-Frequency Fields

Tags 1–15 in all messages within `telemetry.proto` MUST be assigned only to fields that are serialized on every telemetry cycle. Low-frequency or optional fields MUST use tags ≥ 16.

#### Scenario: High-frequency fields occupy tags 1–15

- GIVEN the compiled `telemetry.proto` descriptor
- WHEN all message fields are enumerated
- THEN every field with tag number ≤ 15 MUST belong to `ModbusNodeState.node_id`, `ModbusNodeState.cycle_count`, `ModbusNodeState.status`, `ModbusBridgePayload.nodes`, `HardwareHealthPayload.cpu_temperature`, `HardwareHealthPayload.ram_used_bytes`, or `HardwareHealthPayload.system_load` — no other fields MAY occupy tags 1–15

### Requirement: Cross-Message Binary Isolation

Messages `ModbusBridgePayload` and `HardwareHealthPayload` MUST be independently serializable — deserializing one MUST NOT require knowledge of the other.

#### Scenario: Independent serialization boundaries

- GIVEN serialized bytes of a `ModbusBridgePayload` and a `HardwareHealthPayload`
- WHEN `ModbusBridgePayload` is deserialized from its bytes alone
- THEN the operation MUST succeed without referencing `HardwareHealthPayload` or any shared state
