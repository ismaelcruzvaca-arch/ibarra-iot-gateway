# Delta Spec: Alert Engine — Dynamic Rule Engine (Fase 2: Multi-Tenant + RLS)

## Purpose
Add multi-tenant isolation to the Alert Engine by introducing a physical hierarchy (plants → lines → machines), a hardware catalog (device_models, alert_capabilities), and Hasura Row-Level Security (RLS) policies that restrict data access by `x-hasura-plant-id`.

## Important Design Decisions

- **Compatibility with produccion-ibarra**: The hierarchy (plants→lines→machines) mirrors what exists in produccion-ibarra but is created fresh here since this is a separate Nhost project. When these projects are federated via Hasura Remote Schema, the tables will align.
- **Plant isolation**: All operational tables (`alert_rules`, `alert_events`, `nodes`, `machines`, `lines`) MUST have RLS filtering by `x-hasura-plant-id`
- **SYSTEM rules**: Can have `plant_id = NULL` (global) or be assigned to a specific plant. `USER_DEFINED` rules MUST have `plant_id NOT NULL`.

## New Requirements

| ID | Statement | Strength |
|----|-----------|----------|
| FR-15 | The system SHALL support a hierarchy of plants → lines → machines → nodes | MUST |
| FR-16 | Each plant SHALL be identified by a unique name and code | MUST |
| FR-17 | A hardware device model catalog SHALL define approved hardware (device_models) and their capabilities (alert_capabilities, model_capabilities) | MUST |
| FR-18 | `user_plants` SHALL map auth.users to plants with role (supervisor, maintenance, admin) | MUST |
| FR-19 | `alert_rules` SHALL have an optional `plant_id` FK to support plant-scoped rules | MUST |
| FR-20 | `alert_events` SHALL have a denormalized `plant_id` FK for RLS filtering efficiency | MUST |
| FR-21 | Hasura RLS SHALL enforce that supervisor/maintenance roles only see data from their assigned plant via `x-hasura-plant-id` | MUST |
| FR-22 | Catalog tables (`device_models`, `alert_capabilities`, `model_capabilities`) SHALL allow unfiltered SELECT for all roles (global read-only) | SHOULD |
| FR-23 | `user_plants` SHALL restrict SELECT to the authenticated user's own rows (`x-hasura-user-id`) | MUST |
| FR-24 | Seed data SHALL pre-populate `alert_capabilities` with 6 base capabilities: SILENCE_TIMEOUT, ERROR_THRESHOLD, FREQUENCY_DROP, DEFECT_THRESHOLD, TEMP_CRITICAL, VIBRATION_HIGH | MUST |

## New Tables

### plants

| Column | Type | Constraints |
|--------|------|-------------|
| id | UUID | PK, DEFAULT gen_random_uuid() |
| name | TEXT | NOT NULL, UNIQUE |
| code | TEXT | NOT NULL, UNIQUE |
| location | JSONB | |
| timezone | TEXT | DEFAULT 'America/Mexico_City' |
| created_at | TIMESTAMPTZ | DEFAULT now() |
| updated_at | TIMESTAMPTZ | DEFAULT now() |

### lines

| Column | Type | Constraints |
|--------|------|-------------|
| id | UUID | PK, DEFAULT gen_random_uuid() |
| plant_id | UUID | NOT NULL, FK → plants(id), ON DELETE CASCADE |
| name | TEXT | NOT NULL |
| code | TEXT | |
| created_at | TIMESTAMPTZ | DEFAULT now() |
| updated_at | TIMESTAMPTZ | DEFAULT now() |

### machines

| Column | Type | Constraints |
|--------|------|-------------|
| id | UUID | PK, DEFAULT gen_random_uuid() |
| line_id | UUID | NOT NULL, FK → lines(id), ON DELETE CASCADE |
| name | TEXT | NOT NULL |
| machine_type | TEXT | |
| metadata | JSONB | |
| created_at | TIMESTAMPTZ | DEFAULT now() |
| updated_at | TIMESTAMPTZ | DEFAULT now() |

### device_models

| Column | Type | Constraints |
|--------|------|-------------|
| id | UUID | PK, DEFAULT gen_random_uuid() |
| model_name | TEXT | NOT NULL, UNIQUE |
| manufacturer | TEXT | |
| description | TEXT | |
| created_at | TIMESTAMPTZ | DEFAULT now() |

### alert_capabilities

| Column | Type | Constraints |
|--------|------|-------------|
| id | UUID | PK, DEFAULT gen_random_uuid() |
| capability_key | TEXT | NOT NULL, UNIQUE |
| description | TEXT | |
| created_at | TIMESTAMPTZ | DEFAULT now() |

