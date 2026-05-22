// ============================================================================
// telegram.ts — Telegram Bot API channel adapter
//
// Sends alert messages to a Telegram chat via the Bot API.
// Token stored in NHOST_TELEGRAM_BOT_TOKEN env var (Nhost secret).
// Config: { chat_id: string }
// ============================================================================

import type { AlertDispatcherAdapter, DispatchResult } from '../types';
import type { ChannelConfig, AlertMessage } from '../types';

const TELEGRAM_API_BASE = 'https://api.telegram.org/bot';

/**
 * Creates a Telegram Bot API adapter.
 * Token is read from the NHOST_TELEGRAM_BOT_TOKEN environment variable.
 */
export function createAdapter(): AlertDispatcherAdapter {
  return {
    async send(config: ChannelConfig, message: AlertMessage): Promise<DispatchResult> {
      const token = process.env.NHOST_TELEGRAM_BOT_TOKEN;
      if (!token) {
        return { success: false, error: 'Missing NHOST_TELEGRAM_BOT_TOKEN env var' };
      }

      const chatId = config.chat_id as string;
      if (!chatId) {
        return { success: false, error: 'Missing chat_id in channel config' };
      }

      const url = `${TELEGRAM_API_BASE}${token}/sendMessage`;
      const body = {
        chat_id: chatId,
        text: formatTelegramMessage(message),
        parse_mode: 'HTML',
      };

      try {
        const response = await fetch(url, {
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
          error: `Telegram API error (${response.status}): ${errorBody}`,
        };
      } catch (err) {
        const errorMessage = err instanceof Error ? err.message : String(err);
        return { success: false, error: `Telegram request failed: ${errorMessage}` };
      }
    },
  };
}

/**
 * Formats an AlertMessage for Telegram (HTML parse_mode).
 */
function formatTelegramMessage(message: AlertMessage): string {
  const lines: string[] = [
    `<b>🚨 ALERTA - Nodo: ${escapeHtml(message.node_id)}</b>`,
    `Estado: ${message.status}`,
    `Timestamp: ${message.event_ts}`,
  ];

  const payload = message.payload;
  if (payload && Object.keys(payload).length > 0) {
    lines.push('---');
    for (const [key, value] of Object.entries(payload)) {
      lines.push(`${escapeHtml(key)}: ${escapeHtml(String(value))}`);
    }
  }

  return lines.join('\n');
}

function escapeHtml(str: string): string {
  return str
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;');
}
