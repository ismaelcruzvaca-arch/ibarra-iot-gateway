// ---------------------------------------------------------------------------
// Catch2 unit tests for ModbusRTUMaster
//
// Uses MockSerialPort to simulate NORVI device behaviour without real
// hardware.  Tests cover: happy-path, CRC mismatch, timeout, exception
// response, and parameter validation.
// ---------------------------------------------------------------------------

#include <catch2/catch.hpp>

#include "ModbusRTUMaster.hpp"

#include <cstdint>
#include <vector>
#include <deque>
#include <algorithm>

using namespace modbus_bridge;

// =========================================================================
// MockSerialPort — test double for ISerialPort
//
// Pre-loads response bytes with set_next_response().  Each read() returns
// the next chunk from a deque of canned responses.
// =========================================================================

namespace {

class MockSerialPort : public ISerialPort {
public:
    // ---- ISerialPort interface -----------------------------------------

    std::size_t write(const std::uint8_t* data, std::size_t len) override
    {
        last_tx_.assign(data, data + len);
        return len;  // always succeed
    }

    std::size_t read(std::uint8_t* buffer, std::size_t len) override
    {
        if (responses_.empty()) {
            return 0;  // timeout-like
        }

        auto& chunk = responses_.front();
        std::size_t n = std::min(len, chunk.size());
        std::copy_n(chunk.begin(), n, buffer);
        chunk.erase(chunk.begin(), chunk.begin() + static_cast<long>(n));
        if (chunk.empty()) {
            responses_.pop_front();
        }
        return n;
    }

    void set_timeout(std::uint32_t ms) override
    {
        timeout_ms_ = ms;
    }

    // ---- Test helpers ---------------------------------------------------

    /// Append a canned response (raw bytes, including CRC).
    void set_next_response(std::vector<std::uint8_t> resp)
    {
        responses_.push_back(std::move(resp));
    }

    /// Return the last transmitted ADU (including CRC).
    const std::vector<std::uint8_t>& last_tx() const { return last_tx_; }

    std::uint32_t timeout_ms_ = 1000;

private:
    std::vector<std::uint8_t>              last_tx_;
    std::deque<std::vector<std::uint8_t>>  responses_;
};

}  // anonymous namespace

// =========================================================================
// CRC-16 helper for test fixtures (independent of implementation)
// =========================================================================

namespace {

/// Pre-computed CRC-16 table for polynomial 0xA001.
static constexpr std::uint16_t CRC16_TABLE[256] = {
    0x0000, 0xC0C1, 0xC181, 0x0140, 0xC301, 0x03C0, 0x0280, 0xC241,
    0xC601, 0x06C0, 0x0780, 0xC741, 0x0500, 0xC5C1, 0xC481, 0x0440,
    0xCC01, 0x0CC0, 0x0D80, 0xCD41, 0x0F00, 0xCFC1, 0xCE81, 0x0E40,
    0x0A00, 0xCAC1, 0xCB81, 0x0B40, 0xC901, 0x09C0, 0x0880, 0xC841,
    0xD801, 0x18C0, 0x1980, 0xD941, 0x1B00, 0xDBC1, 0xDA81, 0x1A40,
    0x1E00, 0xDEC1, 0xDF81, 0x1F40, 0xDD01, 0x1DC0, 0x1C80, 0xDC41,
    0x1400, 0xD4C1, 0xD581, 0x1540, 0xD701, 0x17C0, 0x1680, 0xD641,
    0xD201, 0x12C0, 0x1380, 0xD341, 0x1100, 0xD1C1, 0xD081, 0x1040,
    0xF001, 0x30C0, 0x3180, 0xF141, 0x3300, 0xF3C1, 0xF281, 0x3240,
    0x3600, 0xF6C1, 0xF781, 0x3740, 0xF501, 0x35C0, 0x3480, 0xF441,
    0x3C00, 0xFCC1, 0xFD81, 0x3D40, 0xFF01, 0x3FC0, 0x3E80, 0xFE41,
    0xFA01, 0x3AC0, 0x3B80, 0xFB41, 0x3900, 0xF9C1, 0xF881, 0x3840,
    0x2800, 0xE8C1, 0xE981, 0x2940, 0xEB01, 0x2BC0, 0x2A80, 0xEA41,
    0xEE01, 0x2EC0, 0x2F80, 0xEF41, 0x2D00, 0xEDC1, 0xEC81, 0x2C40,
    0xE401, 0x24C0, 0x2580, 0xE541, 0x2700, 0xE7C1, 0xE681, 0x2640,
    0x2200, 0xE2C1, 0xE381, 0x2340, 0xE101, 0x21C0, 0x2080, 0xE041,
    0xA001, 0x60C0, 0x6180, 0xA141, 0x6300, 0xA3C1, 0xA281, 0x6240,
    0x6600, 0xA6C1, 0xA781, 0x6740, 0xA501, 0x65C0, 0x6480, 0xA441,
    0x6C00, 0xACC1, 0xAD81, 0x6D40, 0xAF01, 0x6FC0, 0x6E80, 0xAE41,
    0xAA01, 0x6AC0, 0x6B80, 0xAB41, 0x6900, 0xA9C1, 0xA881, 0x6840,
    0x7800, 0xB8C1, 0xB981, 0x7940, 0xBB01, 0x7BC0, 0x7A80, 0xBA41,
    0xBE01, 0x7EC0, 0x7F80, 0xBF41, 0x7D00, 0xBDC1, 0xBC81, 0x7C40,
    0xB401, 0x74C0, 0x7580, 0xB541, 0x7700, 0xB7C1, 0xB681, 0x7640,
    0x7200, 0xB2C1, 0xB381, 0x7340, 0xB101, 0x71C0, 0x7080, 0xB041,
    0x5000, 0x90C1, 0x9181, 0x5140, 0x9301, 0x53C0, 0x5280, 0x9241,
    0x9601, 0x56C0, 0x5780, 0x9741, 0x5500, 0x95C1, 0x9481, 0x5440,
    0x9C01, 0x5CC0, 0x5D80, 0x9D41, 0x5F00, 0x9FC1, 0x9E81, 0x5E40,
    0x5A00, 0x9AC1, 0x9B81, 0x5B40, 0x9901, 0x59C0, 0x5880, 0x9841,
    0x8801, 0x48C0, 0x4980, 0x8941, 0x4B00, 0x8BC1, 0x8A81, 0x4A40,
    0x4E00, 0x8EC1, 0x8F81, 0x4F40, 0x8D01, 0x4DC0, 0x4C80, 0x8C41,
    0x4400, 0x84C1, 0x8581, 0x4540, 0x8701, 0x47C0, 0x4680, 0x8641,
    0x8201, 0x42C0, 0x4380, 0x8341, 0x4100, 0x81C1, 0x8081, 0x4040,
};

/// Append Modbus CRC-16 (little-endian) to a frame.
void append_crc(std::vector<std::uint8_t>& frame)
{
    std::uint16_t crc = 0xFFFF;
    for (auto byte : frame) {
        std::uint8_t idx = static_cast<std::uint8_t>(crc ^ byte);
        crc = (crc >> 8) ^ CRC16_TABLE[idx];
    }
    frame.push_back(static_cast<std::uint8_t>(crc & 0xFF));
    frame.push_back(static_cast<std::uint8_t>((crc >> 8) & 0xFF));
}

}  // anonymous namespace

