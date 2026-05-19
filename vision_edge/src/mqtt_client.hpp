#pragma once
#include <string>

namespace vision {
namespace mqtt {

class IMqttClient {
public:
    virtual ~IMqttClient() = default;
    virtual bool connect() = 0;
    virtual void disconnect() = 0;
    virtual bool publish(const std::string& topic, const std::string& payload) = 0;
};

class MqttClient : public IMqttClient {
public:
    MqttClient(const std::string& address, const std::string& client_id);
    ~MqttClient() override;

    bool connect() override;
    void disconnect() override;
    bool publish(const std::string& topic, const std::string& payload) override;

private:
    std::string address_;
    std::string client_id_;
    bool connected_{false};
    // Note: paho.mqtt.cpp would be integrated here.
};

} // namespace mqtt
} // namespace vision
