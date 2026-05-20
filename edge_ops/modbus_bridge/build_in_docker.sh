#!/usr/bin/env bash
# =============================================================================
# build_in_docker.sh — Cross-compile modbus_bridge for RV1106 inside Docker
# =============================================================================
#
# Usage: ./build_in_docker.sh [host|cross]
#   host  — native build + run tests (default)
#   cross — cross-compile for RV1106 armhf/uClibc
#
# Prerequisites: Docker installed and running.
# =============================================================================

set -euo pipefail

MODE="${1:-host}"
IMAGE="modbus-bridge-crossbuilder"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "==> Building Docker image: ${IMAGE}"
docker build -t "${IMAGE}" -f "${SCRIPT_DIR}/Dockerfile.crossbuilder" "${SCRIPT_DIR}"

case "${MODE}" in
    host)
        echo "==> Host mode: configure, build, and run tests"
        docker run --rm -v "${SCRIPT_DIR}:/workspace" "${IMAGE}" bash -c "
            mkdir -p build/host && cd build/host &&
            cmake ../.. -DCROSSCOMPILE=OFF &&
            cmake --build . &&
            ctest --output-on-failure
        "
        ;;
    cross)
        echo "==> Cross-compile mode: RV1106 armhf/uClibc"
        docker run --rm -v "${SCRIPT_DIR}:/workspace" "${IMAGE}" bash -c "
            mkdir -p build/cross && cd build/cross &&
            cmake ../.. \
                -DCROSSCOMPILE=ON \
                -DCMAKE_TOOLCHAIN_FILE=../../cmake/rv1106_toolchain.cmake &&
            cmake --build .
        "
        ;;
    *)
        echo "Usage: $0 [host|cross]"
        exit 1
        ;;
esac

echo "==> Done."
