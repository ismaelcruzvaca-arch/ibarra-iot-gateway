// ============================================================================
// ModbusRTUMaster.hpp — Modbus RTU Master Protocol Engine
// ============================================================================
//
// Provides read_input_registers() (FC04) over an ISerialPort abstraction.
// Includes CRC-16/Modbus validation, exception-code detection, and a
// lightweight exception hierarchy for error reporting.
//
// Namespace: ibarra::modbus — matches existing ibarra::telemetry convention.
// ============================================================================

#pragma once

#include "ISerialPort.hpp"

#include <cstdint>
#include <vector>
#include <stdexcept>
#include <string>

namespace ibarra {
namespace modbus {

// ==========================================================================
// Exception Hierarchy
// ==========================================================================

struct ModbusError : public std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct ModbusCRCError : public ModbusError {
    using ModbusError::ModbusError;
};

struct ModbusTimeoutError : public ModbusError {
    using ModbusError::ModbusError;
};

struct ModbusResponseError : public ModbusError {
    uint8_t code;
    explicit ModbusResponseError(uint8_t c)
        : ModbusError("Modbus exception code " + std::to_string(c))
        , code(c) {}
};

// ==========================================================================
// CRC-16/Modbus — exposed for direct unit testing (Phase 3)
// ==========================================================================
//
// Polynomial: 0xA001 (reflected)
// Initial value: 0xFFFF
// No final XOR
// LSB-first (bitwise loop processes LSB of each byte first)

uint16_t crc16_modbus(const std::vector<uint8_t>& data);

// ==========================================================================
// ModbusRTUMaster
// ==========================================================================

class ModbusRTUMaster {
public:
    explicit ModbusRTUMaster(ISerialPort& port);

    /// Read input registers (Function Code 04).
    /// @param slave_id   Modbus slave address (1–247).
    /// @param start_addr Starting register address (0-indexed).
    /// @param quantity   Number of 16-bit registers to read.
    /// @return Vector of register values (big-endian decoded).
    /// @throws ModbusCRCError      on CRC mismatch in response.
    /// @throws ModbusTimeoutError  on serial read timeout.
    /// @throws ModbusResponseError on Modbus exception response.
    std::vector<uint16_t> read_input_registers(
        uint8_t slave_id,
        uint16_t start_addr,
        uint16_t quantity);

private:
    ISerialPort& port_;

    /// Append CRC-16/LSB-first to frame (mutates in place).
    void append_crc(std::vector<uint8_t>& frame) const;

    /// Validate trailing CRC-16 on a received frame.
    /// @return true if CRC is valid.
    bool validate_crc(const std::vector<uint8_t>& frame) const;

    /// Parse big-endian register values from a valid response payload.
    static std::vector<uint16_t> parse_registers(
        const std::vector<uint8_t>& payload,
        uint8_t byte_count,
        uint16_t expected_quantity);
};

} // namespace modbus
} // namespace ibarra
