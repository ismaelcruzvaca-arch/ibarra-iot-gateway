// ============================================================================
// test_modbus_engine.cpp — Modbus RTU Engine Catch2 Tests (STRICT TDD)
// ============================================================================
//
// Tests for ISerialPort interface, MockSerialPort, CRC-16/Modbus, and
// ModbusRTUMaster read_input_registers(). Uses Catch2 v3.5+.
//
// Build: docker run via build_in_docker.sh host
// ============================================================================

#include "catch2/catch_amalgamated.hpp"
#include "ISerialPort.hpp"
#include "ModbusRTUMaster.hpp"

#include <cstdint>
#include <vector>
#include <deque>
#include <string>
#include <stdexcept>

// ============================================================================
// Phase 1 — ISerialPort Interface Contract
// ============================================================================
//
// Contract: ISerialPort is a pure virtual interface. Any class inheriting it
// MUST override write(), read(), and set_timeout(). The following commented-out
// code demonstrates the compile-time enforcement — uncommenting it would cause:
//   error: cannot declare variable 'p' to be of abstract type 'IncompletePort'
//   note: because the following virtual functions are pure within 'IncompletePort'
//
// struct IncompletePort : public ibarra::modbus::ISerialPort {
//     // intentionally empty — missing overrides → WILL NOT COMPILE
// };

// ---------------------------------------------------------------------------
// StubSerialPort — minimal complete ISerialPort override (triangulation #1)
// ---------------------------------------------------------------------------
struct StubSerialPort : public ibarra::modbus::ISerialPort {
    void write(const std::vector<uint8_t>&) override {}
    bool read(std::vector<uint8_t>&, size_t) override { return true; }
    void set_timeout(int) override {}
};

// ---------------------------------------------------------------------------
// SpySerialPort — complete override that records calls (triangulation #2)
// ---------------------------------------------------------------------------
struct SpySerialPort : public ibarra::modbus::ISerialPort {
    bool write_called = false;
    int  timeout_value = -1;
    std::vector<uint8_t> last_written;

    void write(const std::vector<uint8_t>& data) override {
        write_called = true;
        last_written = data;
    }

    bool read(std::vector<uint8_t>& out, size_t max_bytes) override {
        (void)max_bytes;
        out.push_back(0x42);
        return true;
    }

    void set_timeout(int ms) override {
        timeout_value = ms;
    }
};

TEST_CASE("StubSerialPort compiles and can be instantiated", "[serial][compile]") {
    // GIVEN a class that fully overrides ISerialPort
    // WHEN an instance is created and methods are called
    // THEN no compilation error occurs and calls succeed
    StubSerialPort port;
    port.write({});
    std::vector<uint8_t> buf;
    REQUIRE(port.read(buf, 10));
    port.set_timeout(100);
    SUCCEED("StubSerialPort compiled and ran without error");
}

TEST_CASE("SpySerialPort records write calls and timeout configuration", "[serial][spy]") {
    // GIVEN a spy implementation that records interactions
    // WHEN methods are called with specific arguments
    // THEN recorded values match
    SpySerialPort spy;
    REQUIRE_FALSE(spy.write_called);

    std::vector<uint8_t> payload = {0x01, 0x04, 0x00};
    spy.write(payload);

    REQUIRE(spy.write_called);
    REQUIRE(spy.last_written == payload);

    spy.set_timeout(500);
    REQUIRE(spy.timeout_value == 500);
}

// ============================================================================
// Phase 2 — MockSerialPort (TDD helper implementing ISerialPort)
// ============================================================================

// ---------------------------------------------------------------------------
// MockSerialPort — records writes + delivers enqueued responses
// ---------------------------------------------------------------------------
struct MockSerialPort : public ibarra::modbus::ISerialPort {
    std::deque<std::vector<uint8_t>> response_queue;
    std::vector<std::vector<uint8_t>> write_log;

    void write(const std::vector<uint8_t>& data) override {
        write_log.push_back(data);
    }

