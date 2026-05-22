# Tasks: Alert Engine — Dynamic Rule Engine (Fase 2: Multi-Tenant)

## Review Workload Forecast

| Field | Value |
|-------|-------|
| Estimated changed lines | ~700–900 (migration SQL + 10 YAML + test classes + openspec) |
| 400-line budget risk | Low |
| Chained PRs recommended | No |
| Delivery strategy | single-pr |

## Phase 4: Dynamic Rule Engine — Core (Fase 1, completed)

- [x] 4.1 Migration SQL: ALTER alert_rules (scope, tipo_condicion, valor_umbral, canales, cooldown_minutos, last_alerted_at) + CREATE alert_events
- [x] 4.2 Silence Detector engine.ts: SilenceDetectorEngine class with evaluate() + DI pattern
- [x] 4.3 Silence Detector index.ts: Nhost cron handler with production GraphQL implementations
- [x] 4.4 Dispatcher bifurcation: alert-dispatcher/index.ts handles both norvi_telemetry (SYSTEM) and alert_events (USER_DEFINED)
- [x] 4.5 Metadata: cron_triggers.yaml (silence-detector-cron) + event_triggers.yaml (alert_on_silence_detected)
- [x] 4.6 Tests: TestMigrationV2, TestSilenceDetectorFiles, TestSilenceDetectorEngineContract, TestDispatcherBifurcation, TestMetadataCron, TestMetadataEventTrigger, TestCooldownLogic
- [x] 4.7 Openspec artifacts: tasks.md, spec.md, design.md updated

## Phase 5: Multi-Tenant (Fase 2, this change)

- [x] 5.1 Migration SQL: Create multi_tenant_core migration (plants, lines, machines, device_models, alert_capabilities, model_capabilities, nodes, user_plants) + ALTER alert_rules + ALTER alert_events
- [x] 5.2 Hasura Metadata: RLS policies for 10 tables (plants, lines, machines, nodes, device_models, alert_capabilities, model_capabilities, alert_rules, alert_events, user_plants)
- [x] 5.3 Tests: TestMultiTenantMigration, TestHasuraMetadataFiles, TestMultiTenantIsolation, TestSeedData
- [x] 5.4 Openspec artifacts: tasks.md, spec.md, design.md updated

## Future Phases (not in scope)

- [ ] Phase 6: Additional regla types (ERROR_THRESHOLD, FREQUENCY_DROP, DEFECT_THRESHOLD)
- [ ] Phase 7: Rules management API (CRUD endpoints for USER_DEFINED rules)
- [ ] Phase 8: Dashboard integration (rules UI, event history)
