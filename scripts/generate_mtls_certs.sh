#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# GEMA V3.0 EdgeOps PKI Certs Generator
#
# Generates a self-signed Root CA, server certificates for the Mosquitto broker
# (with Subject Alternative Names), and client certificates for IoT field nodes.
#
# Enforces Zero-Trust mTLS standards for external sensor gateways.
# ---------------------------------------------------------------------------
set -euo pipefail

# Determine directory structure dynamically
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BASE_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
CERT_DIR="${BASE_DIR}/docker/certs"

echo "================================================================="
echo "EdgeOps PKI: Generating Secure mTLS Certificates"
echo "Target Directory: ${CERT_DIR}"
echo "================================================================="

# Recreate clean target directory
rm -rf "${CERT_DIR}"
mkdir -p "${CERT_DIR}"
cd "${CERT_DIR}"

# ---------------------------------------------------------------------------
# 1. ROOT CA (Authority)
# ---------------------------------------------------------------------------
echo "--> Step 1: Creating self-signed Root CA (4096-bit)..."
openssl genrsa -out ca.key 4096

openssl req -new -x509 \
  -days 3650 \
  -key ca.key \
  -out ca.crt \
  -subj "/C=MX/ST=Sonora/L=Hermosillo/O=Novamex/OU=IT/CN=Novamex GEMA Root CA"

# ---------------------------------------------------------------------------
# 2. MOSQUITTO BROKER (Server Certs)
# ---------------------------------------------------------------------------
echo "--> Step 2: Creating Mosquitto Broker Server Key and CSR..."
openssl genrsa -out server.key 2048

# Create CSR configuration file with SAN (Subject Alternative Names)
cat <<EOF > server_ext.cnf
[req]
distinguished_name = req_distinguished_name
req_extensions = v3_req
prompt = no

[req_distinguished_name]
C = MX
ST = Sonora
L = Hermosillo
O = Novamex
OU = IT
CN = mosquitto

[v3_req]
keyUsage = keyEncipherment, dataEncipherment
extendedKeyUsage = serverAuth
subjectAltName = @alt_names

[alt_names]
DNS.1 = mosquitto
DNS.2 = localhost
IP.1 = 127.0.0.1
EOF

openssl req -new \
  -key server.key \
  -out server.csr \
  -config server_ext.cnf

echo "--> Step 3: Signing Server Certificate with CA..."
openssl x509 -req \
  -in server.csr \
  -CA ca.crt \
  -CAkey ca.key \
  -CAcreateserial \
  -out server.crt \
  -days 365 \
  -extfile server_ext.cnf \
  -extensions v3_req

# ---------------------------------------------------------------------------
# 3. FIELD NODE ESP32/NORVI (Client Certs)
# ---------------------------------------------------------------------------
echo "--> Step 4: Creating Client Device Key and CSR..."
openssl genrsa -out client.key 2048

openssl req -new \
  -key client.key \
  -out client.csr \
  -subj "/C=MX/ST=Sonora/L=Hermosillo/O=Novamex/OU=IT/CN=rpi_gateway_01"

echo "--> Step 5: Signing Client Certificate..."
openssl x509 -req \
  -in client.csr \
  -CA ca.crt \
  -CAkey ca.key \
  -CAcreateserial \
  -out client.crt \
  -days 365

# ---------------------------------------------------------------------------
# 4. SECURE PERMISSIONS
# ---------------------------------------------------------------------------
echo "--> Step 6: Hardening file read/write permissions..."
chmod 644 ca.crt server.crt client.crt
chmod 600 ca.key server.key client.key ca.srl

# Clean up signing requests and configs
rm -f server.csr client.csr server_ext.cnf

echo "================================================================="
echo "PKI Certificate Generation COMPLETE!"
echo "Files created in ${CERT_DIR}:"
ls -la
echo "================================================================="
