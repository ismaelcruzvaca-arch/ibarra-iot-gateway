"""
gateway_translator — Raspberry Pi 5 MQTT ↔ Hasura bridge worker.

Subscribes to the local Modbus telemetry topic, deserialises Protobuf
payloads, and forwards them in 1-second batches to Hasura (GraphQL).
"""

import logging
import os
import signal
import sys
import time

from hasura_client import HasuraClient
from mqtt_worker import GatewayWorker

# ---------------------------------------------------------------------------
# Logging
# ---------------------------------------------------------------------------
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
    datefmt="%Y-%m-%dT%H:%M:%S%z",
)
logger = logging.getLogger("gateway_translator")

# ---------------------------------------------------------------------------
# Environment
# ---------------------------------------------------------------------------
HASURA_GRAPHQL_URL = os.environ.get("HASURA_GRAPHQL_URL")
HASURA_ADMIN_SECRET = os.environ.get("HASURA_ADMIN_SECRET")
MQTT_BROKER_HOST = os.environ.get("MQTT_BROKER_HOST")


def _check_env() -> None:
    missing = []
    if not HASURA_GRAPHQL_URL:
        missing.append("HASURA_GRAPHQL_URL")
    if not HASURA_ADMIN_SECRET:
        missing.append("HASURA_ADMIN_SECRET")
    if not MQTT_BROKER_HOST:
        missing.append("MQTT_BROKER_HOST")
    if missing:
        logger.fatal(
            "Missing required environment variable(s): %s",
            ", ".join(missing),
        )
        sys.exit(1)


# ---------------------------------------------------------------------------
# Graceful shutdown
# ---------------------------------------------------------------------------
_shutdown_requested = False


def _handle_signal(signum: int, _frame) -> None:  # type: ignore[type-arg]
    global _shutdown_requested
    signame = signal.Signals(signum).name
    logger.info("Received %s — shutting down gracefully ...", signame)
    _shutdown_requested = True


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------
def main() -> None:
    _check_env()

    signal.signal(signal.SIGTERM, _handle_signal)
    signal.signal(signal.SIGINT, _handle_signal)

    # -- Initialise dependencies --------------------------------------
    hasura = HasuraClient(
        endpoint_url=HASURA_GRAPHQL_URL,  # type: ignore[arg-type]
        admin_secret=HASURA_ADMIN_SECRET,  # type: ignore[arg-type]
    )
    worker = GatewayWorker(
        broker_host=MQTT_BROKER_HOST,  # type: ignore[arg-type]
        hasura_client=hasura,
    )
    worker.start()

    logger.info("gateway_translator started — flushing every 1s.")

    # -- Main loop: flush buffer every second -------------------------
    while not _shutdown_requested:
        time.sleep(1)

        batch = worker.drain_buffer()
        if not batch:
            continue

        ok = hasura.insert_telemetry_batch(batch)
        if not ok:
            logger.warning("Batch insert failed — %d entries re-queued.", len(batch))
            for entry in batch:
                worker.enqueue_telemetry(entry)

    # -- Cleanup ------------------------------------------------------
    worker.stop()
    logger.info("gateway_translator stopped.")


if __name__ == "__main__":
    main()
