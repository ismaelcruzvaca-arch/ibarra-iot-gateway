// ---------------------------------------------------------------------------
// Catch2 unit tests for TelemetryOrchestrator
//
// Uses MockMQTTClient + MockSerialPort to verify the full poll-serialise-
// publish pipeline without any real hardware or broker.
// ---------------------------------------------------------------------------

#include <catch2/catch.hpp>

#include "IMQTTClient.hpp"
#include "ModbusRTUMaster.hpp"
#include "TelemetryOrchestrator.hpp"
#include "telemetry.pb.h"

#include <cstdint>
#include <vector>
#include <deque>
#include <map>
#include <algorithm>
#include <cstring>
#include <string>

using namespace ibarra::modbus;
using namespace modbus_bridge;

// =========================================================================
// MockMQTTClient — test double for IMQTTClient
// =========================================================================

class MockMQTTClient : public IMQTTClient {
public:
    bool connect() override { connected_ = true; return true; }
    void disconnect() override { connected_ = false; }

    bool publish(const std::string&          topic,
                 const std::vector<uint8_t>& payload,
                 int                         qos,
                 uint32_t                    expiry_seconds) override
    {
        publications_.push_back({topic, payload, qos, expiry_seconds});
        return true;
    }

    // ---- Test helpers ---------------------------------------------------

    struct Publication {
        std::string          topic;
        std::vector<uint8_t> payload;
        int                  qos;
        uint32_t             expiry_seconds;
    };

    const std::vector<Publication>& publications() const { return publications_; }
    void clear() { publications_.clear(); }

private:
    bool                  connected_ = false;
    std::vector<Publication> publications_;
};

// =========================================================================
// MockSerialPort — test double for ISerialPort (same as test_modbus.cpp)
// =========================================================================

class MockSerialPort : public ISerialPort {
public:
    std::size_t write(const std::uint8_t* data, std::size_t len) override
    {
        last_tx_.assign(data, data + len);
        return len;
    }

    std::size_t read(std::uint8_t* buffer, std::size_t len) override
    {
        // Determine which slave we just polled by looking at the last
        // transmitted ADU first byte (slave address).
        std::uint8_t slave = last_tx_.empty() ? 0 : last_tx_[0];

        // Return pre-configured response for this slave, if any.
        auto it = responses_.find(slave);
        if (it != responses_.end() && !it->second.empty()) {
            const auto& resp = it->second;
            std::size_t n = std::min(len, resp.size());
            std::copy_n(resp.begin(), n, buffer);
            return n;
        }
        // No response → simulate timeout (read returns 0)
        return 0;
    }

    void set_timeout(std::uint32_t ms) override { timeout_ms_ = ms; }

    /// Configure a complete response (including CRC) for a specific slave.
    void set_response_for_slave(std::uint8_t slave,
                                std::vector<std::uint8_t> resp)
    {
        responses_[slave] = std::move(resp);
    }

    const std::vector<std::uint8_t>& last_tx() const { return last_tx_; }

    std::uint32_t timeout_ms_ = 1000;

private:
    std::vector<std::uint8_t>                  last_tx_;
    std::map<std::uint8_t, std::vector<std::uint8_t>> responses_;
};

// =========================================================================
// CRC-16 helper (identical to test_modbus.cpp)
// =========================================================================

