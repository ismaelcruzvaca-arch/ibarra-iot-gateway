# ADR-002: E2E Stress Test Deferred

**Status:** Accepted (deferred execution)
**Date:** 2026-05-21
**Context:** gateway-translator + simulator + Nhost migrations

---

## Decision

The code for the Gateway Translator (`main.py`, `mqtt_worker.py`, `hasura_client.py`),
the MQTT broker infrastructure (`docker-compose.yml`, `mosquitto.conf`), and the
stress-test simulator (`simulator.py`) are **complete and committed**. However,
the End-to-End stress test execution is **deferred** because the current
development environment lacks Docker and a running Nhost instance.

## Rationale

- No Docker daemon available on this machine → cannot run Mosquitto container.
- No Nhost project running locally → cannot verify GraphQL mutations.
- Running the simulator without a broker or upstream would produce no useful
  signal.
- All code has been statically verified (Python syntax check, cross-module
  imports) and is ready for execution.

## Future Execution Checklist

When a target machine with Docker and Nhost is available:

### Step 1 — Start Nhost
```bash
nhost up
```
This provisions PostgreSQL + Hasura with the `init_time_series` migration
applied (norvi_telemetry, vision_telemetry, hardware_health).

### Step 2 — Start Mosquitto (MQTT Broker)
```bash
cd docker/
docker compose up -d
```
Verifies MQTT is listening:
```bash
netstat -an | grep 1883
```

### Step 3 — Start Gateway Translator
```bash
cd edge_ops/gateway_translator/
# Set environment variables
export HASURA_GRAPHQL_URL=http://localhost:8080/v1/graphql
export HASURA_ADMIN_SECRET=<your-admin-secret>
export MQTT_BROKER_HOST=127.0.0.1

# Generate Protobuf stubs (if not already present)
./scripts/build_protos.sh

# Start the gateway
python src/main.py
```

### Step 4 — Start the Stress Simulator ("El Cañón")
In a separate terminal:
```bash
cd edge_ops/gateway_translator/
python src/simulator.py
```
The simulator publishes 15 msg/s (54,000 msg/h) to `novamex/linea1/telemetry`.

### Monitoring
- **RAM:** `top` / `htop` — watch gateway_translator Python process.
- **PostgreSQL:** Monitor `norvi_telemetry` row count and partition sizes:
  ```sql
  SELECT count(*) FROM norvi_telemetry;
  SELECT relname, n_live_tup FROM pg_stat_user_tables
    WHERE relname LIKE 'norvi_telemetry%';
  ```
