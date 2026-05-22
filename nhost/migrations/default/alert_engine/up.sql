-- ============================================================================
-- Migration: alert_engine
-- Description: Create alert engine tables for real-time ALARM detection
--              and multi-channel dispatch (Telegram, WhatsApp, Slack, Email,
--              Torreta, Push).
-- Tables: alert_channels, alert_rules, alert_engine_health
-- ============================================================================

-- ----------------------------------------------------------------------------
-- 1. alert_channels — channel configuration per type
--    Stores connection config and enabled state per notification channel.
--    config_json holds channel-specific settings (e.g. chat_id, webhook_url).
--    Secrets are stored in Nhost secrets, NOT in config_json.
-- ----------------------------------------------------------------------------
CREATE TABLE alert_channels (
    id           UUID         PRIMARY KEY DEFAULT gen_random_uuid(),
    channel_type TEXT         NOT NULL CHECK (channel_type IN (
                                  'telegram', 'whatsapp', 'slack',
                                  'email', 'torreta', 'push'
                              )),
    config_json  JSONB        NOT NULL,
    enabled      BOOLEAN      DEFAULT true,
    created_at   TIMESTAMPTZ  DEFAULT now(),
    updated_at   TIMESTAMPTZ  DEFAULT now()
);

-- ----------------------------------------------------------------------------
-- 2. alert_rules — rules per node/status → channel
--    Maps a node_id + status_filter to a specific alert channel.
--    status_filter defaults to 3 (ALARM).
--    Multiple rules can target the same channel (many-to-one).
-- ----------------------------------------------------------------------------
CREATE TABLE alert_rules (
    id            UUID         PRIMARY KEY DEFAULT gen_random_uuid(),
    node_id       TEXT         NOT NULL,
    status_filter INTEGER      DEFAULT 3,
    channel_id    UUID         NOT NULL REFERENCES alert_channels(id)
                               ON DELETE CASCADE,
    enabled       BOOLEAN      DEFAULT true,
    created_at    TIMESTAMPTZ  DEFAULT now(),
    updated_at    TIMESTAMPTZ  DEFAULT now()
);

-- Fast lookup by node_id (alert-dispatcher resolves rules on INSERT)
CREATE INDEX idx_alert_rules_node_id ON alert_rules (node_id);

-- ----------------------------------------------------------------------------
-- 3. alert_engine_health — uptime + latency tracking / dead-letter store
--    Written by alert-dispatcher on retry exhaustion and by alert-healthbeat
--    cron function on each 5-min cycle.
-- ----------------------------------------------------------------------------
CREATE TABLE alert_engine_health (
    check_id    UUID         PRIMARY KEY DEFAULT gen_random_uuid(),
    checked_at  TIMESTAMPTZ  NOT NULL,
    latency_ms  INTEGER      NOT NULL,
    success     BOOLEAN      NOT NULL,
    detail      TEXT
);

-- Index for time-range health queries
CREATE INDEX idx_alert_engine_health_checked_at
    ON alert_engine_health (checked_at);

-- ============================================================================
-- 4. ALTER TABLE alert_rules — add Dynamic Rule Engine columns (Fase 1)
-- ============================================================================

ALTER TABLE alert_rules
  ADD COLUMN IF NOT EXISTS scope           TEXT    NOT NULL DEFAULT 'SYSTEM'
    CHECK (scope IN ('SYSTEM', 'USER_DEFINED')),
  ADD COLUMN IF NOT EXISTS tipo_condicion   TEXT
    CHECK (tipo_condicion IN ('SILENCE_TIMEOUT', 'ERROR_THRESHOLD', 'FREQUENCY_DROP', 'DEFECT_THRESHOLD')),
  ADD COLUMN IF NOT EXISTS valor_umbral     INTEGER,
  ADD COLUMN IF NOT EXISTS canales          JSONB,
  ADD COLUMN IF NOT EXISTS cooldown_minutos INTEGER DEFAULT 30,
  ADD COLUMN IF NOT EXISTS last_alerted_at  TIMESTAMPTZ;

-- Existing SYSTEM rules get scope='SYSTEM' via the DEFAULT
-- New USER_DEFINED rules will set scope='USER_DEFINED', tipo_condicion, valor_umbral, canales

-- ============================================================================
-- 5. alert_events — event bridge table (decoupling detection from dispatch)
-- ============================================================================

CREATE TABLE IF NOT EXISTS alert_events (
    id              UUID         PRIMARY KEY DEFAULT gen_random_uuid(),
    rule_id         UUID         NOT NULL REFERENCES alert_rules(id)
                                 ON DELETE CASCADE,
    node_id         TEXT         NOT NULL,
    tipo_evento     TEXT         NOT NULL
                     CHECK (tipo_evento IN (
                         'SILENCE_TIMEOUT', 'ERROR_THRESHOLD',
                         'FREQUENCY_DROP', 'DEFECT_THRESHOLD'
                     )),
    mensaje         TEXT,
    detected_at     TIMESTAMPTZ  NOT NULL DEFAULT now(),
    dispatched      BOOLEAN      DEFAULT false,
    dispatch_result JSONB,
    created_at      TIMESTAMPTZ  DEFAULT now()
);

-- Index for efficient polling of undispatched events
CREATE INDEX IF NOT EXISTS idx_alert_events_dispatched
    ON alert_events (dispatched, created_at);
-- Index for rule-based lookups
CREATE INDEX IF NOT EXISTS idx_alert_events_rule_id
    ON alert_events (rule_id);
