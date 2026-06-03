# =============================================================================
# GEMA V3.0 Machine Simulator — Python MQTT Client
# Single-stage build (pure Python — no native compilation needed)
#
# Architecture:
#   - Python 3.12-slim (ARM64 compatible for RPi 5)
#   - paho-mqtt for MQTT v3.1.1 with LWT support
#   - User: non-root (nobody)
# =============================================================================

FROM python:3.12-slim

# Metadata
LABEL org.opencontainers.image.title="GEMA Machine Simulator"
LABEL org.opencontainers.image.description="Industrial machine simulator for GEMA V3.0 — 7 machines, Sparkplug B Lite payloads"
LABEL org.opencontainers.image.source="https://github.com/chocolate-ibarra/gema-edge-worker"
LABEL org.opencontainers.image.version="1.0.0"

WORKDIR /app

# Install Python dependencies
COPY requirements.txt ./
RUN pip install --no-cache-dir -r requirements.txt

# Copy application code
COPY simulator.py ./

# Non-root user for security
USER nobody

# Run with unbuffered output so Docker logs are real-time
CMD ["python", "-u", "simulator.py"]
