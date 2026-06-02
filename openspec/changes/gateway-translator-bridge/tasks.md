# Tasks: Gateway Translator Bridge

## Review Workload Forecast

| Field | Value |
|-------|-------|
| Estimated changed lines | ~970 |
| 400-line budget risk | High |
| Chained PRs recommended | Yes |
| Suggested split | 4 child PRs (tracker + 4 slices) |
| Delivery strategy | auto-chain |
| Chain strategy | feature-branch-chain |

Decision needed before apply: No
Chained PRs recommended: Yes
Chain strategy: feature-branch-chain
400-line budget risk: High

### Suggested Work Units

| Unit | Goal | Likely PR | Notes |
|------|------|-----------|-------|
| 1 | Tracker branch | PR #1 | `feature/gateway-translator-bridge` — empty base |
| 2 | Core source modules | PR #2 | base: tracker. `hasura_client.py`, `mqtt_worker.py`, `main.py`, `telemetry_pb2.py`, `requirements.txt` |
| 3 | Mock infrastructure | PR #3 | base: PR #2. `mock_hasura.py`, `mini_broker.py` |
| 4 | Tests + simulator | PR #4 | base: PR #3. `simulator.py`, `test_resilience.py` |
| 5 | Deployment + docs | PR #5 | base: PR #4. `README.md`, systemd unit |

## Phase 1: Core Source Reconstruction

- [x] 1.1 Create `src/telemetry_pb2.py` — Protobuf classes from serialized descriptor (`OperatingStatus` enum, `ModbusNodeState`, `ModbusBridgePayload`, `HardwareHealthPayload`)
- [x] 1.2 Create `src/hasura_client.py` — `HasuraClient` class: `__init__`, `insert_telemetry_batch()` returning `bool` (no built-in retry — per bytecode accuracy)
- [x] 1.3 Create `src/mqtt_worker.py` — `GatewayWorker` class: MQTT subscribe `novamex/linea1/telemetry`, Protobuf decode via `_status_to_string()`, lock-guarded buffer (no cap/FIFO — per bytecode accuracy)
- [x] 1.4 Create `src/main.py` — Entry: env vars (`HASURA_GRAPHQL_URL`, `HASURA_ADMIN_SECRET`, `MQTT_BROKER_HOST`), signal handlers (SIGTERM/SIGINT→shutdown flag), 1s poll loop with re-queue on failure
- [x] 1.5 Create `requirements.txt` — `paho-mqtt`, `protobuf`, `requests`

## Phase 2: Test Infrastructure

- [ ] 2.1 Create `tests/mock_hasura.py` — HTTP server with `/kill`, `/revive`, `/status` endpoints, GraphQL `InsertTelemetryBatch` handler
- [ ] 2.2 Create `tests/mini_broker.py` — Minimal MQTT v3.1.1 broker for isolated tests

## Phase 3: Integration & Resilience Tests

- [ ] 3.1 Create `tests/simulator.py` — Publishes 15 msg/s from 5 virtual NORVI nodes
- [ ] 3.2 Create `tests/test_resilience.py` — Scenarios: normal flow, broker down (1s/2s/4s retry), Hasura kill/revive (0 lost), buffer overflow FIFO eviction, shutdown flush

## Phase 4: Deployment & Documentation

- [ ] 4.1 Create systemd unit (`gateway-translator.service`) — `After=network-online.target tailscaled.service`, `Restart=on-failure`
- [ ] 4.2 Create `README.md` — Env var reference, Tailscale Funnel setup, systemd install, health check
