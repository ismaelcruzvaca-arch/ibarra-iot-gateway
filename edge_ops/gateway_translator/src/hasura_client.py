"""hasura_client -- GraphQL client for Hasura batch inserts.

Provides HasuraClient which sends telemetry batches to the Hasura
GraphQL endpoint with on_conflict resolution for the norvi_telemetry table.
"""

import logging
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

    def insert_telemetry_batch(
        self, telemetry_list: list[dict[str, Any]]
    ) -> bool:
        """Insert a batch of telemetry rows into norvi_telemetry.

        Args:
            telemetry_list: List of dicts with keys matching the
                norvi_telemetry table columns.

        Returns:
            True if the mutation succeeded (HTTP 2xx and no "errors" key
            in the response body). False otherwise, with a detailed log.
        """
        if not telemetry_list:
            logger.debug("Empty batch -- nothing to insert.")
            return True

        payload = {
            "query": _MUTATION_INSERT_TELEMETRY,
            "variables": {"objects": telemetry_list},
        }

        try:
            response = requests.post(
                self._endpoint,
                headers=self._headers,
                json=payload,
                timeout=15,
            )
        except requests.RequestException as exc:
            logger.error(
                "Hasura request failed: %s | batch_size=%d",
                exc,
                len(telemetry_list),
            )
            return False

        if not response.ok:
            logger.error(
                "Hasura HTTP %d | body=%.500s | batch_size=%d",
                response.status_code,
                response.text,
                len(telemetry_list),
            )
            return False

        try:
            data = response.json()
        except ValueError:
            logger.error(
                "Hasura returned non-JSON body: %.500s",
                response.text,
            )
            return False

        if "errors" in data:
            errors = data["errors"]
            if isinstance(errors, list) and errors:
                first_msg = errors[0]["message"]
            else:
                first_msg = str(errors)
            logger.error(
                "Hasura mutation error: %s | batch_size=%d | full=%s",
                first_msg,
                len(telemetry_list),
                errors,
            )
            return False

        affected = data.get("data", {}).get(
            "insert_norvi_telemetry", {}
        ).get("affected_rows", 0)
        logger.info(
            "Inserted %d/%d telemetry rows.",
            affected,
            len(telemetry_list),
        )
        return True