    bool read(std::vector<uint8_t>& out, size_t /*max_bytes*/) override {
        if (response_queue.empty()) {
            return false; // timeout simulation
        }
        out = response_queue.front();
        response_queue.pop_front();
        return true;
    }

    void set_timeout(int /*ms*/) override {
        // no-op for mock — timeout is simulated via empty queue
    }

    void enqueue_response(const std::vector<uint8_t>& response) {
        response_queue.push_back(response);
    }
};

TEST_CASE("MockSerialPort records written bytes in write_log", "[mock][write]") {
    MockSerialPort mock;
    std::vector<uint8_t> request = {0x01, 0x04, 0x00, 0x00, 0x00, 0x02};

    mock.write(request);
    REQUIRE(mock.write_log.size() == 1);
    REQUIRE(mock.write_log[0] == request);
}

TEST_CASE("MockSerialPort read returns enqueued response", "[mock][read]") {
    MockSerialPort mock;
    std::vector<uint8_t> enqueued = {0x01, 0x04, 0x02, 0x00, 0x0A};
    mock.enqueue_response(enqueued);

    std::vector<uint8_t> out;
    bool ok = mock.read(out, 256);
    REQUIRE(ok);
    REQUIRE(out == enqueued);
}

TEST_CASE("MockSerialPort read returns false when queue is empty", "[mock][empty]") {
    MockSerialPort mock;
    std::vector<uint8_t> out;
    bool ok = mock.read(out, 256);
    REQUIRE_FALSE(ok);
    REQUIRE(out.empty());
}

TEST_CASE("MockSerialPort handles multi-transaction sequence", "[mock][multi]") {
    // GIVEN two different simulated responses enqueued
    // WHEN reads are performed in sequence
    // THEN each read returns the correct response in FIFO order
    MockSerialPort mock;
    std::vector<uint8_t> rsp1 = {0x01, 0x04, 0x02, 0x00, 0x0A};
    std::vector<uint8_t> rsp2 = {0x02, 0x04, 0x02, 0x00, 0x14};

    mock.enqueue_response(rsp1);
    mock.enqueue_response(rsp2);

    std::vector<uint8_t> out1, out2;
    REQUIRE(mock.read(out1, 256));
    REQUIRE(out1 == rsp1);

    REQUIRE(mock.read(out2, 256));
    REQUIRE(out2 == rsp2);

    // Third read should fail — queue exhausted
    std::vector<uint8_t> out3;
    REQUIRE_FALSE(mock.read(out3, 256));
}

TEST_CASE("MockSerialPort write_log accumulates across multiple writes", "[mock][write]") {
    // GIVEN two separate write calls with different payloads
    // WHEN write_log is inspected
    // THEN both payloads are recorded in order
    MockSerialPort mock;
    std::vector<uint8_t> first  = {0x01, 0x04};
    std::vector<uint8_t> second = {0xFF, 0xFE};

    mock.write(first);
    mock.write(second);

    REQUIRE(mock.write_log.size() == 2);
    REQUIRE(mock.write_log[0] == first);
    REQUIRE(mock.write_log[1] == second);
}

// ---------------------------------------------------------------------------

TEST_CASE("ISerialPort supports polymorphic usage via reference", "[serial][poly]") {
    // GIVEN two different ISerialPort implementations
    // WHEN accessed through a base-class reference
    // THEN virtual dispatch routes to the correct override
    SpySerialPort spy;
    StubSerialPort stub;

    ibarra::modbus::ISerialPort& ref_spy = spy;
    ibarra::modbus::ISerialPort& ref_stub = stub;

    ref_spy.set_timeout(200);
    ref_stub.set_timeout(300);

    REQUIRE(spy.timeout_value == 200);
    // Stub does not record, but call must not crash
    SUCCEED("Polymorphic dispatch via ISerialPort& works");
}

// ============================================================================
// Phase 3 — CRC-16/Modbus (direct unit tests via crc16_modbus free function)
// ============================================================================
//
// RED: These tests reference crc16_modbus() declared in ModbusRTUMaster.hpp
//      but the implementation in src/ModbusRTUMaster.cpp does NOT exist yet.
//      WILL NOT LINK (Phase 3.1 RED).