// =========================================================================
// Tests
// =========================================================================

TEST_CASE("Read 8 input registers — happy path", "[modbus][fc04]")
{
    MockSerialPort mock;
    ModbusRTUMaster master(mock);

    // Build valid response: slave=17, FC=04, byte_count=16, 8 registers
    std::vector<std::uint8_t> resp = {
        0x11,                         // slave address
        0x04,                         // function code
        16,                           // byte count
        0x00, 0x01,                   // reg 0
        0x00, 0x02,                   // reg 1
        0x00, 0x03,                   // reg 2
        0x00, 0x04,                   // reg 3
        0x00, 0x05,                   // reg 4
        0x00, 0x06,                   // reg 5
        0x00, 0x07,                   // reg 6
        0x00, 0x08,                   // reg 7
    };
    append_crc(resp);
    mock.set_next_response(resp);

    auto regs = master.read_input_registers(0x11, 0, 8);

    REQUIRE(regs.size() == 8);
    CHECK(regs[0] == 1);
    CHECK(regs[1] == 2);
    CHECK(regs[2] == 3);
    CHECK(regs[3] == 4);
    CHECK(regs[4] == 5);
    CHECK(regs[5] == 6);
    CHECK(regs[6] == 7);
    CHECK(regs[7] == 8);

    // Verify request ADU
    const auto& tx = mock.last_tx();
    // length: 6 (ADU: slave + FC + addr(2) + count(2)) + 2 (CRC)
    REQUIRE(tx.size() == 8);
    CHECK(tx[0] == 0x11);            // slave
    CHECK(tx[1] == 0x04);            // FC
    CHECK(tx[2] == 0x00);            // start_addr hi
    CHECK(tx[3] == 0x00);            // start_addr lo
    CHECK(tx[4] == 0x00);            // count hi
    CHECK(tx[5] == 0x08);            // count lo
}

TEST_CASE("CRC mismatch throws ModbusCRCError", "[modbus][crc]")
{
    MockSerialPort mock;
    ModbusRTUMaster master(mock);

    // Valid data but CRC deliberately wrong
    std::vector<std::uint8_t> resp = { 0x11, 0x04, 2, 0x00, 0x2A };
    resp.push_back(0x00);  // wrong CRC low
    resp.push_back(0x00);  // wrong CRC high

    mock.set_next_response(resp);

    CHECK_THROWS_AS(master.read_input_registers(0x11, 0, 1),
                    ModbusCRCError);
}

TEST_CASE("Timeout throws ModbusTimeout", "[modbus][timeout]")
{
    MockSerialPort mock;
    ModbusRTUMaster master(mock);

    // No response configured -> read() returns 0
    CHECK_THROWS_AS(master.read_input_registers(0x11, 0, 1),
                    ModbusTimeout);
}

TEST_CASE("Exception response 0x02 throws ModbusExceptionResponse",
          "[modbus][exception]")
{
    MockSerialPort mock;
    ModbusRTUMaster master(mock);

    // Exception response: FC | 0x80 = 0x84, code 0x02 (ILLEGAL_DATA_ADDRESS)
    std::vector<std::uint8_t> resp = { 0x11, 0x84, 0x02 };
    append_crc(resp);
    mock.set_next_response(resp);

    REQUIRE_THROWS_AS(master.read_input_registers(0x11, 0, 1),
                      ModbusExceptionResponse);
}

TEST_CASE("Register count out of range throws ModbusFrameError",
          "[modbus][validation]")
{
    MockSerialPort mock;
    ModbusRTUMaster master(mock);

    CHECK_THROWS_AS(master.read_input_registers(0x11, 0, 0),
                    ModbusFrameError);
    CHECK_THROWS_AS(master.read_input_registers(0x11, 0, 126),
                    ModbusFrameError);
}
