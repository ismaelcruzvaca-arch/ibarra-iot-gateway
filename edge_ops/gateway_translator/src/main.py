"""
gateway_translator — Raspberry Pi 5 MQTT ↔ HTTP bridge worker.

Subscribes to industrial telemetry topics (novamex/ibarra/#),
deserialises Protobuf (ModbusBridgePayload) or JSON payloads,
and forwards them to upstream services (Hasura/Nhost) via HTTP POST.

Phase 2: full pipeline with Protobuf deserialisation, HTTP forwarding,
         micro-batching, health reporting, and reconnection logic.
"""

import json
import logging
import os
import signal
import sys
import threading
import time
from collections.abc import Callable
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

# Ensure proto/ is importable (it lives one directory up from src/)
_PROTO_DIR = str(Path(__file__).resolve().parent.parent / "proto")
if _PROTO_DIR not in sys.path:
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import paho.mqtt.client as mqtt
import requests

# ---------------------------------------------------------------------------
# Constants (override via environment variables)
# ---------------------------------------------------------------------------

BROKER_HOST = os.getenv("MQTT_BROKER_HOST", "localhost")
BROKER_PORT = int(os.getenv("MQTT_BROKER_PORT", "1883"))
TELEMETRY_TOPIC = os.getenv("MQTT_TELEMETRY_TOPIC", "novamex/ibarra/#")
UPSTREAM_URL = os.getenv("UPSTREAM_URL", "http://localhost:8080/api/telemetry")
UPSTREAM_HASURA = os.getenv("HASURA_ENDPOINT", "http://localhost:8080/v1/graphql")
HASURA_SECRET = os.getenv("HASURA_SECRET", "")
CLIENT_ID = os.getenv("MQTT_CLIENT_ID", "gateway-translator-rpi5")

# Micro-batching
BATCH_INTERVAL_S = float(os.getenv("BATCH_INTERVAL_S", "5"))
BATCH_MAX_ITEMS = int(os.getenv("BATCH_MAX_ITEMS", "50"))


# ---------------------------------------------------------------------------
# Protobuf (optional — graceful degradation if proto not compiled)
# ---------------------------------------------------------------------------

try:
    from proto.telemetry_pb2 import ModbusBridgePayload  # type: ignore[import-untyped]

    HAS_PROTO = True
except ImportError:
    ModbusBridgePayload = None  # type: ignore[assignment]
    HAS_PROTO = False


# ---------------------------------------------------------------------------
# Micro-batch accumulator
# ---------------------------------------------------------------------------

@dataclass
class TelemetryRecord:
    """Normalised telemetry record forwarded upstream."""

    topic: str
    payload: dict[str, Any]
    received_at: float  # epoch seconds


_batch: list[TelemetryRecord] = []
_batch_lock = threading.Lock()
_batch_flush_event = threading.Event()
STOP_EVENT = threading.Event()


def _accumulate(record: TelemetryRecord) -> None:
    """Thread-safe append to the batch accumulator."""
    with _batch_lock:
        _batch.append(record)
        if len(_batch) >= BATCH_MAX_ITEMS:
            _batch_flush_event.set()


def _drain_batch() -> list[TelemetryRecord]:
    """Thread-safe drain (swap) the accumulator."""
    with _batch_lock:
        records = list(_batch)
        _batch.clear()
    return records


# ---------------------------------------------------------------------------
# Payload parsing
# ---------------------------------------------------------------------------

def _try_parse_modbus_payload(raw: bytes) -> dict[str, Any] | None:
    """Attempt to parse a Protobuf ModbusBridgePayload from raw bytes."""
    if not HAS_PROTO or ModbusBridgePayload is None:
        return None
    try:
        proto = ModbusBridgePayload()
        proto.ParseFromString(raw)
        nodes: list[dict[str, Any]] = []
        for node in proto.nodes:
            nodes.append({
                "node_id": node.node_id,
                "cycle_count": node.cycle_count,
                "status": node.status,
            })
        return {"modbus_nodes": nodes, "format": "protobuf"}
    except Exception:
        return None


def _try_parse_json_payload(raw: bytes) -> dict[str, Any] | None:
    """Attempt to parse a JSON payload from raw bytes."""
    try:
        return json.loads(raw.decode("utf-8"))
    except (json.JSONDecodeError, UnicodeDecodeError):
        return None


