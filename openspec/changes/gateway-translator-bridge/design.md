# Design: Gateway Translator Bridge

## Technical Approach

Reconstruct 7 Python source modules from `.pyc` bytecode, preserving the original producer-consumer architecture: MQTT background thread populates a lock-protected buffer, and the main thread polls every 1s to drain and batch-insert via GraphQL. Deploy as a systemd service behind Tailscale Funnel on Raspberry Pi 5.

## Architecture Decisions

### AD-1: Poll-based drain over timer-thread flush

| Option | Tradeoff | Decision |
|--------|----------|----------|
| Main-thread poll (1s) | Simpler threading; flush bound to main-loop jitter | ✅ |
| Dedicated timer thread | Lower latency but adds concurrent access complexity | ❌ |
**Rationale**: The existing `.pyc` code uses a 1s poll in the main loop. SIMPLER threading model — the MQTT background thread only writes; the main thread reads and sends. No timer race conditions.

### AD-2: Re-queue on failure over dead-letter queue

Failed Hasura inserts re-enqueue entries to the buffer and log a warning. No separate DLQ.
**Rationale**: The main loop has implicit backpressure — if Hasura is down for 100s, the buffer fills and evicts oldest (FIFO at 1000 cap). A DLQ adds complexity for an edge-service that has no durability contract.

### AD-3: Tailscale Funnel over direct HTTPS

| Option | Tradeoff | Decision |
|--------|----------|----------|
| Tailscale Funnel | Encrypted tunnel, no public IP, no DNS config | ✅ |
| Direct HTTPS + public IP | Exposes Pi to internet, needs certbot/DDNS | ❌ |
**Rationale**: Already decided in earlier session. Funnel provides a `[machine].ts.net` URL that Nhost can reach without inbound firewall rules.

### AD-4: Reconstructed Protobuf from serialized descriptor

The `.pyc` contains the serialized `FileDescriptorProto` — we decode it to write `telemetry_pb2.py` without a `.proto` source file.
**Rationale**: No `.proto` file exists in the repo; the serialized descriptor in `telemetry_pb2.pyc` is the canonical schema. Writing a hand-authored `.proto` would risk drift from the actual `modbus_bridge` encoding.

## Data Flow

```
plant MQTT broker (192.168.1.50)
       │
       │ pub "modbus/telemetry" (Protobuf ModbusBridgePayload)
       ▼
┌─────────────────────────────┐
│  GatewayWorker              │  ← MQTT background thread
│  _on_message()              │     paho loop_start / loop_stop
│  → ModbusBridgePayload      │
│  → for each ModbusNodeState │
│  → _buffer.append(entry)   │  ← threading.Lock
└──────────┬──────────────────┘
           │ drain_buffer()  (main thread, every 1s)
           ▼
┌─────────────────────────────┐
│  Main Loop                  │
│  batch = worker.drain()     │
│  hasura.insert_batch(batch) │
│  if fail → re-queue each    │
└──────────┬──────────────────┘
           │ POST /v1/graphql (Tailscale Funnel)
           ▼
┌─────────────────────────────┐
│  Nhost / Hasura             │
│  insert_norvi_telemetry     │
│  on_conflict: upsert        │
└─────────────────────────────┘
```

## Thread Model

| Thread | Role | Lifetime |
|--------|------|----------|
| **Main** | Env check, signal handlers, poll loop (1s), batch insert, re-queue | Process lifetime |
| **MQTT** | `loop_start()` daemon thread — network I/O, callback dispatch | `start()` → `stop()` |

Buffer is guarded by `threading.Lock`. The MQTT callback acquires the lock to append; `drain_buffer()` acquires to copy-and-clear.

## Error Handling

| Scenario | Behaviour |
|----------|-----------|
| MQTT broker unreachable | `connect()` raises → logged; process exits (restarted by systemd) |
| Broker drops connection | paho auto-reconnect with exponential backoff (built-in) |
| Malformed Protobuf payload | Logged and skipped — no crash |
| Hasura HTTP 5xx / network error | `drain_buffer()` returns entries → main loop re-queues them → retry next cycle |
| Hasura HTTP 4xx | Logged, NOT retried (client error — bad data) |
| Buffer full (1000 entries) | Oldest entry evicted (FIFO) with warning |
| SIGTERM/SIGINT | Flush buffer → disconnect MQTT → exit 0 |

