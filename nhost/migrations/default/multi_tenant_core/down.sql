-- ============================================================================
-- Down Migration: multi_tenant_core
-- Reverses up.sql in reverse dependency order.
-- ============================================================================

-- 10. Revert alert_events column
ALTER TABLE alert_events DROP COLUMN IF EXISTS plant_id;
DROP INDEX IF EXISTS idx_alert_events_plant_id;

-- 9. Revert alert_rules column
ALTER TABLE alert_rules DROP COLUMN IF EXISTS plant_id;
DROP INDEX IF EXISTS idx_alert_rules_plant_id;

-- 8. user_plants
DROP INDEX IF EXISTS idx_user_plants_plant_id;
DROP INDEX IF EXISTS idx_user_plants_user_id;
DROP TABLE IF EXISTS user_plants;

-- 7. nodes
DROP INDEX IF EXISTS idx_nodes_machine_id;
DROP TABLE IF EXISTS nodes;

-- 6. model_capabilities
DROP TABLE IF EXISTS model_capabilities;

-- 5. alert_capabilities
DROP TABLE IF EXISTS alert_capabilities;

-- 4. device_models
DROP TABLE IF EXISTS device_models;

-- 3. machines
DROP INDEX IF EXISTS idx_machines_line_id;
DROP TABLE IF EXISTS machines;

-- 2. lines
DROP INDEX IF EXISTS idx_lines_plant_id;
DROP TABLE IF EXISTS lines;

-- 1. plants
DROP TABLE IF EXISTS plants;
