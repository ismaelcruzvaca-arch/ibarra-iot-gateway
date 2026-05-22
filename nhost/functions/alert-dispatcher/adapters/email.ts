// ============================================================================
// email.ts — Nhost SMTP email adapter
//
// Sends alert messages via Nhost SMTP using nodemailer.
// SMTP credentials are stored in Nhost secrets as environment variables:
//   NHOST_SMTP_HOST, NHOST_SMTP_PORT, NHOST_SMTP_USER, NHOST_SMTP_PASS
// Config: { to: string, subject?: string }
// ============================================================================

import type { AlertDispatcherAdapter, DispatchResult } from '../types';
import type { ChannelConfig, AlertMessage } from '../types';

// nodemailer is available in the Nhost Functions runtime.
// Import using the npm: specifier for Deno-compatible Edge Runtime.
// istanbul ignore next — dynamic import handled by runtime
const getNodemailer = async (): Promise<typeof import('npm:nodemailer')> => {
  return await import('npm:nodemailer');
};

/**
 * Creates an SMTP email adapter using Nhost SMTP credentials.
 */
export function createAdapter(): AlertDispatcherAdapter {
  return {
    async send(config: ChannelConfig, message: AlertMessage): Promise<DispatchResult> {
      const to = config.to as string;
      if (!to) {
        return { success: false, error: 'Missing to in channel config' };
      }

      const smtpHost = process.env.NHOST_SMTP_HOST;
      const smtpPort = process.env.NHOST_SMTP_PORT;
      const smtpUser = process.env.NHOST_SMTP_USER;
      const smtpPass = process.env.NHOST_SMTP_PASS;

      if (!smtpHost) {
        return { success: false, error: 'Missing NHOST_SMTP_HOST env var' };
      }

      const subject =
        (config.subject as string) || `ALERTA - Nodo: ${message.node_id}`;
      const textBody = formatEmailMessage(message);

      try {
        const nodemailer = await getNodemailer();
        const transport = nodemailer.createTransport({
          host: smtpHost,
          port: Number(smtpPort) || 587,
          secure: Number(smtpPort) === 465,
          auth: smtpUser && smtpPass
            ? { user: smtpUser, pass: smtpPass }
            : undefined,
        });

        await transport.sendMail({
          from: smtpUser || 'alert@nhost.local',
          to,
          subject,
          text: textBody,
        });

        return { success: true };
      } catch (err) {
        const errorMessage = err instanceof Error ? err.message : String(err);
        return { success: false, error: `Email send failed: ${errorMessage}` };
      }
    },
  };
}

/**
 * Formats an AlertMessage as plain-text email body.
 */
function formatEmailMessage(message: AlertMessage): string {
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
