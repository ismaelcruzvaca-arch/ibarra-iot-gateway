"""Integration & resilience tests for the gateway_translator service.

Tests cover the full pipeline: MiniBroker (MQTT) → GatewayWorker →
MockHasuraServer (GraphQL), plus failure simulation for error handling
paths (5xx, 4xx, timeout) and the main entry point env-var checks.
"""

from __future__ import annotations

import time
from typing import TYPE_CHECKING

import pytest

# Local test helpers (same-package siblings)
from mock_hasura import MockHasuraServer
from mini_broker import MiniBroker
from simulator import TelemetrySimulator

# Source modules — conftest.py adds ``src/`` to sys.path
from hasura_client import HasuraClient
from mqtt_worker import GatewayWorker

if TYPE_CHECKING:
    from collections.abc import Generator

# ---------------------------------------------------------------------------
# Fixtures
# ---------------------------------------------------------------------------


@pytest.fixture
def mock_hasura() -> Generator[MockHasuraServer, None, None]:
    """Start a MockHasuraServer on a random port."""
    server = MockHasuraServer()
    server.start()
    yield server
    server.shutdown()


@pytest.fixture
def mini_broker() -> Generator[MiniBroker, None, None]:
    """Start a MiniBroker on a random port."""
    broker = MiniBroker()
    broker.start()
    yield broker
    broker.shutdown()


@pytest.fixture
def hasura_client(mock_hasura: MockHasuraServer) -> HasuraClient:
    """Pre-configured HasuraClient pointing at the mock server."""
    return HasuraClient(mock_hasura.url, "test-secret")


# ---------------------------------------------------------------------------
# HasuraClient — Happy path
# ---------------------------------------------------------------------------


class TestHasuraClientHappy:
    """HasuraClient normal-operation scenarios."""

    def test_happy_path(
        self, mock_hasura: MockHasuraServer, hasura_client: HasuraClient
    ) -> None:
        """FR-4: Batch success — GIVEN N records WHEN flush THEN affected_rows == N."""
        objects = [
            {
                "event_ts": "2024-01-01T00:00:00Z",
                "node_id": "NORVI_01",
                "cycle_count": 42,
                "status": "RUNNING",
            },
        ]
        assert hasura_client.insert_telemetry_batch(objects) is True

        received = mock_hasura.received_objects()
        assert len(received) == 1
        assert received[0]["node_id"] == "NORVI_01"

    def test_empty_batch_is_noop(
        self, hasura_client: HasuraClient
    ) -> None:
        """FR-3: Empty flush — GIVEN empty buffer WHEN timer fires THEN no GraphQL call (returns True)."""
        assert hasura_client.insert_telemetry_batch([]) is True

    def test_multiple_rows(
        self, mock_hasura: MockHasuraServer, hasura_client: HasuraClient
    ) -> None:
        """Batch with 5 rows returns True and all rows are recorded."""
        objects = [
            {
                "event_ts": "2024-01-01T00:00:00Z",
                "node_id": f"NORVI_{i:02d}",
                "cycle_count": i,
                "status": "RUNNING",
            }
            for i in range(5)
        ]
        assert hasura_client.insert_telemetry_batch(objects) is True
        assert len(mock_hasura.received_objects()) == 5


# ---------------------------------------------------------------------------
# HasuraClient — Error handling
# ---------------------------------------------------------------------------


class TestHasuraClientErrors:
    """HasuraClient failure-handling scenarios."""

    def test_500_error(
        self, mock_hasura: MockHasuraServer, hasura_client: HasuraClient
    ) -> None:
        """FR-6: HTTP 503 — GIVEN server error WHEN flush THEN retry..."""
        mock_hasura.fail_mode = "500"
        objects = [
            {
                "event_ts": "2024-01-01T00:00:00Z",
                "node_id": "NORVI_01",
                "cycle_count": 1,
                "status": "RUNNING",
            },
        ]
        assert hasura_client.insert_telemetry_batch(objects) is False

    def test_400_error_not_retried(
        self, mock_hasura: MockHasuraServer, hasura_client: HasuraClient
    ) -> None:
        """FR-6: HTTP 400 — GIVEN client error WHEN flush THEN NOT retried (returns False)."""
        mock_hasura.fail_mode = "400"
        objects = [
            {
                "event_ts": "2024-01-01T00:00:00Z",
                "node_id": "NORVI_01",
                "cycle_count": 1,
                "status": "RUNNING",
            },
        ]
        assert hasura_client.insert_telemetry_batch(objects) is False

    def test_timeout(
        self, mock_hasura: MockHasuraServer, hasura_client: HasuraClient
    ) -> None:
        """FR-6: Network timeout — GIVEN no response WHEN flush THEN error (returns False)."""
        mock_hasura.fail_mode = "timeout"
        objects = [
            {
                "event_ts": "2024-01-01T00:00:00Z",
                "node_id": "NORVI_01",
                "cycle_count": 1,
                "status": "RUNNING",
            },
        ]
        assert hasura_client.insert_telemetry_batch(objects) is False

    def test_wrong_endpoint_returns_false(
        self, hasura_client: HasuraClient
    ) -> None:
        """GIVEN wrong endpoint URL WHEN flush THEN False (connection refused)."""
        bad_client = HasuraClient(
            "http://127.0.0.1:1/v1/graphql", "test-secret"
        )
        objects = [
            {
                "event_ts": "2024-01-01T00:00:00Z",
                "node_id": "NORVI_01",
                "cycle_count": 1,
                "status": "RUNNING",
            },
        ]
        assert bad_client.insert_telemetry_batch(objects) is False


