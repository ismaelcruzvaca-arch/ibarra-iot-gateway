# Spec: Nhost Time-Series Telemetry Tables

**Status**: Archived (2026-05-21)
**Project**: ibarra-iot-gateway

## Tables

### norvi_telemetry
- Partitioned by RANGE (event_ts)
- PK: (event_ts, node_id)
- BRIN index on event_ts (autosummarize = on)
- Monthly partitions: norvi_telemetry_2026_06 through _08

### vision_telemetry
- Partitioned by RANGE (event_ts)
- PK: (event_ts, camera_id)
- BRIN index on event_ts (autosummarize = on)
- Monthly partitions: vision_telemetry_2026_06 through _08

### hardware_health
- Append-only, no partitioning
- PK: (event_ts, device_id, device_type)
- BRIN index on event_ts (autosummarize = on)

## Columns

| Table | Columns |
|-------|---------|
| norvi_telemetry | node_id TEXT, event_ts TIMESTAMPTZ, payload JSONB |
| vision_telemetry | camera_id TEXT, event_ts TIMESTAMPTZ, frame_ref TEXT, metadata JSONB |
| hardware_health | device_id TEXT, device_type TEXT, event_ts TIMESTAMPTZ, metrics JSONB |

## Files
- `nhost/migrations/default/init_time_series/up.sql`
- `nhost/migrations/default/init_time_series/down.sql`
