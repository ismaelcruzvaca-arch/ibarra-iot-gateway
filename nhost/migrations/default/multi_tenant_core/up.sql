-- ============================================================================
-- Migration: multi_tenant_core
-- Description: Jerarquía física multi-planta, catálogo de hardware,
--              registro de nodos y mapeo usuario-planta.
-- Fase 2 del Dynamic Rule Engine.
-- ============================================================================

-- 1. PLANTS — Tenant raíz (GDL_Tlaquepaque, MTY_Norte, etc.)
CREATE TABLE IF NOT EXISTS plants (
    id         UUID         PRIMARY KEY DEFAULT gen_random_uuid(),
    name       TEXT         NOT NULL UNIQUE,
    code       TEXT         NOT NULL UNIQUE,
    location   JSONB,
    timezone   TEXT         DEFAULT 'America/Mexico_City',
    created_at TIMESTAMPTZ  DEFAULT now(),
    updated_at TIMESTAMPTZ  DEFAULT now()
);

-- 2. LINES — Líneas de producción dentro de una planta
CREATE TABLE IF NOT EXISTS lines (
    id         UUID         PRIMARY KEY DEFAULT gen_random_uuid(),
    plant_id   UUID         NOT NULL REFERENCES plants(id) ON DELETE CASCADE,
    name       TEXT         NOT NULL,
    code       TEXT,
    created_at TIMESTAMPTZ  DEFAULT now(),
    updated_at TIMESTAMPTZ  DEFAULT now()
);
CREATE INDEX IF NOT EXISTS idx_lines_plant_id ON lines (plant_id);

-- 3. MACHINES — Máquinas individuales dentro de una línea
CREATE TABLE IF NOT EXISTS machines (
    id           UUID         PRIMARY KEY DEFAULT gen_random_uuid(),
    line_id      UUID         NOT NULL REFERENCES lines(id) ON DELETE CASCADE,
    name         TEXT         NOT NULL,
    machine_type TEXT,
    metadata     JSONB,
    created_at   TIMESTAMPTZ  DEFAULT now(),
    updated_at   TIMESTAMPTZ  DEFAULT now()
);
CREATE INDEX IF NOT EXISTS idx_machines_line_id ON machines (line_id);

-- 4. DEVICE_MODELS — Catálogo maestro de hardware aprobado
--    (NORVI-ESP32-Inductivo, RV1106-Camara-Vision, etc.)
CREATE TABLE IF NOT EXISTS device_models (
    id           UUID         PRIMARY KEY DEFAULT gen_random_uuid(),
    model_name   TEXT         NOT NULL UNIQUE,
    manufacturer TEXT,
    description  TEXT,
    created_at   TIMESTAMPTZ  DEFAULT now()
);

-- 5. ALERT_CAPABILITIES — Catálogo universal de capacidades de alerta
--    (SILENCE_TIMEOUT, DEFECT_THRESHOLD, TEMP_CRITICAL, etc.)
CREATE TABLE IF NOT EXISTS alert_capabilities (
    id             UUID         PRIMARY KEY DEFAULT gen_random_uuid(),
    capability_key TEXT         NOT NULL UNIQUE,
    description    TEXT,
    created_at     TIMESTAMPTZ  DEFAULT now()
);

-- 6. MODEL_CAPABILITIES — Matriz: qué modelo de hardware SABE hacer qué
CREATE TABLE IF NOT EXISTS model_capabilities (
    model_id       UUID NOT NULL REFERENCES device_models(id) ON DELETE CASCADE,
    capability_id  UUID NOT NULL REFERENCES alert_capabilities(id) ON DELETE CASCADE,
    created_at     TIMESTAMPTZ DEFAULT now(),
    PRIMARY KEY (model_id, capability_id)
);

-- 7. NODES — Dispositivos físicos reales atornillados en máquinas
CREATE TABLE IF NOT EXISTS nodes (
    id              UUID         PRIMARY KEY DEFAULT gen_random_uuid(),
    machine_id      UUID         NOT NULL REFERENCES machines(id) ON DELETE CASCADE,
    device_model_id UUID         NOT NULL REFERENCES device_models(id),
    node_ident      TEXT         NOT NULL UNIQUE,
    metadata        JSONB,
    created_at      TIMESTAMPTZ  DEFAULT now()
);
CREATE INDEX IF NOT EXISTS idx_nodes_machine_id ON nodes (machine_id);

-- 8. USER_PLANTS — Mapeo usuario ↔ planta + rol
--    user_id references auth.users (Nhost auth)
CREATE TABLE IF NOT EXISTS user_plants (
    user_id    UUID        NOT NULL,
    plant_id   UUID        NOT NULL REFERENCES plants(id) ON DELETE CASCADE,
    role       TEXT        NOT NULL DEFAULT 'supervisor'
                           CHECK (role IN ('supervisor', 'maintenance', 'admin')),
    created_at TIMESTAMPTZ DEFAULT now(),
    PRIMARY KEY (user_id, plant_id)
);
CREATE INDEX IF NOT EXISTS idx_user_plants_user_id ON user_plants (user_id);
CREATE INDEX IF NOT EXISTS idx_user_plants_plant_id ON user_plants (plant_id);

-- ============================================================================
-- 9. EVOLUCIÓN: alert_rules — agregar plant_id
-- ============================================================================
ALTER TABLE alert_rules
  ADD COLUMN IF NOT EXISTS plant_id UUID REFERENCES plants(id);

-- Index for plant-scoped queries
CREATE INDEX IF NOT EXISTS idx_alert_rules_plant_id ON alert_rules (plant_id);

-- ============================================================================
-- 10. EVOLUCIÓN: alert_events — agregar plant_id (denormalizado para RLS)
-- ============================================================================
ALTER TABLE alert_events
  ADD COLUMN IF NOT EXISTS plant_id UUID REFERENCES plants(id);

CREATE INDEX IF NOT EXISTS idx_alert_events_plant_id ON alert_events (plant_id);

-- ============================================================================
-- 11. SEED DATA — Capacidades base del sistema
-- ============================================================================
INSERT INTO alert_capabilities (capability_key, description) VALUES
    ('SILENCE_TIMEOUT',   'Paro de máquina por falta de pulsos'),
    ('ERROR_THRESHOLD',   'Límite de errores de comunicación excedido'),
    ('FREQUENCY_DROP',    'Caída en frecuencia de lecturas por debajo del umbral'),
    ('DEFECT_THRESHOLD',  'Cantidad de defectos de calidad excede el límite'),
    ('TEMP_CRITICAL',     'Temperatura del dispositivo excede límite crítico'),
    ('VIBRATION_HIGH',    'Vibración anormal detectada en el equipo')
ON CONFLICT (capability_key) DO NOTHING;
