// ============================================================================
// index.ts — Alert Healthbeat Nhost Function entry point
//
// Cron-triggered function that runs every 5 minutes (via Nhost cron trigger
// alert-healthbeat-cron). Injects a synthetic ALARM into norvi_telemetry and
// asserts end-to-end delivery via alert_engine_health.
//
// Returns the health check result as JSON with status 200 on success or
// status 500 if an unexpected error occurs before the result can be recorded.
// ============================================================================

import { injectAndAssert } from './assert';
import type { HealthCheckResult } from './assert';

/**
 * Nhost Function handler for the alert-healthbeat cron trigger.
 *
 * The cron trigger sends a POST request with an empty JSON payload.
 * This handler calls injectAndAssert() which:
 *   1. INSERTs a synthetic ALARM into norvi_telemetry
 *   2. Polls alert_engine_health every 5s for up to 30s
 *   3. Writes the health check result row
 *
 * @param _payload - The cron trigger payload (ignored — empty {})
 * @returns Response object with health check result
 */
export async function handler(
  _payload: Record<string, unknown>,
): Promise<{
  status: number;
  body: string;
}> {
  try {
    const result: HealthCheckResult = await injectAndAssert();
    return {
      status: 200,
      body: JSON.stringify(result),
    };
  } catch (err) {
    const errorMessage = err instanceof Error ? err.message : String(err);
    return {
      status: 500,
      body: JSON.stringify({ error: errorMessage }),
    };
  }
}
