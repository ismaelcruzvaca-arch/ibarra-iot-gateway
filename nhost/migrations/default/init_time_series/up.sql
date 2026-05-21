-- ============================================================================
-- Migration: init_time_series
-- Description: Create time-series telemetry tables with range partitioning
--              and BRIN indexes for the ibarra-iot-gateway.
-- Partitions: 3-month lookahead (Jun, Jul, Aug 2026)
-- ============================================================================

-- ----------------------------------------------------------------------------
-- 1. norvi_telemetry — partitioned by event_ts
--    Stores sensor readings from Norvi industrial devices (Modbus RTU).
-- ----------------------------------------------------------------------------
CREATE TABLE norvi_telemetry (
    node_id   TEXT        NOT NULL,
    event_ts  TIMESTAMPTZ NOT NULL,
    payload   JSONB,
    PRIMARY KEY (event_ts, node_id)
) PARTITION BY RANGE (event_ts);

-- Pre-partitions: Jun, Jul, Aug 2026
CREATE TABLE norvi_telemetry_2026_06 PARTITION OF norvi_telemetry
    FOR VALUES FROM ('2026-06-01') TO ('2026-07-01');

CREATE TABLE norvi_telemetry_2026_07 PARTITION OF norvi_telemetry
    FOR VALUES FROM ('2026-07-01') TO ('2026-08-01');

CREATE TABLE norvi_telemetry_2026_08 PARTITION OF norvi_telemetry
    FOR VALUES FROM ('2026-08-01') TO ('2026-09-01');

-- BRIN index for time-range scans (autosummarize keeps it current)
CREATE INDEX idx_norvi_telemetry_brin
    ON norvi_telemetry
    USING BRIN (event_ts)
    WITH (autosummarize = on);


-- ----------------------------------------------------------------------------
-- 2. vision_telemetry — partitioned by event_ts
--    Stores camera/vision events (frame references, ML metadata).
-- ----------------------------------------------------------------------------
CREATE TABLE vision_telemetry (
    camera_id TEXT        NOT NULL,
    event_ts  TIMESTAMPTZ NOT NULL,
    frame_ref TEXT,
    metadata  JSONB,
    PRIMARY KEY (event_ts, camera_id)
) PARTITION BY RANGE (event_ts);

-- Pre-partitions: Jun, Jul, Aug 2026
CREATE TABLE vision_telemetry_2026_06 PARTITION OF vision_telemetry
    FOR VALUES FROM ('2026-06-01') TO ('2026-07-01');

CREATE TABLE vision_telemetry_2026_07 PARTITION OF vision_telemetry
    FOR VALUES FROM ('2026-07-01') TO ('2026-08-01');

CREATE TABLE vision_telemetry_2026_08 PARTITION OF vision_telemetry
    FOR VALUES FROM ('2026-08-01') TO ('2026-09-01');

-- BRIN index for time-range scans
CREATE INDEX idx_vision_telemetry_brin
    ON vision_telemetry
    USING BRIN (event_ts)
    WITH (autosummarize = on);


-- ----------------------------------------------------------------------------
-- 3. hardware_health — append-only table
--    Stores device health metrics (temperature, voltage, uptime, etc.).
--    Lower volume than telemetry — not partitioned.
-- ----------------------------------------------------------------------------
CREATE TABLE hardware_health (
    device_id   TEXT        NOT NULL,
    device_type TEXT        NOT NULL,
    event_ts    TIMESTAMPTZ NOT NULL,
    metrics     JSONB,
    PRIMARY KEY (event_ts, device_id, device_type)
) WITH (fillfactor = 100);

-- BRIN index for time-range scans (append-only pattern)
CREATE INDEX idx_hardware_health_brin
    ON hardware_health
    USING BRIN (event_ts)
    WITH (autosummarize = on);
