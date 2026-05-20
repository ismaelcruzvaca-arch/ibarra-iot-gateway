// ============================================================================
// ModbusRTUMaster.cpp — Modbus RTU Master Protocol Engine Implementation
// ============================================================================
//
// Implements:
//   - crc16_modbus() — CRC-16/Modbus per spec (poly 0xA001, init 0xFFFF, LSB-first)
//   - ModbusRTUMaster::read_input_registers() — FC04 with CRC validation
//
// CRC-16/Modbus algorithm reference:
//   Modbus over Serial Line Specification V1.02, Section 6.2.2
//   Polynomial: 0xA001 (reflected, LSB-first)
//   Initial value: 0xFFFF
//   No final XOR
// ============================================================================

#include "ModbusRTUMaster.hpp"

#include <cstdint>
#include <vector>

namespace ibarra {
namespace modbus {

// ==========================================================================
// CRC-16/Modbus — bitwise loop (no lookup table)
// ==========================================================================

uint16_t crc16_modbus(const std::vector<uint8_t>& data) {
    uint16_t crc = 0xFFFF;

    for (uint8_t byte : data) {
        crc ^= byte;
        for (int i = 0; i < 8; ++i) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }

    return crc;
}

// ==========================================================================
// ModbusRTUMaster — Frame Assembly, I/O, Validation, Parsing
// ==========================================================================

ModbusRTUMaster::ModbusRTUMaster(ISerialPort& port)
    : port_(port) {}

std::vector<uint16_t> ModbusRTUMaster::read_input_registers(
    uint8_t slave_id,
    uint16_t start_addr,
    uint16_t quantity)
{
    // 1. Assemble request frame: [slave][0x04][start_hi][start_lo][qty_hi][qty_lo]
    std::vector<uint8_t> request;
    request.reserve(8);  // 6 data + 2 CRC
    request.push_back(slave_id);
    request.push_back(0x04);
    request.push_back(static_cast<uint8_t>((start_addr >> 8) & 0xFF));
    request.push_back(static_cast<uint8_t>(start_addr & 0xFF));
    request.push_back(static_cast<uint8_t>((quantity >> 8) & 0xFF));
    request.push_back(static_cast<uint8_t>(quantity & 0xFF));
    append_crc(request);

    // 2. Send request via serial port
    port_.write(request);

    // 3. Read response (max Modbus RTU frame: 256 bytes)
    std::vector<uint8_t> response;
    bool ok = port_.read(response, 256);
    if (!ok) {
        throw ModbusTimeoutError("Serial port read timed out");
    }

    // 4. Validate trailing CRC-16 on response frame
    if (!validate_crc(response)) {
        throw ModbusCRCError("CRC-16 mismatch on response frame");
    }

    // 5. Detect Modbus exception frame (function code has MSB set)
    if (response.size() >= 3) {
        uint8_t function_code = response[1];
        if (function_code & 0x80) {
            uint8_t exception_code = response[2];
            throw ModbusResponseError(exception_code);
        }
    }

    // 6. Parse register payload (big-endian 16-bit values)
    // Response layout: [slave][fc][byte_count][reg_hi][reg_lo]...[crc_lo][crc_hi]
    if (response.size() < 5) {
        throw ModbusCRCError("Response frame too short for valid payload");
    }
    uint8_t byte_count = response[2];
    return parse_registers(response, byte_count, quantity);
}

// --------------------------------------------------------------------------
// append_crc — compute CRC-16 over frame data, append [CRClo][CRChi]
// --------------------------------------------------------------------------
void ModbusRTUMaster::append_crc(std::vector<uint8_t>& frame) const {
    uint16_t crc = crc16_modbus(frame);
    frame.push_back(static_cast<uint8_t>(crc & 0xFF));
    frame.push_back(static_cast<uint8_t>((crc >> 8) & 0xFF));
}

// --------------------------------------------------------------------------
// validate_crc — recompute CRC over all bytes except trailing 2, compare
// --------------------------------------------------------------------------
bool ModbusRTUMaster::validate_crc(const std::vector<uint8_t>& frame) const {
    if (frame.size() < 2) {
        return false;
    }
    // CRC covers all bytes except the last 2 (which are the CRC itself)
    std::vector<uint8_t> data(frame.begin(), frame.end() - 2);
    uint16_t computed = crc16_modbus(data);
    // Received CRC is stored LSB-first: [CRClo][CRChi]
    uint16_t received = static_cast<uint16_t>(frame[frame.size() - 2])
                      | (static_cast<uint16_t>(frame[frame.size() - 1]) << 8);
    return computed == received;
}

// --------------------------------------------------------------------------
// parse_registers — extract big-endian uint16_t values from payload
// --------------------------------------------------------------------------
std::vector<uint16_t> ModbusRTUMaster::parse_registers(
    const std::vector<uint8_t>& payload,
    uint8_t byte_count,
    uint16_t expected_quantity)
{
    std::vector<uint16_t> registers;
    registers.reserve(expected_quantity);

    // Registers start at offset 3 (after slave_id, fc, byte_count)
    for (uint8_t i = 0; i < byte_count; i += 2) {
        uint16_t reg = (static_cast<uint16_t>(payload[3 + i]) << 8)
                     |  static_cast<uint16_t>(payload[3 + i + 1]);
        registers.push_back(reg);
    }

    return registers;
}

} // namespace modbus
} // namespace ibarra
