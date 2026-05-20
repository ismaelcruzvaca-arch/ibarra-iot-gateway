# RV1106 Cross-Compilation Toolchain
# Target: armhf / uClibc (Rockchip RV1106 IoT SoC)

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER   arm-rockchip830-linux-uclibcgnueabihf-gcc)
set(CMAKE_CXX_COMPILER arm-rockchip830-linux-uclibcgnueabihf-g++)

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
