# Delta Spec: Alert Engine — ERROR_THRESHOLD Polymorphic Extension

## Purpose

Extend the rule evaluator engine to support `ERROR_THRESHOLD` rules that count error events within a configurable time window (`ventana_minutos`). This completes the polymorphic rule evaluation pattern started in Fase 1 (which only handled `SILENCE_TIMEOUT`).

## Important Design Decisions

- **Polymorphic evaluation**: The engine dispatches to SILENCE_TIMEOUT or ERROR_THRESHOLD logic based on `rule.tipo_condicion`. Both branches share the same cooldown and alert-insertion backend.
- **Injected queryErrorCountFn**: Follows the existing DI pattern (5th injected function becomes the 6th) for full testability without a real database.
- **ventana_minutos default**: 5 minutes, stored in the DB column and mapped to the `Rule` interface.

## New Requirement

| ID | Statement | Strength |
|----|-----------|----------|
| FR-25 | The system SHALL support `ERROR_THRESHOLD` rules that count error events in `norvi_telemetry` within a configurable time window (`ventana_minutos`), and trigger an alert when the count meets or exceeds `valor_umbral` | MUST |

## Modified Components

### alert_rules (MODIFIED)

| Column | Type | Constraints |
|--------|------|-------------|
| ... | ... | All existing columns unchanged |
| ventana_minutos | INTEGER | DEFAULT 5 (new column for ERROR_THRESHOLD time window) |

### Rule interface (in engine.ts, RENAMED from SilenceRule)

| Field | Type | Description |
|-------|------|-------------|
| tipo_condicion | `'SILENCE_TIMEOUT' \| 'ERROR_THRESHOLD'` | Polymorphic discriminator |
| ventana_minutos | number | Time window in minutes for ERROR_THRESHOLD (default 5) |
| ... | ... | All existing SilenceRule fields unchanged |

### evaluate() method signature (MODIFIED)

6th injected parameter added:

```
queryErrorCountFn: (nodeId: string, ventanaMinutos: number) => Promise<number>
```

## Scenarios

| Scenario | GIVEN | WHEN | THEN |
|----------|-------|------|------|
| Error threshold exceeded | rule.tipo_condicion = ERROR_THRESHOLD, error count >= valor_umbral | evaluate() runs | Alert event inserted with tipo_evento = ERROR_THRESHOLD |
| Error threshold below | rule.tipo_condicion = ERROR_THRESHOLD, error count < valor_umbral | evaluate() runs | No alert event inserted |
| Error threshold cooldown | rule within cooldown after last alert | evaluate() runs | Rule skipped (continue) |
| Mixed rules | 1 SILENCE_TIMEOUT + 1 ERROR_THRESHOLD rule | evaluate() runs | Both evaluated independently |
| Migration | up.sql applied | alert_rules has ventana_minutos | Column exists with DEFAULT 5 and index |
