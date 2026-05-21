-- ============================================================================
-- Down Migration: init_time_series
-- Description: Reverses up.sql — drops indexes, partitions, and tables
--              in reverse dependency order.
-- ============================================================================

-- 1. hardware_health
DROP INDEX IF EXISTS idx_hardware_health_brin;
DROP TABLE IF EXISTS hardware_health;

-- 2. vision_telemetry (partitions first, then master)
DROP INDEX IF EXISTS idx_vision_telemetry_brin;
DROP TABLE IF EXISTS vision_telemetry_2026_08;
DROP TABLE IF EXISTS vision_telemetry_2026_07;
DROP TABLE IF EXISTS vision_telemetry_2026_06;
DROP TABLE IF EXISTS vision_telemetry;

-- 3. norvi_telemetry (partitions first, then master)
DROP INDEX IF EXISTS idx_norvi_telemetry_brin;
DROP TABLE IF EXISTS norvi_telemetry_2026_08;
DROP TABLE IF EXISTS norvi_telemetry_2026_07;
DROP TABLE IF EXISTS norvi_telemetry_2026_06;
DROP TABLE IF EXISTS norvi_telemetry;
