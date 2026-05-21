"""
gateway_translator — Raspberry Pi 5 MQTT ↔ HTTP bridge worker.

Subscribes to the local Modbus telemetry topic (novamex/linea1/telemetry),
deserialises Protobuf payloads, and forwards them to upstream services.

Phase 1: skeleton — imports, placeholder subscription loop.
Phase 2+: full pipeline with Protobuf deserialisation, HTTP forwarding,
           health reporting, and reconnection logic.
"""

import paho.mqtt.client as mqtt
import protobuf
import requests

import logging
import os
import signal
import sys

from proto.telemetry_pb2 import ModbusBridgePayload

# ---------------------------------------------------------------------------
# Constants (override via environment variables)
# ---------------------------------------------------------------------------

BROKER_HOST = os.getenv("MQTT_BROKER_HOST", "localhost")
BROKER_PORT = int(os.getenv("MQTT_BROKER_PORT", "1883"))
TELEMETRY_TOPIC = os.getenv("MQTT_TELEMETRY_TOPIC", "novamex/linea1/telemetry")
UPSTREAM_URL = os.getenv("UPSTREAM_URL", "http://localhost:8080/api/telemetry")
CLIENT_ID = os.getenv("MQTT_CLIENT_ID", "gateway-translator-rpi5")

# ---------------------------------------------------------------------------
# MQTT callbacks
# ---------------------------------------------------------------------------

def on_connect(client, userdata, flags, rc):
    if rc == 0:
        logger.info("Connected to MQTT broker at %s:%d", BROKER_HOST, BROKER_PORT)
        client.subscribe(TELEMETRY_TOPIC, qos=1)
    else:
        logger.error("Connection refused (rc=%d)", rc)


def on_message(client, userdata, msg):
    """
    Callback fired for each incoming telemetry message.

    Phase 1: log receipt only.
    Phase 2+: deserialise Protobuf, validate, POST to upstream.
    """
    logger.debug("Received %d bytes on %s", len(msg.payload), msg.topic)

    # TODO(Phase 2): parse Protobuf, forward via requests.post(UPSTREAM_URL, ...)


def on_disconnect(client, userdata, rc):
    if rc != 0:
        logger.warning("Unexpected disconnect (rc=%d) — will auto-reconnect", rc)


# ---------------------------------------------------------------------------
# Graceful shutdown
# ---------------------------------------------------------------------------

def handle_signal(signum, frame):
    logger.info("Received signal %d — shutting down", signum)
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
        "gateway_translator starting — broker=%s:%d topic=%s",
        BROKER_HOST, BROKER_PORT, TELEMETRY_TOPIC,
    )

    client = mqtt.Client(client_id=CLIENT_ID, protocol=mqtt.MQTTv311)
    client.on_connect = on_connect
    client.on_message = on_message
    client.on_disconnect = on_disconnect

    # Enable automatic reconnect
    client.reconnect_delay_set(min_delay=1, max_delay=60)

    signal.signal(signal.SIGINT, handle_signal)
    signal.signal(signal.SIGTERM, handle_signal)

    try:
        client.connect(BROKER_HOST, BROKER_PORT, keepalive=60)
        client.loop_forever()
    except Exception as exc:
        logger.critical("Fatal error: %s", exc)
        sys.exit(1)
