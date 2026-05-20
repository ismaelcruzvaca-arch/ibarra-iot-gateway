# Cross-Compilation Toolchain Specification

## Purpose

Defines the CMake dual-mode build system for the Modbus-HaLow bridge module, enabling host-side Catch2 tests on x86_64 and cross-compiled production binaries for the RV1106 (armhf/uClibc) target.

## Immutable Constraints

| Area | Constraint | Status |
|------|-----------|--------|
| **Target Triple** | The cross-compiler prefix MUST be `arm-rockchip830-linux-uclibcgnueabihf`. | **IMMUTABLE** |
| **C++ Standard** | CMake MUST set `CMAKE_CXX_STANDARD=17` and `CMAKE_CXX_STANDARD_REQUIRED=ON`. | **IMMUTABLE** |
| **Try-Compile Guard** | `CMAKE_TRY_COMPILE_TARGET_TYPE` MUST be set to `STATIC_LIBRARY` when cross-compiling. | **IMMUTABLE** |
| **Mode Toggle** | The `CROSSCOMPILE` cache variable MUST gate host-test vs. cross-compile targets. | **IMMUTABLE** |
| **Test Framework** | Host-side tests MUST use Catch2 (single-header, vendored in `tests/`). | **IMMUTABLE** |
| **TDD Guard** | Test targets MUST compile and register with CTest before production targets are linked. | **IMMUTABLE** |

## Requirements

### Requirement: RV1106 Toolchain File

The system MUST provide a CMake toolchain file at `cmake/rv1106_toolchain.cmake` that sets `CMAKE_SYSTEM_NAME=Linux`, `CMAKE_SYSTEM_PROCESSOR=arm`, and points `CMAKE_C_COMPILER` and `CMAKE_CXX_COMPILER` to the `arm-rockchip830-linux-uclibcgnueabihf-gcc/g++` binaries.

#### Scenario: Toolchain sets correct system identifiers

- GIVEN the `cmake/rv1106_toolchain.cmake` file is loaded by CMake
- WHEN `CMAKE_SYSTEM_NAME` and `CMAKE_SYSTEM_PROCESSOR` are inspected
- THEN `CMAKE_SYSTEM_NAME` MUST be `Linux` and `CMAKE_SYSTEM_PROCESSOR` MUST be `arm`

#### Scenario: Toolchain points to correct compilers

- GIVEN the `cmake/rv1106_toolchain.cmake` file is loaded
- WHEN `CMAKE_C_COMPILER` and `CMAKE_CXX_COMPILER` are resolved
- THEN `CMAKE_C_COMPILER` MUST end with `arm-rockchip830-linux-uclibcgnueabihf-gcc` and `CMAKE_CXX_COMPILER` MUST end with `arm-rockchip830-linux-uclibcgnueabihf-g++`

#### Scenario: Try-compile target type prevents link failures

- GIVEN the `cmake/rv1106_toolchain.cmake` file is loaded for cross-compilation
- WHEN `CMAKE_TRY_COMPILE_TARGET_TYPE` is inspected
- THEN it MUST equal `STATIC_LIBRARY` — this prevents CMake from attempting to link executables with the cross-compiler during `try_compile` checks

### Requirement: C++17 Standard Enforcement

All CMake targets in the project MUST compile with C++17. `CMAKE_CXX_STANDARD` MUST be set to 17 and `CMAKE_CXX_STANDARD_REQUIRED` MUST be ON.

#### Scenario: C++17 is the enforced standard

- GIVEN the root `CMakeLists.txt` is processed
- WHEN any C++ target is compiled (host or cross-compile)
- THEN the compiler MUST receive the `-std=c++17` flag and compilation MUST fail if the compiler does not support C++17

### Requirement: Dual-Mode Build with CROSSCOMPILE Toggle

The root `CMakeLists.txt` MUST support two mutually exclusive modes controlled by the `CROSSCOMPILE` cache variable. When `CROSSCOMPILE=OFF` (default), host test targets are built. When `CROSSCOMPILE=ON`, the toolchain file is loaded and only production cross-compile targets are built.

#### Scenario: Host mode builds test targets

- GIVEN the source directory is configured with `cmake -DCROSSCOMPILE=OFF ..`
- WHEN CMake configures the project
- THEN Catch2 test targets MUST be defined, CTest MUST register the test suite, and the host compiler MUST be used

#### Scenario: Cross-compile mode builds production targets only

- GIVEN the source directory is configured with `cmake -DCROSSCOMPILE=ON -DCMAKE_TOOLCHAIN_FILE=cmake/rv1106_toolchain.cmake ..`
- WHEN CMake configures the project
- THEN only production proto-generated targets MUST be defined, no Catch2 test targets, and the cross-compiler MUST be used

#### Scenario: Cross-compile mode excludes test executables

- GIVEN the source directory is configured with `CROSSCOMPILE=ON`
- WHEN CMake processes the `tests/` subdirectory
- THEN `add_subdirectory` for `tests/` MUST be skipped (guarded by `NOT CROSSCOMPILE`)

### Requirement: Protobuf Code Generation Integration

When `CROSSCOMPILE=ON`, CMake MUST invoke `protobuf_generate_cpp` to compile `proto/telemetry.proto` into `*.pb.cc` and `*.pb.h` sources. Generated sources MUST be excluded from the git-tracked source tree.

#### Scenario: Proto generation creates pb.cc and pb.h files

- GIVEN the source directory is configured with `CROSSCOMPILE=ON`
- WHEN CMake processes the `protobuf_generate_cpp` directive
- THEN `telemetry.pb.cc` and `telemetry.pb.h` MUST be generated in the build directory and compiled into the production target

#### Scenario: Generated protobuf files are gitignored

- GIVEN the project's `.gitignore`
- WHEN patterns for `*.pb.cc` and `*.pb.h` are checked
- THEN these patterns MUST be present, preventing generated protobuf files from being committed

### Requirement: CTest Integration for Host Mode

When `CROSSCOMPILE=OFF`, CMake MUST register all Catch2 test cases with CTest so that `ctest --test-dir build` discovers and runs them.

#### Scenario: CTest discovers all Catch2 test cases

- GIVEN a host-mode build configured with `CROSSCOMPILE=OFF`
- WHEN `ctest --test-dir build` is executed
- THEN all Catch2 test cases from `test_protobuf_contract.cpp` MUST be discovered and run

#### Scenario: CTest reports failures accurately

- GIVEN a host-mode build where one Catch2 test case fails
- WHEN `ctest --test-dir build` is executed
- THEN CTest MUST report the failure and return a non-zero exit code

### Requirement: Cross-Compiler Binary Availability Check

CMake MUST verify that the cross-compiler binaries exist on the system before attempting cross-compilation. If the binaries are not found, CMake MUST emit a fatal error with a descriptive message.

#### Scenario: Missing cross-compiler causes configuration failure

- GIVEN a system where `arm-rockchip830-linux-uclibcgnueabihf-gcc` is not in PATH or the configured location
- WHEN CMake is configured with `CROSSCOMPILE=ON` and the toolchain file
- THEN CMake MUST fail with a `FATAL_ERROR` message indicating the cross-compiler was not found