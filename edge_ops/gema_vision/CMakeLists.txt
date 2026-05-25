cmake_minimum_required(VERSION 3.16)

# ============================================================================
# Project: gema_vision
# Description:  C++ vision inference pipeline for Rockchip RV1106.
#               RAII + DI + Producer-Consumer + SIL tests.
# ============================================================================

project(gema_vision
    VERSION 0.1.0
    DESCRIPTION "GEMA V3.0 — Vision inference pipeline"
    LANGUAGES CXX
)

# ---- C++ Standard ---------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ---- Options ---------------------------------------------------------------
option(USE_MOCK_PRODUCER
    "Use MockVideoProducer instead of RgaVideoProducer for gema-vision"
    ON)

option(QEMU_TEST_MODE
    "Force mock implementations for all hardware (NPU, RGA, camera) even
     when compiling for armhf. Required for running armhf SIL tests under
     QEMU user-mode emulation in CI, where no physical Rockchip hardware
     is available."
    OFF)

# ---- Dependencies ---------------------------------------------------------
find_package(OpenCV REQUIRED)
find_package(GTest REQUIRED)

# ---- Library (shared components) ------------------------------------------
add_library(gema_vision_lib
    src/inference_orchestrator.cpp
    src/data_collector_engine.cpp
    src/watchdog.cpp
    src/spatial_calibrator.cpp
    src/thermal_monitor.cpp
    src/color_validator.cpp
    src/postproc_dispatcher.cpp
    src/lprnet_ocr_engine.cpp
    src/telemetry_collector.cpp
)

target_include_directories(gema_vision_lib
    PUBLIC  include
    PRIVATE src
)

target_link_libraries(gema_vision_lib
    PUBLIC  ${OpenCV_LIBS}
)

# ---- Production daemon binary ---------------------------------------------
add_executable(gema-vision
    src/main.cpp
)

target_include_directories(gema-vision
    PRIVATE include
)

if(USE_MOCK_PRODUCER)
    target_compile_definitions(gema-vision PRIVATE USE_MOCK_PRODUCER=1)
endif()


target_link_libraries(gema-vision
    PRIVATE
        gema_vision_lib
        ${OpenCV_LIBS}
)

# ---- SIL tests: vision pipeline -------------------------------------------
add_executable(test_vision_pipeline
    test/test_vision_pipeline.cpp
)

target_include_directories(test_vision_pipeline
    PRIVATE include
)

target_link_libraries(test_vision_pipeline
    PRIVATE
        gema_vision_lib
        ${OpenCV_LIBS}
        GTest::GTest
        GTest::Main
)

# ---- SIL tests: watchdog --------------------------------------------------
add_executable(test_watchdog
    test/test_watchdog.cpp
)

target_include_directories(test_watchdog
    PRIVATE include
)

target_link_libraries(test_watchdog
    PRIVATE
        gema_vision_lib
        GTest::GTest
        GTest::Main
)

# ---- OTA agent binary -----------------------------------------------------
add_executable(gema-ota
    src/ota_manager.cpp
    src/main_ota.cpp
)

target_include_directories(gema-ota
    PRIVATE include
)

target_link_libraries(gema-ota
    PRIVATE
        gema_vision_lib
        ${OpenCV_LIBS}
)

# ---- SIL tests: OTA -------------------------------------------------------
add_executable(test_ota
    src/ota_manager.cpp
    test/test_ota.cpp
)

target_include_directories(test_ota
    PRIVATE include
)

target_link_libraries(test_ota
    PRIVATE
        gema_vision_lib
        ${OpenCV_LIBS}
        GTest::GTest
        GTest::Main
)

# ---- SIL tests: VideoProducer -------------------------------------------
add_executable(test_video_producer
    test/test_video_producer.cpp
)

target_include_directories(test_video_producer
    PRIVATE include
)

target_link_libraries(test_video_producer
    PRIVATE
        gema_vision_lib
        ${OpenCV_LIBS}
        GTest::GTest
        GTest::Main
)

# ---- SIL tests: Telemetry -----------------------------------------------
add_executable(test_telemetry
    test/test_telemetry.cpp
)

target_include_directories(test_telemetry
    PRIVATE include
)

target_link_libraries(test_telemetry
    PRIVATE
        gema_vision_lib
        ${OpenCV_LIBS}
        GTest::GTest
        GTest::Main
)

# ---- Register tests with CTest --------------------------------------------
enable_testing()
add_test(NAME VideoProducer
    COMMAND test_video_producer
)
add_test(NAME Telemetry
    COMMAND test_telemetry
)
add_test(NAME VisionPipeline
    COMMAND test_vision_pipeline
)
add_test(NAME Watchdog
    COMMAND test_watchdog
)
add_test(NAME OtaAgent
    COMMAND test_ota
)

# ---- Compiler warnings ----------------------------------------------------
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(gema_vision_lib PRIVATE
        -Wall -Wextra -Wpedantic -Werror
    )
    target_compile_options(test_vision_pipeline PRIVATE
        -Wall -Wextra -Wpedantic -Werror
    )
    target_compile_options(test_watchdog PRIVATE
        -Wall -Wextra -Wpedantic -Werror
    )
    target_compile_options(gema-vision PRIVATE
        -Wall -Wextra -Wpedantic -Werror
    )
    target_compile_options(gema-ota PRIVATE
        -Wall -Wextra -Wpedantic -Werror
    )
    target_compile_options(test_ota PRIVATE
        -Wall -Wextra -Wpedantic -Werror
    )
    target_compile_options(test_video_producer PRIVATE
        -Wall -Wextra -Wpedantic -Werror
    )
    target_compile_options(test_telemetry PRIVATE
        -Wall -Wextra -Wpedantic -Werror
    )
endif()

# ---- QEMU test mode (force mocks on ALL targets) --------------------------
# This block MUST appear AFTER all targets are defined.
# QEMU_TEST_MODE forces mock implementations for every component so that
# SIL binaries can run without physical hardware (NPU, RGA, camera).
# In native x86_64 builds, this is a no-op (USE_MOCK_PRODUCER already ON).
# In armhf builds under QEMU, it prevents crashes from missing hardware.
if(QEMU_TEST_MODE)
    target_compile_definitions(gema_vision_lib PRIVATE QEMU_TEST_MODE=1)
    target_compile_definitions(gema-vision PRIVATE QEMU_TEST_MODE=1)
    target_compile_definitions(gema-ota PRIVATE QEMU_TEST_MODE=1)
    target_compile_definitions(test_vision_pipeline PRIVATE QEMU_TEST_MODE=1)
    target_compile_definitions(test_watchdog PRIVATE QEMU_TEST_MODE=1)
    target_compile_definitions(test_ota PRIVATE QEMU_TEST_MODE=1)
    target_compile_definitions(test_video_producer PRIVATE QEMU_TEST_MODE=1)
    target_compile_definitions(test_telemetry PRIVATE QEMU_TEST_MODE=1)
endif()