## File Changes

| File | Action | Description |
|------|--------|-------------|
| `src/hasura_client.py` | Create | `HasuraClient` — GraphQL batch inserter with error classification |
| `src/mqtt_worker.py` | Create | `GatewayWorker` — MQTT subscriber, Protobuf decoder, buffer |
| `src/main.py` | Create | Entry point: env vars, signal handlers, poll loop |
| `src/telemetry_pb2.py` | Create | Protobuf classes from serialized descriptor |
| `tests/mock_hasura.py` | Create | HTTP mock Hasura with `/kill`, `/revive`, `/status` |
| `tests/mini_broker.py` | Create | Minimal MQTT v3.1.1 broker for SIL tests |
| `tests/simulator.py` | Create | Stress-test publisher (15 msg/s) |
| `tests/test_resilience.py` | Create | Kill/revive resilience test |
| `requirements.txt` | Create | paho-mqtt, protobuf, requests, pytest |
| `README.md` | Create | Deployment and configuration docs |

## Protobuf Schema (from serialized descriptor)

```
package ibarra.telemetry;

enum OperatingStatus {
  STATUS_UNKNOWN    = 0;
  STATUS_RUNNING    = 1;
  STATUS_STOPPED    = 2;
  STATUS_ALARM      = 3;
  STATUS_MAINTENANCE = 4;
}

message ModbusNodeState {
  string node_id            = 1;
  uint32 cycle_count        = 2;
  OperatingStatus status    = 3;
}

message ModbusBridgePayload {
  repeated ModbusNodeState nodes = 1;
}
```

## Interfaces

```python
class HasuraClient:
    def __init__(self, endpoint_url: str, admin_secret: str) -> None: ...
    def insert_telemetry_batch(self, telemetry_list: list[dict[str, Any]]) -> bool: ...

class GatewayWorker:
    def __init__(self, broker_host: str, hasura_client: HasuraClient,
                 broker_port: int = 1883, topic: str = "modbus/telemetry",
                 client_id: str | None = None) -> None: ...
    def start(self) -> None: ...
    def stop(self) -> None: ...
    def enqueue_telemetry(self, entry: dict) -> None: ...
    def drain_buffer(self) -> list[dict]: ...
```

## State Map

```python
_STATUS_MAP = {
    0: "UNKNOWN", 1: "RUNNING", 2: "STOPPED",
    3: "ALARM",   4: "MAINTENANCE",
}
```

## Testing Strategy

| Layer | What | How |
|-------|------|-----|
| Unit | `HasuraClient` error handling | Mock `requests.post`; test empty batch, 4xx, 5xx, JSON errors |
| SIL | Full pipeline | `mini_broker.py` + `mock_hasura.py` + real `GatewayWorker` + real `HasuraClient` |
| Resilience | Hasura kill/revive | `test_resilience.py`: 30s normal → kill → 15s down → revive → verify 0 lost |
| Stress | 15 msg/s throughput | `simulator.py`: 5 virtual NORVI nodes, assert <10s E2E latency |

## Deployment

```
# /etc/systemd/system/gateway-translator.service
[Unit]
Description=Ibarra IoT Gateway Translator
After=network-online.target tailscaled.service
Wants=tailscaled.service

[Service]
Type=simple
User=pi
WorkingDirectory=/opt/gateway_translator
Environment=HASURA_GRAPHQL_URL=https://<tailscale-funnel>.ts.net/v1/graphql
Environment=HASURA_ADMIN_SECRET=<secret>
Environment=MQTT_BROKER_HOST=192.168.1.50
ExecStart=/usr/bin/python3 /opt/gateway_translator/src/main.py
Restart=on-failure
RestartSec=5

[Install]
WantedBy=multi-user.target
```

Tailscale Funnel: `sudo tailscale funnel --bg 443` with the Nhost GraphQL URL configured as the funnel target.

## Migration / Rollout

No data migration required. Deploy by syncing `src/`, `requirements.txt`, and `systemd` unit. Rollback: stop service and revert files.

## Open Questions

- [ ] Verify `modbus/telemetry` topic name against actual MQTT broker config
- [ ] Confirm Nhost GraphQL endpoint URL for Tailscale Funnel
- [ ] Determine if `client_id` for MQTT should be stable or random
