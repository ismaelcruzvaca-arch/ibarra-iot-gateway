"""
mini_broker — MQTT v3.1.1 broker mínimo para pruebas sin Docker.

Soporta:
  - CONNECT / CONNACK
  - SUBSCRIBE / SUBACK
  - PUBLISH (QoS 0)
  - PINGREQ / PINGRESP
  - Forwarding a todos los subscribers del topic

NO soporta (innecesario para la prueba):
  - QoS > 0
  - Autenticación
  - Retained messages
  - Wildcards (+/#)
  - Persistencia
"""

import logging
import socket
import struct
import threading
from dataclasses import dataclass, field
from typing import Any

logger = logging.getLogger("mini_broker")


# ---------------------------------------------------------------------------
# MQTT Control Packet types
# ---------------------------------------------------------------------------
CONNECT     = 1
CONNACK     = 2
PUBLISH     = 3
PUBACK      = 4
SUBSCRIBE   = 8
SUBACK      = 9
PINGREQ     = 12
PINGRESP    = 13
DISCONNECT  = 14


def _read_remaining_length(s: socket.socket) -> tuple[int, bytes]:
    value = 0
    multiplier = 1
    raw = b""
    while True:
        byte = s.recv(1)
        raw += byte
        b = byte[0]
        value += (b & 127) * multiplier
        multiplier *= 128
        if not (b & 128):
            break
    return value, raw


def _decode_utf8(data: bytes, offset: int = 0) -> tuple[str, int]:
    length = struct.unpack_from("!H", data, offset)[0]
    start = offset + 2
    return data[start : start + length].decode("utf-8", errors="replace"), start + length


def _encode_utf8(s: str) -> bytes:
    encoded = s.encode("utf-8")
    return struct.pack("!H", len(encoded)) + encoded


# ---------------------------------------------------------------------------
# Subscriber registry
# ---------------------------------------------------------------------------
_sub_lock = threading.Lock()
_subscribers: list[tuple[str, socket.socket]] = []


def _broadcast(topic: str, payload: bytes) -> None:
    with _sub_lock:
        for sub_topic, sock in list(_subscribers):
            try:
                if sub_topic == topic:
                    _send_publish(sock, topic, payload)
            except OSError:
                _subscribers.remove((sub_topic, sock))


def _send_publish(sock: socket.socket, topic: str, payload: bytes) -> None:
    topic_enc = topic.encode("utf-8")
    remaining = struct.pack("!H", len(topic_enc)) + topic_enc + payload
    header = bytes([(PUBLISH << 4) | 0x00])  # QoS 0, no retain
    _write_with_remaining(sock, header, remaining)


def _write_with_remaining(sock: socket.socket, header: bytes, remaining: bytes) -> None:
    length = len(remaining)
    encoded = bytearray()
    while True:
        byte = length % 128
        length //= 128
        if length > 0:
            byte |= 128
        encoded.append(byte)
        if length == 0:
            break
    sock.sendall(header + bytes(encoded) + remaining)


# ---------------------------------------------------------------------------
# Per-client handler
# ---------------------------------------------------------------------------
def _handle_client(conn: socket.socket, addr: tuple[str, int]) -> None:
    logger.info("New connection from %s:%d", *addr)
    try:
        while True:
            first_byte = conn.recv(1)
            if not first_byte:
                break
            packet_type = first_byte[0] >> 4
            remaining, _ = _read_remaining_length(conn)
            body = b""
            while len(body) < remaining:
                chunk = conn.recv(remaining - len(body))
                if not chunk:
                    raise ConnectionError("Connection closed")
                body += chunk

            flags = first_byte[0] & 0x0F

            if packet_type == CONNECT:
                _handle_connect(conn, body)
            elif packet_type == SUBSCRIBE:
                _handle_subscribe(conn, body)
            elif packet_type == PUBLISH:
                _handle_publish(conn, body, flags)
            elif packet_type == PINGREQ:
                conn.sendall(bytes([PINGRESP << 4, 0]))
            elif packet_type == DISCONNECT:
                logger.debug("Client disconnected")
                break
            else:
                logger.warning("Unhandled packet type %d", packet_type)
    except (ConnectionError, OSError) as exc:
        logger.info("Client %s:%d disconnected: %s", *addr, exc)
    finally:
        with _sub_lock:
            _subscribers[:] = [(t, s) for t, s in _subscribers if s is not conn]
        conn.close()


def _handle_connect(conn: socket.socket, body: bytes) -> None:
    # Skip protocol name + version + flags + keepalive (7+ bytes)
    # Just send CONNACK: session present=0, return code=0 (accepted)
    conn.sendall(bytes([CONNACK << 4, 2, 0x00, 0x00]))
    logger.debug("CONNACK sent")


def _handle_subscribe(conn: socket.socket, body: bytes) -> None:
    # Skip packet identifier (2 bytes)
    offset = 2
    topics: list[str] = []
    while offset < len(body):
        topic, offset = _decode_utf8(body, offset)
        qos = body[offset] if offset < len(body) else 0
        offset += 1
        topics.append(topic)
        with _sub_lock:
            _subscribers.append((topic, conn))
        logger.debug("Subscription: %s QoS=%d", topic, qos)

    # SUBACK: packet_id + return codes (QoS 0 for each)
    packet_id = body[:2]
    suback_payload = packet_id + bytes([0] * len(topics))
    header = bytes([(SUBACK << 4)])
    _write_with_remaining(conn, header, suback_payload)


def _handle_publish(conn: socket.socket, body: bytes, flags: int = 0) -> None:
    topic, offset = _decode_utf8(body)
    qos = (flags >> 1) & 0x03
    if qos > 0:
        offset += 2  # skip packet identifier
    payload = body[offset:]
    logger.debug("PUBLISH topic=%s qos=%d len=%d", topic, qos, len(payload))
    _broadcast(topic, payload)


# ---------------------------------------------------------------------------
# Server
# ---------------------------------------------------------------------------
def start_broker(host: str = "127.0.0.1", port: int = 1883) -> socket.socket:
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind((host, port))
    server.listen(5)
    logger.info("Mini MQTT broker listening on %s:%d", host, port)

    def _accept() -> None:
        while True:
            try:
                conn, addr = server.accept()
                t = threading.Thread(target=_handle_client, args=(conn, addr), daemon=True)
                t.start()
            except OSError:
                break

    t = threading.Thread(target=_accept, daemon=True)
    t.start()
    return server


if __name__ == "__main__":
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
        datefmt="%Y-%m-%dT%H:%M:%S%z",
    )
    server = start_broker()
    logger.info("Press Ctrl+C to stop")
    try:
        server.getsockname()
        threading.Event().wait()
    except KeyboardInterrupt:
        logger.info("Stopping broker")
