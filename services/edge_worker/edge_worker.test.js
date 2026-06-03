'use strict';

const { describe, it, before, beforeEach, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const initSqlJs = require('sql.js');
const { createEdgeWorker } = require('./edge_worker');

// ---------------------------------------------------------------------------
// sql.js → better-sqlite3 API adapter
//
// better-sqlite3: db.pragma(), db.prepare().run({ named }), db.exec()
// sql.js:         db.run(),       db.prepare().run([positional])
//
// We pass the adapter as the `db` parameter to createEdgeWorker; the
// production better-sqlite3 code uses pragma() and prepare().run({...})
// transparently.
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
      // Extract named param keys in order: @topic → ?, @payload → ?, etc.
      const namedKeys = [];
      const positionalSql = sql.replace(/@(\w+)/g, (_, key) => {
        namedKeys.push(key);
        return '?';
      });
      const stmt = rawDb.prepare(positionalSql);
      return {
        run: (params) => {
          const arr = namedKeys.map((k) => params[k]);
          stmt.run(arr);
          stmt.reset();
        },
      };
    },
    close: () => rawDb.close(),
    /** Expose raw sql.js db for query helpers. */
    _raw: rawDb,
  };
}

// ---------------------------------------------------------------------------
// Query helpers (use raw sql.js API for result access)
// ---------------------------------------------------------------------------

function rowCount(adapter) {
  const res = adapter._raw.exec('SELECT COUNT(*) AS cnt FROM telemetry_buffer');
  return res.length ? res[0].values[0][0] : 0;
}

function firstRow(adapter) {
  const res = adapter._raw.exec('SELECT * FROM telemetry_buffer LIMIT 1');
  if (!res.length || !res[0].values.length) return null;
  const { columns, values } = res[0];
  const row = {};
  columns.forEach((col, i) => { row[col.toLowerCase()] = values[0][i]; });
  return row;
}

function allRows(adapter) {
  const res = adapter._raw.exec('SELECT * FROM telemetry_buffer ORDER BY id');
  if (!res.length) return [];
  const { columns, values } = res[0];
  return values.map((v) => {
    const obj = {};
    columns.forEach((col, i) => { obj[col.toLowerCase()] = v[i]; });
    return obj;
  });
}

// ---------------------------------------------------------------------------
// Mock MQTT client
// ---------------------------------------------------------------------------

function makeMockMqttClient() {
  const handlers = {};
  return {
    on: (event, handler) => { handlers[event] = handler; },
    subscribe: (topic, cb) => { if (cb) cb(null); },
    end: () => {},
    _simulateMessage(topic, payload) {
      if (handlers.message) {
        handlers.message(topic, Buffer.from(payload));
      }
    },
  };
}

// ---------------------------------------------------------------------------
// Suite
// ---------------------------------------------------------------------------

