#include "mqtt_client.hpp"
#include <iostream>

namespace vision {
namespace mqtt {

MqttClient::MqttClient(const std::string& address, const std::string& client_id)
    : address_(address), client_id_(client_id) {}

MqttClient::~MqttClient() {
    disconnect();
}

bool MqttClient::connect() {
    // Stub for paho.mqtt.cpp connect with mTLS
    std::cout << "[MQTT] Connecting to " << address_ << " with client ID " << client_id_ << " via mTLS..." << std::endl;
    connected_ = true;
    return true;
}

void MqttClient::disconnect() {
    if (connected_) {
        std::cout << "[MQTT] Disconnected." << std::endl;
        connected_ = false;
    }
}

bool MqttClient::publish(const std::string& topic, const std::string& payload) {
    if (!connected_) {
        std::cerr << "[MQTT ERROR] Not connected. Cannot publish to " << topic << std::endl;
        return false;
    }
    std::cout << "[MQTT] Published to " << topic << " payload: " << payload << std::endl;
    return true;
}

} // namespace mqtt
} // namespace vision
