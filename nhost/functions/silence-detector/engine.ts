// ============================================================================
// engine.ts — Rule Evaluator Engine
//
// Core evaluation engine that evaluates USER_DEFINED rules (SILENCE_TIMEOUT
// and ERROR_THRESHOLD) against norvi_telemetry data. For SILENCE_TIMEOUT, it
// checks if a machine has exceeded its silence threshold (no telemetry received
// within valor_umbral seconds). For ERROR_THRESHOLD, it counts error events
// within a configurable time window (ventana_minutos). When a condition is met,
// the engine inserts an event into alert_events for downstream dispatch by
// alert-dispatcher.
//
// All data-access functions are injected for testability (same pattern as
// DispatcherEngine in alert-dispatcher/engine.ts).
// ============================================================================

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

/** A USER_DEFINED rule from alert_rules (SILENCE_TIMEOUT or ERROR_THRESHOLD) */
export interface Rule {
  /** alert_rules.id */
  id: string;
  /** node_id of the monitored device */
  node_id: string;
  /** Condition type: SILENCE_TIMEOUT or ERROR_THRESHOLD */
  tipo_condicion: 'SILENCE_TIMEOUT' | 'ERROR_THRESHOLD';
  /** Threshold: silence timeout in seconds (SILENCE_TIMEOUT) or error count (ERROR_THRESHOLD) */
  valor_umbral: number;
  /** Allowed channel types (from canales JSONB — e.g. ["telegram", "slack"]) */
  canales: string[];
  /** Cooldown period in minutes before a re-alert is allowed */
  cooldown_minutos: number;
  /** ISO-8601 timestamp of last alert, or null if never alerted */
  last_alerted_at: string | null;
  /** Time window in minutes for ERROR_THRESHOLD rules (default 5) */
  ventana_minutos: number;
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
  /** Number of USER_DEFINED rules evaluated (SILENCE_TIMEOUT + ERROR_THRESHOLD) */
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
 * Core engine that evaluates USER_DEFINED rules (SILENCE_TIMEOUT and ERROR_THRESHOLD).
 *
 * For SILENCE_TIMEOUT rules:
 *   1. Query the latest event_ts from norvi_telemetry for that node
 *   2. Check if the silence threshold has been exceeded
 *   3. Check if the cooldown period has elapsed since the last alert
 *   4. If both conditions are met, INSERT an alert_event and UPDATE last_alerted_at
 *
 * For ERROR_THRESHOLD rules:
 *   1. Count error events in the time window (ventana_minutos)
 *   2. Check if count >= valor_umbral
 *   3. Check if the cooldown period has elapsed since the last alert
 *   4. If both conditions are met, INSERT an alert_event and UPDATE last_alerted_at
 *
 * All data-access functions are injected for testability.
 */
export class SilenceDetectorEngine {
  /**
   * Runs a single evaluation cycle across all USER_DEFINED rules.
   *
   * @param queryRulesFn - Returns all enabled USER_DEFINED rules
   * @param queryLastEventFn - Returns the latest event_ts for a node, or null
   * @param queryErrorCountFn - Returns error count for a node within a time window
   * @param insertAlertEventFn - Inserts a new alert_event row
   * @param updateLastAlertedFn - Updates last_alerted_at for a rule
   * @param writeHealthFn - Writes a health entry to alert_engine_health
   * @returns EvaluationResult with summary of the cycle
   */
  async evaluate(
    queryRulesFn: () => Promise<Rule[]>,
    queryLastEventFn: (nodeId: string) => Promise<string | null>,
    queryErrorCountFn: (nodeId: string, ventanaMinutos: number) => Promise<number>,
    insertAlertEventFn: (event: AlertEventInsert) => Promise<void>,
    updateLastAlertedFn: (ruleId: string) => Promise<void>,
    writeHealthFn: (entry: HealthEntry) => Promise<void>,
  ): Promise<EvaluationResult> {
    const startTime = Date.now();
    const checkedAt = new Date().toISOString();
    let alertsTriggered = 0;
    const errors: string[] = [];

    let rules: Rule[];
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
        let shouldAlert = false;
        let mensaje = '';
        const detectedAt = new Date().toISOString();
        const now = Date.now();

        if (rule.tipo_condicion === 'SILENCE_TIMEOUT') {
          const lastEventTs = await queryLastEventFn(rule.node_id);

          // If no telemetry exists yet for this node, skip
          if (lastEventTs === null) {
            continue;
          }

          const lastEventTime = new Date(lastEventTs).getTime();
          const elapsedSeconds = (now - lastEventTime) / 1000;

          // Check if silence threshold has been exceeded
          if (elapsedSeconds <= rule.valor_umbral) {
            continue; // Node is still active — no silence violation
          }

          // Check cooldown
          if (rule.last_alerted_at !== null) {
            const lastAlertTime = new Date(rule.last_alerted_at).getTime();
            const cooldownMs = rule.cooldown_minutos * 60 * 1000;
            if (now - lastAlertTime <= cooldownMs) {
              continue; // Still within cooldown — skip
            }
          }

          shouldAlert = true;
          mensaje = [
            `SILENCE TIMEOUT - Nodo: ${rule.node_id}`,
            `Último evento: ${lastEventTs}`,
            `Umbral de silencio: ${rule.valor_umbral}s`,
            `Segundos desde último evento: ${Math.round(elapsedSeconds)}s`,
          ].join('\n');
        } else if (rule.tipo_condicion === 'ERROR_THRESHOLD') {
          // Count errors in the time window
          const errorCount = await queryErrorCountFn(rule.node_id, rule.ventana_minutos);
          const windowMinutes = rule.ventana_minutos ?? 5;

          if (errorCount >= rule.valor_umbral) {
            // Check cooldown (same logic as SILENCE_TIMEOUT)
            if (rule.last_alerted_at !== null) {
              const lastAlertTime = new Date(rule.last_alerted_at).getTime();
              const cooldownMs = rule.cooldown_minutos * 60 * 1000;
              if (now - lastAlertTime <= cooldownMs) {
                continue;
              }
            }
            shouldAlert = true;
            mensaje = `ERROR THRESHOLD - Nodo: ${rule.node_id}\nErrores en últimos ${windowMinutes} min: ${errorCount}\nUmbral: ${rule.valor_umbral}`;
          }
        }

        if (shouldAlert) {
          await insertAlertEventFn({
            rule_id: rule.id,
            node_id: rule.node_id,
            tipo_evento: rule.tipo_condicion,
            mensaje,
            detected_at: detectedAt,
          });
          await updateLastAlertedFn(rule.id);
          alertsTriggered++;
        }
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
