-- ============================================================================
-- Down Migration: alert_engine
-- Description: Reverses up.sql — drops indexes and tables
--              in reverse dependency order.
-- ============================================================================

-- 1. alert_engine_health (no FK dependencies)
DROP TABLE IF EXISTS alert_engine_health;

-- 2. alert_rules (FK on alert_channels.id, cascade handles channel cleanup)
DROP INDEX IF EXISTS idx_alert_rules_node_id;
DROP TABLE IF EXISTS alert_rules;

-- 3. alert_channels (parent table)
DROP TABLE IF EXISTS alert_channels;

-- 4. alert_events (new table)
DROP INDEX IF EXISTS idx_alert_events_dispatched;
DROP INDEX IF EXISTS idx_alert_events_rule_id;
DROP TABLE IF EXISTS alert_events;

-- 5. Revert alert_rules ALTER TABLE (drop added columns)
ALTER TABLE alert_rules
  DROP COLUMN IF EXISTS last_alerted_at,
  DROP COLUMN IF EXISTS cooldown_minutos,
  DROP COLUMN IF EXISTS canales,
  DROP COLUMN IF EXISTS valor_umbral,
  DROP COLUMN IF EXISTS tipo_condicion,
  DROP COLUMN IF EXISTS scope;
