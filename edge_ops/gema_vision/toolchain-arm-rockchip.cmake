# ============================================================================
# toolchain-arm-rockchip.cmake
#
# CMake toolchain file for cross-compiling to Rockchip RV1106 (armhf).
#
# ## Two toolchain profiles
#
#   CI / QEMU_TEST_MODE builds:
#     Uses arm-linux-gnueabihf-gcc from Ubuntu apt (glibc-based).
#     Works under QEMU user-mode emulation.  Available as:
#       apt install g++-arm-linux-gnueabihf
#
#   Production builds (RV1106 uClibc):
#     Needs Rockchip's uClibc toolchain from the Luckfox SDK:
#       arm-rockchip830-linux-uclibcgnueabihf-gcc
#     Install by cloning https://github.com/LuckfoxTECH/luckfox-pico.git
#     and sourcing tools/linux/toolchain/arm-rockchip830-linux-uclibcgnueabihf/env_install_toolchain.sh
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
# Try the Rockchip uClibc toolchain first (production), fall back to
# arm-linux-gnueabihf (CI / QEMU test builds).
#
# To switch to the Rockchip toolchain for production builds, change these
# to:
#   set(CMAKE_C_COMPILER   arm-rockchip830-linux-uclibcgnueabihf-gcc)
#   set(CMAKE_CXX_COMPILER arm-rockchip830-linux-uclibcgnueabihf-g++)
#
# The arm-linux-gnueabihf variant is the default because it's available
# from Ubuntu apt and works under QEMU for SIL testing.

find_program(CMAKE_C_COMPILER   NAMES arm-rockchip830-linux-uclibcgnueabihf-gcc
                                 arm-linux-gnueabihf-gcc)
find_program(CMAKE_CXX_COMPILER NAMES arm-rockchip830-linux-uclibcgnueabihf-g++
                                 arm-linux-gnueabihf-g++)

if(NOT CMAKE_CXX_COMPILER)
    message(FATAL_ERROR "No ARM cross-compiler found. "
        "Install g++-arm-linux-gnueabihf (apt) or set up the Rockchip SDK.")
endif()

message(STATUS "Using C++ compiler: ${CMAKE_CXX_COMPILER}")
message(STATUS "Using C compiler:   ${CMAKE_C_COMPILER}")

# ---- Compiler flags --------------------------------------------------------
set(CMAKE_CXX_FLAGS_INIT "-march=armv7-a -mtune=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard -D__arm__")
set(CMAKE_C_FLAGS_INIT   "-march=armv7-a -mtune=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard -D__arm__")

# ---- Find root -------------------------------------------------------------
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# ---- GTest root (cross-compiled) -------------------------------------------
if(DEFINED GTEST_ROOT)
    set(GTEST_ROOT "${GTEST_ROOT}" CACHE PATH "Cross-compiled GTest root")
endif()

# ---- OpenCV (disabled in QEMU_TEST_MODE) -----------------------------------
# In QEMU_TEST_MODE, frame_pool.hpp is the only OpenCV dependency in the
# library, and it's header-only.  The test executables link against OpenCV
# but frames are created via cv::Mat::zeros which doesn't need video I/O.
# If find_package(OpenCV) fails below, the build will error.
# For CI without OpenCV, set -DOpenCV_DIR to a cross-compiled OpenCV path.
