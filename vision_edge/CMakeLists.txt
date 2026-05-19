cmake_minimum_required(VERSION 3.14)
project(VisionEdge VERSION 1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Base optimizations
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -Wall -Wextra")

# Hardware-specific flags for ARM64 NEON optimization
if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64" OR CMAKE_SYSTEM_PROCESSOR MATCHES "arm64")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=armv8-a+simd")
    add_compile_definitions(__aarch64__)
endif()

# We simulate external dependencies using interface libraries for SIL validation without heavy downloads
add_library(OpenCV INTERFACE)
add_library(PahoMqtt INTERFACE)
add_library(GTest INTERFACE)
add_library(GMock INTERFACE)

# Main Executable
add_executable(vision_edge_daemon
    src/main.cpp
    src/engine.cpp
    src/watchdog.cpp
    src/mqtt_client.cpp
)
target_link_libraries(vision_edge_daemon PRIVATE OpenCV PahoMqtt)

# Test Executable
add_executable(vision_edge_tests
    test/engine_test.cpp
    test/mqtt_mock_test.cpp
    src/engine.cpp
    src/mqtt_client.cpp
)
target_link_libraries(vision_edge_tests PRIVATE OpenCV PahoMqtt GTest GMock)

enable_testing()
add_test(NAME VisionEdgeUnitTests COMMAND vision_edge_tests)