# ---------------------------------------------------------------------------
# GatewayWorker — MQTT receive and drain
# ---------------------------------------------------------------------------


class TestGatewayWorkerReceive:
    """GatewayWorker subscription and buffer-population scenarios."""

    def test_receive_message(
        self, mini_broker: MiniBroker, hasura_client: HasuraClient
    ) -> None:
        """FR-1: Subscribe — GIVEN broker reachable WHEN msg published THEN buffer has entry."""
        worker = GatewayWorker(
            "127.0.0.1", hasura_client, broker_port=mini_broker.port
        )
        worker.start()
        time.sleep(0.5)  # allow connect + subscribe to complete

        sim = TelemetrySimulator(
            port=mini_broker.port, topic="novamex/linea1/telemetry"
        )
        sim.send_once()
        time.sleep(0.5)  # allow message to flow through

        batch = worker.drain_buffer()
        assert len(batch) > 0
        assert batch[0]["node_id"] in sim.node_ids
        assert batch[0]["status"] in ("UNKNOWN", "RUNNING", "STOPPED", "ALARM", "MAINTENANCE")

        worker.stop()

    def test_drain_to_hasura(
        self,
        mini_broker: MiniBroker,
        mock_hasura: MockHasuraServer,
        hasura_client: HasuraClient,
    ) -> None:
        """FR-4: Full pipeline — GIVEN published message WHEN drain+insert THEN Hasura receives it."""
        worker = GatewayWorker(
            "127.0.0.1", hasura_client, broker_port=mini_broker.port
        )
        worker.start()
        time.sleep(0.5)

        sim = TelemetrySimulator(
            port=mini_broker.port, topic="novamex/linea1/telemetry"
        )
        sim.send_once()
        time.sleep(0.5)

        batch = worker.drain_buffer()
        assert len(batch) > 0

        ok = hasura_client.insert_telemetry_batch(batch)
        assert ok is True
        assert len(mock_hasura.received_objects()) == len(batch)

        worker.stop()

    def test_empty_drain_is_safe(
        self, mini_broker: MiniBroker, hasura_client: HasuraClient
    ) -> None:
        """FR-3: Empty no-op — GIVEN no messages WHEN drain THEN empty list (no error)."""
        worker = GatewayWorker(
            "127.0.0.1", hasura_client, broker_port=mini_broker.port
        )
        worker.start()
        time.sleep(0.3)

        batch = worker.drain_buffer()
        assert batch == []

        worker.stop()

    def test_multiple_messages_batched(
        self,
        mini_broker: MiniBroker,
        mock_hasura: MockHasuraServer,
        hasura_client: HasuraClient,
    ) -> None:
        """GIVEN 5 published messages WHEN drain THEN all 5 returned."""
        worker = GatewayWorker(
            "127.0.0.1", hasura_client, broker_port=mini_broker.port
        )
        worker.start()
        time.sleep(0.5)

        sim = TelemetrySimulator(
            port=mini_broker.port, topic="novamex/linea1/telemetry"
        )
        sim.send_burst(count=5, interval_ms=50)
        time.sleep(0.5)

        batch = worker.drain_buffer()
        assert len(batch) == 5

        ok = hasura_client.insert_telemetry_batch(batch)
        assert ok is True
        assert len(mock_hasura.received_objects()) == 5

        worker.stop()


# ---------------------------------------------------------------------------
# GatewayWorker — Hasura failure and re-queue
# ---------------------------------------------------------------------------


class TestGatewayWorkerResilience:
    """GatewayWorker behaviour when Hasura is unavailable."""

    def test_hasura_down_requeue(
        self, mini_broker: MiniBroker, mock_hasura: MockHasuraServer
    ) -> None:
        """AD-2: Re-queue — GIVEN Hasura down WHEN drain+insert fails THEN entries re-queued."""
        mock_hasura.fail_mode = "500"
        hasura = HasuraClient(mock_hasura.url, "test-secret")
        worker = GatewayWorker(
            "127.0.0.1", hasura, broker_port=mini_broker.port
        )
        worker.start()
        time.sleep(0.5)

        sim = TelemetrySimulator(
            port=mini_broker.port, topic="novamex/linea1/telemetry"
        )
        sim.send_once()
        time.sleep(0.5)

        # First drain — message arrives
        batch = worker.drain_buffer()
        assert len(batch) > 0

        # Insert fails
        ok = hasura.insert_telemetry_batch(batch)
        assert ok is False

        # Simulate main-loop re-queue
        for entry in batch:
            worker.enqueue_telemetry(entry)

        # Second drain — entries preserved
        re_queued = worker.drain_buffer()
        assert len(re_queued) == len(batch)

        worker.stop()

    def test_hasura_recovers_after_failure(
        self, mini_broker: MiniBroker, mock_hasura: MockHasuraServer
    ) -> None:
        """GIVEN Hasura fails then recovers WHEN retry THEN insert succeeds."""
        # Phase 1: Hasura is down
        mock_hasura.fail_mode = "500"
        hasura = HasuraClient(mock_hasura.url, "test-secret")
        worker = GatewayWorker(
            "127.0.0.1", hasura, broker_port=mini_broker.port
        )
        worker.start()
        time.sleep(0.5)

        sim = TelemetrySimulator(
            port=mini_broker.port, topic="novamex/linea1/telemetry"
        )
        sim.send_once()
        time.sleep(0.5)

        batch = worker.drain_buffer()
        assert len(batch) > 0
        ok = hasura.insert_telemetry_batch(batch)
        assert ok is False

        # Phase 2: Hasura recovers
        mock_hasura.fail_mode = None
        ok = hasura.insert_telemetry_batch(batch)
        assert ok is True
        assert len(mock_hasura.received_objects()) > 0

        worker.stop()


