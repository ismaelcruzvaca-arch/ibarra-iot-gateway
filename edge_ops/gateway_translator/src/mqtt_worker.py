"""mqtt_worker -- MQTT consumer with Protobuf deserialisation and
thread-safe micro-batching buffer.

Listens for ModbusBridgePayload messages on a configurable MQTT topic,
decodes each ModbusNodeState entry and queues it for batch insertion
into Hasura via HasuraClient.
"""

import logging
import threading
import time
from datetime import datetime, timezone
from typing import Any

import paho.mqtt.client as mqtt

from telemetry_pb2 import ModbusBridgePayload

logger = logging.getLogger(__name__)

# Backoff intervals for MQTT connection retries (1s, 2s, 4s)
_MQTT_CONNECT_BACKOFFS = [1, 2, 4]

_STATUS_MAP: dict[int, str] = {
    0: "UNKNOWN",
    1: "RUNNING",
    2: "STOPPED",
    3: "ALARM",
    4: "MAINTENANCE",
}


def _status_to_string(value: int) -> str:
    """Convert an OperatingStatus enum value to its human-readable string."""
    return _STATUS_MAP.get(value, str(value))


class GatewayWorker:
    """Subscribes to an MQTT topic, decodes Protobuf payloads, and buffers
    entries for batch insertion into Hasura."""

    def __init__(
        self,
        broker_host: str,
        hasura_client: "HasuraClient",  # noqa: F821
        *,
        broker_port: int = 1883,
        topic: str = "novamex/linea1/telemetry",
        client_id: str = "gateway-translator",
    ) -> None:
        """Initialise the GatewayWorker.

        Args:
            broker_host: MQTT broker hostname or IP.
            hasura_client: Pre-configured HasuraClient.
            broker_port: MQTT broker port (default 1883).
            topic: MQTT topic to subscribe to.
            client_id: MQTT client identifier.
        """
        self._topic = topic
        self._hasura = hasura_client
        self._lock = threading.Lock()
        self._buffer: list[dict[str, Any]] = []

        self._client = mqtt.Client(
            callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
            client_id=client_id,
        )
        self._client.on_connect = self._on_connect
        self._client.on_message = self._on_message

        logger.info(
            "GatewayWorker configured -- broker=%s:%d topic=%s",
            broker_host,
            broker_port,
            topic,
        )

        # MQTT connection with backoff retry (1s, 2s, 4s)
        last_error: Exception | None = None
        for attempt, backoff in enumerate(_MQTT_CONNECT_BACKOFFS):
            try:
                self._client.connect(broker_host, broker_port, keepalive=60)
                last_error = None
                break
            except Exception as exc:
                last_error = exc
                logger.warning(
                    "MQTT connect attempt %d/%d failed: %s "
                    "-- retrying in %ds ...",
                    attempt + 1,
                    len(_MQTT_CONNECT_BACKOFFS),
                    exc,
                    backoff,
                )
                time.sleep(backoff)

        if last_error is not None:
            logger.error(
                "Failed to connect to MQTT broker %s:%d "
                "after %d attempts.",
                broker_host,
                broker_port,
                len(_MQTT_CONNECT_BACKOFFS),
            )
            raise last_error

    def _on_connect(
        self,
        _client: mqtt.Client,
        _userdata: Any,
        _flags: mqtt.ConnectFlags,
        reason_code: mqtt.ReasonCodes,
        _properties: Any,
    ) -> None:
        """Callback when the MQTT client connects (or reconnects)."""
        if reason_code == 0:
            logger.info(
                "Connected to MQTT broker. Subscribing to %s ...",
                self._topic,
            )
            self._client.subscribe(self._topic, qos=1)
        else:
            logger.error(
                "MQTT connection failed -- reason_code=%s",
                reason_code,
            )

    def _on_message(
        self,
        _client: mqtt.Client,
        _userdata: Any,
        msg: mqtt.MQTTMessage,
    ) -> None:
        """Decode a ModbusBridgePayload and enqueue each ModbusNodeState."""
        try:
            payload = ModbusBridgePayload()
            payload.ParseFromString(msg.payload)
        except Exception:
            logger.exception(
                "Failed to decode Protobuf from topic=%s len=%d",
                msg.topic,
                len(msg.payload),
            )
            return

        now_iso = datetime.now(timezone.utc).isoformat()

        for node in payload.nodes:
            entry = {
                "event_ts": now_iso,
                "node_id": node.node_id,
                "cycle_count": node.cycle_count,
                "status": _status_to_string(node.status),
            }
            self.enqueue_telemetry(entry)

        logger.debug(
            "Enqueued %d telemetry entries.",
            len(payload.nodes),
        )

    def enqueue_telemetry(self, data: dict[str, Any]) -> None:
        """Thread-safe append to the internal buffer."""
        with self._lock:
            self._buffer.append(data)

    def drain_buffer(self) -> list[dict[str, Any]]:
        """Atomically swap the buffer with a fresh empty list.

        Returns:
            The previous buffer contents.
        """
        with self._lock:
            items = self._buffer
            self._buffer = []
        return items

    def start(self) -> None:
        """Start the MQTT network loop in a background daemon thread."""
        self._client.loop_start()
        logger.info("MQTT loop started (background thread).")

    def stop(self) -> None:
        """Gracefully stop the MQTT loop."""
        self._client.loop_stop()
        self._client.disconnect()
        logger.info("MQTT loop stopped.")
