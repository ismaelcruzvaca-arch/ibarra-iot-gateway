// ============================================================================
// push.ts — Expo Push Notification adapter
//
// Sends alert messages via Expo Push Notification API.
// No authentication required (Expo uses a free push service).
// Config: { push_tokens: string[] } — array of Expo push tokens
// ============================================================================

import type { AlertDispatcherAdapter, DispatchResult } from '../types';
import type { ChannelConfig, AlertMessage } from '../types';

const EXPO_PUSH_API = 'https://exp.host/--/api/v2/push/send';

/**
 * Creates an Expo Push Notification adapter.
 * Push tokens must be provided in the channel config as `push_tokens`.
 */
export function createAdapter(): AlertDispatcherAdapter {
  return {
    async send(config: ChannelConfig, message: AlertMessage): Promise<DispatchResult> {
      const pushTokens = config.push_tokens as string[] | undefined;

      if (!pushTokens || pushTokens.length === 0) {
        return { success: false, error: 'Missing or empty push_tokens in channel config' };
      }

      const title = formatPushTitle(message);
      const body = formatPushBody(message);

      // Expo accepts up to 100 messages per request; send to each token
      const messages = pushTokens.map((token) => ({
        to: token,
        sound: 'default' as const,
        title,
        body,
        data: {
          node_id: message.node_id,
          status: message.status,
          event_ts: message.event_ts,
        },
      }));

      try {
        const response = await fetch(EXPO_PUSH_API, {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify(messages),
        });

        if (response.ok) {
          return { success: true, statusCode: response.status };
        }

        const errorBody = await response.text().catch(() => '');
        return {
          success: false,
          statusCode: response.status,
          error: `Expo Push API error (${response.status}): ${errorBody}`,
        };
      } catch (err) {
        const errorMessage = err instanceof Error ? err.message : String(err);
        return { success: false, error: `Expo push request failed: ${errorMessage}` };
      }
    },
  };
}

/**
 * Formats the push notification title.
 */
function formatPushTitle(message: AlertMessage): string {
  return `🚨 ALERTA - ${message.node_id}`;
}

/**
 * Formats the push notification body text.
 */
function formatPushBody(message: AlertMessage): string {
  const lines: string[] = [
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