def parse_payload(raw: bytes, topic: str) -> dict[str, Any] | None:
    """Parse incoming MQTT payload — tries Protobuf first, then JSON."""
    # Protobuf (Modbus bridge telemetry)
    result = _try_parse_modbus_payload(raw)
    if result is not None:
        return result

    # JSON (simulator, health, or other JSON publishers)
    result = _try_parse_json_payload(raw)
    if result is not None:
        result["format"] = "json"
        return result

    return None


# ---------------------------------------------------------------------------
# Hasura forwarding
# ---------------------------------------------------------------------------

HASURA_INSERT_MUTATION = """
mutation InsertTelemetry($objects: [telemetry_telemetry_events_insert_input!]!) {
  insert_telemetry_telemetry_events(objects: $objects) {
    returning { id }
    affected_rows
  }
}
"""


def _parse_topic(topic: str) -> dict[str, str]:
    """Extract area, device_id, and health from a novamex topic path.

    Expected format: novamex/ibarra/{area}/{device_id}/{suffix}
    Falls back to reasonable defaults for unrecognised topics.
    """
    parts = topic.split("/")
    if len(parts) >= 5 and parts[0] == "novamex" and parts[1] == "ibarra":
        return {"area": parts[2], "device_id": parts[3], "suffix": parts[4]}
    if len(parts) >= 4:
        return {"area": parts[0], "device_id": parts[1], "suffix": parts[2]}
    return {"area": "unknown", "device_id": topic.replace("/", "_"), "suffix": "data"}


def _forward_to_hasura(records: list[TelemetryRecord]) -> int:
    """POST a batch of records to Hasura. Returns number of successfully inserted rows."""
    if not UPSTREAM_HASURA or not records:
        return 0

    objects: list[dict[str, Any]] = []
    for rec in records:
        topic_info = _parse_topic(rec.topic)
        payload = rec.payload

        # Extract health / metrics from the Sparkplug B Lite format
        health = payload.get("node_health", "ONLINE")
        metrics = payload.get("metrics", payload)  # full payload as metrics fallback
        device_id = topic_info["device_id"]
        area = topic_info["area"]

        # Convert Unix timestamp to ISO 8601 for Hasura timestamptz column
        timestamp_iso = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(rec.received_at))

        objects.append({
            "enterprise": "chocolate-ibarra",
            "site": "ibarra",
            "area": area,
            "device_id": device_id,
            "timestamp": timestamp_iso,
            "health": health,
            "metrics": metrics,
        })

    headers = {"Content-Type": "application/json"}
    if HASURA_SECRET:
        headers["x-hasura-admin-secret"] = HASURA_SECRET

    try:
        resp = requests.post(
            UPSTREAM_HASURA,
            json={"query": HASURA_INSERT_MUTATION, "variables": {"objects": objects}},
            headers=headers,
            timeout=10,
        )
        if resp.status_code >= 200 and resp.status_code < 300:
            data = resp.json()
            # Log GraphQL errors even on HTTP 200
            if "errors" in data:
                for err in data["errors"]:
                    logger.warning("Hasura GraphQL error: %s", err.get("message", str(err)))
            affected = data.get("data", {}).get("insert_telemetry_telemetry_events", {}).get("affected_rows", 0)
            return affected
        else:
            logger.warning("Hasura returned HTTP %d: %s", resp.status_code, resp.text[:200])
            return 0
    except requests.ConnectionError:
        logger.warning("Hasura connection refused — upstream may be down")
        return 0
    except Exception as exc:
        logger.warning("Hasura POST failed: %s", exc)
        return 0


def flush_batch() -> None:
    """Flush the current batch to upstream (Hasura)."""
    records = _drain_batch()
    if not records:
        return

    count = len(records)
    inserted = _forward_to_hasura(records)
    if inserted > 0:
        logger.info("Batch flushed: %d records, %d inserted", count, inserted)
    else:
        logger.debug("Batch drained: %d records (upstream skipped or unavailable)", count)


def _batch_flusher_loop() -> None:
    """Background thread that periodically flushes accumulated telemetry."""
    logger.info("Batch flusher started (interval=%ss, max_items=%d)", BATCH_INTERVAL_S, BATCH_MAX_ITEMS)
    while not STOP_EVENT.is_set():
        # Wait for either the interval or a force-flush signal
        _batch_flush_event.wait(timeout=BATCH_INTERVAL_S)
        _batch_flush_event.clear()
        flush_batch()
    # Final flush on shutdown
    flush_batch()
    logger.info("Batch flusher stopped")


