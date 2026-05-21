# ---------------------------------------------------------------------------
# Toolchain file for Rockchip RV1106 (ARM Cortex-A7)
# Cross-compiler: arm-rockchip830-linux-uclibcgnueabihf
# ---------------------------------------------------------------------------

# Target system
set(CMAKE_SYSTEM_NAME               Linux)
set(CMAKE_SYSTEM_PROCESSOR          arm)
set(CMAKE_SYSTEM_VERSION            1)

# Cross compiler prefixes
set(CMAKE_C_COMPILER                arm-rockchip830-linux-uclibcgnueabihf-gcc)
set(CMAKE_CXX_COMPILER              arm-rockchip830-linux-uclibcgnueabihf-g++)

# Static library try-compile (avoids trying to link executables without a sysroot)
set(CMAKE_TRY_COMPILE_TARGET_TYPE   STATIC_LIBRARY)

# Find programs on the host
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)

# Libraries and headers come from the cross toolchain
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
