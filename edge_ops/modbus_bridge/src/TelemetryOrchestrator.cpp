#include "TelemetryOrchestrator.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace ibarra::modbus {

TelemetryOrchestrator::TelemetryOrchestrator(
    ::modbus_bridge::ModbusRTUMaster& modbus_master,
    IMQTTClient&                      mqtt_client)
    : modbus_(modbus_master)
    , mqtt_(mqtt_client)
{
}

void TelemetryOrchestrator::periodic_poll_and_publish()
{
    using namespace ::modbus_bridge;

    // ---- Build Protobuf payload -----------------------------------------
    ModbusBridgePayload payload;

    // Poll slaves 1 through 5
    for (std::uint8_t slave = 1; slave <= 5; ++slave) {
        auto* node_state = payload.add_nodes();
        node_state->set_node_id(slave);

        try {
            std::vector<std::uint16_t> regs =
                modbus_.read_input_registers(slave, 0, REGISTER_COUNT);

            if (!regs.empty()) {
                node_state->set_cycle_count(regs[0]);
                node_state->set_status(NodeStatus::NODE_ONLINE);
            } else {
                node_state->set_cycle_count(0);
                node_state->set_status(NodeStatus::NODE_OFFLINE);
            }
        }
        catch (const ModbusException&) {
            // Any Modbus error (timeout, CRC, exception response) maps to
            // NODE_ERROR.  The cycle_count stays at its default (0).
            node_state->set_status(NodeStatus::NODE_ERROR);
        }
    }

    // ---- Serialise to wire format ---------------------------------------
    std::vector<std::uint8_t> wire;
    wire.resize(payload.ByteSizeLong());
    if (!payload.SerializeToArray(wire.data(),
                                  static_cast<int>(wire.size()))) {
        // Serialisation failure — nothing to publish; silently bail out.
        return;
    }

    // ---- Publish to MQTT -------------------------------------------------
    // Topic:  novamex/linea1/telemetry
    // QOS:    1  (at-least-once delivery)
    // Expiry: 3 seconds (MQTT 5 message-expiry-interval property)
    mqtt_.publish("novamex/linea1/telemetry", wire, 1, 3);
}

}  // namespace ibarra::modbus
