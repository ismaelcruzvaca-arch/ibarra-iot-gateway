#ifndef TELEMETRYORCHESTRATOR_HPP
#define TELEMETRYORCHESTRATOR_HPP

#include <cstdint>
#include <vector>

#include "IMQTTClient.hpp"
#include "ModbusRTUMaster.hpp"
#include "telemetry.pb.h"

namespace ibarra::modbus {

// ---------------------------------------------------------------------------
// TelemetryOrchestrator — high-level poll-and-publish loop
//
// Polls a set of Modbus slave nodes for input registers, wraps the results
// in a Protobuf ModbusBridgePayload, and publishes the serialised buffer
// to MQTT.  All I/O boundaries (serial, network) are injected as interfaces
// so the class is fully testable without hardware.
// ---------------------------------------------------------------------------
class TelemetryOrchestrator {
public:
    /// @param modbus_master  Modbus RTU master (injected).
    /// @param mqtt_client    MQTT 5 client (injected).
    ///                        Both references must outlive this instance.
    TelemetryOrchestrator(::modbus_bridge::ModbusRTUMaster& modbus_master,
                          IMQTTClient&                      mqtt_client);

    /// Poll slave nodes 1..5 for their cycle-count register, build a
    /// Protobuf payload, serialise it, and publish to MQTT on topic
    /// "novamex/linea1/telemetry" with QOS 1 and 3-second expiry.
    ///
    /// Nodes that throw a ModbusException are reported as NODE_ERROR
    /// while the healthy nodes carry their real register values.
    void periodic_poll_and_publish();

private:
    ::modbus_bridge::ModbusRTUMaster& modbus_;
    IMQTTClient&                      mqtt_;

    /// Maximum number of consecutive registers to read per slave.
    static constexpr std::uint16_t REGISTER_COUNT = 1;
};

}  // namespace ibarra::modbus

#endif  // TELEMETRYORCHESTRATOR_HPP
