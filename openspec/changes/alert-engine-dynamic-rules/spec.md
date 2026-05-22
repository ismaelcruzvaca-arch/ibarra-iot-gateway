# Delta Spec: Alert Engine — Dynamic Rule Engine (Fase 1)

## Purpose
Extend the Alert Engine to support USER_DEFINED rules that trigger on conditions other than norvi status=3. Phase 1 introduces SILENCE_TIMEOUT detection: when a device stops sending telemetry for a configurable period, an alert event is generated and dispatched through configured channels.

## Architecture Decision

**Event-driven decoupling**: A new `silence-detector` Nhost Function (cron, 1 min) detects silence violations and writes to `alert_events`. The existing `alert-dispatcher` reads from `alert_events` via a new Hasura Event Trigger and dispatches using the same adapter registry. This avoids code duplication and keeps detection separate from dispatch.

## New Requirements

| ID | Statement | Strength |
|----|-----------|----------|
| FR-7 | `alert_rules` SHALL support a `scope` column with values 'SYSTEM' (status-based) or 'USER_DEFINED' (dynamic conditions) | MUST |
| FR-8 | USER_DEFINED rules SHALL support `tipo_condicion` (SILENCE_TIMEOUT, ERROR_THRESHOLD, FREQUENCY_DROP, DEFECT_THRESHOLD), `valor_umbral` (threshold value), `canales` (JSONB array of channel types), `cooldown_minutos` (re-alert cooldown), and `last_alerted_at` | MUST |
| FR-9 | `alert_events` SHALL be created as an event bridge table: `rule_id` (FK → alert_rules), `node_id`, `tipo_evento`, `mensaje`, `detected_at`, `dispatched`, `dispatch_result` | MUST |
| FR-10 | `silence-detector` SHALL run every 1 minute via Nhost cron, evaluate USER_DEFINED SILENCE_TIMEOUT rules, and INSERT into `alert_events` when a machine exceeds its silence threshold | MUST |
| FR-11 | `alert-dispatcher` SHALL bifurcate on `payload.table.name`: norvi_telemetry → SYSTEM flow, alert_events → USER_DEFINED flow | MUST |
| FR-12 | For USER_DEFINED events, `alert-dispatcher` SHALL read `rule.canales`, query `alert_channels` by channel type, and dispatch via the existing adapter registry | SHALL |
| FR-13 | A cooldown SHALL prevent re-alerting if `now() - last_alerted_at < cooldown_minutos * 60s` | MUST |
| FR-14 | The silence-detector SHALL write evaluation health data to `alert_engine_health` | SHALL |

## Modified Tables

### Table: `alert_rules` (MODIFIED)

| Column | Type | Constraints |
|--------|------|-------------|
| id | UUID | PK, DEFAULT gen_random_uuid() |
| node_id | TEXT | NOT NULL |
| status_filter | INTEGER | DEFAULT 3 |
| channel_id | UUID | FK → alert_channels(id) ON DELETE CASCADE |
| enabled | BOOLEAN | DEFAULT true |
| scope | TEXT | NOT NULL DEFAULT 'SYSTEM', CHECK IN (SYSTEM, USER_DEFINED) |
| tipo_condicion | TEXT | CHECK IN (SILENCE_TIMEOUT, ERROR_THRESHOLD, FREQUENCY_DROP, DEFECT_THRESHOLD) |
| valor_umbral | INTEGER | |
| canales | JSONB | |
| cooldown_minutos | INTEGER | DEFAULT 30 |
| last_alerted_at | TIMESTAMPTZ | |
| created_at | TIMESTAMPTZ | DEFAULT now() |
| updated_at | TIMESTAMPTZ | DEFAULT now() |

### Table: `alert_events` (NEW)

| Column | Type | Constraints |
|--------|------|-------------|
| id | UUID | PK, DEFAULT gen_random_uuid() |
| rule_id | UUID | NOT NULL, FK → alert_rules(id) ON DELETE CASCADE |
| node_id | TEXT | NOT NULL |
| tipo_evento | TEXT | NOT NULL, CHECK IN (SILENCE_TIMEOUT, ERROR_THRESHOLD, FREQUENCY_DROP, DEFECT_THRESHOLD) |
| mensaje | TEXT | |
| detected_at | TIMESTAMPTZ | NOT NULL DEFAULT now() |
| dispatched | BOOLEAN | DEFAULT false |
| dispatch_result | JSONB | |
| created_at | TIMESTAMPTZ | DEFAULT now() |

## New Scenarios

| Scenario | GIVEN | WHEN | THEN |
|----------|-------|------|------|
| Happy: SILENCE_TIMEOUT detected | Machine stops sending telemetry for >valor_umbral seconds | silence-detector evaluates | alert_event inserted with tipo_evento='SILENCE_TIMEOUT' |
| Cooldown: Within window | Machine still silent, but last_alerted_at < cooldown_minutos ago | silence-detector evaluates | No new alert_event (cooldown active) |
| Cooldown: Expired | Machine still silent, cooldown elapsed | silence-detector evaluates | New alert_event inserted |
| No telemetry yet | Machine never sent data | silence-detector evaluates | Rule skipped (no data) |
| Dispatch: alert_events flow | alert_event INSERT into alert_events | Hasura trigger fires | alert-dispatcher reads rule.canales, dispatches to each channel type |
| No channels | rule.canales is empty or no enabled alert_channels found | alert-dispatcher evaluates | Clean 200, dispatched:false |