### model_capabilities

| Column | Type | Constraints |
|--------|------|-------------|
| model_id | UUID | NOT NULL, FK → device_models(id), ON DELETE CASCADE |
| capability_id | UUID | NOT NULL, FK → alert_capabilities(id), ON DELETE CASCADE |
| created_at | TIMESTAMPTZ | DEFAULT now() |
| PK | (model_id, capability_id) | |

### nodes

| Column | Type | Constraints |
|--------|------|-------------|
| id | UUID | PK, DEFAULT gen_random_uuid() |
| machine_id | UUID | NOT NULL, FK → machines(id), ON DELETE CASCADE |
| device_model_id | UUID | NOT NULL, FK → device_models(id) |
| node_ident | TEXT | NOT NULL, UNIQUE |
| metadata | JSONB | |
| created_at | TIMESTAMPTZ | DEFAULT now() |

### user_plants

| Column | Type | Constraints |
|--------|------|-------------|
| user_id | UUID | NOT NULL |
| plant_id | UUID | NOT NULL, FK → plants(id), ON DELETE CASCADE |
| role | TEXT | NOT NULL, DEFAULT 'supervisor', CHECK IN (supervisor, maintenance, admin) |
| created_at | TIMESTAMPTZ | DEFAULT now() |
| PK | (user_id, plant_id) | |

## Modified Tables

### alert_rules (MODIFIED)

| Column | Type | Constraints |
|--------|------|-------------|
| ... | ... | All existing columns unchanged |
| plant_id | UUID | NULL, FK → plants(id) (new column) |

### alert_events (MODIFIED)

| Column | Type | Constraints |
|--------|------|-------------|
| ... | ... | All existing columns unchanged |
| plant_id | UUID | NULL, FK → plants(id) (new column, denormalized for RLS) |

## RLS Policy Matrix

| Table | Supervisor | Maintenance | Admin |
|-------|-----------|-------------|-------|
| plants | SELECT (filter by id = x-hasura-plant-id) | SELECT (filter by id = x-hasura-plant-id) | ALL |
| lines | ALL (filter by plant_id) | ALL (filter by plant_id) | ALL |
| machines | ALL (filter via line→plant_id) | ALL (filter via line→plant_id) | ALL |
| nodes | SELECT/INSERT/UPDATE (filter via machine→line→plant_id) | SELECT/INSERT/UPDATE (filter via machine→line→plant_id) | ALL |
| device_models | SELECT (unfiltered) | SELECT (unfiltered) | ALL |
| alert_capabilities | SELECT (unfiltered) | SELECT (unfiltered) | ALL |
| model_capabilities | SELECT (unfiltered) | SELECT (unfiltered) | ALL |
| alert_rules | ALL (filter by plant_id) | ALL (filter by plant_id) | ALL |
| alert_events | SELECT (filter by plant_id) | SELECT (filter by plant_id) | ALL |
| user_plants | SELECT (filter by user_id + plant_id) | SELECT (filter by user_id + plant_id) | ALL |

## Seed Data

The following capabilities are pre-seeded:

| capability_key | description |
|---------------|-------------|
| SILENCE_TIMEOUT | Paro de máquina por falta de pulsos |
| ERROR_THRESHOLD | Límite de errores de comunicación excedido |
| FREQUENCY_DROP | Caída en frecuencia de lecturas por debajo del umbral |
| DEFECT_THRESHOLD | Cantidad de defectos de calidad excede el límite |
| TEMP_CRITICAL | Temperatura del dispositivo excede límite crítico |
| VIBRATION_HIGH | Vibración anormal detectada en el equipo |

## New Scenarios

| Scenario | GIVEN | WHEN | THEN |
|----------|-------|------|------|
| Multi-tenant migration | `multi_tenant_core/up.sql` exists | Read SQL | Contains CREATE TABLE for all 8 new tables, ALTER TABLE for alert_rules and alert_events, seed INSERT |
| Hasura metadata files | Tables metadata directory exists | Read YAML files | Each expected table has a YAML file with supervisor role and x-hasura-plant-id filter |
| RLS isolation | Supervisor from plant A queries alert_rules | Hasura applies filter | Only rows with plant_id = x-hasura-plant-id are returned |
| RLS relationship traversal | Supervisor queries machines | Hasura applies filter via line relationship | Only machines whose line.plant_id = x-hasura-plant-id |
| Seed data completeness | up.sql read | Verify seed INSERT | All 6 capability keys present with ON CONFLICT DO NOTHING |