TEST_CASE("CRC-16/Modbus matches known byte sequence 01 04 00 00 00 02", "[crc]") {
    // GIVEN the standard Modbus RTU example frame body
    // WHEN CRC-16/Modbus is computed
    // THEN the 16-bit result MUST be 0x71CB
    std::vector<uint8_t> frame = {0x01, 0x04, 0x00, 0x00, 0x00, 0x02};
    uint16_t crc = ibarra::modbus::crc16_modbus(frame);
    REQUIRE(crc == 0x71CB);
}

TEST_CASE("CRC-16/Modbus wire order is LSB-first", "[crc][endian]") {
    // GIVEN the computed CRC value
    // WHEN appended to the frame as [CRClo, CRChi]
    // THEN the low byte comes first (Little-Endian CRC convention)
    std::vector<uint8_t> frame = {0x01, 0x04, 0x00, 0x00, 0x00, 0x02};
    uint16_t crc = ibarra::modbus::crc16_modbus(frame);

    uint8_t crc_lo = crc & 0xFF;
    uint8_t crc_hi = (crc >> 8) & 0xFF;

    // 0x71CB → lo=0xCB, hi=0x71
    REQUIRE(crc_lo == 0xCB);
    REQUIRE(crc_hi == 0x71);
}

TEST_CASE("CRC-16/Modbus empty input returns initial value", "[crc][edge]") {
    // GIVEN an empty byte sequence
    // WHEN CRC-16 is computed
    // THEN the result should be the initial value 0xFFFF (no data processed)
    std::vector<uint8_t> empty;
    uint16_t crc = ibarra::modbus::crc16_modbus(empty);
    REQUIRE(crc == 0xFFFF);
}

TEST_CASE("CRC-16/Modbus single byte input", "[crc][edge]") {
    // GIVEN a single-byte input
    // WHEN CRC-16 is computed
    // THEN the result is deterministic and non-trivial
    std::vector<uint8_t> single = {0x01};
    uint16_t crc = ibarra::modbus::crc16_modbus(single);
    // Verify result is not the initial value (proves processing happened)
    REQUIRE(crc != 0xFFFF);
    // Known value for single byte 0x01 with poly 0xA001, init 0xFFFF
    REQUIRE(crc == 0x807E);
}

TEST_CASE("CRC-16/Modbus max frame size (256 bytes)", "[crc][edge]") {
    // GIVEN a 256-byte frame (maximum Modbus RTU frame)
    // WHEN CRC-16 is computed
    // THEN the calculation completes without overflow
    std::vector<uint8_t> max_frame(256, 0xAA);
    uint16_t crc = ibarra::modbus::crc16_modbus(max_frame);
    // Result must be deterministic — any non-trivial value proves completion
    REQUIRE(crc > 0);
    // Known value for 256 bytes of 0xAA
    REQUIRE(crc == 0xEC5B);
}

TEST_CASE("CRC-16/Modbus different inputs produce different CRCs", "[crc][triangulate]") {
    // GIVEN two different input frames
    // WHEN CRC-16 is computed on each
    // THEN the results differ (proves CRC is not a constant)
    std::vector<uint8_t> frame_a = {0x01, 0x04, 0x00, 0x00, 0x00, 0x02};
    std::vector<uint8_t> frame_b = {0x02, 0x04, 0x00, 0x00, 0x00, 0x02};

    uint16_t crc_a = ibarra::modbus::crc16_modbus(frame_a);
    uint16_t crc_b = ibarra::modbus::crc16_modbus(frame_b);

    REQUIRE(crc_a != crc_b);
    REQUIRE(crc_a == 0x71CB);  // slave 1
    REQUIRE(crc_b == 0x71F8);  // slave 2
}

// ============================================================================
// Phase 4 — ModbusRTUMaster read_input_registers
// ============================================================================
//
// RED: These tests compile (header + stub exist) but the stub returns empty.
//      Tests WILL FAIL at runtime because {10, 20, 30} ≠ {} (Phase 4.1 RED).

