// ============================================================================
// whatsapp.ts — Meta Cloud API WhatsApp adapter
//
// Sends alert messages via the Meta WhatsApp Cloud API.
// Token stored in NHOST_WHATSAPP_TOKEN env var (Nhost secret).
// Config: { to: string } — recipient phone number
// Phone number ID is set via NHOST_WHATSAPP_PHONE_ID env var.
// ============================================================================

import type { AlertDispatcherAdapter, DispatchResult } from '../types';
import type { ChannelConfig, AlertMessage } from '../types';

const WHATSAPP_API_BASE = 'https://graph.facebook.com/v21.0';

/**
 * Creates a WhatsApp Cloud API adapter.
 * Token and Phone ID are read from environment variables.
 */
export function createAdapter(): AlertDispatcherAdapter {
  return {
    async send(config: ChannelConfig, message: AlertMessage): Promise<DispatchResult> {
      const token = process.env.NHOST_WHATSAPP_TOKEN;
      const phoneId = process.env.NHOST_WHATSAPP_PHONE_ID;

      if (!token) {
        return { success: false, error: 'Missing NHOST_WHATSAPP_TOKEN env var' };
      }
      if (!phoneId) {
        return { success: false, error: 'Missing NHOST_WHATSAPP_PHONE_ID env var' };
      }

      const to = config.to as string;
      if (!to) {
        return { success: false, error: 'Missing to in channel config' };
      }

      const url = `${WHATSAPP_API_BASE}/${phoneId}/messages`;
      const body = {
        messaging_product: 'whatsapp',
        to,
        type: 'text',
        text: { body: formatWhatsAppMessage(message) },
      };

      try {
        const response = await fetch(url, {
          method: 'POST',
          headers: {
            'Content-Type': 'application/json',
            Authorization: `Bearer ${token}`,
          },
          body: JSON.stringify(body),
        });

        if (response.ok) {
          return { success: true, statusCode: response.status };
        }

        const errorBody = await response.text().catch(() => '');
        return {
          success: false,
          statusCode: response.status,
          error: `WhatsApp API error (${response.status}): ${errorBody}`,
        };
      } catch (err) {
        const errorMessage = err instanceof Error ? err.message : String(err);
        return { success: false, error: `WhatsApp request failed: ${errorMessage}` };
      }
    },
  };
}

/**
 * Formats an AlertMessage for WhatsApp plain-text.
 */
function formatWhatsAppMessage(message: AlertMessage): string {
  const lines: string[] = [
    `🚨 ALERTA - Nodo: ${message.node_id}`,
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
