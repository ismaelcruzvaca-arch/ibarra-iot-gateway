#!/usr/bin/env bash
set -euo pipefail

# ==============================================================================
# gateway-translator — Install script for Raspberry Pi 5
#
# Usage:
#   sudo ./install.sh
#
# Prerequisites:
#   - Python 3.10+ installed
#   - pip3 available
#   - systemd available (Raspberry Pi OS / Ubuntu Server)
# ==============================================================================

INSTALL_DIR="/opt/gema/gateway-translator"
SERVICE_NAME="gateway-translator"

echo "=== Installing gateway-translator ==="

# 1. Create install directory
sudo mkdir -p "$INSTALL_DIR"
sudo mkdir -p "$INSTALL_DIR/src"
sudo mkdir -p "$INSTALL_DIR/tests"

# 2. Copy source files
sudo cp -r src/* "$INSTALL_DIR/src/"
sudo cp -r tests/ "$INSTALL_DIR/tests/"
sudo cp requirements.txt "$INSTALL_DIR/"

# 3. Install Python dependencies
sudo pip3 install -r "$INSTALL_DIR/requirements.txt"

# 4. Install systemd service
sudo cp deploy/gateway-translator.service "/etc/systemd/system/$SERVICE_NAME.service"
sudo systemctl daemon-reload

# 5. Configure env vars (create override if needed)
if [ ! -f /etc/systemd/system/$SERVICE_NAME.service.d/override.conf ]; then
    sudo mkdir -p /etc/systemd/system/$SERVICE_NAME.service.d
    cat << EOF | sudo tee /etc/systemd/system/$SERVICE_NAME.service.d/override.conf
[Service]
# Override these for your environment
Environment=HASURA_GRAPHQL_URL=http://localhost:8080/v1/graphql
Environment=HASURA_ADMIN_SECRET=changeme
Environment=MQTT_BROKER_HOST=192.168.1.50
EOF
fi

echo ""
echo "=== Installation complete ==="
echo ""
echo "To start the service:"
echo "  sudo systemctl enable --now $SERVICE_NAME"
echo ""
echo "To check status:"
echo "  sudo systemctl status $SERVICE_NAME"
echo ""
echo "To view logs:"
echo "  sudo journalctl -u $SERVICE_NAME -f"
echo ""
echo "IMPORTANT: Configure environment variables before starting:"
echo "  sudo nano /etc/systemd/system/$SERVICE_NAME.service.d/override.conf"
echo "  sudo systemctl daemon-reload"
echo "  sudo systemctl restart $SERVICE_NAME"
