"""hasura_client -- GraphQL client for Hasura batch inserts.

Provides HasuraClient which sends telemetry batches to the Hasura
GraphQL endpoint with on_conflict resolution for the norvi_telemetry table.
"""

import logging
import time
from typing import Any

import requests

logger = logging.getLogger(__name__)

_MUTATION_INSERT_TELEMETRY = """
mutation InsertTelemetryBatch($objects: [norvi_telemetry_insert_input!]!) {
  insert_norvi_telemetry(
    objects: $objects
    on_conflict: {
      constraint: norvi_telemetry_pkey
      update_columns: [cycle_count, status]
    }
  ) {
    affected_rows
  }
}
"""


class HasuraClient:
    """Sends telemetry batches to a Hasura GraphQL endpoint."""

    def __init__(self, endpoint_url: str, admin_secret: str) -> None:
        """Initialise the Hasura client.

        Args:
            endpoint_url: Full GraphQL endpoint URL
                (e.g. http://hasura:8080/v1/graphql).
            admin_secret: Hasura admin secret for x-hasura-admin-secret header.
        """
        self._endpoint = endpoint_url
        self._headers = {
            "Content-Type": "application/json",
            "x-hasura-admin-secret": admin_secret,
        }

    # FR-6: backoff intervals for Hasura retry (2s, 4s, 8s)
    _RETRY_BACKOFFS = [2, 4, 8]

    def insert_telemetry_batch(
        self, telemetry_list: list[dict[str, Any]]
    ) -> bool:
        """Insert a batch of telemetry rows into norvi_telemetry.

        Implements FR-6: retries on transient errors (5xx, network) with
        2s/4s/8s exponential backoff. Client errors (4xx) are NOT retried.

        Args:
            telemetry_list: List of dicts with keys matching the
                norvi_telemetry table columns.

        Returns:
            True if the mutation succeeded. False if all retries exhausted
            or a non-retriable error occurred.
        """
        if not telemetry_list:
            logger.debug("Empty batch -- nothing to insert.")
            return True

        payload = {
            "query": _MUTATION_INSERT_TELEMETRY,
            "variables": {"objects": telemetry_list},
        }

        last_error: Exception | None = None
        last_status: int | None = None
        last_body: str | None = None

        for attempt, backoff in enumerate(self._RETRY_BACKOFFS):
            if attempt > 0:
                logger.info(
                    "Retry %d/%d in %ds ...",
                    attempt + 1,
                    len(self._RETRY_BACKOFFS),
                    backoff,
                )
                time.sleep(backoff)

            # --- HTTP call ---
            try:
                response = requests.post(
                    self._endpoint,
                    headers=self._headers,
                    json=payload,
                    timeout=15,
                )
            except requests.RequestException as exc:
                last_error = exc
                logger.warning(
                    "Hasura request failed (attempt %d/%d): %s",
                    attempt + 1,
                    len(self._RETRY_BACKOFFS),
                    exc,
                )
                continue  # network error → retry

            last_status = response.status_code
            last_body = response.text[:500]

            # 4xx → NOT retriable (FR-6)
            if 400 <= response.status_code < 500:
                logger.error(
                    "Hasura HTTP %d (non-retriable) | body=%.500s "
                    "| batch_size=%d",
                    response.status_code,
                    response.text,
                    len(telemetry_list),
                )
                return False

            if not response.ok:  # 5xx → retriable
                logger.warning(
                    "Hasura HTTP %d (attempt %d/%d) | body=%.500s",
                    response.status_code,
                    attempt + 1,
                    len(self._RETRY_BACKOFFS),
                    response.text,
                )
                continue

            # --- Parse JSON ---
            try:
                data = response.json()
            except ValueError:
                last_error = ValueError("non-JSON response")
                logger.warning(
                    "Hasura returned non-JSON (attempt %d/%d): %.500s",
                    attempt + 1,
                    len(self._RETRY_BACKOFFS),
                    response.text,
                )
                continue

            if "errors" in data:
                errors = data["errors"]
                if isinstance(errors, list) and errors:
                    first_msg = errors[0].get("message", str(errors))
                else:
                    first_msg = str(errors)
                # GraphQL errors are usually server-side → retriable
                logger.warning(
                    "Hasura mutation error (attempt %d/%d): %s",
                    attempt + 1,
                    len(self._RETRY_BACKOFFS),
                    first_msg,
                )
                continue

            # --- Success ---
            affected = data.get("data", {}).get(
                "insert_norvi_telemetry", {}
            ).get("affected_rows", 0)
            if attempt > 0:
                logger.info(
                    "Inserted %d/%d rows after %d retries.",
                    affected,
                    len(telemetry_list),
                    attempt,
                )
            else:
                logger.info(
                    "Inserted %d/%d telemetry rows.",
                    affected,
                    len(telemetry_list),
                )
            return True

        # --- All retries exhausted ---
        if last_error:
            logger.error(
                "Hasura request FAILED after %d retries: %s "
                "| batch_size=%d",
                len(self._RETRY_BACKOFFS),
                last_error,
                len(telemetry_list),
            )
        elif last_status:
            logger.error(
                "Hasura FAILED after %d retries: HTTP %d | body=%.500s "
                "| batch_size=%d",
                len(self._RETRY_BACKOFFS),
                last_status,
                last_body or "",
                len(telemetry_list),
            )
        else:
            logger.error(
                "Hasura FAILED after %d retries | batch_size=%d",
                len(self._RETRY_BACKOFFS),
                len(telemetry_list),
            )
        return False
