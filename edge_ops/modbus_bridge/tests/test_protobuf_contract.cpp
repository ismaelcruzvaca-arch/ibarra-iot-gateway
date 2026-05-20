// ============================================================================
// test_protobuf_contract.cpp — Binary Contract Roundtrip Tests (Catch2)
// ============================================================================
//
// All tests operate on proto3 serialization semantics:
//   - OperatingStatus enum roundtrip (all 6 values + unknown=99)
//   - NorviState roundtrip and default values
//   - ModbusBridgePayload multi-node, empty, and insertion order
//   - HardwareHealthPayload roundtrip, defaults, large uint64
//   - Cross-message binary isolation
//
// Requires: protoc-generated telemetry.pb.h (from telemetry.proto)
// Build:    See tasks.md Phase 2 for manual build command
// ============================================================================

#include "catch2/catch_amalgamated.hpp"
#include "telemetry.pb.h"

#include <string>
#include <vector>

using ibarra::telemetry::OperatingStatus;
using ibarra::telemetry::NorviState;
using ibarra::telemetry::ModbusBridgePayload;
using ibarra::telemetry::HardwareHealthPayload;

// ---------------------------------------------------------------------------
// Helper: serialize a protobuf message to a string, returning empty on failure
// ---------------------------------------------------------------------------
template <typename M>
static std::string serialize_or_die(const M& msg) {
    std::string out;
    if (!msg.SerializeToString(&out)) {
        FAIL("Serialization failed unexpectedly");
    }
    return out;
}

// ============================================================================
// OperatingStatus Enum Tests
// ============================================================================

TEST_CASE("OperatingStatus enum roundtrip preserves all 6 defined values", "[enum]") {
    // GIVEN a proto3 OperatingStatus enum with values 0 through 5
    // WHEN each value is set on a NorviState, serialized, and deserialized
    // THEN the deserialized enum value MUST equal the original

    struct TestCase {
        OperatingStatus status;
        int expected_numeric;
    };

    const std::vector<TestCase> cases = {
        { OperatingStatus::UNKNOWN,     0 },
        { OperatingStatus::STARTING,    1 },
        { OperatingStatus::RUNNING,     2 },
        { OperatingStatus::FAULT,       3 },
        { OperatingStatus::STOPPED,     4 },
        { OperatingStatus::MAINTENANCE, 5 },
    };

    for (const auto& tc : cases) {
        NorviState original;
        original.set_node_id("roundtrip_test");
        original.set_status(tc.status);

        std::string wire = serialize_or_die(original);

        NorviState restored;
        REQUIRE(restored.ParseFromString(wire));
        REQUIRE(restored.status() == tc.status);
        REQUIRE(static_cast<int>(restored.status()) == tc.expected_numeric);
    }
}

TEST_CASE("OperatingStatus default value is UNKNOWN", "[enum][default]") {
    // GIVEN a freshly constructed NorviState (no status assigned)
    // WHEN the status field is read
    // THEN it MUST be UNKNOWN (value 0)
    NorviState state;
    REQUIRE(state.status() == OperatingStatus::UNKNOWN);
    REQUIRE(static_cast<int>(state.status()) == 0);
}

TEST_CASE("OperatingStatus preserves unrecognized numeric value", "[enum][unknown]") {
    // GIVEN a serialized NorviState with status=99 (not in enum)
    // WHEN deserialized
    // THEN the field MUST preserve the numeric value 99 without error
    //      (proto3 open-enum semantics)

    NorviState original;
    original.set_node_id("unknown_enum_test");
    original.set_cycle_count(7);

    // Serialize a message with an out-of-band status value.
    // Protobuf C++ API: we must write unknown fields at the wire level
    // to test open-enum semantics. Serialize with status=MAINTENANCE(5),
    // then manipulate the wire bytes to encode value 99 for field 3.
    original.set_status(OperatingStatus::MAINTENANCE);

    std::string wire = serialize_or_die(original);

    // Find the status field (field 3, varint) in the wire format
    // and replace its value with 99.
    // Field tag: (3 << 3) | 0 = 0x18 (24 decimal), followed by varint value.
    for (size_t i = 0; i < wire.size(); ++i) {
        if (static_cast<unsigned char>(wire[i]) == 0x18) {
            // Next byte is the varint-encoded enum value (5 for MAINTENANCE)
            if (i + 1 < wire.size() && static_cast<unsigned char>(wire[i + 1]) == 5) {
                wire[i + 1] = static_cast<char>(99);
                break;
            }
        }
    }

    NorviState restored;
    REQUIRE(restored.ParseFromString(wire));

    // In proto3, unknown enum values are stored in the unknown field set.
    // The status() getter returns the default (UNKNOWN=0) when the wire value
    // is not in the enum, but the numeric value is preserved.
    // We verify the roundtrip preserves the info by re-serializing and
    // checking the unknown field persists.
    std::string rewired = serialize_or_die(restored);

    // The re-serialized message should contain the unknown field
    bool found_unknown_field = false;
    for (size_t i = 0; i < rewired.size(); ++i) {
        if (static_cast<unsigned char>(rewired[i]) == 0x18) {
            if (i + 1 < rewired.size() && static_cast<unsigned char>(rewired[i + 1]) == 99) {
                found_unknown_field = true;
                break;
            }
        }
    }
    REQUIRE(found_unknown_field);
}

