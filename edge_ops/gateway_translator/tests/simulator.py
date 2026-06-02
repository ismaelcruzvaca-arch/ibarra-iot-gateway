"""simulator -- MQTT telemetry simulator for integration testing.

Generates random ModbusBridgePayload Protobuf messages and publishes them
through a real paho-mqtt client.  Designed to work with MiniBroker for
fully isolated test scenarios.
"""

from __future__ import annotations

import logging
import random
import threading
import time
from typing import Any

logger = logging.getLogger(__name__)


class TelemetrySimulator:
    """Publishes synthetic ModbusBridgePayload messages on an MQTT topic.

    Usage::

        sim = TelemetrySimulator(port=broker.port)
        sim.send_once()
        sim.send_burst(count=10, interval_ms=50)
        sim.start_background(interval_s=2)
        # ...
        sim.stop_background()
    """

    def __init__(
        self,
        broker_host: str = "127.0.0.1",
        port: int = 1883,
        topic: str = "novamex/linea1/telemetry",
        node_ids: list[str] | None = None,
    ) -> None:
        self._broker_host = broker_host
        self._port = port
        self._topic = topic
        self._node_ids = node_ids or [
            "NORVI_01",
            "NORVI_02",
            "NORVI_03",
        ]

        self._client: Any = None
        self._running = False
        self._thread: threading.Thread | None = None

    # ------------------------------------------------------------------
    # Public API
    # ------------------------------------------------------------------

    @property
    def node_ids(self) -> list[str]:
        """Node IDs the simulator cycles through."""
        return list(self._node_ids)

    def generate_payload(
        self,
        node_id: str | None = None,
        status: int | None = None,
    ) -> bytes:
        """Return serialized ``ModbusBridgePayload`` Protobuf bytes.

        Args:
            node_id: Node ID to use (random if ``None``).
            status: ``OperatingStatus`` enum value (random if ``None``).

        Returns:
            Serialized Protobuf bytes ready for MQTT publish.
        """
        # Late import so the simulator can be imported before the Protobuf
        # module is fully loaded (e.g. during pytest collection).
        from telemetry_pb2 import ModbusBridgePayload  # type: ignore[import-untyped]

        payload = ModbusBridgePayload()
        node = payload.nodes.add()
        node.node_id = node_id or random.choice(self._node_ids)
        node.cycle_count = random.randint(1, 10_000)
        node.status = (
            status if status is not None else random.choice([0, 1, 2, 3, 4])
        )
        return payload.SerializeToString()

    def send_once(self) -> int:
        """Publish a single random telemetry message.

        Returns:
            ``1`` (number of messages published).
        """
        if self._client is None:
            self._client = self._make_client()
        node_id = random.choice(self._node_ids)
        data = self.generate_payload(node_id=node_id)
        self._client.publish(self._topic, data, qos=1)
        logger.debug("Published 1 message to %s", self._topic)
        return 1

    def send_burst(self, count: int = 10, interval_ms: int = 100) -> int:
        """Publish *count* messages with *interval_ms* between each.

        Returns:
            Number of messages actually published.
        """
        if self._client is None:
            self._client = self._make_client()

        sent = 0
        for _ in range(count):
            node_id = random.choice(self._node_ids)
            data = self.generate_payload(node_id=node_id)
            self._client.publish(self._topic, data, qos=1)
            sent += 1
            if interval_ms > 0:
                time.sleep(interval_ms / 1000.0)

        logger.debug("Published %d messages to %s", sent, self._topic)
        return sent

    def start_background(self, interval_s: float = 1.0) -> None:
        """Start a background thread that publishes one message every
        *interval_s* seconds."""
        if self._client is None:
            self._client = self._make_client()
        self._running = True
        self._thread = threading.Thread(
            target=self._run_loop,
            args=(interval_s,),
            daemon=True,
        )
        self._thread.start()

    def stop_background(self) -> None:
        """Stop the background publishing thread and disconnect."""
        self._running = False
        if self._thread is not None:
            self._thread.join(timeout=3)
        self._disconnect()

    # ------------------------------------------------------------------
    # Internal helpers
    # ------------------------------------------------------------------

    def _make_client(self) -> Any:
        """Create and connect a paho-mqtt client."""
        import paho.mqtt.client as mqtt

        client = mqtt.Client(
            callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
            client_id=f"sim-{random.randint(0, 99999)}",
        )
        client.connect(self._broker_host, self._port, keepalive=10)
        client.loop_start()
        return client

    def _run_loop(self, interval_s: float) -> None:
        while self._running:
            self.send_once()
            time.sleep(interval_s)

    def _disconnect(self) -> None:
        if self._client is not None:
            try:
                self._client.loop_stop()
                self._client.disconnect()
            except Exception:
                logger.exception("Error disconnecting simulator client")
            self._client = None
