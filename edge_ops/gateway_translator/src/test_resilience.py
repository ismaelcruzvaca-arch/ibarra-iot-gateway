"""
test_resilience — Prueba de caída de Hasura con buffer en RAM.

Escenario:
  1. Broker MQTT + Mock Hasura + Gateway + Simulador arrancan
  2. A los 30s → Mock Hasura es "asesinado" (responde 500)
  3. Gateway bufferra en RAM sin perder datos ni crashear
  4. A los 45s → Mock Hasura revive
  5. Gateway vacía el buffer acumulado
  6. Validación: 0 mensajes perdidos

Arquitectura:
  - Mini MQTT Broker (TCP socket, MQTT v3.1.1 mínimo)
  - Mock Hasura (HTTP server con /kill, /revive)
  - GatewayWorker real (conectado al mini broker)
  - Simulador real (publica 15 msg/s al mini broker)
"""

import logging
import threading
import time
from datetime import datetime, timezone

import requests

from mini_broker import start_broker
from mock_hasura import start_mock
from hasura_client import HasuraClient
from mqtt_worker import GatewayWorker

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
    datefmt="%Y-%m-%dT%H:%M:%S%z",
)
logger = logging.getLogger("test_resilience")

BROKER_PORT = 1883
MOCK_PORT = 8080
HASURA_URL = f"http://127.0.0.1:{MOCK_PORT}/v1/graphql"
KILL_URL = f"http://127.0.0.1:{MOCK_PORT}/kill"
REVIVE_URL = f"http://127.0.0.1:{MOCK_PORT}/revive"
STATUS_URL = f"http://127.0.0.1:{MOCK_PORT}/status"

# Simulación
TARGET_HZ = 15
SLEEP_INTERVAL = 1.0 / TARGET_HZ
TOPIC = "novamex/linea1/telemetry"

# Tiempos de la prueba (segundos)
T_KILL = 30
T_REVIVE = 45
T_DURATION = 60

_sent_counter = 0
_sent_lock = threading.Lock()
_stop_publisher = threading.Event()


def _publisher_loop() -> None:
    """Publica mensajes Protobuf al broker a 15 Hz."""
    global _sent_counter
    import random
    from telemetry_pb2 import ModbusBridgePayload

    import paho.mqtt.client as mqtt

    client = mqtt.Client(
        callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
        client_id="test-simulator",
    )
    client.connect("127.0.0.1", BROKER_PORT, keepalive=60)
    client.loop_start()

    cycle = 0
    logger.info("Publisher started — %d msg/s", TARGET_HZ)

    while not _stop_publisher.is_set():
        cycle += 1
        payload = ModbusBridgePayload()
        for i in range(1, 6):
            node = payload.nodes.add()
            node.node_id = f"norvi-node-{i}"
            node.cycle_count = cycle
            node.status = 3 if random.random() < 0.01 else 1

        data = payload.SerializeToString()
        client.publish(TOPIC, data, qos=0)

        with _sent_lock:
            _sent_counter += 5  # 5 nodos por mensaje

        time.sleep(SLEEP_INTERVAL)

    client.loop_stop()
    client.disconnect()
    logger.info("Publisher stopped — total cycles=%d", cycle)


def main() -> None:
    logger.info("=" * 60)
    logger.info("RESILIENCE TEST — Hasura caída 30s→45s")
    logger.info("=" * 60)

    # 1. Arrancar Mini MQTT Broker
    logger.info("[1/4] Arrancando Mini MQTT Broker...")
    broker = start_broker(port=BROKER_PORT)

    # 2. Arrancar Mock Hasura
    logger.info("[2/4] Arrancando Mock Hasura...")
    mock_server = start_mock(port=MOCK_PORT)
    mock_thread = threading.Thread(target=mock_server.serve_forever, daemon=True)
    mock_thread.start()
    time.sleep(0.5)

    # 3. Arrancar GatewayWorker
    logger.info("[3/4] Arrancando GatewayWorker...")
    hasura = HasuraClient(endpoint_url=HASURA_URL, admin_secret="test-secret")
    worker = GatewayWorker(broker_host="127.0.0.1", hasura_client=hasura)
    worker.start()
    time.sleep(1)  # Esperar conexión MQTT

    # 4. Arrancar Publisher (simulador)
    logger.info("[4/4] Arrancando Publisher...")
    pub_thread = threading.Thread(target=_publisher_loop, daemon=True)
    pub_thread.start()

    # Loop de control
    t_start = time.time()
    kill_done = False
    revive_done = False
    total_batches = 0
    total_requeued = 0

    logger.info("Prueba en ejecución — matando Hasura en T=%ds...", T_KILL)

    try:
        while True:
            elapsed = time.time() - t_start

            # Matar Hasura a los 30s
            if elapsed >= T_KILL and not kill_done:
                kill_done = True
                requests.get(KILL_URL)
                logger.warning("🔴 Mock Hasura KILLED a T=%.1fs", elapsed)

            # Revivir Hasura a los 45s
            if elapsed >= T_REVIVE and not revive_done:
                revive_done = True
                requests.get(REVIVE_URL)
                logger.info("🟢 Mock Hasura REVIVED a T=%.1fs", elapsed)

            # Drain del buffer
            batch = worker.drain_buffer()
            if batch:
                ok = hasura.insert_telemetry_batch(batch)
                total_batches += 1
                if not ok:
                    for entry in batch:
                        worker.enqueue_telemetry(entry)
                    total_requeued += 1
                    logger.warning(
                        "Batch re-queued — size=%d total_req=%d",
                        len(batch), total_requeued,
                    )

            if elapsed >= T_DURATION:
                break

            time.sleep(1)

    except KeyboardInterrupt:
        logger.info("Prueba interrumpida por usuario")

    # 1. Parar publisher
    _stop_publisher.set()
    pub_thread.join(timeout=5)

    # 2. Último drain + flush del buffer    
    final_batch = worker.drain_buffer()
    if final_batch:
        ok = hasura.insert_telemetry_batch(final_batch)
        total_batches += 1

    # 3. Leer estado ANTES de apagar el mock
    total_sent = _sent_counter
    try:
        status = requests.get(STATUS_URL, timeout=3).json()
    except Exception as exc:
        logger.error("No se pudo leer status del mock: %s", exc)
        status = {"total_rows": 0, "total_requests": 0}

    total_received = status["total_rows"]
    total_requests = status["total_requests"]

    # 4. Detener todo
    worker.stop()
    mock_server.shutdown()
    broker.close()

    # 5. Validación
    diff = total_sent - total_received

    logger.info("")
    logger.info("=" * 60)
    logger.info("RESULTADOS")
    logger.info("=" * 60)
    logger.info("Mensajes enviados    : %d", total_sent)
    logger.info("Mensajes recibidos   : %d", total_received)
    logger.info("Requests a Hasura    : %d", total_requests)
    logger.info("Batches procesados   : %d", total_batches)
    logger.info("Batches re-encolados : %d", total_requeued)
    logger.info("Diferencia           : %d", diff)

    if diff == 0:
        logger.info("✅ PRUEBA PASS — 0 mensajes perdidos")
    elif diff > 0:
        logger.error("❌ PRUEBA FAIL — %d mensajes perdidos", diff)
    else:
        logger.warning("⚠️ PRUEBA EXTRA — %d mensajes de más recibidos", -diff)


if __name__ == "__main__":
    main()
