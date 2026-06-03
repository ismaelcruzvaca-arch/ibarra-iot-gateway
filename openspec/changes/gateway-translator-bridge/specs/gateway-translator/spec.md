# Gateway Translator Specification

## Purpose

MQTT→GraphQL bridge that subscribes to `ModbusBridgePayload` from the plant broker, decodes each `ModbusNodeState`, and batch-inserts into Hasura's `norvi_telemetry` table with `on_conflict` MERGE.

## Protobuf Schema

From `edge_ops/modbus_bridge/proto/telemetry.proto` (package `ibarra.telemetry`):

| Message | Fields |
|---------|--------|
| `ModbusBridgePayload` | `repeated ModbusNodeState nodes` |
| `ModbusNodeState` | `string node_id`, `uint32 cycle_count`, `OperatingStatus status` |
| `OperatingStatus` | Enum: `UNKNOWN=0, RUNNING=1, STOPPED=2, ALARM=3, MAINTENANCE=4` |

Mutation: `InsertTelemetryBatch($objects: [norvi_telemetry_insert_input!]!)` → `insert_norvi_telemetry(on_conflict: {constraint: norvi_telemetry_pkey, update_columns: [cycle_count, status]}) { affected_rows }`.

## Requirements

### FR-1: MQTT Subscribe

SHALL connect to `MQTT_BROKER_HOST` and subscribe to `modbus/telemetry`. If unreachable, retry with 1s/2s/4s backoff.

- **Subscription ok:** GIVEN broker reachable WHEN gateway starts THEN subscribe within 5s AND log success
- **Broker down:** GIVEN broker unreachable WHEN connecting THEN retry 1s/2s/4s AND log each attempt

### FR-2: Protobuf Deserialization

Each MQTT payload SHALL be parsed as `ModbusBridgePayload`. Enum values map to strings via `_STATUS_MAP`. Malformed payloads SHALL be logged and skipped without crashing.

- **Valid payload:** GIVEN a valid `ModbusBridgePayload` WHEN it arrives THEN each node decoded AND `status` mapped to string
- **Malformed:** GIVEN non-Protobuf bytes WHEN parse fails THEN log error AND skip

### FR-3: Batch Buffer

Records SHALL buffer in a thread-safe queue and flush every `FLUSH_INTERVAL` (default 5s). Empty flushes SHALL be no-ops.

- **Periodic flush:** GIVEN 10 buffered records WHEN timer fires THEN all 10 sent as one batch
- **Empty no-op:** GIVEN empty buffer WHEN timer fires THEN no GraphQL call

### FR-4: Hasura Batch Insert

Buffered records SHALL be sent via `InsertTelemetryBatch` with `on_conflict` upsert. Hasura endpoint credentials come from `HASURA_GRAPHQL_URL` / `HASURA_ADMIN_SECRET`.

- **Batch success:** GIVEN N records WHEN flush fires THEN `affected_rows` SHALL equal N

### FR-5: Graceful Shutdown

On SIGTERM/SIGINT: flush buffer → disconnect MQTT → exit code 0.

- **Shutdown flush:** GIVEN 3 buffered records WHEN SIGTERM THEN flush to Hasura THEN disconnect THEN exit 0

### FR-6: Hasura Retry

On Hasura failure (5xx, network) SHALL retry 3× with 2s/4s/8s backoff. Client errors (4xx) SHALL NOT retry.

- **Transient recovered:** GIVEN HTTP 503 WHEN flush THEN retry 2s/4s/8s. If 3rd succeeds, log `retries: 2`
- **Client error no retry:** GIVEN HTTP 400 WHEN flush THEN log error, no retry

### FR-7: Health Logging

SHALL log connection status, buffer size, and last flush age every 60s. No HTTP endpoint required for edge.

- **Heartbeat:** GIVEN gateway running EVERY 60s THEN log MQTT status, buffer size, flush age

### NR-1: End-to-End Latency

SHALL achieve <10s from MQTT publish → Hasura insert under normal conditions.

### NR-2: Memory Bound

Buffer max size SHALL be configurable (default 1000). When full, oldest records SHALL be evicted (FIFO) with a warning.

- **Backpressure eviction:** GIVEN buffer full (1000) WHEN new record arrives THEN drop oldest AND log warning

## Constraints

| Item | Value |
|------|-------|
| Python | 3.14+ (CPython 314) |
| MQTT lib | paho-mqtt |
| HTTP lib | requests |
| Protobuf | protobuf (Python) |
| Platform | Raspberry Pi 5 (ARM64) + Tailscale |

## Files

| Path | Purpose |
|------|---------|
| `src/main.py` | Entry, env vars, signal handlers |
| `src/mqtt_worker.py` | GatewayWorker: subscribe + decode + buffer |
| `src/hasura_client.py` | HasuraClient: batch insert + retry |
| `src/telemetry_pb2.py` | Generated Protobuf classes |
| `tests/` | pytest suite (mock broker + mock Hasura) |
| `requirements.txt` | paho-mqtt, protobuf, requests, pytest |
