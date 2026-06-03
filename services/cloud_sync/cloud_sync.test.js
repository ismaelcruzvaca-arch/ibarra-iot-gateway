'use strict';

// ---------------------------------------------------------------------------
// GEMA V3.0 Cloud Sync — Hasura Upload Test Suite
//
// Tests cloud_sync.js using node:test + node:assert with an in-memory sql.js
// database (via a better-sqlite3 API adapter) and a mock fetch function.
//
// Follows Strict TDD: RED (failing test before implementation) → GREEN.
// ---------------------------------------------------------------------------

const { describe, it, before, beforeEach, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const initSqlJs = require('sql.js');

// The module under test — this will fail with MODULE_NOT_FOUND initially
// (RED phase), then resolve after cloud_sync.js is created (GREEN phase).
const { createCloudSync } = require('./cloud_sync');

// ---------------------------------------------------------------------------
// sql.js → better-sqlite3 API adapter
//
// better-sqlite3: db.pragma(), db.prepare().run({ named }), .all(), .get()
// sql.js:         db.run(),       db.prepare().run([positional]), .step()
//
// This adapter bridges the gap so the production code (written for
// better-sqlite3) runs transparently on sql.js during tests.
// ---------------------------------------------------------------------------

function makeBetterLikeDb(rawDb) {
  return {
    pragma: (str) => {
      rawDb.run(`PRAGMA ${str}`);
    },
    exec: (sql) => {
      rawDb.run(sql);
    },
    prepare: (sql) => {
      // Extract @named keys and replace with ? for positional binding
      const namedKeys = [];
      const positionalSql = sql.replace(/@(\w+)/g, (_, key) => {
        namedKeys.push(key);
        return '?';
      });
      const isNamed = namedKeys.length > 0;
      const stmt = rawDb.prepare(positionalSql);

      // Convert params to array: named-object → [v1, v2, ...] or raw
      function toArray(params) {
        if (params === undefined || params === null) return null;
        if (isNamed) return namedKeys.map((k) => params[k]);
        if (Array.isArray(params)) return params;
        return [params];
      }

      return {
        // better-sqlite3 accepts .run(a), .run([a,b]), .run(a,b,c)
        run: (...args) => {
          const params = args.length <= 1 ? args[0] : args;
          const arr = toArray(params);
          if (arr) stmt.run(arr);
          else stmt.run();
          stmt.reset();
        },

        all: (params) => {
          const arr = toArray(params);
          if (arr) stmt.bind(arr);
          const results = [];
          while (stmt.step()) results.push(stmt.getAsObject());
          stmt.reset();
          return results;
        },

        get: (params) => {
          const arr = toArray(params);
          if (arr) stmt.bind(arr);
          let row = null;
          if (stmt.step()) row = stmt.getAsObject();
          stmt.reset();
          return row;
        },
      };
    },
    close: () => rawDb.close(),
    /** Expose raw sql.js db for query helpers. */
    _raw: rawDb,
  };
}

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------

/**
 * Returns a mock fetch function that returns the given responses in sequence.
 * When exhausted, subsequent calls throw an error (safety net for tests).
 *
 * @param {...object} responses  Each is an object with { ok, status, json, text }
 * @returns {function}  async (url, options) => response
 */
function mockFetchSequence(...responses) {
  let idx = 0;
  return async (_url, _options) => {
    if (idx >= responses.length) {
      throw new Error(`Unexpected fetch call #${idx} — no more mock responses`);
    }
    return responses[idx++];
  };
}

/**
 * Builds a minimal fetch-like response object.
 *
 * @param {number} status  HTTP status code
 * @param {object} body    JSON body (returned by .json() and .text())
 * @returns {{ ok, status, json, text }}
 */
function mockResponse(status, body) {
  return {
    ok: status >= 200 && status < 300,
    status,
    json: async () => body,
    text: async () => JSON.stringify(body),
  };
}

/**
 * INSERT a telemetry_buffer row directly for test setup.
 * Uses the adapter's raw sql.js access for flexibility.
 *
 * @param {object} adapter  The makeBetterLikeDb adapter
 * @param {object} [overrides]
 */
function insertRecord(adapter, overrides = {}) {
  const sql = `
    INSERT INTO telemetry_buffer (topic, payload, node_health, received_at, sync_status, retry_count)
    VALUES (?, ?, ?, COALESCE(?, datetime('now')), ?, ?)
  `;
  adapter._raw.run(sql, [
    overrides.topic         || 'novamex/test/topic',
    overrides.payload       || JSON.stringify({ metrics: [{ name: 'temp', value: 42 }] }),
    overrides.node_health   || 'ONLINE',
    overrides.received_at   || null,
    overrides.sync_status   || 'PENDING',
    overrides.retry_count   || 0,
  ]);
}

/**
 * Return all rows from telemetry_buffer ordered by id.
 * @param {object} adapter  The makeBetterLikeDb adapter
 */
function allRows(adapter) {
  const res = adapter._raw.exec('SELECT * FROM telemetry_buffer ORDER BY id ASC');
  if (!res.length) return [];
  const { columns, values } = res[0];
  return values.map((v) => {
    const obj = {};
    columns.forEach((col, i) => { obj[col.toLowerCase()] = v[i]; });
    return obj;
  });
}

// ---------------------------------------------------------------------------
// Suite
// ---------------------------------------------------------------------------

describe('Cloud Sync — Hasura Upload', () => {
  let SQL;
  let rawDb;
  let db;
  let sync;

  before(async () => {
    SQL = await initSqlJs();
  });

  /** Create a fresh in-memory DB before each test. */
  beforeEach(() => {
    rawDb = new SQL.Database();
    db = makeBetterLikeDb(rawDb);
    // Create the telemetry_buffer table so test helpers can write directly.
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
  });

  /** Tear down after each test. */
  afterEach(() => {
    if (sync && typeof sync.stop === 'function') sync.stop();
    db.close();
  });

  // -----------------------------------------------------------------------
  // Scenario: Successful sync
  //   GIVEN cloud_sync finds a PENDING record
  //   WHEN Hasura responds HTTP 200
  //   THEN cloud_sync marks the record as SYNCED
  // -----------------------------------------------------------------------

  it('must mark record as SYNCED after HTTP 200', async () => {
    const mockFetch = mockFetchSequence(
      mockResponse(200, { data: { insert_telemetry: { returning: [{ id: 1 }] } } })
    );
    sync = createCloudSync({ db, fetch: mockFetch, config: { maxRetries: 5 } });

    insertRecord(db);

    await sync.pollOnce();

    const rows = allRows(db);
    assert.equal(rows.length, 1);
    assert.equal(rows[0].sync_status, 'SYNCED');
    assert.equal(rows[0].retry_count, 0, 'retry_count must remain 0 on success');
  });

  // -----------------------------------------------------------------------
  // Scenario: Hasura 500 keeps PENDING
  //   GIVEN cloud_sync finds a PENDING record
  //   WHEN Hasura responds HTTP 500
  //   THEN record status stays PENDING
  //   AND retry_count is incremented
  // -----------------------------------------------------------------------

  it('must keep record as PENDING after HTTP 500 and increment retry_count', async () => {
    // First call returns 500; recursive retry call throws (simulates end)
    let callCount = 0;
    const mockFetch = async () => {
      callCount++;
      if (callCount === 1) return mockResponse(500, { errors: [{ message: 'Internal' }] });
      throw new Error('No more mock responses — stop retry chain');
    };
    sync = createCloudSync({ db, fetch: mockFetch, config: { maxRetries: 5, baseDelayMs: 1 } });

    insertRecord(db);

    await sync.pollOnce();

    const rows = allRows(db);
    assert.equal(rows.length, 1);
    assert.equal(rows[0].sync_status, 'PENDING', 'record must remain PENDING after 500');
    assert.equal(rows[0].retry_count, 1, 'retry_count must be 1 after one 500');
    assert.ok(rows[0].last_error, 'last_error must be set');
  });

  // -----------------------------------------------------------------------
  // Scenario: Exponential Backoff timing
  //   GIVEN cloud_sync finds a PENDING record
  //   WHEN first call returns 500, second returns 200
  //   THEN retry delay >= 1000ms (2^0 × baseDelayMs)
  //   AND record ends up SYNCED
  // -----------------------------------------------------------------------

  it('must apply exponential backoff delay before retry (baseDelayMs=1000)', async () => {
    let callCount = 0;
    const mockFetch = async () => {
      callCount++;
      if (callCount === 1) return mockResponse(500, {});
      return mockResponse(200, { data: { insert_telemetry: { returning: [{ id: 1 }] } } });
    };
    sync = createCloudSync({ db, fetch: mockFetch, config: { maxRetries: 5, baseDelayMs: 1000 } });

    insertRecord(db);

    const start = Date.now();
    await sync.pollOnce();
    const elapsed = Date.now() - start;

    // First retry delay = min(1000 * 2^0, 60000) = 1000ms
    assert.ok(elapsed >= 1000, `Expected delay >= 1000ms, got ${elapsed}ms`);

    const rows = allRows(db);
    assert.equal(rows[0].sync_status, 'SYNCED', 'record must be SYNCED after retry succeeds');
  });

  // -----------------------------------------------------------------------
  // Scenario: Max retries reached
  //   GIVEN cloud_sync finds a PENDING record
  //   WHEN Hasura returns 500 for all 5 attempts
  //   THEN record status = FAILED
  //   AND retry_count = 5
  // -----------------------------------------------------------------------

  it('must mark record as FAILED after exhausting maxRetries', async () => {
    const mockFetch = mockFetchSequence(
      mockResponse(500, {}),
      mockResponse(500, {}),
      mockResponse(500, {}),
      mockResponse(500, {}),
      mockResponse(500, {})
    );
    sync = createCloudSync({ db, fetch: mockFetch, config: { maxRetries: 5, baseDelayMs: 1 } });

    insertRecord(db);

    await sync.pollOnce();

    const rows = allRows(db);
    assert.equal(rows.length, 1);
    assert.equal(rows[0].sync_status, 'FAILED', 'status must be FAILED after 5 retries');
    assert.equal(rows[0].retry_count, 5, 'retry_count must be 5');
  });

  // -----------------------------------------------------------------------
  // Triangulation: Empty poll (no PENDING records)
  //   GIVEN there are no PENDING records in the buffer
  //   WHEN cloud_sync polls
  //   THEN it resolves gracefully without errors
  // -----------------------------------------------------------------------

  it('must handle empty poll gracefully with no PENDING records', async () => {
    const mockFetch = async () => {
      throw new Error('fetch must not be called when there are no records');
    };
    sync = createCloudSync({ db, fetch: mockFetch, config: { maxRetries: 1, baseDelayMs: 1 } });

    // No records inserted — buffer is empty
    const result = await sync.pollOnce();

    assert.deepEqual(result, [], 'empty poll must resolve to empty array');
  });

  // -----------------------------------------------------------------------
  // Scenario: Invalid JSON in DB record
  //   GIVEN a PENDING record has malformed JSON in the payload column
  //   WHEN cloud_sync processes it
  //   THEN it skips gracefully (no crash, no fetch called)
  //   AND record remains PENDING with last_error set
  // -----------------------------------------------------------------------

  it('must handle invalid JSON payload gracefully without calling fetch', async () => {
    const mockFetch = async () => {
      throw new Error('fetch must not be called for invalid JSON payload');
    };
    sync = createCloudSync({ db, fetch: mockFetch, config: { maxRetries: 1, baseDelayMs: 1 } });

    insertRecord(db, { payload: '{ this is not valid json }' });

    // Must not throw
    await sync.pollOnce();

    const rows = allRows(db);
    assert.equal(rows.length, 1);
    assert.equal(rows[0].sync_status, 'PENDING', 'must remain PENDING on invalid payload');
    assert.ok(rows[0].last_error, 'last_error must be set for invalid JSON');
  });
});
