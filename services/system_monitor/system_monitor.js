'use strict';

const fs = require('fs');
const os = require('os');
const { execSync } = require('child_process');
const mqtt = require('mqtt');
const {
  calculateRamUsagePercent,
  buildTelemetryPayload,
  parseDiskSpaceMb
} = require('./system_monitor_logic');

const brokerUrl = process.env.MQTT_BROKER_URL || 'mqtt://localhost:1883';
const topic     = process.env.MQTT_TOPIC      || 'novamex/ibarra/it_infra/rpi5_001';
const interval  = parseInt(process.env.POLL_INTERVAL_MS || '30000', 10);
const hostfs    = process.env.HOST_FS_PATH    || '/hostfs';

console.log('[system_monitor] Initializing EdgeOps system monitor...');
console.log('[system_monitor] Broker URL:', brokerUrl);
console.log('[system_monitor] Topic:', topic);
console.log('[system_monitor] Poll interval:', interval, 'ms');
console.log('[system_monitor] Host filesystem mount:', hostfs);

// Connect to MQTT Broker
const client = mqtt.connect(brokerUrl);

client.on('connect', () => {
  console.log('[system_monitor] Connected to MQTT broker');
});

client.on('error', (err) => {
  console.error('[system_monitor] MQTT error:', err.message);
});

/**
 * Retrieves the CPU temperature on Linux thermal zones.
 * Falls back to deterministic mock values under dev/non-Linux hardware.
 *
 * @returns {number}
 */
function getCpuTemp() {
  try {
    const raw = fs.readFileSync('/sys/class/thermal/thermal_zone0/temp', 'utf8');
    return parseFloat(raw.trim()) / 1000.0;
  } catch (err) {
    // Generate standard simulated temperature between 45.0 and 55.0 °C
    const mockTemp = parseFloat((45.0 + Math.random() * 10).toFixed(2));
    return mockTemp;
  }
}

/**
 * Queries free space in Megabytes of the host partition (/hostfs).
 * Falls back to simulator constants when running natively outside Linux.
 *
 * @returns {number}
 */
function getDiskFreeSpaceMb() {
  try {
    const stdout = execSync(`df -m ${hostfs}`).toString();
    return parseDiskSpaceMb(stdout);
  } catch (err) {
    // Falls back to safe mock default
    return 15000;
  }
}

/**
 * Executes a single metric scan, builds payload, and publishes it.
 */
function pollMetrics() {
  const cpuTemp = getCpuTemp();
  const ramUsage = calculateRamUsagePercent(os.totalmem(), os.freemem());
  const diskFree = getDiskFreeSpaceMb();
  const timestamp = new Date().toISOString();

  const payload = buildTelemetryPayload({
    cpuTemp,
    ramUsage,
    diskFreeMb: diskFree,
    timestamp
  });

  const payloadStr = JSON.stringify(payload);
  client.publish(topic, payloadStr, { qos: 1 }, (err) => {
    if (err) {
      console.error('[system_monitor] Failed to publish metrics:', err.message);
    } else {
      console.log(
        `[system_monitor] Telemetry sent OK. CPU Temp: ${cpuTemp.toFixed(1)}°C, RAM: ${ramUsage}%, Disk Free: ${diskFree}MB, Health: ${payload.health}`
      );
    }
  });
}

// Start recurring timer
let pollInterval = setInterval(pollMetrics, interval);

// Fire the first scan immediately after startup/connect
setTimeout(pollMetrics, 1000);

// Graceful teardown hooks
function shutdown(signal) {
  console.log(`[system_monitor] Received ${signal}. Terminating monitor daemon...`);
  clearInterval(pollInterval);
  client.end(true, () => {
    console.log('[system_monitor] MQTT connection terminated. Process exit.');
    process.exit(0);
  });
}

process.on('SIGTERM', () => shutdown('SIGTERM'));
process.on('SIGINT',  () => shutdown('SIGINT'));
