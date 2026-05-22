// ============================================================================
// utils.ts — Shared utilities for the Alert Dispatcher
//
// Provides:
//   - retryBackoff(): Exponential backoff with configurable retries
//   - formatMessage(): Formats an AlertMessage into human-readable text
// ============================================================================

import type { AlertMessage } from './types';

// ---------------------------------------------------------------------------
// retryBackoff
// ---------------------------------------------------------------------------

/** Configuration for retryBackoff */
export interface RetryOptions {
  /** Maximum number of retry attempts (after initial try) */
  maxRetries: number;
  /** Base delay in milliseconds (doubles each attempt: 1s, 2s, 4s) */
  baseDelayMs: number;
}

const DEFAULT_RETRY: RetryOptions = {
  maxRetries: 3,
  baseDelayMs: 1000,
};

/**
 * Executes an async function with exponential backoff retry.
 *
 * Wait times: baseDelayMs * 2^attempt
 * Default: 1s, 2s, 4s (3 retries after initial attempt)
 *
 * @param fn - Async function to execute and retry
 * @param options - Retry configuration (defaults: 3 retries, 1s base delay)
 * @returns Promise resolving to the function's return value
 * @throws The last error if all retries are exhausted
 */
export async function retryBackoff<T>(
  fn: () => Promise<T>,
  options: RetryOptions = DEFAULT_RETRY,
): Promise<T> {
  let lastError: unknown;

  for (let attempt = 0; attempt <= options.maxRetries; attempt++) {
    try {
      return await fn();
    } catch (err) {
      lastError = err;
      if (attempt < options.maxRetries) {
        const delay = options.baseDelayMs * Math.pow(2, attempt);
        await new Promise((resolve) => setTimeout(resolve, delay));
      }
    }
  }

  throw lastError;
}

// ---------------------------------------------------------------------------
// formatMessage
// ---------------------------------------------------------------------------

/**
 * Formats an AlertMessage into a human-readable plain-text string
 * suitable for sending via any channel adapter (Telegram, WhatsApp, etc.).
 *
 * @param message - The alert message to format
 * @returns Formatted plain-text string
 */
export function formatMessage(message: AlertMessage): string {
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
