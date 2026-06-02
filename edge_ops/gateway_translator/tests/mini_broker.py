"""mini_broker -- Minimal MQTT v3.1.1 broker for test isolation.

Provides a MiniBroker class that implements enough of the MQTT v3.1.1
protocol for paho-mqtt clients to connect, subscribe, and exchange
messages — without requiring an external Mosquitto broker.
"""

from __future__ import annotations

import logging
import socket
import struct
import threading
from typing import Any

logger = logging.getLogger(__name__)


def _encode_remaining(length: int) -> bytes:
    """Encode *length* using MQTT variable-length encoding (1–4 bytes)."""
    buf = bytearray()
    while True:
        byte = length % 128
        length //= 128
        if length > 0:
            byte |= 0x80
        buf.append(byte)
        if length == 0:
            break
    return bytes(buf)


def _decode_remaining(reader: socket.socket) -> int | None:
    """Read and decode an MQTT variable-length integer from *reader*."""
    multiplier = 1
    value = 0
    while True:
        data = _recv_exact(reader, 1)
        if data is None:
            return None
        value += (data[0] & 0x7F) * multiplier
        multiplier *= 128
        if not (data[0] & 0x80):
            break
    return value


def _recv_exact(reader: socket.socket, n: int) -> bytes | None:
    """Receive exactly *n* bytes, or return ``None`` on EOF."""
    buf = bytearray()
    while len(buf) < n:
        try:
            chunk = reader.recv(n - len(buf))
        except OSError:
            return None
        if not chunk:
            return None
        buf.extend(chunk)
    return bytes(buf)


