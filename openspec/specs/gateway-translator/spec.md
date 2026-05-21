# Spec: Gateway Translator

**Status**: Archived (2026-05-21)
**Project**: ibarra-iot-gateway

## Modules

| File | Responsibility |
|------|---------------|
| `scripts/build_protos.sh` | Protobuf Python stub codegen |
| `src/hasura_client.py` | HasuraClient — GraphQL batch insert |
| `src/mqtt_worker.py` | GatewayWorker — MQTT consumer, buffer, Protobuf decode |
| `src/main.py` | Env vars, lifecycle, 1s flush loop |

## Key Design

- Micro-batching: 1-second window, thread-safe Lock+list buffer
- on_conflict: upsert on norvi_telemetry_pkey, updates cycle_count + status
- Protobuf: ModbusBridgePayload → ModbusNodeState entries
- Error handling: failed batches are re-queued into the buffer
