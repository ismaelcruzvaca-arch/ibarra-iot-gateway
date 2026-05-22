// ============================================================================
// types.ts — Alert Dispatcher type definitions
//
// Defines the core interfaces and types for the Alert Engine's multi-channel
// dispatch system. All adapters implement AlertDispatcherAdapter.
// ============================================================================

/** Supported notification channel types */
export type ChannelType =
  | 'telegram'
  | 'whatsapp'
  | 'slack'
  | 'email'
  | 'torreta'
  | 'push';

/**
 * Channel-specific configuration.
 * Secrets (tokens, API keys) are stored in Nhost secrets, NEVER in config_json.
 * Only non-sensitive settings like chat_id, webhook_url, to address, etc.
 */
export interface ChannelConfig {
  [key: string]: unknown;
}

/**
 * Normalized alert message sent to all channel adapters.
 * Derived from the Hasura event trigger payload.
 */
export interface AlertMessage {
  /** Norvi device/node identifier */
  node_id: string;
  /** Status code (3 = ALARM, others filtered at trigger level) */
  status: number;
  /** Full JSONB payload from norvi_telemetry */
  payload: Record<string, unknown>;
  /** ISO-8601 timestamp of the telemetry event */
  event_ts: string;
}

/**
 * Result of a single channel dispatch attempt.
 * Does NOT represent retry exhaustion — that is handled by engine.ts.
 */
export interface DispatchResult {
  /** true if the channel API accepted the message (2xx) */
  success: boolean;
  /** HTTP status code from the channel API, if applicable */
  statusCode?: number;
  /** Error message if the request failed entirely (network, timeout, etc.) */
  error?: string;
}

/**
 * Contract every channel adapter must implement.
 * @param config - Channel-specific configuration (from alert_channels.config_json)
 * @param message - Normalized alert message
 * @returns Promise resolving to DispatchResult
 */
export interface AlertDispatcherAdapter {
  send(config: ChannelConfig, message: AlertMessage): Promise<DispatchResult>;
}