class MiniBroker:
    """Minimal single-host MQTT v3.1.1 broker for use in integration tests.

    The broker listens on ``127.0.0.1`` and picks a random ephemeral port
    when ``port`` is ``0`` (the default).  After starting, use the
    :attr:`port` property to configure MQTT clients::

        broker = MiniBroker()
        broker.start()
        worker = GatewayWorker("127.0.0.1", hasura, broker_port=broker.port)
        worker.start()
        # ...
        broker.shutdown()
    """

    def __init__(self, host: str = "127.0.0.1", port: int = 0) -> None:
        self._host = host
        self._port = port
        self._server: socket.socket | None = None
        self._thread: threading.Thread | None = None
        self._running = False

        self._lock = threading.Lock()
        # conn_id -> set[subscribed_topic_str]
        self._subscriptions: dict[int, set[str]] = {}
        # conn_id -> (socket, peer_address)
        self._conn_writers: dict[int, tuple[socket.socket, Any]] = {}
        self._next_id = 0
        self._pending_qos2: dict[int, bool] = {}

    # ------------------------------------------------------------------
    # Public API
    # ------------------------------------------------------------------

    @property
    def port(self) -> int:
        """Actual listening port (useful when initial ``port`` was ``0``)."""
        return self._port

    def start(self) -> None:
        """Start the broker in a background daemon thread."""
        self._server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._server.bind((self._host, self._port))
        self._server.listen(5)
        self._server.settimeout(1.0)
        self._port = self._server.getsockname()[1]

        self._running = True
        self._thread = threading.Thread(
            target=self._accept_loop, daemon=True
        )
        self._thread.start()
        logger.info("MiniBroker listening on %s:%d", self._host, self._port)

    def shutdown(self) -> None:
        """Gracefully stop the broker and close all client connections."""
        self._running = False
        if self._server is not None:
            try:
                self._server.close()
            except OSError:
                pass
        with self._lock:
            for sock, _ in self._conn_writers.values():
                try:
                    sock.close()
                except OSError:
                    pass
            self._subscriptions.clear()
            self._conn_writers.clear()
        if self._thread is not None:
            self._thread.join(timeout=2)
        logger.info("MiniBroker stopped.")

    # ------------------------------------------------------------------
    # Internal — accept loop
    # ------------------------------------------------------------------

    def _accept_loop(self) -> None:
        while self._running:
            try:
                client, addr = self._server.accept()  # type: ignore[union-attr]
            except socket.timeout:
                continue
            except OSError:
                break

            conn_id = self._next_id
            self._next_id += 1
            t = threading.Thread(
                target=self._handle_client,
                args=(conn_id, client, addr),
                daemon=True,
            )
            t.start()

    # ------------------------------------------------------------------
    # Internal — per-client connection handler
    # ------------------------------------------------------------------

    def _handle_client(
        self, conn_id: int, sock: socket.socket, addr: Any
    ) -> None:
        logger.debug("Client %d connected from %s:%d", conn_id, *addr[:2])
        with self._lock:
            self._conn_writers[conn_id] = (sock, addr)
            self._subscriptions[conn_id] = set()

        try:
            while True:
                header = _recv_exact(sock, 1)
                if header is None:
                    break
                packet_type = header[0] & 0xF0

                remaining = _decode_remaining(sock)
                if remaining is None:
                    break

                if packet_type == 0x10:
                    self._handle_connect(sock, remaining)
                elif packet_type == 0x80:
                    self._handle_subscribe(sock, conn_id, remaining)
                elif packet_type == 0x30:
                    # PUBLISH (QoS 0: 0x30, QoS 1: 0x32, QoS 2: 0x34)
                    qos = (header[0] >> 1) & 0x03
                    if qos == 2:
                        self._handle_publish_qos2(sock, conn_id, remaining)
                    elif qos == 1:
                        self._handle_publish_qos1(sock, conn_id, remaining)
                    else:
                        self._handle_publish_qos0(sock, conn_id, remaining)
                elif packet_type == 0x60:
                    # PUBREL (0x62 → masked 0x60) → respond PUBCOMP
                    self._handle_pubrel(sock, remaining)
                elif packet_type == 0x50:
                    # PUBREC (0x50 → masked 0x50) → respond PUBREL
                    self._handle_pubrec(sock, remaining)
                elif packet_type == 0xC0:
                    _send_packet(sock, b"\xD0\x00")  # PINGRESP
                elif packet_type == 0xE0:
                    break  # DISCONNECT
                else:
                    logger.warning(
                        "Client %d sent unknown packet 0x%02x",
                        conn_id,
                        packet_type,
                    )
                    break
        except (ConnectionResetError, ConnectionError, OSError):
            pass
        finally:
            with self._lock:
                self._subscriptions.pop(conn_id, None)
                self._conn_writers.pop(conn_id, None)
            try:
                sock.close()
            except OSError:
                pass

    # ------------------------------------------------------------------
    # Internal — MQTT packet handlers
    # ------------------------------------------------------------------

    @staticmethod
    def _handle_connect(sock: socket.socket, remaining: int) -> None:
        """Read the CONNECT payload and reply with CONNACK (accepted)."""
        data = _recv_exact(sock, remaining)
        if data is None:
            return
        # CONNACK: session_present=0x00, return_code=0x00 (accepted)
        _send_packet(sock, b"\x20\x02\x00\x00")

    def _handle_subscribe(
        self, sock: socket.socket, conn_id: int, remaining: int
    ) -> None:
        """Read SUBSCRIBE payload, register topics, send SUBACK."""
        data = _recv_exact(sock, remaining)
        if data is None:
            return

        packet_id = struct.unpack("!H", data[0:2])[0]
        pos = 2
        topics: set[str] = set()

        while pos < len(data):
            topic_len = struct.unpack("!H", data[pos : pos + 2])[0]
            pos += 2
            topic = data[pos : pos + topic_len].decode("utf-8", errors="replace")
            pos += topic_len
            if pos < len(data):
                pos += 1  # skip requested QoS byte
            topics.add(topic)

        with self._lock:
            self._subscriptions[conn_id] = topics

        # SUBACK: packet_id + granted QoS 0 per topic
        granted = b"\x00" * len(topics)
        suback_payload = struct.pack("!H", packet_id) + granted
        _send_packet(
            sock,
            b"\x90" + _encode_remaining(len(suback_payload)) + suback_payload,
        )

    def _handle_publish_qos0(
        self, sock: socket.socket, conn_id: int, remaining: int
    ) -> None:
        """Read a QoS 0 PUBLISH and forward to subscribers."""
        data = _recv_exact(sock, remaining)
        if data is None:
            return
        topic, payload = _parse_publish(data, qos=0)
        if topic is not None:
            self._forward(topic, payload, exclude=conn_id)

    def _handle_publish_qos2(
        self, sock: socket.socket, conn_id: int, remaining: int
    ) -> None:
        """Read a QoS 2 PUBLISH, send PUBREC, and forward to subscribers."""
        data = _recv_exact(sock, remaining)
        if data is None:
            return
        topic, payload_and_pid = _parse_publish(data, qos=1)
        if topic is None:
            return

        topic_name, inner = payload_and_pid
        packet_id = struct.unpack("!H", inner[:2])[0]
        payload_bytes = inner[2:]

        # PUBREC (0x50): acknowledge receipt, wait for PUBREL
        _send_packet(
            sock,
            b"\x50\x02" + struct.pack("!H", packet_id),
        )
        # Store packet ID for PUBREL→PUBCOMP handshake
        with self._lock:
            if not hasattr(self, "_pending_qos2"):
                self._pending_qos2: dict[int, bool] = {}
            self._pending_qos2[packet_id] = True

        self._forward(topic_name, payload_bytes, exclude=conn_id)

    def _handle_pubrec(
        self, sock: socket.socket, remaining: int
    ) -> None:
        """Respond to PUBREC with PUBREL (QoS 2 handshake, receiver side)."""
        data = _recv_exact(sock, remaining)
        if data is None or len(data) < 2:
            return
        packet_id = struct.unpack("!H", data[:2])[0]
        # PUBREL (0x62)
        _send_packet(
            sock,
            b"\x62\x02" + struct.pack("!H", packet_id),
        )

    def _handle_pubrel(
        self, sock: socket.socket, remaining: int
    ) -> None:
        """Respond to PUBREL with PUBCOMP (QoS 2 handshake completion)."""
        data = _recv_exact(sock, remaining)
        if data is None or len(data) < 2:
            return
        packet_id = struct.unpack("!H", data[:2])[0]
        # PUBCOMP (0x70)
        _send_packet(
            sock,
            b"\x70\x02" + struct.pack("!H", packet_id),
        )
        with self._lock:
            if hasattr(self, "_pending_qos2"):
                self._pending_qos2.pop(packet_id, None)

    def _handle_publish_qos1(
        self, sock: socket.socket, conn_id: int, remaining: int
    ) -> None:
        """Read a QoS 1 PUBLISH, reply PUBACK, and forward to subscribers."""
        data = _recv_exact(sock, remaining)
        if data is None:
            return
        topic, payload_and_pid = _parse_publish(data, qos=1)
        if topic is None:
            return

        topic_name, inner = payload_and_pid
        packet_id = struct.unpack("!H", inner[:2])[0]
        payload_bytes = inner[2:]

        # PUBACK
        _send_packet(
            sock,
            b"\x40\x02" + struct.pack("!H", packet_id),
        )
        self._forward(topic_name, payload_bytes, exclude=conn_id)

    # ------------------------------------------------------------------
    # Internal — forwarding
    # ------------------------------------------------------------------

    def _forward(
        self,
        topic: str,
        payload: bytes,
        exclude: int | None = None,
    ) -> None:
        """Forward a PUBLISH (QoS 0) to every subscribed client."""
        topic_enc = struct.pack("!H", len(topic)) + topic.encode()
        publish_packet = (
            b"\x30"
            + _encode_remaining(len(topic_enc) + len(payload))
            + topic_enc
            + payload
        )

        with self._lock:
            for cid, topics in self._subscriptions.items():
                if cid == exclude:
                    continue
                if topic in topics:
                    writer, _ = self._conn_writers.get(cid, (None, None))
                    if writer is not None:
                        try:
                            writer.sendall(publish_packet)
                        except OSError:
                            pass


# ------------------------------------------------------------------
# Module-level helpers
# ------------------------------------------------------------------


def _send_packet(sock: socket.socket, data: bytes) -> None:
    """Send raw bytes over *sock*, swallowing socket errors."""
    try:
        sock.sendall(data)
    except OSError:
        pass


def _parse_publish(
    data: bytes, qos: int = 0
) -> tuple[str | None, Any]:
    """Extract ``(topic, rest)`` from a PUBLISH variable header + payload.

    For QoS 0, *rest* is the raw payload.
    For QoS 1, *rest* is ``(topic, packet_id_bytes + payload)``.
    """
    if len(data) < 2:
        return None, b""
    topic_len = struct.unpack("!H", data[0:2])[0]
    if 2 + topic_len > len(data):
        return None, b""
    topic = data[2 : 2 + topic_len].decode("utf-8", errors="replace")
    rest = data[2 + topic_len :]
    if qos == 0:
        return topic, rest
    # QoS 1: next 2 bytes are packet ID
    return topic, (topic, rest)
