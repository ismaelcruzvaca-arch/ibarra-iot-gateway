-- ============================================================================
-- Down Migration: alert_rules_window
-- ============================================================================

DROP INDEX IF EXISTS idx_alert_rules_ventana;

ALTER TABLE alert_rules
  DROP COLUMN IF EXISTS ventana_minutos;
