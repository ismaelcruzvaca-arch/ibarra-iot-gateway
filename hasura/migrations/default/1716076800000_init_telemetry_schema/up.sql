CREATE SCHEMA IF NOT EXISTS telemetry;

-- Enable UUID extension in case it's not active
CREATE EXTENSION IF NOT EXISTS "uuid-ossp";

CREATE TABLE telemetry.telemetry_events (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    enterprise TEXT NOT NULL,
    site TEXT NOT NULL,
    area TEXT NOT NULL,
    device_id TEXT NOT NULL,
    timestamp TIMESTAMPTZ NOT NULL DEFAULT now(),
    health TEXT NOT NULL,
    metrics JSONB NOT NULL
);

-- B-Tree indices for search filtering and RLS security path
CREATE INDEX idx_telemetry_events_site ON telemetry.telemetry_events (site);
CREATE INDEX idx_telemetry_events_timestamp ON telemetry.telemetry_events (timestamp DESC);

-- GIN index for deep JSONB metrics querying
CREATE INDEX idx_telemetry_events_metrics_gin ON telemetry.telemetry_events USING GIN (metrics);
