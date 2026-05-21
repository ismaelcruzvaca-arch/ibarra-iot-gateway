#ifndef MODBUSRTUMASTER_HPP
#define MODBUSRTUMASTER_HPP

#include <cstdint>
#include <vector>
#include <stdexcept>
#include <string>

#include "ISerialPort.hpp"

namespace modbus_bridge {

// ---------------------------------------------------------------------------
// Custom exceptions
// ---------------------------------------------------------------------------

class ModbusException : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class ModbusTimeout : public ModbusException {
public:
    explicit ModbusTimeout()
        : ModbusException("Modbus RTU: response timeout") {}
};

class ModbusCRCError : public ModbusException {
public:
    explicit ModbusCRCError()
        : ModbusException("Modbus RTU: CRC mismatch in response") {}
};

class ModbusFrameError : public ModbusException {
public:
    explicit ModbusFrameError(const std::string& detail)
        : ModbusException("Modbus RTU: frame error — " + detail) {}
};

class ModbusExceptionResponse : public ModbusException {
public:
    explicit ModbusExceptionResponse(std::uint8_t exception_code)
        : ModbusException("Modbus RTU: exception response 0x"
                          + to_hex(exception_code))
        , code_(exception_code) {}

    std::uint8_t code() const noexcept { return code_; }

    /// Convert a byte to a 2-character hex string (e.g. 0x04 → "04").
    static std::string to_hex(std::uint8_t v);

private:
    std::uint8_t code_;
};

// ---------------------------------------------------------------------------
// ModbusRTUMaster — high-level Modbus RTU master for Function Code 04
//
// Uses dependency injection of ISerialPort so it works with real hardware
// (NORVI nodes via real serial port) and unit tests (MockSerialPort).
// ---------------------------------------------------------------------------
class ModbusRTUMaster {
public:
    /// @param port  Reference to the serial port (injected).
    ///              Must outlive this instance.
    explicit ModbusRTUMaster(ISerialPort& port);

    /// Read N consecutive input registers (Function Code 0x04).
    ///
    /// @param slave_id   Modbus slave address (1–247).
    /// @param start_addr Starting register address (0-based).
    /// @param count      Number of 16-bit registers to read (1–125).
    /// @return           Vector of register values.
    ///
    /// @throws ModbusTimeout           No response within timeout.
    /// @throws ModbusCRCError          Response CRC does not match.
    /// @throws ModbusFrameError        Response length or format is invalid.
    /// @throws ModbusExceptionResponse Slave returned an exception.
    std::vector<std::uint16_t> read_input_registers(
        std::uint8_t  slave_id,
        std::uint16_t start_addr,
        std::uint16_t count);

private:
    ISerialPort& port_;

    // ---- CRC-16 (Modbus) ------------------------------------------------
    static std::uint16_t crc16(const std::uint8_t* data, std::size_t len);

    // ---- Low-level ADU helpers -----------------------------------------
    void send_request(const std::vector<std::uint8_t>& adu);
    std::vector<std::uint8_t> receive_response(std::size_t expected_len);
};

}  // namespace modbus_bridge

#endif  // MODBUSRTUMASTER_HPP