# ---------------------------------------------------------------------------
# Main module — env-var validation
# ---------------------------------------------------------------------------


class TestMainModule:
    """Startup validation in ``main.py``."""

    def test_env_missing_exits(self, monkeypatch: pytest.MonkeyPatch) -> None:
        """GIVEN all env vars missing WHEN _check_env THEN sys.exit(1)."""
        import main as main_module

        monkeypatch.setattr(main_module, "HASURA_GRAPHQL_URL", None)
        monkeypatch.setattr(main_module, "HASURA_ADMIN_SECRET", None)
        monkeypatch.setattr(main_module, "MQTT_BROKER_HOST", None)

        with pytest.raises(SystemExit) as exc:
            main_module._check_env()
        assert exc.value.code == 1

    def test_env_partial_missing_exits(
        self, monkeypatch: pytest.MonkeyPatch
    ) -> None:
        """GIVEN one env var missing WHEN _check_env THEN sys.exit(1)."""
        import main as main_module

        monkeypatch.setattr(
            main_module, "HASURA_GRAPHQL_URL", "http://test:8080/v1/graphql"
        )
        monkeypatch.setattr(main_module, "HASURA_ADMIN_SECRET", None)
        monkeypatch.setattr(
            main_module, "MQTT_BROKER_HOST", "192.168.1.50"
        )

        with pytest.raises(SystemExit) as exc:
            main_module._check_env()
        assert exc.value.code == 1

    def test_env_all_set_passes(
        self, monkeypatch: pytest.MonkeyPatch
    ) -> None:
        """GIVEN all env vars present WHEN _check_env THEN no exception."""
        import main as main_module

        monkeypatch.setattr(
            main_module,
            "HASURA_GRAPHQL_URL",
            "http://hasura:8080/v1/graphql",
        )
        monkeypatch.setattr(main_module, "HASURA_ADMIN_SECRET", "secret123")
        monkeypatch.setattr(main_module, "MQTT_BROKER_HOST", "192.168.1.50")

        # Should not raise
        main_module._check_env()


# ---------------------------------------------------------------------------
# TelemetrySimulator — payload generation and publish
# ---------------------------------------------------------------------------


class TestSimulator:
    """TelemetrySimulator correctness."""

    def test_generate_payload_structure(self) -> None:
        """GIVEN a node_id WHEN generate_payload THEN valid Protobuf bytes."""
        sim = TelemetrySimulator()
        data = sim.generate_payload(node_id="NORVI_TEST", status=1)

        assert isinstance(data, bytes)
        assert len(data) > 0

        # Round-trip through the Protobuf decoder
        from telemetry_pb2 import ModbusBridgePayload  # type: ignore[import-untyped]

        payload = ModbusBridgePayload()
        payload.ParseFromString(data)
        assert len(payload.nodes) == 1
        assert payload.nodes[0].node_id == "NORVI_TEST"
        assert payload.nodes[0].status == 1  # STATUS_RUNNING

    def test_generate_random_defaults(self) -> None:
        """GIVEN no args WHEN generate_payload THEN uses random values."""
        sim = TelemetrySimulator()
        data = sim.generate_payload()
        assert isinstance(data, bytes)
        assert len(data) > 0

    def test_simulator_publishes_to_broker(
        self, mini_broker: MiniBroker, hasura_client: HasuraClient
    ) -> None:
        """GIVEN MiniBroker + GatewayWorker WHEN simulator publishes THEN message received."""
        worker = GatewayWorker(
            "127.0.0.1", hasura_client, broker_port=mini_broker.port
        )
        worker.start()
        time.sleep(0.5)

        sim = TelemetrySimulator(
            port=mini_broker.port, topic="novamex/linea1/telemetry"
        )
        sim.send_once()
        time.sleep(0.5)

        batch = worker.drain_buffer()
        assert len(batch) > 0
        # The node_id should be one of the simulator's known IDs
        assert batch[0]["node_id"] in sim.node_ids

        worker.stop()
