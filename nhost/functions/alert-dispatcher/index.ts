// ============================================================================
// index.ts — Alert Dispatcher Nhost Function entry point
//
// BIFURCATED handler that supports two sources:
//
// 1. norvi_telemetry (SYSTEM rules):
//    Receives Hasura Event Trigger payload for norvi_telemetry INSERT events
//    where payload->>'status' = '3'. Parses the event, resolves matching
//    alert rules via the DispatcherEngine, and fans out to all enabled channels.
//
// 2. alert_events (USER_DEFINED rules):
//    Receives Hasura Event Trigger payload for alert_events INSERT events
//    (created by silence-detector cron). Reads the rule's canales JSONB array,
//    queries matching alert_channels by type, and dispatches via the adapters.
// ============================================================================

import type { AlertMessage, AlertDispatcherAdapter, ChannelConfig, DispatchResult } from './types';
import type { ChannelType } from './types';
import { DispatcherEngine } from './engine';
import type { ResolvedRule } from './engine';
import { defaultRegistry } from './adapters/registry';
import { retryBackoff } from './utils';

// ---------------------------------------------------------------------------
// Hasura Event Trigger payload shape
// ---------------------------------------------------------------------------

/** Shape of a single column value in Hasura Event Trigger data */
interface HasuraColumnValue {
  value: unknown;
}

/** Shape for norvi_telemetry event payload */
interface NorviTelemetryNewRow {
  node_id: string;
  payload: Record<string, unknown>;
  event_ts: string;
  [key: string]: unknown;
}

/** Shape for alert_events event payload */
interface AlertEventRow {
  id: string;
  rule_id: string;
  node_id: string;
  tipo_evento: string;
  mensaje: string;
  detected_at: string;
  dispatched: boolean;
  dispatch_result: Record<string, unknown> | null;
  created_at: string;
  [key: string]: unknown;
}

/**
 * Hasura Event Trigger payload that supports both norvi_telemetry
 * and alert_events table schemas.
 */
export interface HasuraEventPayload {
  event: {
    op: 'INSERT' | 'UPDATE' | 'DELETE';
    data: {
      new: NorviTelemetryNewRow | AlertEventRow;
      old?: Record<string, unknown> | null;
    };
  };
  created_at?: string;
  id?: string;
  table?: {
    schema: string;
    name: string;
  };
  trigger?: {
    name: string;
  };
}

// ---------------------------------------------------------------------------
// GraphQL helpers (production)
// ---------------------------------------------------------------------------

/**
 * Makes a GraphQL request to the Nhost Hasura endpoint using the admin secret.
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
 * Queries a USER_DEFINED rule by ID and returns its canales and related data.
 */
async function queryUserDefinedRule(
  ruleId: string,
): Promise<{ canales: string[]; node_id: string; cooldown_minutos: number } | null> {
  const query = `
    query {
      alert_rules_by_pk(id: "${ruleId}") {
        id
        node_id
        canales
        cooldown_minutos
        scope
        enabled
      }
    }
  `;

  const result = await graphqlRequest(query);
  const data = result.data as Record<string, unknown> | undefined;
  const rule = data?.alert_rules_by_pk as Record<string, unknown> | undefined;

  if (!rule || rule.enabled === false) return null;

  return {
    canales: (rule.canales as string[]) ?? [],
    node_id: rule.node_id as string,
    cooldown_minutos: (rule.cooldown_minutos as number) ?? 30,
  };
}

/**
 * Queries enabled alert_channels by channel type.
 */
async function queryChannelsByType(
  channelType: string,
): Promise<Array<{ id: string; config_json: ChannelConfig }>> {
  const query = `
    query {
      alert_channels(
        where: {
          channel_type: { _eq: "${channelType}" },
          enabled: { _eq: true }
        }
      ) {
        id
        config_json
      }
    }
  `;

  const result = await graphqlRequest(query);
  const data = result.data as Record<string, unknown> | undefined;
  const channels = data?.alert_channels as
    | Array<{ id: string; config_json: Record<string, unknown> }>
    | undefined;

  if (!channels) return [];
  return channels.map((ch) => ({
    id: ch.id,
    config_json: ch.config_json as ChannelConfig,
  }));
}

/**
 * Updates an alert_event to mark it as dispatched with the result.
 */
async function updateAlertEventDispatched(
  eventId: string,
  dispatchResult: Record<string, unknown>,
): Promise<void> {
  const mutation = `
    mutation {
      update_alert_events_by_pk(
        pk_columns: { id: "${eventId}" },
        _set: {
          dispatched: true,
          dispatch_result: ${JSON.stringify(JSON.stringify(dispatchResult))}
        }
      ) { id }
    }
  `;

  await graphqlRequest(mutation);
}

// ---------------------------------------------------------------------------
// Dispatch helpers
// ---------------------------------------------------------------------------

/**
 * Dispatches an alert from an alert_event through all channels specified
 * in the rule's canales array.
 */
