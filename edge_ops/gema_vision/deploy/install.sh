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
CALIB_DIR="/userdata/vision/calibration"
CONFIG_DIR="/userdata/vision"

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

# --- 4. OTA agent -----------------------------------------------------------
OTA_BIN_SRC="/tmp/deploy/gema-ota"
OTA_BIN_DST="/userdata/bin/gema-ota"
OTA_SERVICE_SRC="/tmp/deploy/gema-ota.service"
OTA_SERVICE_DST="/etc/systemd/system/gema-ota.service"

if [ -f "$OTA_BIN_SRC" ]; then
    echo "Installing OTA agent binary: ${OTA_BIN_DST}"
    mkdir -p "/userdata/bin"
    cp "$OTA_BIN_SRC" "$OTA_BIN_DST"
    chmod 755 "$OTA_BIN_DST"
else
    echo "WARNING: OTA agent binary not found at ${OTA_BIN_SRC}"
    echo "  Build it first with:  cmake -B build && cmake --build build"
fi

if [ -f "$OTA_SERVICE_SRC" ]; then
    echo "Installing OTA service: ${OTA_SERVICE_DST}"
    cp "$OTA_SERVICE_SRC" "$OTA_SERVICE_DST"
    chmod 644 "$OTA_SERVICE_DST"
else
    echo "WARNING: OTA service file not found at ${OTA_SERVICE_SRC}"
fi

# --- 5. Data directories ----------------------------------------------------
echo "Creating data directories: ${DATA_DIR}"
mkdir -p "$MODEL_DIR" "$CALIB_DIR" "$CONFIG_DIR" "/userdata/ota_tmp" "/backup"
chmod 755 "$DATA_DIR" "$MODEL_DIR" "$CALIB_DIR" "$CONFIG_DIR" "/userdata/ota_tmp" "/backup"

# --- 6. Enable and start the services ---------------------------------------
echo "Reloading systemd daemon..."
systemctl daemon-reload

echo "Enabling gema-vision (auto-start on boot)..."
systemctl enable gema-vision

echo "Starting gema-vision..."
systemctl start gema-vision

echo "Enabling gema-ota (auto-start on boot)..."
systemctl enable gema-ota

echo "Starting gema-ota..."
systemctl start gema-ota

# --- 7. Verify --------------------------------------------------------------
echo ""
echo "=== Status ==="
systemctl status gema-vision --no-pager || true

echo ""
echo "=== Installation complete ==="
echo "  Logs:  journalctl -u gema-vision -f"
echo "  Stop:  systemctl stop gema-vision"
echo "  Start: systemctl start gema-vision"
