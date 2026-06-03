# ADR-001: RV1106 Cross-Compilation Toolchain Deferred

**Status:** Accepted (Deferred)
**Date:** 2026-05-20
**Deciders:** Ismael Cruz Vaca
**Tags:** infrastructure, cross-compilation, rv1106, toolchain, debt

---

## Context

The Modbus RTU Bridge (`edge_ops/modbus_bridge/`) targets dual-platform deployment:

- **Phase A — Host (x86_64):** Compilation and testing on Ubuntu 22.04. All 7 tests pass
  (56 assertions) on the host builder image.
- **Phase B — Target (ARM RV1106):** Cross-compilation for the Rockchip RV1106 SoC
  (ARM Cortex-A7, uclibc) used in NORVI devices.

Phase B requires the toolchain `arm-rockchip830-linux-uclibcgnueabihf`, which is part of
Rockchip's SDK distribution. This tarball is **not available from any public package
repository or direct download URL** — it must be obtained manually from the Rockchip /
Luckfox partner portal.

## Decision

**Defer Phase B** until the toolchain SDK is manually obtained. All infrastructure is in
place:

- `cmake/rv1106_toolchain.cmake` — CMake toolchain file (stub ready)
- `Dockerfile.crossbuilder` — Cross-build Docker image (toolchain extraction commented out)
- The host build (`Phase A`) is **not blocked** and continues to serve for development,
  integration testing, and CI validation.

## Consequences

### Positive
- Development velocity on the Modbus Bridge logic is unblocked.
- All Modbus protocol logic, MQTT abstraction, and Protobuf serialization are validated
  on x86_64 before touching ARM.
- The Dockerfile.crossbuilder acts as living documentation of what the cross-build
  environment requires.

### Negative
- Binaries for RV1106 are not yet produced.
- End-to-end hardware testing on NORVI devices cannot happen until Phase B is unblocked.
- Risk of toolchain-specific issues (endianness, library compatibility) being discovered
  later in the cycle.

## Unblocking Instructions

When the Rockchip / Luckfox SDK tarball is available:

1. Place the toolchain tarball (e.g., `arm-rockchip830-linux-uclibcgnueabihf.tar.xz`) in
   a directory accessible to Docker (or download it inside the build step).
2. Uncomment the `ADD` / `COPY` and extraction steps in
   `edge_ops/modbus_bridge/Dockerfile.crossbuilder`.
3. Set the toolchain path in `cmake/rv1106_toolchain.cmake` to match the extraction
   location inside the image.
4. Build with:
   ```bash
   docker build -f Dockerfile.crossbuilder -t rv1106-builder .
   docker run --rm -v //$(pwd):/workspace rv1106-builder bash -c "cd /workspace && cmake -B build_rv1106 -DCMAKE_TOOLCHAIN_FILE=cmake/rv1106_toolchain.cmake && make -C build_rv1106"
   ```

## References

- `edge_ops/modbus_bridge/Dockerfile.crossbuilder`
- `edge_ops/modbus_bridge/cmake/rv1106_toolchain.cmake`
- Rockchip SDK documentation (portal required)