# ---------------------------------------------------------------------------
# MQTT callbacks (paho-mqtt v2 API)
# ---------------------------------------------------------------------------

_connected = False  # module-level flag for first-connect subscription


def on_connect(
    client: mqtt.Client,
    userdata: Any,
    flags: dict[str, int] | Any,
    rc: Any,
    properties: Any = None,
) -> None:
    global _connected
    rc_val: int = getattr(rc, "value", rc) if not isinstance(rc, int) else rc
    if rc_val == 0:
        if not _connected:
            logger.info("Connected to MQTT broker at %s:%d", BROKER_HOST, BROKER_PORT)
            # Subscribe OUTSIDE the callback to avoid paho-mqtt v2.1.0 bug
            _connected = True
    else:
        logger.error("Connection refused (rc=%d)", rc_val)


def on_message(client: mqtt.Client, userdata: Any, msg: mqtt.MQTTMessage) -> None:
    """Deserialise and accumulate every incoming telemetry message."""
    try:
        payload = parse_payload(msg.payload, msg.topic)
        if payload is None:
            logger.debug("Unparseable payload on %s (%d bytes)", msg.topic, len(msg.payload))
            return

        record = TelemetryRecord(
            topic=msg.topic,
            payload=payload,
            received_at=time.time(),
        )
        _accumulate(record)

        logger.debug("Accumulated %d bytes from %s", len(msg.payload), msg.topic)
    except Exception as exc:
        logger.error("on_message crashed: %s", exc, exc_info=True)


def on_disconnect(
    client: mqtt.Client,
    userdata: Any,
    rc: Any,
    properties: Any = None,
    reasonCode: Any = None,
) -> None:
    if isinstance(rc, int) and rc != 0:
        logger.warning("Unexpected disconnect (rc=%d) — will auto-reconnect", rc)


# ---------------------------------------------------------------------------
# Graceful shutdown
# ---------------------------------------------------------------------------

def handle_signal(signum: int, frame: Any) -> None:
    logger.info("Received signal %d — shutting down", signum)
    STOP_EVENT.set()
    _batch_flush_event.set()  # wake flusher for final flush
    client.disconnect()
    client.loop_stop()
    sys.exit(0)


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    logging.basicConfig(
        level=getattr(logging, os.getenv("LOG_LEVEL", "INFO").upper()),
        format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
    )
    logger = logging.getLogger("gateway_translator")

    logger.info(
        "gateway_translator starting — broker=%s:%d topic=%s upstream=%s",
        BROKER_HOST, BROKER_PORT, TELEMETRY_TOPIC, UPSTREAM_HASURA,
    )
    if HAS_PROTO:
        logger.info("Protobuf support: enabled (ModbusBridgePayload)")
    else:
        logger.warning("Protobuf support: disabled (compile proto/telemetry.proto to enable)")

    # Create MQTT client with callback API v2
    client = mqtt.Client(
        callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
        client_id=CLIENT_ID,
        protocol=mqtt.MQTTv311,
    )
    client.on_connect = on_connect
    client.on_message = on_message
    client.on_disconnect = on_disconnect

    # Enable automatic reconnect
    client.reconnect_delay_set(min_delay=1, max_delay=60)

    # Start background batch flusher
    flusher = threading.Thread(target=_batch_flusher_loop, daemon=True, name="batch-flusher")
    flusher.start()

    signal.signal(signal.SIGINT, handle_signal)
    signal.signal(signal.SIGTERM, handle_signal)

    try:
        client.connect(BROKER_HOST, BROKER_PORT, keepalive=60)
        client.loop_start()

        # Wait for connection, then subscribe (outside callback to avoid paho bug)
        import time as _time
        for _ in range(50):  # up to 5 seconds
            if _connected:
                client.subscribe(TELEMETRY_TOPIC, qos=1)
                logger.info("Subscribed to %s", TELEMETRY_TOPIC)
                break
            _time.sleep(0.1)

        # Keep main thread alive
        while not STOP_EVENT.is_set():
            STOP_EVENT.wait(1)
    except Exception as exc:
        logger.critical("Fatal error: %s", exc)
        sys.exit(1)
