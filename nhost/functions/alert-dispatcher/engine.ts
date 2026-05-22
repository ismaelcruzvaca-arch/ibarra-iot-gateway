// ============================================================================
// engine.ts — Alert Dispatcher Engine
//
// Core dispatch engine that resolves alert rules for a given node and fans
// out alerts to all matching channel adapters. Uses retryBackoff for each
// channel dispatch and writes dead-letter entries to alert_engine_health
// when all retries are exhausted.
// ============================================================================

import type { AlertMessage, AlertDispatcherAdapter, ChannelConfig, DispatchResult } from './types';
import type { ChannelType } from './types';
import { retryBackoff } from './utils';

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

/** A matched rule with its resolved channel configuration */
export interface ResolvedRule {
  ruleId: string;
  channelType: ChannelType;
  config: ChannelConfig;
  enabled: boolean;
}

/** Result of a fan-out operation across all channels */
export interface FanOutResult {
  channelResults: Array<{
    channelType: ChannelType;
    success: boolean;
    statusCode?: number;
    error?: string;
  }>;
}

// ---------------------------------------------------------------------------
// DispatcherEngine
// ---------------------------------------------------------------------------

/**
 * Core dispatch engine that resolves rules for a node and fans out alerts
 * to all matching channel adapters.
 *
 * The engine is designed to be instantiated with an adapter registry and
 * a data-access function for querying rules from the database.
 */
export class DispatcherEngine {
  private adapters: Record<ChannelType, AlertDispatcherAdapter>;

  constructor(adapters: Record<ChannelType, AlertDispatcherAdapter>) {
    this.adapters = adapters;
  }

  /**
   * Resolves the matching enabled alert rules for a given node.
   *
   * In production this queries `alert_rules WHERE node_id = X AND enabled = true`
   * via the Nhost Hasura client. For testability, the query function is injected.
   *
   * @param nodeId - The Norvi node identifier
   * @param queryFn - Function that queries rules from the database
   * @returns Array of resolved rules (empty if no rules match)
   */
  async resolveRules(
    nodeId: string,
    queryFn: (nodeId: string) => Promise<ResolvedRule[]> = defaultQueryRules,
  ): Promise<ResolvedRule[]> {
    const rules = await queryFn(nodeId);
    // Return only enabled rules
    return rules.filter((rule) => rule.enabled);
  }

  /**
   * Fans out an alert message to all provided channels via their adapters.
   *
   * Each channel dispatch is wrapped in retryBackoff (3× exponential: 1s, 2s, 4s).
   * If all retries are exhausted, a dead-letter entry is written to
   * alert_engine_health via the provided deadLetterFn.
   *
   * @param channels - Array of resolved channel rules to dispatch to
   * @param message - The alert message to send
   * @param deadLetterFn - Function to write dead-letter entries on failure
   * @returns FanOutResult with per-channel results
   */
  async fanOut(
    channels: ResolvedRule[],
    message: AlertMessage,
    deadLetterFn: (entry: DeadLetterEntry) => Promise<void> = defaultDeadLetter,
  ): Promise<FanOutResult> {
    const results: FanOutResult['channelResults'] = [];

    for (const channel of channels) {
      if (!channel.enabled) {
        continue; // Skip disabled channels
      }

      const adapter = this.adapters[channel.channelType];
      if (!adapter) {
        results.push({
          channelType: channel.channelType,
          success: false,
          error: `No adapter registered for channel type: ${channel.channelType}`,
        });
        continue;
      }

      try {
        const result: DispatchResult = await retryBackoff(
          () => adapter.send(channel.config, message),
          { maxRetries: 3, baseDelayMs: 1000 },
        );
        results.push({
          channelType: channel.channelType,
          success: result.success,
          statusCode: result.statusCode,
          error: result.error,
        });
      } catch (err) {
        // All retries exhausted — write dead-letter
        const errorMessage = err instanceof Error ? err.message : String(err);
        const deadLetter: DeadLetterEntry = {
          success: false,
          detail: errorMessage,
          checked_at: new Date().toISOString(),
          channel_type: channel.channelType,
          node_id: message.node_id,
        };
        await deadLetterFn(deadLetter);
        results.push({
          channelType: channel.channelType,
          success: false,
          error: errorMessage,
        });
      }
    }

    return { channelResults: results };
  }
}

// ---------------------------------------------------------------------------
// Dead letter entry
// ---------------------------------------------------------------------------

export interface DeadLetterEntry {
  success: boolean;
  detail: string;
  checked_at: string;
  channel_type: string;
  node_id: string;
}

// ---------------------------------------------------------------------------
// Default implementations (production)
// ---------------------------------------------------------------------------

/**
 * Default query function that queries alert_rules from the database.
 * In production this uses the Nhost Hasura GraphQL client.
 * For now, returns an empty array (rules are resolved via the actual
 * Nhost runtime at deploy time).
 */
async function defaultQueryRules(_nodeId: string): Promise<ResolvedRule[]> {
  // Production implementation will query:
  //   query AlertRules($nodeId: String!) {
  //     alert_rules(where: { node_id: { _eq: $nodeId }, enabled: { _eq: true } }) {
  //       id, channel_id, enabled, alert_channel { channel_type, config_json, enabled }
  //     }
  //   }
  return [];
}

/**
 * Default dead-letter writer that inserts a failure entry into
 * alert_engine_health. In production this uses the Nhost Hasura client.
 */
async function defaultDeadLetter(_entry: DeadLetterEntry): Promise<void> {
  // Production implementation will mutate:
  //   mutation InsertDeadLetter($entry: alert_engine_health_insert_input!) {
  //     insert_alert_engine_health_one(object: $entry) { check_id }
  //   }
}
