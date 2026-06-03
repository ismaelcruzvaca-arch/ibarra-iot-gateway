'use strict';

/**
 * Calculates the memory usage percentage based on total and free bytes.
 *
 * @param {number} totalMemory
 * @param {number} freeMemory
 * @returns {number}
 */
function calculateRamUsagePercent(totalMemory, freeMemory) {
  if (!totalMemory || totalMemory <= 0) return 0;
  const used = totalMemory - freeMemory;
  return Math.round((used / totalMemory) * 100);
}

/**
 * Checks CPU temp and RAM limits to evaluate overall health status.
 * WARNING triggers if cpu temp > 75°C OR RAM usage > 90%.
 *
 * @param {number} cpuTemp
 * @param {number} ramUsage
 * @returns {string}
 */
function evaluateHealthState(cpuTemp, ramUsage) {
  if (cpuTemp > 75.0 || ramUsage > 90) {
    return 'WARNING';
  }
  return 'ONLINE';
}

/**
 * Builds the structured Universal payload for telemetry.
 * Matches Sparkplug B validation requirements of edge_worker by supplying node_health.
 *
 * @param {object} params
 * @param {number} params.cpuTemp
 * @param {number} params.ramUsage
 * @param {number} params.diskFreeMb
 * @param {string} params.timestamp
 * @param {string} [params.deviceId] - Device identifier (default: 'rpi_gateway_01')
 * @returns {object}
 */
function buildTelemetryPayload({ cpuTemp, ramUsage, diskFreeMb, timestamp, deviceId }) {
  const health = evaluateHealthState(cpuTemp, ramUsage);
  return {
    device_id: deviceId || 'rpi_gateway_01',
    device_type: 'gateway',
    timestamp,
    node_health: health,
    metrics: [
      { name: 'cpu_temp_celsius', value: cpuTemp, unit: '°C' },
      { name: 'ram_usage_percent', value: ramUsage },
      { name: 'disk_free_mb', value: diskFreeMb, unit: 'MB' }
    ]
  };
}

/**
 * Parses stdout from command df to extract free disk space in megabytes.
 *
 * @param {string} dfOutput
 * @returns {number}
 */
function parseDiskSpaceMb(dfOutput) {
  if (!dfOutput) return 15000;
  const lines = dfOutput.trim().split('\n');
  if (lines.length <= 1) return 15000;

  // Search for the data row (usually the second line)
  const columnsLine = lines[1].replace(/\s+/g, ' ').trim();
  const cols = columnsLine.split(' ');
  if (cols.length < 4) return 15000;

  // Standard df -m columns: Filesystem, 1M-blocks, Used, Available
  const freeMb = parseInt(cols[3], 10);
  return isNaN(freeMb) ? 15000 : freeMb;
}

module.exports = {
  calculateRamUsagePercent,
  evaluateHealthState,
  buildTelemetryPayload,
  parseDiskSpaceMb
};
