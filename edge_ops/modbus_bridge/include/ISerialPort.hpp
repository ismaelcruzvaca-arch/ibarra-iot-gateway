#ifndef ISERIALPORT_HPP
#define ISERIALPORT_HPP

#include <cstddef>
#include <cstdint>
#include <vector>

namespace modbus_bridge {

// ---------------------------------------------------------------------------
// ISerialPort — pure virtual interface for serial port communication
//
// Designed for dependency injection so tests can substitute a MockSerialPort
// without touching the real hardware (NORVI nodes).
// ---------------------------------------------------------------------------
class ISerialPort {
public:
    virtual ~ISerialPort() = default;

    /// Write `len` bytes from `data` to the serial port.
    /// Returns the number of bytes actually written.
    /// Throws SerialPortError on fatal hardware error.
    virtual std::size_t write(const std::uint8_t* data, std::size_t len) = 0;

    /// Read up to `len` bytes into `buffer`.
    /// Blocks at most until the configured timeout; returns the number of bytes
    /// actually received (may be zero on timeout).
    /// Throws SerialPortError on fatal hardware error.
    virtual std::size_t read(std::uint8_t* buffer, std::size_t len) = 0;

    /// Set the read timeout in milliseconds.
    virtual void set_timeout(std::uint32_t ms) = 0;
};

}  // namespace modbus_bridge

#endif  // ISERIALPORT_HPP
