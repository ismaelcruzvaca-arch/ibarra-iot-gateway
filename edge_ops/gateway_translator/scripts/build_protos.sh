#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# build_protos.sh — Generate Python Protobuf stubs from telemetry.proto
#
# Usage:
#   ./scripts/build_protos.sh
#
# Requires: protoc (Protobuf compiler) in PATH
# Output:   src/telemetry_pb2.py
# ---------------------------------------------------------------------------
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

protoc \
    --python_out="$PROJECT_DIR/src/" \
    --proto_path="$PROJECT_DIR/proto/" \
    "$PROJECT_DIR/proto/telemetry.proto"

echo "✓ Generated src/telemetry_pb2.py"
