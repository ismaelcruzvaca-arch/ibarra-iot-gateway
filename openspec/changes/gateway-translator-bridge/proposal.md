# Proposal: Gateway Translator Bridge

## Intent

The `gateway_translator` Python service is the critical bridge between the local plant network (MQTT) and Nhost cloud. Its source files were deleted from the repo — only `.pyc` caches remain in `edge_ops/gateway_translator/src/__pycache__/`. Without source, the service cannot be deployed, iterated, or verified. This change reconstructs all source files, adds test infrastructure, establishes secure connectivity, and documents deployment.

## Scope

### In Scope
- Reconstruct all 7 Python source modules from `.pyc` bytecode
- Recreate `telemetry_pb2.py` from the `ModbusBridgePayload` Protobuf schema (no `.proto` file found — reconstruct from bytecode analysis)
- Full pytest suite with mock MQTT broker + mock Hasura server
- `requirements.txt` and deployment config for Raspberry Pi 5
- Tailscale-based secure tunnel for Pi → Nhost connectivity
- Deployment documentation

### Out of Scope
- Changes to `modbus_bridge` C++ service
- Changes to Nhost/Hasura schema, tables, or event triggers
- Containerization or CI/CD pipeline
- Alert-engine changes

## Capabilities

### New Capabilities
- `gateway-translator`: MQTT→GraphQL bridge service — subscribes to `ModbusBridgePayload` on MQTT broker (`192.168.1.50`), deserializes Protobuf, batches `norvi_telemetry` inserts to Nhost/Hasura via `InsertTelemetryBatch` mutation with `on_conflict` resolution

### Modified Capabilities
None

## Approach

1. **Reconstruct sources** — decompile `.pyc` from `__pycache__/` to recover full source: `hasura_client.py`, `mqtt_worker.py`, `main.py`, `mock_hasura.py`, `simulator.py`, `mini_broker.py`, `test_resilience.py`
2. **Recreate `telemetry_pb2.py`** — analyze `ModbusBridgePayload` usage in `mqtt_worker.py` bytecode and cross-reference with runtime behavior to produce an accurate Protobuf definition
3. **Write tests** — adapt existing test infrastructure from `.pyc` into full pytest suite covering MQTT subscription, Protobuf deserialization, batch insert, and resilience (retry, reconnect)
4. **Dependencies** — `requirements.txt`: `paho-mqtt`, `protobuf`, `requests`, `pytest`
5. **Connectivity** — configure Tailscale on Raspberry Pi 5 for secure tunnel to Nhost; document fallback direct HTTPS
6. **Document** — deployment steps covering env vars (`HASURA_GRAPHQL_URL`, `HASURA_ADMIN_SECRET`, `MQTT_BROKER_HOST`), service startup, and health check

## Affected Areas

| Area | Impact | Description |
|------|--------|-------------|
| `edge_ops/gateway_translator/src/*.py` | New | All 7 source modules reconstructed |
| `edge_ops/gateway_translator/tests/` | New | pytest suite |
| `edge_ops/gateway_translator/requirements.txt` | New | Python dependencies |
| `edge_ops/gateway_translator/README.md` | New | Deployment documentation |

## Risks

| Risk | Likelihood | Mitigation |
|------|------------|------------|
| Incomplete reconstruction from `.pyc` | Med | Cross-reference with Protobuf schema, test coverage, and runtime behavior |
| Missing `.proto` definition for `ModbusBridgePayload` | High | Reverse-engineer from `telemetry_pb2.pyc` and usage patterns in `mqtt_worker.pyc` |
| Tailscale connectivity issues on Pi | Low | Document fallback direct HTTPS; test procedures included |

## Rollback Plan

No schema or infrastructure changes — rollback is restoring the previous state (no source files). Revert the PR to delete all reconstructed files. The `.pyc` caches remain unchanged and continue to work if the service was previously deployed.

## Dependencies

- Tailscale installed on Raspberry Pi 5
- Python 3.14+ (matching `.pyc` magic: CPython 314)
- Nhost endpoint credentials (`HASURA_GRAPHQL_URL`, `HASURA_ADMIN_SECRET`)

## Success Criteria

- [ ] All 7 source modules reconstructed and pass `python -c "import <module>"` without errors
- [ ] Full pytest suite passes (mock broker + mock Hasura)
- [ ] `telemetry_pb2.py` accurately reflects the `ModbusBridgePayload` schema used by `modbus_bridge`
- [ ] MQTT subscription deserializes inbound `ModbusBridgePayload` and batches `norvi_telemetry` inserts correctly
- [ ] Deployment documented and reproducible on a fresh Raspberry Pi 5
