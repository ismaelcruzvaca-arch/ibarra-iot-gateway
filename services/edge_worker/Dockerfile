# =============================================================================
# GEMA V3.0 Edge Worker — MQTT to SQLite Ingestion
# Multi-stage build: builder (deps) → runner (minimal)
#
# Architecture:
#   - Node.js 20 Alpine (ARM64 compatible for RPi 5)
#   - better-sqlite3 compiles during npm install (node:20-alpine has build tools)
#   - User: node (non-root)
# =============================================================================

# ---- Builder stage ----------------------------------------------------------
FROM node:20-alpine AS builder

WORKDIR /app

# Install production dependencies only
COPY package.json ./
RUN npm ci --only=production

# ---- Runner stage -----------------------------------------------------------
FROM node:20-alpine

# Metadata
LABEL org.opencontainers.image.title="GEMA Edge Worker"
LABEL org.opencontainers.image.description="MQTT to SQLite ingestion service for GEMA V3.0"
LABEL org.opencontainers.image.source="https://github.com/chocolate-ibarra/gema-edge-worker"
LABEL org.opencontainers.image.version="1.0.0"

WORKDIR /app

# Copy node_modules with native bindings (pre-compiled for ARM64)
COPY --from=builder /app/node_modules ./node_modules

# Copy application code (no tests, no dev files)
COPY edge_worker.js ./

# Non-root user for security
USER node

# Health check via SQLite — if the process is alive, SQLite is accessible
HEALTHCHECK --interval=30s --timeout=5s --start-period=10s --retries=3 \
  CMD node -e "require('better-sqlite3')(process.env.SQLITE_DB_PATH||'/data/telemetry_buffer.db').pragma('journal_mode')" \
  || exit 1

ENTRYPOINT ["node", "edge_worker.js"]
