#ifndef IMQTTCLIENT_HPP
#define IMQTTCLIENT_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace ibarra::modbus {

// ---------------------------------------------------------------------------
// IMQTTClient — pure virtual interface for MQTT 5 publishing
//
// Designed for dependency injection so the orchestrator can be tested with
// MockMQTTClient without requiring a real broker connection.
// ---------------------------------------------------------------------------
class IMQTTClient {
public:
    virtual ~IMQTTClient() = default;

    /// Open the connection to the MQTT broker.
    /// Returns true if the connection was established.
    virtual bool connect() = 0;

    /// Gracefully close the connection.
    virtual void disconnect() = 0;

    /// Publish a binary payload to a topic.
    ///
    /// @param topic           MQTT topic string.
    /// @param payload         Binary payload (e.g. serialised Protobuf).
    /// @param qos             Quality of Service (0, 1, or 2).
    /// @param expiry_seconds  Message expiry interval (MQTT 5 property).
    /// @return true if the publish was accepted by the client.
    virtual bool publish(
        const std::string&          topic,
        const std::vector<uint8_t>& payload,
        int                         qos,
        uint32_t                    expiry_seconds) = 0;
};

}  // namespace ibarra::modbus

#endif  // IMQTTCLIENT_HPP
