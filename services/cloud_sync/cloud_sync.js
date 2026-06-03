'use strict';

// ---------------------------------------------------------------------------
// GEMA V3.0 Cloud Sync — SQLite to Hasura GraphQL Upload
//
// Polls the telemetry_buffer table for PENDING records, constructs dynamic
// GraphQL mutations, and POSTs them to a Hasura endpoint. Implements
// Exponential Backoff on HTTP 5xx errors and marks records as FAILED when
// maxRetries is exhausted.
//
// Architecture rules enforced:
//   #2  — SQLite WAL mode on every connection
//   #3  — Exponential Backoff: delay = min(baseDelayMs × 2^attempt, 60000)
//   AD1 — Bare Node.js (no Express, no axios — native fetch only)
// ---------------------------------------------------------------------------

/**
 * Creates a cloud sync instance.
 *
 * Dependency injection friendly — pass mocks for { db, fetch } in tests,
 * or real instances in production.
 *
 * @param {object} deps
 * @param {object} deps.db                SQLite connection (better-sqlite3 API)
 * @param {function} [deps.fetch]         Async fetch(url, options) → Response.
 *                                        Defaults to globalThis.fetch.
 * @param {object} [deps.config]
 * @param {string} [deps.config.hasuraUrl]        Hasura GraphQL endpoint.
 *                                                 Default: $HASURA_ENDPOINT
 * @param {string} [deps.config.hasuraSecret]     Hasura admin secret.
 *                                                 Default: $HASURA_SECRET
 * @param {number} [deps.config.pollInterval]     Poll interval in ms.
 *                                                 Default: $POLL_INTERVAL_MS or 5000
 * @param {number} [deps.config.maxRetries]       Max retry attempts per record.
 *                                                 Default: $MAX_RETRIES or 5
 * @param {number} [deps.config.baseDelayMs]      Base delay in ms for backoff.
 *                                                 Default: 1000
 * @param {function} [deps.config.log]            Info logger.
 * @param {function} [deps.config.logWarn]        Warning logger.
 * @returns {{ start: function, stop: function, pollOnce: function,
 *             syncRecord: function, buildMutation: function }}
 */
