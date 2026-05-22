// ============================================================================
// torreta.ts — Torreta/Sirena HTTP adapter
//
// Sends alert messages to a physical siren endpoint via HTTP POST.
// Config: { endpoint: string, api_key: string }
// The endpoint URL and API key are stored in channel config (config_json).
// ============================================================================

import type { AlertDispatcherAdapter, DispatchResult } from '../types';
import type { ChannelConfig, AlertMessage } from '../types';

/**
 * Creates a Torreta siren HTTP adapter.
 * The endpoint URL and API key must be provided in channel config.
 */
export function createAdapter(): AlertDispatcherAdapter {
  return {
    async send(config: ChannelConfig, message: AlertMessage): Promise<DispatchResult> {
      const endpoint = config.endpoint as string;
      const apiKey = config.api_key as string;

      if (!endpoint) {
        return { success: false, error: 'Missing endpoint in channel config' };
      }
      if (!apiKey) {
        return { success: false, error: 'Missing api_key in channel config' };
      }

      const body = {
        api_key: apiKey,
        message: formatTorretaMessage(message),
        severity: 'alarm',
        node_id: message.node_id,
        timestamp: message.event_ts,
      };

      try {
        const response = await fetch(endpoint, {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify(body),
        });

        if (response.ok) {
          return { success: true, statusCode: response.status };
        }

        const errorBody = await response.text().catch(() => '');
        return {
          success: false,
          statusCode: response.status,
          error: `Torreta API error (${response.status}): ${errorBody}`,
        };
      } catch (err) {
        const errorMessage = err instanceof Error ? err.message : String(err);
        return { success: false, error: `Torreta request failed: ${errorMessage}` };
      }
    },
  };
}

/**
 * Formats an AlertMessage for Torreta siren activation message.
 */
function formatTorretaMessage(message: AlertMessage): string {
  const lines: string[] = [
    `ALERTA - Nodo: ${message.node_id}`,
    `Estado: ${message.status}`,
    `Timestamp: ${message.event_ts}`,
  ];

  const payload = message.payload;
  if (payload && Object.keys(payload).length > 0) {
    lines.push('---');
    for (const [key, value] of Object.entries(payload)) {
      lines.push(`${key}: ${String(value)}`);
    }
  }

  return lines.join('\n');
}