namespace {

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

TEST_CASE("TelemetryOrchestrator — full poll-publish cycle",
          "[orchestrator][protobuf][mqtt]")
{
    MockSerialPort   serial;
    MockMQTTClient   mqtt;
    ModbusRTUMaster  modbus(serial);

    // Pre-load responses for slaves 1..5
    // Each response: slave_id + FC=04 + byte_count(2) + register(2 bytes, big-endian) + CRC
    for (std::uint8_t slave = 1; slave <= 5; ++slave) {
        std::uint16_t reg_val = static_cast<std::uint16_t>(slave * 10);
        std::vector<std::uint8_t> resp = {
            slave,                                    // slave address
            0x04,                                     // function code
            2,                                        // byte count (1 register)
            static_cast<std::uint8_t>((reg_val >> 8) & 0xFF),  // reg hi
            static_cast<std::uint8_t>(reg_val & 0xFF)          // reg lo
        };
        append_crc(resp);
        serial.set_response_for_slave(slave, resp);
    }

    TelemetryOrchestrator orchestrator(modbus, mqtt);
    orchestrator.periodic_poll_and_publish();

    // ---- Verifications --------------------------------------------------

    // 1. Exactly one publication
    REQUIRE(mqtt.publications().size() == 1);

    const auto& pub = mqtt.publications().front();

    // 2. Topic
    CHECK(pub.topic == "novamex/linea1/telemetry");

    // 3. QOS = 1, expiry = 3
    CHECK(pub.qos == 1);
    CHECK(pub.expiry_seconds == 3);

    // 4. Payload is non-empty (serialised Protobuf)
    REQUIRE_FALSE(pub.payload.empty());

    // 5. Deserialise payload back and verify node states
    ModbusBridgePayload decoded;
    REQUIRE(decoded.ParseFromArray(pub.payload.data(),
                                   static_cast<int>(pub.payload.size())));

    REQUIRE(decoded.nodes_size() == 5);

    for (int i = 0; i < 5; ++i) {
        const auto& node = decoded.nodes(i);
        INFO("Checking slave " << (i + 1));
        CHECK(node.node_id() == static_cast<std::uint32_t>(i + 1));
        CHECK(node.cycle_count() == static_cast<std::uint32_t>((i + 1) * 10));
        CHECK(node.status() == NodeStatus::NODE_ONLINE);
    }
}

TEST_CASE("TelemetryOrchestrator — slave failure maps to NODE_ERROR",
          "[orchestrator][error]")
{
    MockSerialPort   serial;
    MockMQTTClient   mqtt;
    ModbusRTUMaster  modbus(serial);

    // Slaves 1, 3, 5 respond with valid data; 2 and 4 time out (no response)
    for (std::uint8_t slave : {1, 3, 5}) {
        std::vector<std::uint8_t> resp = {
            slave, 0x04, 2,
            0x00, static_cast<std::uint8_t>(slave)  // big-endian register value
        };
        append_crc(resp);
        serial.set_response_for_slave(slave, resp);
    }
    // Slaves 2 and 4: no response configured => ModbusTimeout

    TelemetryOrchestrator orchestrator(modbus, mqtt);
    orchestrator.periodic_poll_and_publish();

    REQUIRE(mqtt.publications().size() == 1);

    ModbusBridgePayload decoded;
    REQUIRE(decoded.ParseFromArray(
        mqtt.publications().front().payload.data(),
        static_cast<int>(mqtt.publications().front().payload.size())));

    REQUIRE(decoded.nodes_size() == 5);

    // Nodes 1, 3, 5 should be ONLINE with cycle_count matching
    CHECK(decoded.nodes(0).status() == NodeStatus::NODE_ONLINE);
    CHECK(decoded.nodes(0).cycle_count() == 1);
    CHECK(decoded.nodes(2).status() == NodeStatus::NODE_ONLINE);
    CHECK(decoded.nodes(2).cycle_count() == 3);
    CHECK(decoded.nodes(4).status() == NodeStatus::NODE_ONLINE);
    CHECK(decoded.nodes(4).cycle_count() == 5);

    // Nodes 2 and 4 should be ERROR with cycle_count = 0
    CHECK(decoded.nodes(1).status() == NodeStatus::NODE_ERROR);
    CHECK(decoded.nodes(1).cycle_count() == 0);
    CHECK(decoded.nodes(3).status() == NodeStatus::NODE_ERROR);
    CHECK(decoded.nodes(3).cycle_count() == 0);
}