async function dispatchAlertEvent(
  event: AlertEventRow,
  adapters: Record<ChannelType, AlertDispatcherAdapter>,
): Promise<{
  eventId: string;
  node_id: string;
  dispatched: boolean;
  channelResults: Array<{
    channelType: string;
    success: boolean;
    statusCode?: number;
    error?: string;
  }>;
}> {
  const ruleInfo = await queryUserDefinedRule(event.rule_id);

  if (!ruleInfo || !ruleInfo.canales || ruleInfo.canales.length === 0) {
    return {
      eventId: event.id,
      node_id: event.node_id,
      dispatched: false,
      channelResults: [],
    };
  }

  // Build a message from the alert_event data
  const message: AlertMessage = {
    node_id: event.node_id,
    status: 0, // USER_DEFINED alerts don't have a norvi status code
    payload: {
      tipo_evento: event.tipo_evento,
      mensaje: event.mensaje,
      detected_at: event.detected_at,
    },
    event_ts: event.detected_at,
  };

  const channelResults: Array<{
    channelType: string;
    success: boolean;
    statusCode?: number;
    error?: string;
  }> = [];

  for (const channelType of ruleInfo.canales) {
    const adapter = adapters[channelType as ChannelType];

    if (!adapter) {
      channelResults.push({
        channelType,
        success: false,
        error: `No adapter registered for channel type: ${channelType}`,
      });
      continue;
    }

    // Query all enabled channels of this type
    const channels = await queryChannelsByType(channelType);

    if (channels.length === 0) {
      channelResults.push({
        channelType,
        success: false,
        error: `No enabled alert_channels found for type: ${channelType}`,
      });
      continue;
    }

    // Send to each enabled channel of this type
    for (const ch of channels) {
      try {
        const result: DispatchResult = await retryBackoff(
          () => adapter.send(ch.config_json, message),
          { maxRetries: 3, baseDelayMs: 1000 },
        );
        channelResults.push({
          channelType,
          success: result.success,
          statusCode: result.statusCode,
          error: result.error,
        });
      } catch (err) {
        const errorMessage = err instanceof Error ? err.message : String(err);
        channelResults.push({
          channelType,
          success: false,
          error: errorMessage,
        });
      }
    }
  }

  const anySuccess = channelResults.some((r) => r.success);

  // Mark the event as dispatched
  await updateAlertEventDispatched(event.id, {
    dispatched: true,
    channelResults,
  });

  return {
    eventId: event.id,
    node_id: event.node_id,
    dispatched: anySuccess,
    channelResults,
  };
}

// ---------------------------------------------------------------------------
// Nhost Function handler
// ---------------------------------------------------------------------------

/**
 * Nhost Function handler for the alert-dispatcher.
 *
 * BIFURCATED: handles both norvi_telemetry (SYSTEM) and alert_events
 * (USER_DEFINED) sources via Hasura Event Triggers.
 *
 * @param payload - The raw HTTP request body (Hasura Event Trigger payload)
 * @returns Response object with dispatch results
 */
export async function handler(payload: HasuraEventPayload): Promise<{
  status: number;
  body: string;
}> {
  try {
    // -----------------------------------------------------------------------
    // 1. Parse the Hasura Event Trigger payload
    // -----------------------------------------------------------------------
    const { event, table } = payload;

    if (!event || event.op !== 'INSERT') {
      return {
        status: 200,
        body: JSON.stringify({ message: 'Non-INSERT event ignored' }),
      };
    }

    const row = event.data?.new;
    if (!row || !row.node_id) {
      return {
        status: 400,
        body: JSON.stringify({ error: 'Missing node_id in event payload' }),
      };
    }

    // -----------------------------------------------------------------------
    // 2. BIFURCATE: Determine source table
    // -----------------------------------------------------------------------
    const tableName = table?.name || 'norvi_telemetry';

    if (tableName === 'alert_events') {
      // -------------------------------------------------------------------
      // BRANCH B: USER_DEFINED alert_events flow
      // -------------------------------------------------------------------
      const alertEvent = row as AlertEventRow;
      const adapters = defaultRegistry.getAllAdapters();
      const dispatchResult = await dispatchAlertEvent(alertEvent, adapters);

      return {
        status: 200,
        body: JSON.stringify({
          message: 'Alert event dispatch completed',
          source: 'alert_events',
          event_id: dispatchResult.eventId,
          node_id: dispatchResult.node_id,
          dispatched: dispatchResult.dispatched,
          channelResults: dispatchResult.channelResults,
        }),
      };
    }

    // -----------------------------------------------------------------------
    // BRANCH A: norvi_telemetry (SYSTEM rules) — current flow
    // -----------------------------------------------------------------------
    const norviRow = row as NorviTelemetryNewRow;

    // Build the alert message
    const message: AlertMessage = {
      node_id: norviRow.node_id,
      status: 3, // ALARM — guaranteed by Hasura filter
      payload: norviRow.payload ?? {},
      event_ts: norviRow.event_ts,
    };

    // Resolve rules for this node
    const engine = new DispatcherEngine(defaultRegistry.getAllAdapters());
    const rules: ResolvedRule[] = await engine.resolveRules(message.node_id);

    // If no rules match, return cleanly (valid state — no error)
    if (rules.length === 0) {
      return {
        status: 200,
        body: JSON.stringify({
          message: 'No matching rules for node',
          source: 'norvi_telemetry',
          node_id: message.node_id,
          dispatched: false,
        }),
      };
    }

    // Fan out to all matching channels
    const result = await engine.fanOut(rules, message);

    return {
      status: 200,
      body: JSON.stringify({
        message: 'Dispatch completed',
        source: 'norvi_telemetry',
        node_id: message.node_id,
        dispatched: true,
        results: result.channelResults,
      }),
    };
  } catch (err) {
    const errorMessage = err instanceof Error ? err.message : String(err);
    return {
      status: 500,
      body: JSON.stringify({ error: 'Internal dispatch error', detail: errorMessage }),
    };
  }
}