// Helper: build a valid FC04 response with correct CRC
static std::vector<uint8_t> make_fc04_response(
    uint8_t slave_id,
    const std::vector<uint16_t>& registers)
{
    uint8_t byte_count = static_cast<uint8_t>(registers.size() * 2);
    std::vector<uint8_t> frame;
    frame.push_back(slave_id);
    frame.push_back(0x04);       // FC04
    frame.push_back(byte_count);

    for (uint16_t reg : registers) {
        // Big-Endian: high byte first
        frame.push_back(static_cast<uint8_t>((reg >> 8) & 0xFF));
        frame.push_back(static_cast<uint8_t>(reg & 0xFF));
    }

    uint16_t crc = ibarra::modbus::crc16_modbus(frame);
    // CRC Little-Endian: low byte first
    frame.push_back(static_cast<uint8_t>(crc & 0xFF));
    frame.push_back(static_cast<uint8_t>((crc >> 8) & 0xFF));

    return frame;
}

// Helper: build a Modbus exception response
static std::vector<uint8_t> make_exception_response(
    uint8_t slave_id,
    uint8_t exception_code)
{
    std::vector<uint8_t> frame;
    frame.push_back(slave_id);
    frame.push_back(0x84);  // FC04 with MSB set
    frame.push_back(exception_code);

    uint16_t crc = ibarra::modbus::crc16_modbus(frame);
    frame.push_back(static_cast<uint8_t>(crc & 0xFF));
    frame.push_back(static_cast<uint8_t>((crc >> 8) & 0xFF));

    return frame;
}

TEST_CASE("ModbusRTUMaster happy path — 3 registers", "[master][happy]") {
    // GIVEN a valid 3-register FC04 response enqueued
    // WHEN read_input_registers(1, 0, 3) is called
    // THEN the result MUST equal {10, 20, 30}
    MockSerialPort mock;
    std::vector<uint8_t> response = make_fc04_response(
        0x01, {10, 20, 30});

    mock.enqueue_response(response);

    ibarra::modbus::ModbusRTUMaster master(mock);
    auto result = master.read_input_registers(1, 0, 3);

    REQUIRE(result.size() == 3);
    REQUIRE(result[0] == 10);
    REQUIRE(result[1] == 20);
    REQUIRE(result[2] == 30);
}

TEST_CASE("ModbusRTUMaster request frame byte assertion", "[master][frame]") {
    // GIVEN slave ID=1, start_addr=0, quantity=2
    // WHEN read_input_registers is called with a valid response enqueued
    // THEN the transmitted frame bytes are correct
    MockSerialPort mock;
    // Enqueue ANY valid response to satisfy the read side
    mock.enqueue_response(make_fc04_response(0x01, {0x0042}));

    ibarra::modbus::ModbusRTUMaster master(mock);
    master.read_input_registers(1, 0, 2);

    REQUIRE(mock.write_log.size() == 1);
    std::vector<uint8_t> request = mock.write_log[0];

    // Expected: 01 04 00 00 00 02 [CRC_LO] [CRC_HI]
    REQUIRE(request[0] == 0x01);  // slave ID
    REQUIRE(request[1] == 0x04);  // FC04
    REQUIRE(request[2] == 0x00);  // start addr hi
    REQUIRE(request[3] == 0x00);  // start addr lo
    REQUIRE(request[4] == 0x00);  // quantity hi
    REQUIRE(request[5] == 0x02);  // quantity lo

    // Verify CRC is present and correct
    uint16_t frame_crc = ibarra::modbus::crc16_modbus(
        std::vector<uint8_t>(request.begin(), request.begin() + 6));
    uint16_t appended_crc = static_cast<uint16_t>(request[6])
                          | (static_cast<uint16_t>(request[7]) << 8);
    REQUIRE(frame_crc == appended_crc);
}

