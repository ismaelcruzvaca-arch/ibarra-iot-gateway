# Design: Alert Engine — Dynamic Rule Engine (Fase 2: Multi-Tenant + RLS)

## Technical Approach
Phase 2 extends the Dynamic Rule Engine with multi-tenant isolation via a physical hierarchy (plants → lines → machines), a hardware catalog, and Hasura RLS policies. The hierarchy tables mirror the existing structure in produccion-ibarra for future federation via Hasura Remote Schema.

## Architecture Decisions

### AD-8: Fresh Hierarchy Tables (Not Remote Schema)
| Option | Tradeoff | Decision |
|--------|----------|----------|
| Use Hasura Remote Schema to reference produccion-ibarra tables | Tight coupling, dependency on external schema availability, no local control | ❌ |
| **Create fresh hierarchy tables locally** | Independent, self-contained, can be federated later. Duplicates structure but not data. | ✅ |
**Rationale**: This is a separate Nhost project. When federation is needed, Hasura Remote Schema will align the tables. For now, local tables keep the system fully self-contained.

### AD-9: Denormalized plant_id on alert_events
| Option | Tradeoff | Decision |
|--------|----------|----------|
| Derive plant_id via rule_id → alert_rules → plant_id join | RLS would require subquery, less efficient, complex relationship chain | ❌ |
| **Denormalize plant_id directly on alert_events** | Simple RLS filter, efficient queries. Requires application-level sync. | ✅ |
**Rationale**: RLS on alert_events must be fast (millions of rows). A denormalized FK allows `plant_id: { _eq: x-hasura-plant-id }` — no joins, no subqueries. The silence-detector writes plant_id when inserting the event.

### AD-10: Relationship Traversal for machines and nodes RLS
| Option | Tradeoff | Decision |
|--------|----------|----------|
| Denormalize plant_id on all hierarchy tables | Data redundancy, sync complexity | ❌ |
| **Use Hasura relationship traversal** | Clean FK chain, no redundancy. Hasura RLS supports `line.plant_id` and `machine.line.plant_id` notation. | ✅ |
**Rationale**: Hasura's RLS engine handles relationship traversal natively. Machines filter via `line.plant_id`, nodes via `machine.line.plant_id`. No denormalization needed.

### AD-11: Three-Role Model (supervisor, maintenance, admin)
| Option | Tradeoff | Decision |
|--------|----------|----------|
| Two roles (user, admin) | Too coarse — can't distinguish operational from config access | ❌ |
| **Three roles** | Clean separation: supervisor (config), maintenance (read+operate), admin (full) | ✅ |
**Rationale**: Matches real factory hierarchy. Maintenance can read and operate (insert/update) but not delete config. Supervisor has full CRUD on operational tables. Admin has no restrictions.

## Component Design

### Hierarchy Data Model
```
plants (1) ──→ (N) lines (1) ──→ (N) machines (1) ──→ (N) nodes
                                  │
                                  └── device_models (M:M) alert_capabilities
                                                  (via model_capabilities)

users ──→ user_plants ──→ plants (many-to-many with role)
```

### RLS Policy Design

Each operational table gets a `plant_id` filter (either direct FK or relationship traversal):

```yaml
# Direct FK tables (lines, alert_rules, alert_events):
filter:
  plant_id:
    _eq: x-hasura-plant-id

# Relationship traversal (machines → lines):
filter:
  line:
    plant_id:
      _eq: x-hasura-plant-id

# Deep traversal (nodes → machines → lines):
filter:
  machine:
    line:
      plant_id:
        _eq: x-hasura-plant-id
```

## File Changes

| File | Action | Description |
|------|--------|-------------|
| `nhost/migrations/default/multi_tenant_core/up.sql` | CREATE | Full migration: 8 new tables, 2 ALTER TABLE, seed data |
| `nhost/migrations/default/multi_tenant_core/down.sql` | CREATE | Reverses up.sql in dependency order |
| `nhost/metadata/databases/default/tables/public_plants.yaml` | CREATE | RLS: supervisor SELECT by plant_id, admin ALL |
| `nhost/metadata/databases/default/tables/public_lines.yaml` | CREATE | RLS: supervisor ALL filtered by plant_id |
| `nhost/metadata/databases/default/tables/public_machines.yaml` | CREATE | RLS: via line relationship traversal |
| `nhost/metadata/databases/default/tables/public_nodes.yaml` | CREATE | RLS: via machine→line relationship traversal |
| `nhost/metadata/databases/default/tables/public_device_models.yaml` | CREATE | RLS: SELECT only (global catalog) |
| `nhost/metadata/databases/default/tables/public_alert_capabilities.yaml` | CREATE | RLS: SELECT only (global catalog) |
| `nhost/metadata/databases/default/tables/public_model_capabilities.yaml` | CREATE | RLS: SELECT only (global catalog) |
| `nhost/metadata/databases/default/tables/public_alert_rules.yaml` | CREATE | RLS: supervisor/maintenance ALL by plant_id |
| `nhost/metadata/databases/default/tables/public_alert_events.yaml` | CREATE | RLS: supervisor SELECT only by plant_id |
| `nhost/metadata/databases/default/tables/public_user_plants.yaml` | CREATE | RLS: SELECT by user_id + plant_id |
| `edge_ops/modbus_bridge/tests/test_alert_engine.py` | MODIFY | Add 4 test classes: TestMultiTenantMigration, TestHasuraMetadataFiles, TestMultiTenantIsolation, TestSeedData |
| `openspec/changes/alert-engine-dynamic-rules/tasks.md` | MODIFY | Add Phase 5 tasks with [x] marks |
| `openspec/changes/alert-engine-dynamic-rules/spec.md` | MODIFY | Delta spec for multi-tenant tables, RLS, seed data |
| `openspec/changes/alert-engine-dynamic-rules/design.md` | MODIFY | Design decisions AD-8 through AD-11, hierarchy model, RLS design |

## Migration Plan

- **Up**: Create 8 new tables in dependency order, ALTER alert_rules ADD plant_id, ALTER alert_events ADD plant_id, seed INSERT with ON CONFLICT DO NOTHING
- **Down**: Revert alert_events plant_id, revert alert_rules plant_id, DROP all 8 tables in reverse dependency order
- **Idempotent**: All CREATE TABLE uses IF NOT EXISTS, all ALTER uses IF NOT EXISTS, seed uses ON CONFLICT DO NOTHING

## Testing Strategy

| Layer | What | Approach |
|-------|------|----------|
| Multi-tenant migration | up.sql + down.sql content | Assert CREATE TABLE for all 8 tables, ALTER TABLE, seed INSERT, DROP in correct order |
| Hasura metadata | 10 YAML files existence and content | Assert each file exists, contains supervisor role, references x-hasura-plant-id in filter |
| RLS isolation | Filter assertions | Assert operational tables have plant_id filter, catalog tables are unfiltered, user_plants has user_id filter |
| Seed data | 6 capability keys | Assert all 6 keys exist in INSERT with descriptions and ON CONFLICT |