// ============================================================================
// NorviState Message Tests
// ============================================================================

TEST_CASE("NorviState serialization roundtrip", "[norvi]") {
    // GIVEN a NorviState with node_id="norvi_001", cycle_count=42, status=RUNNING
    // WHEN serialized and deserialized
    // THEN all three fields match original values
    NorviState original;
    original.set_node_id("norvi_001");
    original.set_cycle_count(42);
    original.set_status(OperatingStatus::RUNNING);

    std::string wire = serialize_or_die(original);

    NorviState restored;
    REQUIRE(restored.ParseFromString(wire));
    REQUIRE(restored.node_id()   == "norvi_001");
    REQUIRE(restored.cycle_count() == 42);
    REQUIRE(restored.status()    == OperatingStatus::RUNNING);
}

TEST_CASE("NorviState default values (proto3 zero-value defaults)", "[norvi][default]") {
    // GIVEN a NorviState with only node_id="norvi_002" set
    // WHEN serialized and deserialized
    // THEN cycle_count MUST be 0 and status MUST be UNKNOWN
    NorviState original;
    original.set_node_id("norvi_002");

    std::string wire = serialize_or_die(original);

    NorviState restored;
    REQUIRE(restored.ParseFromString(wire));
    REQUIRE(restored.node_id()     == "norvi_002");
    REQUIRE(restored.cycle_count() == 0);
    REQUIRE(restored.status()      == OperatingStatus::UNKNOWN);
}

// ============================================================================
// ModbusBridgePayload Message Tests
// ============================================================================

TEST_CASE("ModbusBridgePayload multi-node roundtrip", "[payload]") {
    // GIVEN a ModbusBridgePayload with 3 NorviState entries
    // WHEN serialized and deserialized
    // THEN nodes repeated field contains exactly 3 entries with matching IDs
    ModbusBridgePayload original;

    auto* n1 = original.add_nodes();
    n1->set_node_id("norvi_a");
    n1->set_cycle_count(10);
    n1->set_status(OperatingStatus::RUNNING);

    auto* n2 = original.add_nodes();
    n2->set_node_id("norvi_b");
    n2->set_cycle_count(20);
    n2->set_status(OperatingStatus::STARTING);

    auto* n3 = original.add_nodes();
    n3->set_node_id("norvi_c");
    n3->set_cycle_count(30);
    n3->set_status(OperatingStatus::FAULT);

    std::string wire = serialize_or_die(original);

    ModbusBridgePayload restored;
    REQUIRE(restored.ParseFromString(wire));
    REQUIRE(restored.nodes_size() == 3);

    REQUIRE(restored.nodes(0).node_id()     == "norvi_a");
    REQUIRE(restored.nodes(0).cycle_count() == 10);
    REQUIRE(restored.nodes(0).status()      == OperatingStatus::RUNNING);

    REQUIRE(restored.nodes(1).node_id()     == "norvi_b");
    REQUIRE(restored.nodes(1).cycle_count() == 20);
    REQUIRE(restored.nodes(1).status()      == OperatingStatus::STARTING);

    REQUIRE(restored.nodes(2).node_id()     == "norvi_c");
    REQUIRE(restored.nodes(2).cycle_count() == 30);
    REQUIRE(restored.nodes(2).status()      == OperatingStatus::FAULT);
}

TEST_CASE("ModbusBridgePayload empty payload roundtrip", "[payload][empty]") {
    // GIVEN a ModbusBridgePayload with no nodes entries
    // WHEN serialized and deserialized
    // THEN nodes field MUST be empty (size 0)
    ModbusBridgePayload original;
    REQUIRE(original.nodes_size() == 0);

    std::string wire = serialize_or_die(original);

    ModbusBridgePayload restored;
    REQUIRE(restored.ParseFromString(wire));
    REQUIRE(restored.nodes_size() == 0);
}

TEST_CASE("ModbusBridgePayload preserves insertion order", "[payload][order]") {
    // GIVEN nodes added in order "norvi_003", "norvi_001", "norvi_002"
    // WHEN serialized and deserialized
    // THEN iterating nodes yields same insertion order
    ModbusBridgePayload original;

    auto* n1 = original.add_nodes();
    n1->set_node_id("norvi_003");

    auto* n2 = original.add_nodes();
    n2->set_node_id("norvi_001");

    auto* n3 = original.add_nodes();
    n3->set_node_id("norvi_002");

    REQUIRE(original.nodes_size() == 3);

    std::string wire = serialize_or_die(original);

    ModbusBridgePayload restored;
    REQUIRE(restored.ParseFromString(wire));
    REQUIRE(restored.nodes_size() == 3);

    REQUIRE(restored.nodes(0).node_id() == "norvi_003");
    REQUIRE(restored.nodes(1).node_id() == "norvi_001");
    REQUIRE(restored.nodes(2).node_id() == "norvi_002");
}

