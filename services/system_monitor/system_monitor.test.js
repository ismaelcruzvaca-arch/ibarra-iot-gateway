'use strict';

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const {
  calculateRamUsagePercent,
  evaluateHealthState,
  buildTelemetryPayload,
  parseDiskSpaceMb
} = require('./system_monitor_logic');

describe('System Monitor — Business Logic & Rules', () => {

  describe('calculateRamUsagePercent', () => {
    it('should correctly compute memory usage percentage', () => {
      const total = 16000000;
      const free = 4000000;
      // 12000000 used / 16000000 total = 75%
      assert.equal(calculateRamUsagePercent(total, free), 75);
    });

    it('should handle zero total memory gracefully without crashing', () => {
      assert.equal(calculateRamUsagePercent(0, 0), 0);
    });

    it('should round memory usage percentage correctly', () => {
      const total = 3000;
      const free = 1999; // 1001 used = 33.3666%
      assert.equal(calculateRamUsagePercent(total, free), 33);
    });
  });

  describe('evaluateHealthState', () => {
    it('should return ONLINE when CPU and RAM are under limits', () => {
      assert.equal(evaluateHealthState(45.0, 60), 'ONLINE');
      assert.equal(evaluateHealthState(75.0, 90), 'ONLINE');
    });

    it('should return WARNING if CPU temperature exceeds 75 degrees', () => {
      assert.equal(evaluateHealthState(75.1, 50), 'WARNING');
      assert.equal(evaluateHealthState(82.0, 80), 'WARNING');
    });

    it('should return WARNING if RAM usage percentage exceeds 90 percent', () => {
      assert.equal(evaluateHealthState(50.0, 91), 'WARNING');
      assert.equal(evaluateHealthState(60.0, 99), 'WARNING');
    });

    it('should return WARNING if both thresholds are exceeded', () => {
      assert.equal(evaluateHealthState(79.0, 95), 'WARNING');
    });
  });

  describe('buildTelemetryPayload', () => {
    it('should format a valid universal telemetry payload structure', () => {
      const payload = buildTelemetryPayload({
        cpuTemp: 52.5,
        ramUsage: 70,
        diskFreeMb: 12500,
        timestamp: '2026-05-19T00:00:00.000Z'
      });

      assert.deepEqual(payload, {
        device_id: 'rpi_gateway_01',
        device_type: 'gateway',
        timestamp: '2026-05-19T00:00:00.000Z',
        node_health: 'ONLINE',
        metrics: [
          { name: 'cpu_temp_celsius', value: 52.5, unit: '°C' },
          { name: 'ram_usage_percent', value: 70 },
          { name: 'disk_free_mb', value: 12500, unit: 'MB' }
        ]
      });
    });

    it('should toggle warning when evaluated as unhealthy', () => {
      const payload = buildTelemetryPayload({
        cpuTemp: 80.0,
        ramUsage: 45,
        diskFreeMb: 12500,
        timestamp: '2026-05-19T00:00:00.000Z'
      });

      assert.equal(payload.node_health, 'WARNING');
    });

    it('should use fallback device_id when none provided', () => {
      const payload = buildTelemetryPayload({
        cpuTemp: 50.0,
        ramUsage: 50,
        diskFreeMb: 10000,
        timestamp: '2026-06-03T00:00:00.000Z'
      });
      assert.equal(payload.device_id, 'rpi_gateway_01');
    });

    it('should accept custom device_id from caller', () => {
      const payload = buildTelemetryPayload({
        cpuTemp: 50.0,
        ramUsage: 50,
        diskFreeMb: 10000,
        timestamp: '2026-06-03T00:00:00.000Z',
        deviceId: 'custom-gateway-02'
      });
      assert.equal(payload.device_id, 'custom-gateway-02');
    });
  });

  describe('parseDiskSpaceMb', () => {
    it('should extract correct available MB column from df system call output', () => {
      const sampleOutput = `
Filesystem           1M-blocks      Used Available Use% Mounted on
/dev/root                59000     45000     14000  76% /hostfs
      `;
      assert.equal(parseDiskSpaceMb(sampleOutput), 14000);
    });

    it('should fallback to 15000 if input output string is empty or invalid', () => {
      assert.equal(parseDiskSpaceMb(''), 15000);
      assert.equal(parseDiskSpaceMb('Invalid command output'), 15000);
    });
  });

});
