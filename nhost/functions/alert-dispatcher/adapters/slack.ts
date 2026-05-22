// ============================================================================
// slack.ts — Slack Incoming Webhook adapter
//
// Sends alert messages to a Slack channel via an incoming webhook URL.
// The webhook URL is stored in the channel config (config_json.webhook_url).
// No additional secrets needed — the webhook URL itself contains the auth.
// ============================================================================

import type { AlertDispatcherAdapter, DispatchResult } from '../types';
import type { ChannelConfig, AlertMessage } from '../types';

/**
 * Creates a Slack webhook adapter.
 * The webhook URL must be provided in the channel config as `webhook_url`.
 */
export function createAdapter(): AlertDispatcherAdapter {
  return {
    async send(config: ChannelConfig, message: AlertMessage): Promise<DispatchResult> {
      const webhookUrl = config.webhook_url as string;
      if (!webhookUrl) {
        return { success: false, error: 'Missing webhook_url in channel config' };
      }

      const body = {
        text: formatSlackMessage(message),
        mrkdwn: true,
      };

      try {
        const response = await fetch(webhookUrl, {
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
          error: `Slack webhook error (${response.status}): ${errorBody}`,
        };
      } catch (err) {
        const errorMessage = err instanceof Error ? err.message : String(err);
        return { success: false, error: `Slack request failed: ${errorMessage}` };
      }
    },
  };
}

/**
 * Formats an AlertMessage for Slack (mrkdwn format).
 */
function formatSlackMessage(message: AlertMessage): string {
  const lines: string[] = [
    `🚨 *ALERTA - Nodo: ${message.node_id}*`,
    `Estado: ${message.status}`,
    `Timestamp: ${message.event_ts}`,
  ];

  const payload = message.payload;
  if (payload && Object.keys(payload).length > 0) {
    lines.push('---');
    for (const [key, value] of Object.entries(payload)) {
      lines.push(`*${key}:* ${String(value)}`);
    }
  }

  return lines.join('\n');
}