describe('Edge Worker — MQTT Ingestion', () => {
  let SQL;
  let mockMqtt;
  let db;
  let worker;

  before(async () => {
    SQL = await initSqlJs();
  });

  beforeEach(() => {
    const rawDb = new SQL.Database();
    mockMqtt = makeMockMqttClient();
    db = makeBetterLikeDb(rawDb);
    worker = createEdgeWorker({ mqttClient: mockMqtt, db });
    worker.start();
  });

  afterEach(() => {
    worker.stop();
    db.close();
  });

  // -----------------------------------------------------------------------
  // Scenario 1 — Noise injection: malformed JSON
  // -----------------------------------------------------------------------

  it('must discard malformed JSON without crashing', () => {
    mockMqtt._simulateMessage('novamex/ibarra/area1/machine1/temp',
      'this is not json');
    assert.equal(rowCount(db), 0);
  });

  it('must discard binary/non-UTF8 payload without crashing', () => {
    mockMqtt._simulateMessage('novamex/ibarra/area1/machine1/raw',
      Buffer.from([0xff, 0xfe, 0x00, 0x01]));
    assert.equal(rowCount(db), 0);
  });

  // -----------------------------------------------------------------------
  // Scenario 1 — Noise injection: missing node_health
  // -----------------------------------------------------------------------

  it('must discard valid JSON that lacks node_health', () => {
    mockMqtt._simulateMessage('novamex/ibarra/area1/machine1/temp',
      JSON.stringify({ metrics: [{ name: 'temperature', value: 82.5 }] }));
    assert.equal(rowCount(db), 0);
  });

  it('must discard when node_health is null', () => {
    mockMqtt._simulateMessage('novamex/ibarra/area1/machine1/temp',
      JSON.stringify({ node_health: null, metrics: [] }));
    assert.equal(rowCount(db), 0);
  });

  // -----------------------------------------------------------------------
  // Valid payloads
  // -----------------------------------------------------------------------

  it('must insert valid message with node_health ONLINE and sync_status PENDING', () => {
    mockMqtt._simulateMessage('novamex/ibarra/area2/machine4/temp', JSON.stringify({
      node_health: 'ONLINE',
      metrics: [{ name: 'temperature', value: 85.3, timestamp: new Date().toISOString(), unit: '°C' }],
    }));

    const row = firstRow(db);
    assert.ok(row, 'Row inserted');
    assert.equal(row.sync_status, 'PENDING');
    assert.equal(row.node_health, 'ONLINE');
    assert.equal(row.topic, 'novamex/ibarra/area2/machine4/temp');
    assert.equal(row.retry_count, 0);
  });

  it('must insert valid message with empty metrics array', () => {
    mockMqtt._simulateMessage('novamex/ibarra/area1/machine1/vibration', JSON.stringify({
      node_health: 'ONLINE',
      metrics: [],
    }));

    const row = firstRow(db);
    assert.ok(row);
    assert.equal(row.sync_status, 'PENDING');
    assert.equal(row.node_health, 'ONLINE');
  });

  it('must insert multiple valid messages in sequence', () => {
    for (let i = 1; i <= 3; i++) {
      mockMqtt._simulateMessage(`novamex/ibarra/area1/machine${i}/sensor`, JSON.stringify({
        node_health: 'ONLINE',
        metrics: [{ name: `sensor_${i}`, value: i * 10, timestamp: new Date().toISOString() }],
      }));
    }

    const rows = allRows(db);
    assert.equal(rows.length, 3);
    assert.equal(rows[0].topic, 'novamex/ibarra/area1/machine1/sensor');
    assert.equal(rows[1].topic, 'novamex/ibarra/area1/machine2/sensor');
    assert.equal(rows[2].topic, 'novamex/ibarra/area1/machine3/sensor');
    rows.forEach((r) => assert.equal(r.sync_status, 'PENDING'));
  });

  // -----------------------------------------------------------------------
  // Scenario 3 — LWT / NDEATH
  // -----------------------------------------------------------------------

  it('must register NDEATH event with node_health OFFLINE when LWT arrives', () => {
    mockMqtt._simulateMessage('novamex/ibarra/area3/machine7/node1', JSON.stringify({
      node_health: 'OFFLINE',
      metrics: [],
    }));

    const row = firstRow(db);
    assert.ok(row);
    assert.equal(row.node_health, 'OFFLINE');
    assert.equal(row.sync_status, 'PENDING');
  });

  it('must register NDEATH with reason metric', () => {
    mockMqtt._simulateMessage('novamex/ibarra/area2/machine5/node2', JSON.stringify({
      node_health: 'OFFLINE',
      metrics: [{ name: 'reason', value: 1, timestamp: new Date().toISOString() }],
    }));

    const row = firstRow(db);
    assert.ok(row);
    assert.equal(row.node_health, 'OFFLINE');
    const parsed = JSON.parse(row.payload);
    assert.equal(parsed.node_health, 'OFFLINE');
    assert.equal(parsed.metrics[0].name, 'reason');
  });
});
