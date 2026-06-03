# Gateway Translator

**MQTT → Hasura GraphQL bridge** for the GEMA IoT Gateway.

Subscribes to MQTT telemetry (published by `modbus_bridge`), deserialises
Protobuf `ModbusBridgePayload` messages, and batch-inserts them into
Nhost/Produccion-ibarra via Hasura GraphQL mutations.

## Architecture

```
NORVI PLC → Modbus RTU → modbus_bridge (C++) → MQTT broker (192.168.1.50)
                                                     │
                                                     ▼
                                            gateway-translator (Python)
                                                     │
                                                     ▼
                                            Nhost / produccion-ibarra (Tailscale)
```

## Quick Start

```bash
# Install dependencies
pip install -r requirements.txt

# Set environment variables
export HASURA_GRAPHQL_URL="https://gateway.tailscale-xxx.ts.net/v1/graphql"
export HASURA_ADMIN_SECRET="your-admin-secret"
export MQTT_BROKER_HOST="192.168.1.50"

# Run
python src/main.py
```

## Environment Variables

| Variable | Required | Default | Description |
|----------|----------|---------|-------------|
| `HASURA_GRAPHQL_URL` | ✅ | — | Nhost Hasura GraphQL endpoint (via Tailscale Funnel) |
| `HASURA_ADMIN_SECRET` | ✅ | — | Hasura admin secret for x-hasura-admin-secret header |
| `MQTT_BROKER_HOST` | ✅ | — | MQTT broker hostname or IP |
| `MQTT_TOPIC` | ❌ | `novamex/linea1/telemetry` | MQTT topic to subscribe to |
| `BATCH_INTERVAL_SEC` | ❌ | `5` | Seconds between batch flushes to Hasura |
| `BUFFER_MAX` | ❌ | `1000` | Max buffer entries (0 = unlimited). Oldest evicted first when full (NR-2) |

## Deployment

See [`deploy/install.sh`](deploy/install.sh) for automated installation on
Raspberry Pi 5 with systemd service.

```bash
sudo ./deploy/install.sh
sudo systemctl enable --now gateway-translator
```

## Testing

```bash
cd tests
python -m pytest test_resilience.py -v
```

Requires no external infrastructure — tests use:
- `mini_broker.py` — In-process MQTT v3.1.1 broker
- `mock_hasura.py` — Lightweight HTTP GraphQL mock server
- `simulator.py` — Protobuf telemetry generator

## Protobuf Schema

Messages are defined in `src/telemetry_pb2.py` (generated from
`modbus_bridge/proto/telemetry.proto`).

```protobuf
message ModbusNodeState {
  string node_id       = 1;  // NORVI node identifier
  int32  node_type     = 2;  // PLC type
  int32  cycle_count   = 3;  // Production cycle counter
  int32  status        = 4;  // OperatingStatus enum
  int64  timestamp     = 5;  // Unix ms
  string error_msg     = 6;
  float  temperature   = 7;
  map<string, float> analogs = 8;  // Analog sensor values
  int32  rssi          = 9;  // WiFi signal strength
}

message ModbusBridgePayload {
  repeated ModbusNodeState nodes = 1;
}
```
