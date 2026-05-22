# Tasks: Alert Engine — ERROR_THRESHOLD Polymorphic Extension

## Review Workload Forecast

| Field | Value |
|-------|-------|
| Estimated changed lines | ~350–450 (migration SQL + 2 TypeScript + test classes + openspec) |
| 400-line budget risk | Medium |
| Chained PRs recommended | No |
| Delivery strategy | single-pr |

## Phase 6: ERROR_THRESHOLD Extension

- [x] 6.1 Migration SQL: `alert_rules_window/up.sql` + `down.sql` — ADD COLUMN `ventana_minutos` + index
- [x] 6.2 Engine polymorphic: engine.ts — `SilenceRule` → `Rule`, `queryErrorCountFn` DI, SILENCE_TIMEOUT/ERROR_THRESHOLD branches
- [x] 6.3 Entry point: index.ts — query both rule types, add `defaultQueryErrorCount`, update handler
- [x] 6.4 Tests: `TestErrorThresholdMigration`, extend `TestSilenceDetectorEngineContract` (7 new tests), extend `TestSilenceDetectorFiles`
- [x] 6.5 Openspec artifacts: tasks.md, spec.md, design.md
