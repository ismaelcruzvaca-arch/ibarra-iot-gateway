// ============================================================================
// assert.ts — Healthbeat assert module
//
// Injects a synthetic ALARM into norvi_telemetry, polls alert_engine_health
// for up to 30s (every 5s) to verify end-to-end dispatch, and writes the
// health check result row back to alert_engine_health.
//
// Used by the cron-triggered healthbeat function (index.ts) that runs every
// 5 minutes via Nhost cron trigger (alert-healthbeat-cron).
// ============================================================================

// ---------------------------------------------------------------------------
// HealthCheckResult
// ---------------------------------------------------------------------------

export interface HealthCheckResult {
  /** ISO-8601 timestamp when the health check started */
  checked_at: string;
  /** Total elapsed time in milliseconds for the check */
  latency_ms: number;
  /** true if the health row was found within the polling window */
  success: boolean;
  /** Human-readable detail about the check outcome */
  detail: string;
}

// ---------------------------------------------------------------------------
// GraphQL helpers
// ---------------------------------------------------------------------------

/**
 * Makes a GraphQL request to the Nhost Hasura endpoint using the admin secret
 * from environment variables.
 */
async function graphqlRequest(query: string): Promise<Record<string, unknown>> {
  const backendUrl = process.env.NHOST_BACKEND_URL || '';
  const adminSecret = process.env.NHOST_ADMIN_SECRET || '';

  const response = await fetch(`${backendUrl}/v1/graphql`, {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json',
      'x-hasura-admin-secret': adminSecret,
    },
    body: JSON.stringify({ query }),
  });

  return response.json() as Promise<Record<string, unknown>>;
}

// ---------------------------------------------------------------------------
// injectAndAssert
// ---------------------------------------------------------------------------

/**
 * Injects a synthetic ALARM into norvi_telemetry and polls alert_engine_health
 * for up to 30 seconds to verify that the end-to-end dispatch pipeline is
 * functioning. Writes the health check result row to alert_engine_health.
 *
 * Flow:
 *   1. INSERT a synthetic ALARM row into norvi_telemetry
 *      { node_id: "__healthbeat__", payload: { status: 3 } }
 *   2. Poll alert_engine_health every 5s (up to 30s / 6 attempts) for
 *      a row created after the check start time
 *   3. INSERT the health check result { checked_at, latency_ms, success, detail }
 *
 * @returns HealthCheckResult with the outcome of the check
 */
export async function injectAndAssert(): Promise<HealthCheckResult> {
  const startTime = Date.now();
  const checkedAt = new Date().toISOString();
  const maxAttempts = 6; // 30s / 5s poll interval
  const pollIntervalMs = 5000;

  try {
    // -----------------------------------------------------------------------
    // 1. INSERT synthetic ALARM into norvi_telemetry
    // -----------------------------------------------------------------------
    const insertMutation = `
      mutation {
        insert_norvi_telemetry_one(object: {
          node_id: "__healthbeat__",
          payload: { status: 3 },
          event_ts: "${checkedAt}"
        }) { node_id }
      }
    `;

    await graphqlRequest(insertMutation);

    // -----------------------------------------------------------------------
    // 2. Poll alert_engine_health for up to 30s
    // -----------------------------------------------------------------------
    let found = false;
    let detail = '';

    for (let attempt = 0; attempt < maxAttempts; attempt++) {
      // Wait 5s between polls (skip wait on first attempt for speed)
      if (attempt > 0) {
        await new Promise((resolve) => setTimeout(resolve, pollIntervalMs));
      }

      const pollQuery = `
        query {
          alert_engine_health(
            where: { checked_at: { _gte: "${checkedAt}" } }
            order_by: { checked_at: desc }
            limit: 1
          ) {
            check_id
            success
            latency_ms
            detail
          }
        }
      `;

      const pollResult = await graphqlRequest(pollQuery);
      const data = pollResult.data as Record<string, unknown> | undefined;
      const rows = data?.alert_engine_health as Array<Record<string, unknown>> | undefined;

      if (rows && rows.length > 0) {
        found = true;
        detail = 'Health check passed: alert_engine_health updated within polling window';
        break;
      }
    }

    if (!found) {
      detail = 'Health check failed: no alert_engine_health row found within 30s polling window';
    }

    const latencyMs = Date.now() - startTime;

    // -----------------------------------------------------------------------
    // 3. INSERT health check result row
    // -----------------------------------------------------------------------
    const resultMutation = `
      mutation {
        insert_alert_engine_health_one(object: {
          checked_at: "${checkedAt}",
          latency_ms: ${latencyMs},
          success: ${found},
          detail: "${detail}"
        }) { check_id }
      }
    `;

    await graphqlRequest(resultMutation);

    return {
      checked_at: checkedAt,
      latency_ms: latencyMs,
      success: found,
      detail,
    };
  } catch (err) {
    const errorMsg = (err instanceof Error ? err.message : String(err))
      .replace(/"/g, '\\"')   // Escape double quotes for GraphQL string
      .replace(/\n/g, ' ');   // Replace newlines with spaces
    const latencyMs = Date.now() - startTime;

    // INSERT failure result row
    try {
      const failMutation = `
        mutation {
          insert_alert_engine_health_one(object: {
            checked_at: "${checkedAt}",
            latency_ms: ${latencyMs},
            success: false,
            detail: "Exception: ${errorMsg}"
          }) { check_id }
        }
      `;
      await graphqlRequest(failMutation);
    } catch {
      // Best-effort — if even the failure write fails, swallow
    }

    return {
      checked_at: checkedAt,
      latency_ms: latencyMs,
      success: false,
      detail: errorMsg,
    };
  }
}
