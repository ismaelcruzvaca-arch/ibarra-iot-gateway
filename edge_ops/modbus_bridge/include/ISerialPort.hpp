// ============================================================================
// ISerialPort.hpp — Abstract Serial Port Interface
// ============================================================================
//
// Pure virtual interface decoupling the Modbus protocol engine from physical
// UART hardware. Enables TDD via mock serial ports before hardware integration.
//
// Design: Runtime polymorphism (virtual dispatch) — clean headers, easy mocking.
// Namespace: ibarra::modbus — matches existing ibarra::telemetry convention.
// ============================================================================

#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

namespace ibarra {
namespace modbus {

class ISerialPort {
public:
    virtual ~ISerialPort() = default;

    /// Write raw bytes to the serial port.
    virtual void write(const std::vector<uint8_t>& data) = 0;

    /// Read up to max_bytes from the serial port.
    /// @param out  Receives the read bytes (appended).
    /// @param max_bytes  Maximum bytes to attempt to read.
    /// @return true on success, false on timeout or error.
    virtual bool read(std::vector<uint8_t>& out, size_t max_bytes) = 0;

    /// Configure the inter-byte and response timeout in milliseconds.
    virtual void set_timeout(int ms) = 0;
};

} // namespace modbus
} // namespace ibarra
