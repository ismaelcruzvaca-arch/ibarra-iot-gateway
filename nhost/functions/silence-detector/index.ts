// ============================================================================
// index.ts — Silence Detector Nhost Function entry point
//
// Cron-triggered function that runs every 1 minute (via Nhost cron trigger
// silence-detector-cron). Evaluates all USER_DEFINED SILENCE_TIMEOUT rules
// and inserts alert_events for any machines that have exceeded their silence
// threshold.
//
// This function does NOT dispatch messages — it only detects silence violations
// and inserts events. The existing alert-dispatcher handles the actual dispatch
// via a separate Event Trigger on alert_events.
// ============================================================================

import { SilenceDetectorEngine } from './engine';
import type { SilenceRule, AlertEventInsert, HealthEntry, EvaluationResult } from './engine';

// ---------------------------------------------------------------------------
// Default implementations (production)
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

/**
 * Default implementation: queries all enabled USER_DEFINED SILENCE_TIMEOUT rules.
 */
async function defaultQueryRules(): Promise<SilenceRule[]> {
  const query = `
    query {
      alert_rules(
        where: {
          scope: { _eq: "USER_DEFINED" },
          tipo_condicion: { _eq: "SILENCE_TIMEOUT" },
          enabled: { _eq: true }
        }
      ) {
        id
        node_id
        valor_umbral
        canales
        cooldown_minutos
        last_alerted_at
      }
    }
  `;

  const result = await graphqlRequest(query);
  const data = result.data as Record<string, unknown> | undefined;
  const rules = data?.alert_rules as Array<Record<string, unknown>> | undefined;

  if (!rules) return [];

  return rules.map((r) => ({
    id: r.id as string,
    node_id: r.node_id as string,
    valor_umbral: r.valor_umbral as number,
    canales: (r.canales as string[]) ?? [],
    cooldown_minutos: (r.cooldown_minutos as number) ?? 30,
    last_alerted_at: (r.last_alerted_at as string | null) ?? null,
  }));
}

/**
 * Default implementation: queries the latest event_ts from norvi_telemetry
 * for a given node_id.
 */
async function defaultQueryLastEvent(nodeId: string): Promise<string | null> {
  const query = `
    query {
      norvi_telemetry(
        where: { node_id: { _eq: "${nodeId}" } }
        order_by: { event_ts: desc }
        limit: 1
      ) {
        event_ts
      }
    }
  `;

  const result = await graphqlRequest(query);
  const data = result.data as Record<string, unknown> | undefined;
  const rows = data?.norvi_telemetry as Array<{ event_ts: string }> | undefined;

  if (!rows || rows.length === 0) return null;
  return rows[0].event_ts;
}

/**
 * Default implementation: inserts a new alert event into alert_events.
 */
async function defaultInsertAlertEvent(event: AlertEventInsert): Promise<void> {
  const mutation = `
    mutation {
      insert_alert_events_one(object: {
        rule_id: "${event.rule_id}",
        node_id: "${event.node_id}",
        tipo_evento: "${event.tipo_evento}",
        mensaje: "${event.mensaje.replace(/"/g, '\\"')}",
        detected_at: "${event.detected_at}"
      }) { id }
    }
  `;

  await graphqlRequest(mutation);
}

/**
 * Default implementation: updates last_alerted_at for a rule.
 */
async function defaultUpdateLastAlerted(ruleId: string): Promise<void> {
  const now = new Date().toISOString();
  const mutation = `
    mutation {
      update_alert_rules_by_pk(
        pk_columns: { id: "${ruleId}" },
        _set: { last_alerted_at: "${now}" }
      ) { id }
    }
  `;

  await graphqlRequest(mutation);
}

/**
 * Default implementation: writes a health entry to alert_engine_health.
 */
async function defaultWriteHealth(entry: HealthEntry): Promise<void> {
  const mutation = `
    mutation {
      insert_alert_engine_health_one(object: {
        checked_at: "${entry.checked_at}",
        latency_ms: ${entry.latency_ms},
        success: ${entry.success},
        detail: "${entry.detail.replace(/"/g, '\\"')}"
      }) { check_id }
    }
  `;

  await graphqlRequest(mutation);
}

// ---------------------------------------------------------------------------
// Nhost Function handler
// ---------------------------------------------------------------------------

/**
 * Nhost Function handler for the silence-detector cron trigger.
 *
 * Runs every 1 minute (via silence-detector-cron). Evaluates all
 * USER_DEFINED SILENCE_TIMEOUT rules and inserts alert_events for
 * any machines that have exceeded their silence threshold.
 *
 * Returns HTTP 200 with evaluation summary on success,
 * HTTP 500 if an unexpected error occurs.
 *
 * @param _payload - The cron trigger payload (ignored — empty {})
 * @returns Response object with evaluation summary
 */
export async function handler(
  _payload: Record<string, unknown>,
): Promise<{
  status: number;
  body: string;
}> {
  try {
    const engine = new SilenceDetectorEngine();

    const result: EvaluationResult = await engine.evaluate(
      defaultQueryRules,
      defaultQueryLastEvent,
      defaultInsertAlertEvent,
      defaultUpdateLastAlerted,
      defaultWriteHealth,
    );

    const httpStatus = result.errors.length > 0 ? 200 : 200;
    // We still return 200 even with errors; errors are captured in the result

    return {
      status: httpStatus,
      body: JSON.stringify(result),
    };
  } catch (err) {
    const errorMessage = err instanceof Error ? err.message : String(err);
    return {
      status: 500,
      body: JSON.stringify({ error: 'Silence detector evaluation failed', detail: errorMessage }),
    };
  }
}
