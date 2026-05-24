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

# ---- Dependencies ---------------------------------------------------------
find_package(OpenCV REQUIRED)
find_package(GTest REQUIRED)

# ---- Library (orchestrator implementation) --------------------------------
add_library(gema_vision_lib
    src/inference_orchestrator.cpp
    src/data_collector_engine.cpp
)

target_include_directories(gema_vision_lib
    PUBLIC  include
    PRIVATE src
)

target_link_libraries(gema_vision_lib
    PUBLIC  ${OpenCV_LIBS}
)

# ---- Unit Tests (SIL) ----------------------------------------------------
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

# Register test with CTest.
enable_testing()
add_test(NAME VisionPipeline
    COMMAND test_vision_pipeline
)

# ---- Compiler warnings (optional, helps catch bugs early) -----------------
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(gema_vision_lib PRIVATE
        -Wall -Wextra -Wpedantic -Werror
    )
    target_compile_options(test_vision_pipeline PRIVATE
        -Wall -Wextra -Wpedantic -Werror
    )
endif()
