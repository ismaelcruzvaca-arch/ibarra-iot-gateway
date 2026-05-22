# Design: Alert Engine — Dynamic Rule Engine (Fase 1)

## Technical Approach
Event-driven decoupling: silence-detector (cron, 1min) detects USER_DEFINED SILENCE_TIMEOUT violations and writes to `alert_events`. The existing alert-dispatcher handles dispatch via a new Event Trigger on `alert_events`. No code duplication — adapters stay in `alert-dispatcher/adapters/`.

## Architecture Decisions

### AD-5: Event-Driven Decoupling
| Option | Tradeoff | Decision |
|--------|----------|----------|
| silence-detector sends messages directly | Tight coupling, duplicate adapter logic in two functions | ❌ |
| **Event bridge via alert_events** | Decoupled: detector writes events, dispatcher handles dispatch. Single adapter registry. | ✅ |
**Rationale**: The silence-detector should NOT know about channels, adapters, or dispatch. It only detects silence and records events. The existing alert-dispatcher (which already has the adapter registry) handles dispatch for both SYSTEM and USER_DEFINED sources via bifurcation on `payload.table.name`.

### AD-6: Bifurcation over Duplication
Extending alert-dispatcher/index.ts to check `payload.table.name` and branch is simpler than creating a separate dispatch function. Zero new adapters, zero new dispatch logic.
**Rationale**: The EVENT_DRIVEN decoupling means the dispatcher already has everything it needs. The bifurcation is a single if/else that routes to the correct rule-resolution path.

### AD-7: Injected Functions for Testability
SilenceDetectorEngine.evaluate() accepts 5 injected functions (same pattern as DispatcherEngine).
**Rationale**: Consistent with existing codebase pattern. Enables unit testing without a live database.

## Component Design

### silence-detector (Nhost Function)
```
nhost/functions/silence-detector/
├── index.ts    ← cron handler (1 min), wires default GraphQL implementations
└── engine.ts   ← SilenceDetectorEngine: evaluate() with DI
```

**Flow**: Cron triggers every 1 min → `index.ts` calls `SilenceDetectorEngine.evaluate()` with production queries → For each rule:

1. Query `SELECT MAX(event_ts) FROM norvi_telemetry WHERE node_id = rule.node_id`
2. Calculate elapsed seconds: `now() - max_event_ts`
3. If elapsed > `rule.valor_umbral` → silence timeout detected
4. Check cooldown: `now() - rule.last_alerted_at > rule.cooldown_minutos * 60` (or if `last_alerted_at IS NULL`, allow)
5. If both conditions met → INSERT into `alert_events` (rule_id, node_id, tipo_evento='SILENCE_TIMEOUT', mensaje, detected_at)
6. UPDATE `alert_rules SET last_alerted_at = now() WHERE id = rule.id`
7. Write health row to `alert_engine_health` with counts

### alert-dispatcher Bifurcation

```typescript
// In the handler:
const tableName = payload.table?.name || 'norvi_telemetry';

if (tableName === 'alert_events') {
  // BRANCH B: USER_DEFINED flow
  // 1. Extract alert_event from payload
  // 2. Query alert_rules WHERE id = event.rule_id
  // 3. Read rule.canales (JSONB array of channel types)
  // 4. For each channel type: query alert_channels WHERE type=X AND enabled=true
  // 5. adapter.send(config, message)
  // 6. UPDATE alert_events SET dispatched=true, dispatch_result=result
} else {
  // BRANCH A: norvi_telemetry flow (unchanged)
}
```

## Data Flow

```
SILENCE TIMEOUT (cron every 1 min)
        │
        ▼
silence-detector Nhost Function
        │
        ├─► Query USER_DEFINED SILENCE_TIMEOUT rules
        │       │
        │       ▼ (for each rule)
        │   Query latest event_ts from norvi_telemetry
        │       │
        │       ├─► elapsed > valor_umbral AND cooldown passed
        │       │       │
        │       │       ▼
        │       │   INSERT into alert_events
        │       │   UPDATE alert_rules.last_alerted_at
        │       │
        │       └─► (no violation or cooldown active) → skip
        │
        └─► Write health row to alert_engine_health

alert_events INSERT
        │
        ▼
Hasura Event Trigger (alert_on_silence_detected)
        │
        ▼
alert-dispatcher Nhost Function
        │
        ├─► BIFURCATE: table.name === 'alert_events'
        │       │
        │       ├─► Query alert_rules WHERE id = event.rule_id
        │       │       │
        │       │       ▼
        │       │   Read rule.canales (e.g., ["telegram", "slack"])
        │       │       │
        │       │       ▼ (for each channel type)
        │       │   Query alert_channels WHERE channel_type=X AND enabled=true
        │       │       │
        │       │       ▼
        │       │   adapter.send(config, message) [with retry]
        │       │
        │       └─► UPDATE alert_events SET dispatched=true, dispatch_result=...
        │
        └─► (norvi_telemetry branch — unchanged)
```

## File Changes

| File | Action | Description |
|------|--------|-------------|
| `nhost/migrations/default/alert_engine/up.sql` | MODIFY | Add ALTER alert_rules + CREATE alert_events at bottom |
| `nhost/migrations/default/alert_engine/down.sql` | MODIFY | Add DROP alert_events + revert ALTER TABLE |
| `nhost/functions/silence-detector/engine.ts` | CREATE | SilenceDetectorEngine class with evaluate() + DI |
| `nhost/functions/silence-detector/index.ts` | CREATE | Cron handler with production GraphQL implementations |
| `nhost/functions/alert-dispatcher/index.ts` | MODIFY | Bifurcate: handle norvi_telemetry AND alert_events |
| `nhost/metadata/cron_triggers.yaml` | MODIFY | Add silence-detector-cron (every 1 min) |
| `nhost/metadata/event_triggers.yaml` | MODIFY | Add alert_on_silence_detected on alert_events |
| `edge_ops/modbus_bridge/tests/test_alert_engine.py` | MODIFY | Add 7 Phase 4 test classes (~50 tests) |
| `openspec/changes/alert-engine-dynamic-rules/tasks.md` | CREATE | Phase 4 tasks |
| `openspec/changes/alert-engine-dynamic-rules/spec.md` | CREATE | Dynamic rules delta spec |
| `openspec/changes/alert-engine-dynamic-rules/design.md` | CREATE | Dynamic rules design |

## Migration Plan
- **Up**: ALTER alert_rules ADD COLUMNS (all IF NOT EXISTS), CREATE alert_events, CREATE indexes
- **Down**: DROP alert_events indexes, DROP alert_events, ALTER alert_rules DROP COLUMNS
- Existing SYSTEM rules are unaffected (default scope='SYSTEM')

## Testing Strategy

| Layer | What | Approach |
|-------|------|----------|
| Migration V2 | New columns + alert_events table | Assert SQL content in up.sql and down.sql |
| Silence Detector | Engine contract, interfaces, evaluate() signature | Assert TypeScript exports and method params |
| Dispatcher Bifurcation | Both table.name branches, canales flow | Assert index.ts contains both paths |
| Metadata | Cron + event trigger YAMLs | Assert YAML content for new entries |
| Cooldown | Logic to prevent re-alerting | Assert engine.ts has cooldown_minutos comparison |
