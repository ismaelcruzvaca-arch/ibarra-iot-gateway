'use strict';

// ---------------------------------------------------------------------------
// GEMA V3.0 Edge Worker — MQTT to SQLite Ingestion
//
// Subscribes to a configurable MQTT topic prefix, validates incoming JSON
// payloads (must be parseable AND contain a truthy `node_health` field), and
// inserts valid records into the telemetry_buffer table with
// sync_status = 'PENDING'.
//
// Architecture rules enforced:
//   #2  — SQLite WAL mode on every connection
//   #4  — Sparkplug B Lite: every message MUST include node_health
// ---------------------------------------------------------------------------

/**
 * Creates an edge worker instance.
 *
 * Dependency injection friendly — pass mocks for { mqttClient, db } in tests,
 * or real instances in production.
 *
 * @param {object} deps
 * @param {object} deps.mqttClient    MQTT client with .on(), .subscribe(), .end()
 * @param {import('better-sqlite3').Database} deps.db  SQLite connection
 * @param {object} [deps.config]
 * @param {string} [deps.config.topicPrefix='novamex/#']
 * @param {function} [deps.config.log=console.log]
 * @param {function} [deps.config.logWarn=console.warn]
 * @returns {{ start: function, stop: function }}
 */
function createEdgeWorker({ mqttClient, db, config = {} }) {
  const topicPrefix = config.topicPrefix || 'novamex/#';
  const log     = config.log     || console.log.bind(console, '[edge_worker]');
  const logWarn = config.logWarn || console.warn.bind(console, '[edge_worker]');

  // -----------------------------------------------------------------------
  // Validation pipeline  (spec: Edge Processing — MQTT to SQLite)
  // -----------------------------------------------------------------------

  /**
   * Validates a raw MQTT message payload.
   *
   * @param {string} topic  MQTT topic the message arrived on
   * @param {Buffer} raw    Raw payload buffer
   * @returns {object|null}  Normalised record { topic, payload, node_health }
   *                          or null if the message should be discarded.
   */
  function validate(topic, raw) {
    let parsed;
    try {
      parsed = JSON.parse(raw.toString());
    } catch {
      logWarn('Discarding malformed JSON on topic', topic);
      return null;
    }

    if (!parsed.node_health) {
      logWarn('Discarding message without node_health on topic', topic);
      return null;
    }

    return {
      topic,
      payload: JSON.stringify(parsed),
      node_health: parsed.node_health,
    };
  }

  // -----------------------------------------------------------------------
  // Persistence
  // -----------------------------------------------------------------------

  /** Prepared INSERT statement — created once for performance. */
  let insertStmt = null;

  function ensureInsertStmt() {
    if (!insertStmt) {
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

      insertStmt = db.prepare(`
        INSERT INTO telemetry_buffer (topic, payload, node_health)
        VALUES (@topic, @payload, @node_health)
      `);
    }
  }

  // -----------------------------------------------------------------------
  // MQTT message handler
  // -----------------------------------------------------------------------

  function handleMessage(topic, rawPayload) {
    const record = validate(topic, rawPayload);
    if (!record) return;

    try {
      ensureInsertStmt();
      insertStmt.run(record);
      log('Inserted record from topic', topic, 'node_health:', record.node_health);
    } catch (err) {
      // Must NEVER crash the process — log and let the message be lost
      // (the MQTT broker will have the message; on reconnect we re-subscribe
      //  but QoS 0 means no guaranteed delivery — acceptable for telemetry).
      logWarn('Failed to insert record:', err.message);
    }
  }

  // -----------------------------------------------------------------------
  // Lifecycle
  // -----------------------------------------------------------------------

  function start() {
    ensureInsertStmt();
    mqttClient.on('message', handleMessage);
    mqttClient.subscribe(topicPrefix, (err) => {
      if (err) {
        logWarn('Failed to subscribe to', topicPrefix, err.message);
      } else {
        log('Subscribed to', topicPrefix);
      }
    });
  }

  function stop() {
    mqttClient.end();
  }

  return { start, stop };
}

// ---------------------------------------------------------------------------
// Standalone entry point (when run via `npm start` or `node edge_worker.js`)
// ---------------------------------------------------------------------------

if (require.main === module) {
  const mqtt = require('mqtt');
  const Database = require('better-sqlite3');

  const brokerUrl = process.env.MQTT_BROKER_URL || 'mqtt://localhost:1883';
  const dbPath    = process.env.SQLITE_DB_PATH   || '/data/telemetry_buffer.db';

  const client = mqtt.connect(brokerUrl);
  const db     = new Database(dbPath);

  const worker = createEdgeWorker({ mqttClient: client, db });
  worker.start();

  // Graceful shutdown on SIGTERM / SIGINT (Docker stop, Ctrl+C)
  function shutdown() {
    log('Shutting down...');
    worker.stop();
    db.close();
    process.exit(0);
  }
  process.on('SIGTERM', shutdown);
  process.on('SIGINT',  shutdown);
}

module.exports = { createEdgeWorker };