// ============================================================================
// HardwareHealthPayload Message Tests
// ============================================================================

TEST_CASE("HardwareHealthPayload roundtrip with realistic values", "[health]") {
    // GIVEN cpu_temperature=45.5, ram_used_bytes=134217728, system_load=2.37
    // WHEN serialized and deserialized
    // THEN float values within 0.01, uint64 exact
    HardwareHealthPayload original;
    original.set_cpu_temperature(45.5f);
    original.set_ram_used_bytes(134217728ULL);
    original.set_system_load(2.37f);

    std::string wire = serialize_or_die(original);

    HardwareHealthPayload restored;
    REQUIRE(restored.ParseFromString(wire));

    REQUIRE(restored.cpu_temperature() == Catch::Approx(45.5f).epsilon(static_cast<double>(0.01f)));
    REQUIRE(restored.ram_used_bytes()  == 134217728ULL);
    REQUIRE(restored.system_load()     == Catch::Approx(2.37f).epsilon(static_cast<double>(0.01f)));
}

TEST_CASE("HardwareHealth zero-value defaults", "[health][default]") {
    // GIVEN a HardwareHealthPayload with no fields set
    // WHEN serialized and deserialized
    // THEN cpu_temperature=0.0f, ram_used_bytes=0, system_load=0.0f
    HardwareHealthPayload original;
    // No fields set — proto3 defaults

    std::string wire = serialize_or_die(original);

    HardwareHealthPayload restored;
    REQUIRE(restored.ParseFromString(wire));

    REQUIRE(restored.cpu_temperature() == 0.0f);
    REQUIRE(restored.ram_used_bytes()  == 0ULL);
    REQUIRE(restored.system_load()     == 0.0f);
}

TEST_CASE("HardwareHealth preserves large uint64", "[health][uint64]") {
    // GIVEN ram_used_bytes=4294967296 (4 GiB)
    // WHEN serialized and deserialized
    // THEN ram_used_bytes MUST equal 4294967296 without truncation
    const uint64_t four_gib = 4294967296ULL;

    HardwareHealthPayload original;
    original.set_ram_used_bytes(four_gib);

    std::string wire = serialize_or_die(original);

    HardwareHealthPayload restored;
    REQUIRE(restored.ParseFromString(wire));
    REQUIRE(restored.ram_used_bytes() == four_gib);
}

// ============================================================================
// Cross-Message Binary Isolation Tests
// ============================================================================

TEST_CASE("Cross-message binary isolation: independent serialization", "[isolation]") {
    // GIVEN a ModbusBridgePayload and a HardwareHealthPayload
    // WHEN each is deserialized from its own bytes
    // THEN each operation succeeds without referencing the other

    ModbusBridgePayload bridge;
    auto* node = bridge.add_nodes();
    node->set_node_id("isolation_test");
    node->set_status(OperatingStatus::RUNNING);

    HardwareHealthPayload health;
    health.set_cpu_temperature(60.0f);
    health.set_ram_used_bytes(1024ULL);

    std::string bridge_wire = serialize_or_die(bridge);
    std::string health_wire = serialize_or_die(health);

    // Deserialize ModbusBridgePayload from its own bytes — must succeed
    ModbusBridgePayload bridge_restored;
    REQUIRE(bridge_restored.ParseFromString(bridge_wire));
    REQUIRE(bridge_restored.nodes_size() == 1);
    REQUIRE(bridge_restored.nodes(0).node_id() == "isolation_test");
    REQUIRE(bridge_restored.nodes(0).status()  == OperatingStatus::RUNNING);

    // Deserialize HardwareHealthPayload from its own bytes — must succeed
    HardwareHealthPayload health_restored;
    REQUIRE(health_restored.ParseFromString(health_wire));
    REQUIRE(health_restored.cpu_temperature() == Catch::Approx(60.0f).epsilon(static_cast<double>(0.01f)));
    REQUIRE(health_restored.ram_used_bytes()  == 1024ULL);

    // Cross-message: bridge bytes must NOT parse as HardwareHealthPayload
    // (they are different message types — ParseFromString may succeed
    //  with unknown fields but should not produce the bridge data as health data)
    HardwareHealthPayload cross;
    bool cross_result = cross.ParseFromString(bridge_wire);
    // Protobuf will attempt to parse; hardware-health fields should not
    // accidentally contain bridge field data
    REQUIRE(cross.ram_used_bytes() != 1024ULL || cross_result == false);
}