function createCloudSync({ db, fetch: fetchFn, config = {} }) {
  const hasuraUrl    = config.hasuraUrl    || process.env.HASURA_ENDPOINT;
  const hasuraSecret = config.hasuraSecret || process.env.HASURA_SECRET;
  const pollInterval = config.pollInterval || parseInt(process.env.POLL_INTERVAL_MS || '5000', 10);
  const maxRetries   = config.maxRetries   || parseInt(process.env.MAX_RETRIES   || '5', 10);
  const baseDelayMs  = config.baseDelayMs  || 1000;
  const reqFetch     = fetchFn             || globalThis.fetch;
  const log     = config.log     || console.log.bind(console, '[cloud_sync]');
  const logWarn = config.logWarn || console.warn.bind(console, '[cloud_sync]');

  // -----------------------------------------------------------------------
  // Helpers
  // -----------------------------------------------------------------------

  const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

  // -----------------------------------------------------------------------
  // Schema / table bootstrap
  // -----------------------------------------------------------------------

  function ensureTable() {
    db.pragma('journal_mode = WAL');
    db.pragma('busy_timeout = 5000');

    db.exec(`
      CREATE TABLE IF NOT EXISTS telemetry_buffer (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        topic TEXT NOT NULL,
        payload TEXT NOT NULL,
        node_health TEXT NOT NULL,
        received_at TEXT NOT NULL DEFAULT (datetime('now')),
        sync_status TEXT NOT NULL DEFAULT 'PENDING',
        retry_count INTEGER NOT NULL DEFAULT 0,
        last_error TEXT
      )
    `);
  }

  // -----------------------------------------------------------------------
  // GraphQL mutation builder
  // -----------------------------------------------------------------------

  /**
   * Builds a dynamic GraphQL mutation from a telemetry_buffer row.
   *
   * @param {object} row  Row from telemetry_buffer
   * @returns {{ query: string, variables: object }}
   */
  function buildMutation(row) {
    return {
      query: `mutation InsertTelemetry($objects: [telemetry_insert_input!]!) {
        insert_telemetry(objects: $objects) { returning { id } }
      }`,
      variables: {
        objects: [
          {
            topic: row.topic,
            payload: row.payload,
            node_health: row.node_health,
            received_at: row.received_at,
          },
        ],
      },
    };
  }

  // -----------------------------------------------------------------------
  // Core sync logic — per record
  // -----------------------------------------------------------------------

  /**
   * Attempts to sync a single PENDING record to Hasura.
   *
   * On HTTP 2xx: marks the record as SYNCED.
   * On HTTP 5xx: increments retry_count, applies Exponential Backoff, and
   *   retries recursively.  Records become FAILED when maxRetries is reached.
   * On invalid JSON payload: sets last_error and skips (no fetch call).
   * On network/parse error: sets last_error and stops retrying for this call.
   *
   * @param {object} row  Row from telemetry_buffer
   * @returns {Promise<void>}
   */
  async function syncRecord(row) {
    try {
      // ---- Validate payload ——————————————————————————
      let payload;
      try {
        payload = JSON.parse(row.payload);
      } catch {
        db.prepare('UPDATE telemetry_buffer SET last_error = ? WHERE id = ?')
          .run('Invalid JSON payload', row.id);
        logWarn('Record', row.id, '- invalid JSON payload, skipping');
        return;
      }

      // ---- Build mutation and POST ———————————————————
      const mutation = buildMutation(row);

      const headers = { 'Content-Type': 'application/json' };
      if (hasuraSecret) {
        headers['x-hasura-admin-secret'] = hasuraSecret;
      }

      const response = await reqFetch(hasuraUrl, {
        method: 'POST',
        headers,
        body: JSON.stringify(mutation),
      });

      // ---- HTTP 2xx → success ————————————————————————
      if (response.ok) {
        db.prepare("UPDATE telemetry_buffer SET sync_status = 'SYNCED' WHERE id = ?")
          .run(row.id);
        log('Record', row.id, 'synced OK');
        return;
      }

      // ---- HTTP 5xx → Exponential Backoff ———————————
      if (response.status >= 500) {
        const attempt = (row.retry_count || 0) + 1;
        const lastError = `HTTP ${response.status}`;

        if (attempt >= maxRetries) {
          db.prepare(
            "UPDATE telemetry_buffer SET sync_status = 'FAILED', retry_count = ?, last_error = ? WHERE id = ?"
          ).run(attempt, lastError, row.id);
          logWarn('Record', row.id, 'FAILED after', attempt, 'retries');
          return;
        }

        db.prepare(
          'UPDATE telemetry_buffer SET retry_count = ?, last_error = ? WHERE id = ?'
        ).run(attempt, lastError, row.id);

        // Exponential Backoff: delay = min(baseDelayMs × 2^(attempt-1), 60000)
        const delay = Math.min(baseDelayMs * Math.pow(2, attempt - 1), 60000);
        logWarn('Record', row.id, 'HTTP', response.status, '- retry', attempt, 'in', delay, 'ms');
        await sleep(delay);

        // Re-read the record (may have changed by another poll cycle)
        const updated = db.prepare('SELECT * FROM telemetry_buffer WHERE id = ?').get(row.id);
        if (updated && updated.sync_status === 'PENDING') {
          return syncRecord(updated);
        }
        return;
      }

      // ---- Non-5xx error (4xx, etc.) —————————————————
      logWarn('Record', row.id, '- unexpected HTTP', response.status, '- not retrying');
      db.prepare('UPDATE telemetry_buffer SET last_error = ? WHERE id = ?')
        .run(`HTTP ${response.status}`, row.id);

    } catch (err) {
      // Network error, JSON response parse error, etc.
      const msg = err.message || String(err);
      db.prepare('UPDATE telemetry_buffer SET last_error = ? WHERE id = ?')
        .run(msg, row.id);
      logWarn('Record', row.id, '- error:', msg);
    }
  }

  // -----------------------------------------------------------------------
  // Poll — query PENDING records and sync each one
  // -----------------------------------------------------------------------

  /**
   * Queries up to 10 PENDING records from the buffer and syncs each one.
   *
   * @returns {Promise<PromiseSettledResult[]>}
   */
  function pollOnce() {
    ensureTable();
    const rows = db.prepare(
      "SELECT * FROM telemetry_buffer WHERE sync_status = 'PENDING' ORDER BY id ASC LIMIT 10"
    ).all();

    if (rows.length === 0) {
      return Promise.resolve([]);
    }

    return Promise.allSettled(rows.map((row) => syncRecord(row)));
  }

  // -----------------------------------------------------------------------
  // Lifecycle
  // -----------------------------------------------------------------------

  let intervalHandle = null;

  function start() {
    ensureTable();
    log('Starting — polling every', pollInterval, 'ms');
    pollOnce(); // fire first poll immediately
    intervalHandle = setInterval(() => pollOnce(), pollInterval);
  }

  function stop() {
    if (intervalHandle) {
      clearInterval(intervalHandle);
      intervalHandle = null;
    }
  }

  return { start, stop, pollOnce, syncRecord, buildMutation };
}

// ---------------------------------------------------------------------------
// Standalone entry point (when run via `npm start` or `node cloud_sync.js`)
// ---------------------------------------------------------------------------

if (require.main === module) {
  const Database = require('better-sqlite3');

  const hasuraUrl = process.env.HASURA_ENDPOINT;
  const dbPath    = process.env.SQLITE_DB_PATH || '/data/telemetry_buffer.db';

  if (!hasuraUrl) {
    console.error('[cloud_sync] FATAL: HASURA_ENDPOINT environment variable is required');
    process.exit(1);
  }

  const db = new Database(dbPath);
  const sync = createCloudSync({ db });
  sync.start();

  // Graceful shutdown on SIGTERM / SIGINT (Docker stop, Ctrl+C)
  function shutdown() {
    console.log('[cloud_sync] Shutting down...');
    sync.stop();
    db.close();
    process.exit(0);
  }
  process.on('SIGTERM', shutdown);
  process.on('SIGINT', shutdown);
}

module.exports = { createCloudSync };
