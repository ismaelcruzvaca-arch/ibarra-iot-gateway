# ============================================================================
# toolchain-arm-rockchip.cmake
#
# CMake toolchain file for cross-compiling to Rockchip RV1106
# (armhf, uClibc).  Designed for use with the Luckfox SDK toolchain:
#
#   arm-rockchip830-linux-uclibcgnueabihf-gcc
#
# Usage:
#   cmake -B build \
#       -DCMAKE_TOOLCHAIN_FILE=toolchain-arm-rockchip.cmake \
#       -DQEMU_TEST_MODE=ON \
#       -DUSE_MOCK_PRODUCER=ON
#
# The QEMU_TEST_MODE flag forces mock implementations for all hardware
# (NPU, RGA, camera) so that armhf SIL binaries can run under QEMU
# user-mode emulation in CI without requiring physical Rockchip hardware.
# ============================================================================

# ---- Target system ---------------------------------------------------------
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

# ---- Cross-compilation tools -----------------------------------------------
set(CMAKE_C_COMPILER   arm-rockchip830-linux-uclibcgnueabihf-gcc)
set(CMAKE_CXX_COMPILER arm-rockchip830-linux-uclibcgnueabihf-g++)

set(CMAKE_AR       arm-rockchip830-linux-uclibcgnueabihf-ar)
set(CMAKE_RANLIB   arm-rockchip830-linux-uclibcgnueabihf-ranlib)
set(CMAKE_STRIP    arm-rockchip830-linux-uclibcgnueabihf-strip)
set(CMAKE_OBJCOPY  arm-rockchip830-linux-uclibcgnueabihf-objcopy)

# ---- Find root (toolchain sysroot) -----------------------------------------
# The Luckfox SDK installs its sysroot under the toolchain directory.
# CMAKE_FIND_ROOT_PATH should point to the toolchain's sysroot so that
# find_package and find_library search there first.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# ---- Compiler flags --------------------------------------------------------
set(CMAKE_CXX_FLAGS_INIT "-march=armv7-a -mtune=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard -D__arm__")
set(CMAKE_C_FLAGS_INIT   "-march=armv7-a -mtune=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard -D__arm__")

# ---- GTest root (cross-compiled) -------------------------------------------
# GTest must be pre-compiled for armhf/uClibc.  Set GTEST_ROOT to the
# installation prefix of the cross-compiled GTest.
# Example:
#   cmake -B build -DGTEST_ROOT=/opt/arm-gtest ...
if(DEFINED GTEST_ROOT)
    set(GTEST_ROOT "${GTEST_ROOT}" CACHE PATH "Cross-compiled GTest root")
endif()

# ---- OpenCV root (comes with Luckfox SDK) ----------------------------------
# OpenCV-Mobile is installed by the Luckfox SDK at a known path.
# If find_package(OpenCV) fails, point here:
# set(OpenCV_DIR /opt/luckfox-sdk/media/opencv-mobile/lib/cmake/opencv4)

# ---- pkg-config (optional, for cross deps) ---------------------------------
set(PKG_CONFIG_EXECUTABLE arm-rockchip830-linux-uclibcgnueabihf-pkg-config)
