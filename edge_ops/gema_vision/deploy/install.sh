#!/bin/sh
# ============================================================================
# install.sh  —  Deploy gema-vision to a Rockchip RV1106 target
#
# Run this ON THE TARGET BOARD after copying the files:
#
#   scp -r edge_ops/gema_vision/deploy/* root@camara:/tmp/deploy/
#   ssh root@camara "sh /tmp/deploy/install.sh"
#
# Or, if building with Buildroot, add this as a post-build script.
# ============================================================================

set -e

BIN_SRC="/tmp/deploy/gema-vision"
BIN_DST="/usr/bin/gema-vision"
SERVICE_SRC="/tmp/deploy/gema-vision.service"
SERVICE_DST="/etc/systemd/system/gema-vision.service"
ENV_SRC="/tmp/deploy/gema-vision.env"
ENV_DST="/etc/gema-vision.env"
DATA_DIR="/userdata/gema-vision"
MODEL_DIR="${DATA_DIR}/model"

echo "=== GEMA Vision — Installation ==="

# --- 1. Binary --------------------------------------------------------------
if [ -f "$BIN_SRC" ]; then
    echo "Installing binary: ${BIN_DST}"
    cp "$BIN_SRC" "$BIN_DST"
    chmod 755 "$BIN_DST"
    # If the binary has capabilities (e.g. real-time scheduling):
    # setcap cap_sys_nice=ep "$BIN_DST"
else
    echo "WARNING: Binary not found at ${BIN_SRC}"
    echo "  Build it first with:  cmake -B build && cmake --build build"
fi

# --- 2. Systemd service -----------------------------------------------------
if [ -f "$SERVICE_SRC" ]; then
    echo "Installing service: ${SERVICE_DST}"
    cp "$SERVICE_SRC" "$SERVICE_DST"
    chmod 644 "$SERVICE_DST"
else
    echo "WARNING: Service file not found at ${SERVICE_SRC}"
fi

# --- 3. Environment file ----------------------------------------------------
if [ -f "$ENV_SRC" ]; then
    echo "Installing environment: ${ENV_DST}"
    cp "$ENV_SRC" "$ENV_DST"
    chmod 600 "$ENV_DST"  # may contain secrets (MQTT passwords)
fi

# --- 4. Data directories ----------------------------------------------------
echo "Creating data directories: ${DATA_DIR}"
mkdir -p "$MODEL_DIR"
chmod 755 "$DATA_DIR" "$MODEL_DIR"

# --- 5. Enable and start the service ----------------------------------------
echo "Reloading systemd daemon..."
systemctl daemon-reload

echo "Enabling gema-vision (auto-start on boot)..."
systemctl enable gema-vision

echo "Starting gema-vision..."
systemctl start gema-vision

# --- 6. Verify --------------------------------------------------------------
echo ""
echo "=== Status ==="
systemctl status gema-vision --no-pager || true

echo ""
echo "=== Installation complete ==="
echo "  Logs:  journalctl -u gema-vision -f"
echo "  Stop:  systemctl stop gema-vision"
echo "  Start: systemctl start gema-vision"
