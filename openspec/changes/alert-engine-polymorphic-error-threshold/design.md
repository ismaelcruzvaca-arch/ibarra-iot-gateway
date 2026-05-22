# Design: Alert Engine — ERROR_THRESHOLD Polymorphic Extension

## Technical Approach

Extend the existing `SilenceDetectorEngine` to handle multiple rule types via a polymorphic discriminator pattern. The `tipo_condicion` field (already present in the DB schema from Fase 1) determines which evaluation logic applies. A new injected function `queryErrorCountFn` provides the error count for `ERROR_THRESHOLD` rules, following the same dependency injection pattern used for all data-access functions.

## Architecture Decisions

### AD-12: Polymorphic Discriminator (Not Strategy Pattern)
| Option | Tradeoff | Decision |
|--------|----------|----------|
| **Polymorphic if/else in loop** | Simple, single class, no abstraction overhead. Condition count is bounded (2-4 types max). | ✅ |
| Strategy Pattern (separate evaluator classes) | Cleaner Open/Closed, but over-engineered for 2 rule types. Each strategy would need identical DI wiring. | ❌ |
| Visitor Pattern | Even more abstract, no benefit here. | ❌ |
**Rationale**: The engine currently has a single loop. Adding an if/else on `tipo_condicion` keeps the code readable and avoids premature abstraction. When a 3rd type (e.g., FREQUENCY_DROP) is added, we can extract strategies if the loop body grows beyond ~50 lines per branch.

### AD-13: queryErrorCountFn as 6th Parameter (Not Object Wrapper)
| Option | Tradeoff | Decision |
|--------|----------|----------|
| **Add as 6th positional parameter** | Consistent with existing 5-param signature. Callers see the full dependency graph. | ✅ |
| Single options object | Cleaner for many params, but breaks existing call pattern. All 5 existing call sites would need refactoring. | ❌ |
**Rationale**: Following YAGNI — keep the existing positional pattern. If the param list grows to 8+, refactor to an options object.

## Component Design

### Engine Loop Flow (Polymorphic)

```
for each rule:
  ├─ tipo_condicion === 'SILENCE_TIMEOUT'
  │   ├─ queryLastEventFn(nodeId) → lastEventTs | null
  │   ├─ if null → continue (no data yet)
  │   ├─ if elapsedSeconds <= valor_umbral → continue (still active)
  │   ├─ if within cooldown → continue
  │   └─ shouldAlert = true, build SILENCE_TIMEOUT mensaje
  │
  ├─ tipo_condicion === 'ERROR_THRESHOLD'
  │   ├─ queryErrorCountFn(nodeId, ventanaMinutos) → errorCount
  │   ├─ if errorCount < valor_umbral → continue (below threshold)
  │   ├─ if within cooldown → continue
  │   └─ shouldAlert = true, build ERROR_THRESHOLD mensaje
  │
  └─ if shouldAlert:
      ├─ insertAlertEventFn({ rule_id, node_id, tipo_evento, mensaje, detected_at })
      └─ updateLastAlertedFn(ruleId)
```

### Database Migration

```
alert_rules
└── ventana_minutos INTEGER DEFAULT 5  (NEW)
    └── idx_alert_rules_ventana (INDEX)
```

### Rule Interface (renamed from SilenceRule)

```typescript
interface Rule {
  id: string;
  node_id: string;
  tipo_condicion: 'SILENCE_TIMEOUT' | 'ERROR_THRESHOLD';
  valor_umbral: number;
  canales: string[];
  cooldown_minutos: number;
  last_alerted_at: string | null;
  ventana_minutos: number;  // NEW
}
```

## File Changes

| File | Action | Description |
|------|--------|-------------|
| `nhost/migrations/default/alert_rules_window/up.sql` | CREATE | ADD COLUMN ventana_minutos + index + comment |
| `nhost/migrations/default/alert_rules_window/down.sql` | CREATE | DROP index + DROP COLUMN |
| `nhost/functions/silence-detector/engine.ts` | MODIFY | SilenceRule → Rule, add tipo_condicion + ventana_minutos, add queryErrorCountFn, polymorphic loop |
| `nhost/functions/silence-detector/index.ts` | MODIFY | Query both rule types, add defaultQueryErrorCount, update handler |
| `edge_ops/modbus_bridge/tests/test_alert_engine.py` | MODIFY | Add TestErrorThresholdMigration + 7 ERROR_THRESHOLD engine contract tests + extend TestSilenceDetectorFiles |
| `openspec/changes/alert-engine-polymorphic-error-threshold/tasks.md` | CREATE | Phase 6 tasks with [x] |
| `openspec/changes/alert-engine-polymorphic-error-threshold/spec.md` | CREATE | FR-25 + scenarios |
| `openspec/changes/alert-engine-polymorphic-error-threshold/design.md` | CREATE | Polymorphic design + AD-12 + AD-13 |

## Migration Plan

- **Up**: ALTER TABLE alert_rules ADD COLUMN ventana_minutos (default 5), add comment, CREATE INDEX
- **Down**: DROP INDEX, ALTER TABLE DROP COLUMN
- **Idempotent**: Uses IF NOT EXISTS for both ADD COLUMN and CREATE INDEX

## Testing Strategy

| Layer | What | Approach |
|-------|------|----------|
| Migration | ventana_minutos column + index | Assert up.sql has ADD COLUMN + CREATE INDEX; down.sql has DROP COLUMN + DROP INDEX |
| Engine contract | Rule interface exports | Assert engine.ts exports Rule (renamed from SilenceRule) with all fields |
| Engine contract | Polymorphic loop | Assert both tipo_condicion branches exist |
| Engine contract | ERROR_THRESHOLD logic | Assert queryErrorCountFn call, count >= umbral alert, count < umbral skip, cooldown respect |
| Engine contract | Mixed rules | Assert both rule types handled in one cycle |
| File existence | Rule + AlertEventInsert exports | Assert engine.ts exports both types |
