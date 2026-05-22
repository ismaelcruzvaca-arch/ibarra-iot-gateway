// ============================================================================
// engine.ts — Silence Detector Engine
//
// Core evaluation engine that USER_DEFINED SILENCE_TIMEOUT rules against
// norvi_telemetry data. When a machine exceeds its silence threshold (i.e.,
// no telemetry received within valor_umbral seconds), the engine inserts an
// event into alert_events for downstream dispatch by alert-dispatcher.
//
// All data-access functions are injected for testability (same pattern as
// DispatcherEngine in alert-dispatcher/engine.ts).
// ============================================================================

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

/** A USER_DEFINED SILENCE_TIMEOUT rule from alert_rules */
export interface SilenceRule {
  /** alert_rules.id */
  id: string;
  /** node_id of the monitored device */
  node_id: string;
  /** Silence threshold in seconds (valor_umbral) */
  valor_umbral: number;
  /** Allowed channel types (from canales JSONB — e.g. ["telegram", "slack"]) */
  canales: string[];
  /** Cooldown period in minutes before a re-alert is allowed */
  cooldown_minutos: number;
  /** ISO-8601 timestamp of last alert, or null if never alerted */
  last_alerted_at: string | null;
}

/** Insert payload for alert_events */
export interface AlertEventInsert {
  rule_id: string;
  node_id: string;
  tipo_evento: 'SILENCE_TIMEOUT' | 'ERROR_THRESHOLD' | 'FREQUENCY_DROP' | 'DEFECT_THRESHOLD';
  mensaje: string;
  detected_at: string;
}

/** Health entry written to alert_engine_health after evaluation cycle */
export interface HealthEntry {
  checked_at: string;
  latency_ms: number;
  success: boolean;
  detail: string;
}

/** Summary of a single evaluation cycle */
export interface EvaluationResult {
  /** Number of USER_DEFINED SILENCE_TIMEOUT rules evaluated */
  rulesEvaluated: number;
  /** Number of rules that triggered a new alert event */
  alertsTriggered: number;
  /** Errors encountered during evaluation (empty if all clear) */
  errors: string[];
}

// ---------------------------------------------------------------------------
// SilenceDetectorEngine
// ---------------------------------------------------------------------------

/**
 * Core engine that evaluates USER_DEFINED SILENCE_TIMEOUT rules.
 *
 * For each rule:
 *   1. Query the latest event_ts from norvi_telemetry for that node
 *   2. Check if the silence threshold has been exceeded
 *   3. Check if the cooldown period has elapsed since the last alert
 *   4. If both conditions are met, INSERT an alert_event and UPDATE last_alerted_at
 *
 * All data-access functions are injected for testability.
 */
export class SilenceDetectorEngine {
  /**
   * Runs a single evaluation cycle across all USER_DEFINED SILENCE_TIMEOUT rules.
   *
   * @param queryRulesFn - Returns all enabled USER_DEFINED SILENCE_TIMEOUT rules
   * @param queryLastEventFn - Returns the latest event_ts for a node, or null
   * @param insertAlertEventFn - Inserts a new alert_event row
   * @param updateLastAlertedFn - Updates last_alerted_at for a rule
   * @param writeHealthFn - Writes a health entry to alert_engine_health
   * @returns EvaluationResult with summary of the cycle
   */
  async evaluate(
    queryRulesFn: () => Promise<SilenceRule[]>,
    queryLastEventFn: (nodeId: string) => Promise<string | null>,
    insertAlertEventFn: (event: AlertEventInsert) => Promise<void>,
    updateLastAlertedFn: (ruleId: string) => Promise<void>,
    writeHealthFn: (entry: HealthEntry) => Promise<void>,
  ): Promise<EvaluationResult> {
    const startTime = Date.now();
    const checkedAt = new Date().toISOString();
    let alertsTriggered = 0;
    const errors: string[] = [];

    let rules: SilenceRule[];
    try {
      rules = await queryRulesFn();
    } catch (err) {
      const errorMessage = err instanceof Error ? err.message : String(err);
      // Write a failure health entry and return early
      const latencyMs = Date.now() - startTime;
      await writeHealthFn({
        checked_at: checkedAt,
        latency_ms: latencyMs,
        success: false,
        detail: `Failed to query rules: ${errorMessage}`,
      }).catch(() => { /* best-effort */ });
      return {
        rulesEvaluated: 0,
        alertsTriggered: 0,
        errors: [errorMessage],
      };
    }

    for (const rule of rules) {
      try {
        // Step 1: Get the latest event timestamp for this node
        const lastEventTs = await queryLastEventFn(rule.node_id);

        // If no telemetry exists yet for this node, skip
        if (lastEventTs === null) {
          continue;
        }

        const lastEventTime = new Date(lastEventTs).getTime();
        const now = Date.now();
        const elapsedSeconds = (now - lastEventTime) / 1000;

        // Step 2: Check if silence threshold has been exceeded
        if (elapsedSeconds <= rule.valor_umbral) {
          continue; // Node is still active — no silence violation
        }

        // Step 3: Check cooldown
        if (rule.last_alerted_at !== null) {
          const lastAlertTime = new Date(rule.last_alerted_at).getTime();
          const cooldownMs = rule.cooldown_minutos * 60 * 1000;
          if (now - lastAlertTime <= cooldownMs) {
            continue; // Still within cooldown — skip
          }
        }

        // Step 4: Both conditions met — INSERT alert event
        const mensaje = [
          `SILENCE TIMEOUT - Nodo: ${rule.node_id}`,
          `Último evento: ${lastEventTs}`,
          `Umbral de silencio: ${rule.valor_umbral}s`,
          `Segundos desde último evento: ${Math.round(elapsedSeconds)}s`,
        ].join('\n');

        const detectedAt = new Date().toISOString();
        await insertAlertEventFn({
          rule_id: rule.id,
          node_id: rule.node_id,
          tipo_evento: 'SILENCE_TIMEOUT',
          mensaje,
          detected_at: detectedAt,
        });

        // Step 5: Update last_alerted_at
        await updateLastAlertedFn(rule.id);

        alertsTriggered++;
      } catch (err) {
        const errorMessage = err instanceof Error ? err.message : String(err);
        errors.push(`Rule ${rule.id} (node ${rule.node_id}): ${errorMessage}`);
      }
    }

    // Write health entry
    const latencyMs = Date.now() - startTime;
    const success = errors.length === 0;
    const detail = success
      ? `Evaluated ${rules.length} rules, triggered ${alertsTriggered} alerts`
      : `Evaluated ${rules.length} rules, triggered ${alertsTriggered} alerts, ${errors.length} errors`;

    try {
      await writeHealthFn({
        checked_at: checkedAt,
        latency_ms: latencyMs,
        success,
        detail,
      });
    } catch {
      // best-effort
    }

    return {
      rulesEvaluated: rules.length,
      alertsTriggered,
      errors,
    };
  }
}