TEST_CASE("ModbusRTUMaster CRC mismatch raises ModbusCRCError", "[master][crc]") {
    // GIVEN a response with an invalid CRC
    // WHEN read_input_registers is called
    // THEN ModbusCRCError is thrown
    MockSerialPort mock;
    // Build valid response then corrupt CRC
    std::vector<uint8_t> corrupt = make_fc04_response(0x01, {10, 20, 30});
    // Flip one byte of CRC to invalidate it
    corrupt.back() ^= 0xFF;
    mock.enqueue_response(corrupt);

    ibarra::modbus::ModbusRTUMaster master(mock);
    REQUIRE_THROWS_AS(
        master.read_input_registers(1, 0, 3),
        ibarra::modbus::ModbusCRCError
    );
}

TEST_CASE("ModbusRTUMaster timeout raises ModbusTimeoutError", "[master][timeout]") {
    // GIVEN a mock with no enqueued response (empty queue)
    // WHEN read_input_registers is called
    // THEN ModbusTimeoutError is thrown (read returns false)
    MockSerialPort mock;
    // No response enqueued — read() will return false
    ibarra::modbus::ModbusRTUMaster master(mock);
    REQUIRE_THROWS_AS(
        master.read_input_registers(1, 0, 3),
        ibarra::modbus::ModbusTimeoutError
    );
}

TEST_CASE("ModbusRTUMaster exception code 01 — Illegal Function", "[master][exception]") {
    MockSerialPort mock;
    // Enqueue TWO identical responses: one for REQUIRE_THROWS_AS, one for code assertion
    mock.enqueue_response(make_exception_response(0x01, 0x01));
    mock.enqueue_response(make_exception_response(0x01, 0x01));
    ibarra::modbus::ModbusRTUMaster master(mock);
    REQUIRE_THROWS_AS(
        master.read_input_registers(1, 0, 1),
        ibarra::modbus::ModbusResponseError
    );
    try {
        master.read_input_registers(1, 0, 1);
    } catch (const ibarra::modbus::ModbusResponseError& e) {
        REQUIRE(e.code == 0x01);
    }
}

TEST_CASE("ModbusRTUMaster exception code 02 — Illegal Data Address", "[master][exception]") {
    MockSerialPort mock;
    mock.enqueue_response(make_exception_response(0x01, 0x02));
    mock.enqueue_response(make_exception_response(0x01, 0x02));
    ibarra::modbus::ModbusRTUMaster master(mock);
    REQUIRE_THROWS_AS(
        master.read_input_registers(1, 0, 1),
        ibarra::modbus::ModbusResponseError
    );
    try {
        master.read_input_registers(1, 0, 1);
    } catch (const ibarra::modbus::ModbusResponseError& e) {
        REQUIRE(e.code == 0x02);
    }
}

TEST_CASE("ModbusRTUMaster exception code 03 — Illegal Data Value", "[master][exception]") {
    MockSerialPort mock;
    mock.enqueue_response(make_exception_response(0x01, 0x03));
    mock.enqueue_response(make_exception_response(0x01, 0x03));
    ibarra::modbus::ModbusRTUMaster master(mock);
    REQUIRE_THROWS_AS(
        master.read_input_registers(1, 0, 1),
        ibarra::modbus::ModbusResponseError
    );
    try {
        master.read_input_registers(1, 0, 1);
    } catch (const ibarra::modbus::ModbusResponseError& e) {
        REQUIRE(e.code == 0x03);
    }
}

TEST_CASE("ModbusRTUMaster exception code 04 — Slave Device Failure", "[master][exception]") {
    MockSerialPort mock;
    mock.enqueue_response(make_exception_response(0x01, 0x04));
    mock.enqueue_response(make_exception_response(0x01, 0x04));
    ibarra::modbus::ModbusRTUMaster master(mock);
    REQUIRE_THROWS_AS(
        master.read_input_registers(1, 0, 1),
        ibarra::modbus::ModbusResponseError
    );
    try {
        master.read_input_registers(1, 0, 1);
    } catch (const ibarra::modbus::ModbusResponseError& e) {
        REQUIRE(e.code == 0x04);
    }
}
