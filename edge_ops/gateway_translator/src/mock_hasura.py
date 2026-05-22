"""
mock_hasura — Servidor HTTP que simula Hasura GraphQL para pruebas.

Endpoints:
  POST /v1/graphql  → mutación (200 OK) o error (500) según estado
  GET  /kill        → activa modo falla (responde 500)
  GET  /revive      → desactiva modo falla
  GET  /status      → estado actual + contadores
"""

import json
import logging
from http.server import HTTPServer, BaseHTTPRequestHandler
from typing import Any

logger = logging.getLogger("mock_hasura")

_killed = False
_total_requests = 0
_total_rows = 0


class _Handler(BaseHTTPRequestHandler):
    def do_GET(self) -> None:
        global _killed
        path = self.path.rstrip("/")
        if path == "/kill":
            _killed = True
            self._ok({"status": "killed"})
        elif path == "/revive":
            _killed = False
            self._ok({"status": "alive"})
        elif path == "/status":
            self._ok({
                "killed": _killed,
                "total_requests": _total_requests,
                "total_rows": _total_rows,
            })
        else:
            self._ok({"status": "running", "killed": _killed})

    def do_POST(self) -> None:
        global _total_requests, _total_rows
        _total_requests += 1

        content_len = int(self.headers.get("Content-Length", 0))
        body = json.loads(self.rfile.read(content_len)) if content_len else {}

        objects = body.get("variables", {}).get("objects", [])

        if _killed:
            logger.warning(
                "Mock Hasura KILLED — returning 500 (batch_size=%d)",
                len(objects),
            )
            self._err(500, "Hasura is down (mock killed)")
            return

        _total_rows += len(objects)

        logger.info(
            "Mutation received — batch_size=%d total_rows=%d total_requests=%d",
            len(objects), _total_rows, _total_requests,
        )

        self._ok({
            "data": {
                "insert_norvi_telemetry": {
                    "affected_rows": len(objects),
                }
            }
        })

    def _ok(self, data: dict[str, Any]) -> None:
        body = json.dumps(data).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.end_headers()
        self.wfile.write(body)

    def _err(self, code: int, msg: str) -> None:
        body = json.dumps({"error": msg}).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, fmt: str, *args: Any) -> None:
        logger.debug(fmt, *args)


def start_mock(host: str = "127.0.0.1", port: int = 8080) -> HTTPServer:
    server = HTTPServer((host, port), _Handler)
    logger.info("Mock Hasura listening on http://%s:%d", host, port)
    return server


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO)
    srv = start_mock()
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        srv.shutdown()
