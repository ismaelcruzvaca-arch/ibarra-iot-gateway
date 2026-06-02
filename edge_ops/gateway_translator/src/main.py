"""gateway_translator -- MQTT to Hasura bridge service.

Subscribes to ModbusBridgePayload on the plant MQTT broker, decodes
Protobuf telemetry, and batch-inserts into Hasura via GraphQL mutation.
"""

import logging
import os
import signal
import sys
import time

from hasura_client import HasuraClient
from mqtt_worker import GatewayWorker

logger = logging.getLogger("gateway_translator")

# Read environment variables (fail-fast if missing)
HASURA_GRAPHQL_URL = os.environ.get("HASURA_GRAPHQL_URL")
HASURA_ADMIN_SECRET = os.environ.get("HASURA_ADMIN_SECRET")
MQTT_BROKER_HOST = os.environ.get("MQTT_BROKER_HOST")

_shutdown_requested = False


def _check_env() -> None:
    """Fail fast if any required environment variable is missing."""
    missing: list[str] = []

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


def _handle_signal(signum: int, _frame: object) -> None:
    """Set the shutdown flag on SIGTERM/SIGINT for graceful shutdown."""
    signame = signal.Signals(signum).name
    logger.info("Received %s -- shutting down gracefully ...", signame)
    global _shutdown_requested  # noqa: PLW0603
    _shutdown_requested = True


def main() -> None:
    """Entry point: check env, wire signal handlers, run poll loop."""
    _check_env()

    signal.signal(signal.SIGTERM, _handle_signal)
    signal.signal(signal.SIGINT, _handle_signal)

    hasura = HasuraClient(
        endpoint_url=HASURA_GRAPHQL_URL,
        admin_secret=HASURA_ADMIN_SECRET,
    )
    worker = GatewayWorker(
        broker_host=MQTT_BROKER_HOST,
        hasura_client=hasura,
    )
    worker.start()
    logger.info("gateway_translator started -- flushing every 1s.")

    # FR-7: health tracking
    last_flush_time = time.monotonic()
    health_interval = 60  # seconds between health logs
    next_health_log = health_interval

    while not _shutdown_requested:
        time.sleep(1)
        batch = worker.drain_buffer()
        if not batch:
            # FR-7: periodic health log
            elapsed = time.monotonic()
            if elapsed >= next_health_log:
                next_health_log = elapsed + health_interval
                age = elapsed - last_flush_time
                logger.info(
                    "HEALTH — MQTT=%s buffer=%d last_flush=%ds ago",
                    "connected" if worker.is_connected else "DISCONNECTED",
                    worker.buffer_size,
                    int(age),
                )
            continue

        ok = hasura.insert_telemetry_batch(batch)
        if not ok:
            logger.warning(
                "Batch insert failed -- %d entries re-queued.",
                len(batch),
            )
            for entry in batch:
                worker.enqueue_telemetry(entry)
        else:
            last_flush_time = time.monotonic()

    # FR-5: Final flush before shutdown — drain remaining buffer
    final_batch = worker.drain_buffer()
    if final_batch:
        logger.info(
            "Final flush: %d entries remaining in buffer.",
            len(final_batch),
        )
        ok = hasura.insert_telemetry_batch(final_batch)
        if ok:
            logger.info("Final flush succeeded.")
        else:
            logger.warning(
                "Final flush failed -- %d entries LOST.",
                len(final_batch),
            )

    worker.stop()
    logger.info("gateway_translator stopped.")


if __name__ == "__main__":
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
    )
    main()
