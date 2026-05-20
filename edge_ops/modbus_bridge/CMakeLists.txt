cmake_minimum_required(VERSION 3.10)
project(modbus_bridge LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Protobuf dependency (required for all targets)
find_package(Protobuf REQUIRED)

# Generated protobuf sources from proto/telemetry.proto
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS proto/telemetry.proto)

# Cross-compilation toggle: OFF = host, ON = RV1106 armhf/uClibc
option(CROSSCOMPILE "Cross-compile for RV1106" OFF)

if(CROSSCOMPILE)
    message(STATUS "modbus_bridge: cross-compiling for RV1106 (armhf/uClibc)")

    # Production static library
    add_library(modbus_bridge_lib STATIC
        ${PROTO_SRCS}
        # src/ files will be added here as the module grows
    )
    target_include_directories(modbus_bridge_lib PUBLIC
        ${CMAKE_CURRENT_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(modbus_bridge_lib PUBLIC protobuf::libprotobuf)
else()
    message(STATUS "modbus_bridge: host mode (native build)")

    # Test executable (host mode only)
    add_executable(test_runner
        ${PROTO_SRCS}
        tests/test_protobuf_contract.cpp
        tests/catch2/catch_amalgamated.cpp
    )
    target_include_directories(test_runner PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )
    target_link_libraries(test_runner PRIVATE protobuf::libprotobuf)

    # CTest integration
    enable_testing()
    add_test(NAME protobuf_contract COMMAND test_runner)
endif()
