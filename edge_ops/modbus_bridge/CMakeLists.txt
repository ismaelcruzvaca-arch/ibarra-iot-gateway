# ---------------------------------------------------------------------------
# Modbus RTU Bridge — GEMA V3.0 Edge Module
#
# Builds the modbus_bridge static library and test executable.
# Protobuf is a REQUIRED dependency — the build aborts if not found.
# ---------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.16)
project(modbus-bridge VERSION 1.0.0 LANGUAGES CXX)

# ---- C++ Standard -------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- Protobuf (REQUIRED) ------------------------------------------------
find_package(Protobuf REQUIRED)

# ---- Library ------------------------------------------------------------
add_library(modbus_bridge
    src/ModbusRTUMaster.cpp
)

target_include_directories(modbus_bridge
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# ---- Proto generation ---------------------------------------------------
file(GLOB PROTO_FILES proto/*.proto)

set(PROTO_GEN_DIR "${CMAKE_BINARY_DIR}/generated/proto")
file(MAKE_DIRECTORY "${PROTO_GEN_DIR}")

foreach(PROTO_FILE ${PROTO_FILES})
    get_filename_component(PROTO_NAME ${PROTO_FILE} NAME_WE)
    set(PROTO_SRC "${PROTO_GEN_DIR}/${PROTO_NAME}.pb.cc")
    set(PROTO_HDR "${PROTO_GEN_DIR}/${PROTO_NAME}.pb.h")

    add_custom_command(
        OUTPUT  ${PROTO_SRC} ${PROTO_HDR}
        COMMAND ${Protobuf_PROTOC_EXECUTABLE}
            --cpp_out=${PROTO_GEN_DIR}
            -I${CMAKE_CURRENT_SOURCE_DIR}/proto
            ${PROTO_FILE}
        DEPENDS ${PROTO_FILE}
        COMMENT "Generating protobuf sources for ${PROTO_NAME}"
    )

    target_sources(modbus_bridge PRIVATE ${PROTO_SRC})
    target_include_directories(modbus_bridge
        PUBLIC ${PROTO_GEN_DIR}
    )
endforeach()

target_link_libraries(modbus_bridge
    PUBLIC
        protobuf::libprotobuf
)

# ---- Tests (Catch2) -----------------------------------------------------
option(BUILD_TESTS "Build unit tests" ON)

if(BUILD_TESTS)
    find_package(Catch2 QUIET)
    if(Catch2_FOUND)
        add_executable(modbus_bridge_test
            test/test_modbus.cpp
        )
        target_link_libraries(modbus_bridge_test
            PRIVATE
                modbus_bridge
                Catch2::Catch2WithMain
        )
        add_test(NAME modbus_bridge_test COMMAND modbus_bridge_test)
    else()
        message(WARNING "Catch2 not found — tests will NOT be built. "
                        "Install Catch2 or set -DBUILD_TESTS=OFF")
    endif()
endif()
