-- ============================================================================
-- Migration: alert_rules_window
-- Description: Add ventana_minutos column for ERROR_THRESHOLD rules.
-- Defines the time window (in minutes) for counting error events.
-- ============================================================================

ALTER TABLE alert_rules
  ADD COLUMN IF NOT EXISTS ventana_minutos INTEGER DEFAULT 5;

COMMENT ON COLUMN alert_rules.ventana_minutos IS
  'Time window in minutes for ERROR_THRESHOLD rules. Error count is evaluated within this window. Default 5 min.';

-- Index for time-window queries
CREATE INDEX IF NOT EXISTS idx_alert_rules_ventana
    ON alert_rules (ventana_minutos);
