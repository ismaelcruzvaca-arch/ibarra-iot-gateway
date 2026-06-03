"""mock_hasura -- Lightweight HTTP server that mocks the Hasura GraphQL endpoint.

Provides a MockHasuraServer class that can be configured to simulate
success, 4xx errors, 5xx errors, or timeouts for integration testing.
"""

from __future__ import annotations

import json
import logging
import threading
from http import HTTPStatus
from http.server import HTTPServer, BaseHTTPRequestHandler
from socketserver import ThreadingMixIn
from typing import Any

logger = logging.getLogger(__name__)


class _ThreadedHTTPServer(ThreadingMixIn, HTTPServer):
    """HTTPServer that handles each request in a new daemon thread."""

    allow_reuse_address = True
    daemon_threads = True


class _MockHasuraHandler(BaseHTTPRequestHandler):
    """Request handler that parses InsertTelemetryBatch mutations."""

    server_version = "MockHasura/0.1"

    def do_POST(self) -> None:
        """Handle POST requests — only /v1/graphql is valid."""
        if self.path != "/v1/graphql":
            self._respond(HTTPStatus.NOT_FOUND, {"error": "Not found"})
            return

        server: MockHasuraServer = self.server  # type: ignore[assignment]
        fail_mode: str | None = getattr(server, "fail_mode", None)

        content_length = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(content_length) if content_length > 0 else b""

        # --- Failure modes ---
        if fail_mode == "timeout":
            # Read the body but never send a response — the client's
            # requests.post(timeout=…) will raise RequestException.
            return

        if fail_mode == "500":
            self._respond(
                HTTPStatus.INTERNAL_SERVER_ERROR,
                {"error": "Internal Server Error"},
            )
            return

        if fail_mode == "400":
            self._respond(
                HTTPStatus.BAD_REQUEST,
                {"error": "Bad request"},
            )
            return

        # --- Normal (success) mode ---
        try:
            payload: dict[str, Any] = json.loads(body)
        except (ValueError, TypeError):
            self._respond(
                HTTPStatus.BAD_REQUEST,
                {"error": "Invalid JSON body"},
            )
            return

        variables = payload.get("variables", {})
        objects: list[dict[str, Any]] = variables.get("objects", [])

        query: str = payload.get("query", "")
        if "InsertTelemetryBatch" not in query:
            self._respond(
                HTTPStatus.BAD_REQUEST,
                {"error": "Unknown GraphQL mutation"},
            )
            return

        # Record received objects for test assertions.
        received: list[dict[str, Any]] | None = getattr(
            server, "_received", None
        )
        if received is not None:
            with server._lock:
                received.extend(objects)

        self._respond(
            HTTPStatus.OK,
            {
                "data": {
                    "insert_norvi_telemetry": {
                        "affected_rows": len(objects),
                    },
                },
            },
        )

    def _respond(
        self, status: HTTPStatus, body: dict[str, Any]
    ) -> None:
        """Send a JSON response."""
        payload = json.dumps(body).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def log_message(self, fmt: str, *args: Any) -> None:
        logger.debug("MockHasura: %s", fmt % args)


class MockHasuraServer:
    """A lightweight HTTP server that mocks the Nhost Hasura GraphQL endpoint.

    Usage::

        server = MockHasuraServer()
        server.start()
        client = HasuraClient(server.url(), "my-secret")
        # ...
        server.shutdown()

    Control failure simulation via the ``fail_mode`` property:

    * ``None`` (default) — returns ``{data: …}`` with ``affected_rows``
    * ``"400"`` — returns HTTP 400
    * ``"500"`` — returns HTTP 500
    * ``"timeout"`` — reads the body but never sends a response
    """

    def __init__(self, port: int = 0) -> None:
        self._port = port
        self._lock = threading.Lock()
        self._received: list[dict[str, Any]] = []
        self._server: _ThreadedHTTPServer | None = None
        self._thread: threading.Thread | None = None

    # ------------------------------------------------------------------
    # Public API
    # ------------------------------------------------------------------

    @property
    def url(self) -> str:
        """Return the full GraphQL endpoint URL for HasuraClient."""
        return f"http://127.0.0.1:{self._port}/v1/graphql"

    @property
    def fail_mode(self) -> str | None:
        """Current failure simulation mode."""
        return getattr(self._server, "fail_mode", None) if self._server else None

    @fail_mode.setter
    def fail_mode(self, mode: str | None) -> None:
        """Set failure mode: ``None``, ``"400"``, ``"500"``, or ``"timeout"``."""
        if self._server is not None:
            self._server.fail_mode = mode

    def received_objects(self) -> list[dict[str, Any]]:
        """Return a copy of all objects received by the mock server."""
        with self._lock:
            return list(self._received)

    def clear_received(self) -> None:
        """Clear the received-objects accumulator."""
        with self._lock:
            self._received.clear()

    def start(self) -> None:
        """Start the server on ``127.0.0.1`` in a background thread."""
        self._server = _ThreadedHTTPServer(
            ("127.0.0.1", self._port), _MockHasuraHandler
        )
        self._server.fail_mode = None
        self._server._lock = self._lock
        self._server._received = self._received
        self._port = self._server.server_address[1]

        self._thread = threading.Thread(
            target=self._server.serve_forever, daemon=True
        )
        self._thread.start()
        logger.info("MockHasura listening on %s", self.url)

    def shutdown(self) -> None:
        """Gracefully stop the server."""
        if self._server is not None:
            self._server.shutdown()
        if self._thread is not None:
            self._thread.join(timeout=2)
