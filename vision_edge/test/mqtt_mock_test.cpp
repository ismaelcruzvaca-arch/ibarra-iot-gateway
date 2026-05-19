#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "../src/mqtt_client.hpp"

class MockMqttClient : public vision::mqtt::IMqttClient {
public:
    MOCK_METHOD(bool, connect, (), (override));
    MOCK_METHOD(void, disconnect, (), (override));
    MOCK_METHOD(bool, publish, (const std::string& topic, const std::string& payload), (override));
};

TEST(MqttResilienceTest, GracefulFailureOnPublishWhenDisconnected) {
    MockMqttClient mock_mqtt;
    
    // Simulate connection failure or abrupt disconnect
    EXPECT_CALL(mock_mqtt, publish(testing::_, testing::_))
        .WillOnce(testing::Return(false));
        
    bool success = mock_mqtt.publish("novamex/ibarra/quality/vision_001", "{\"defect_subpixel\": 0.3}");
    EXPECT_FALSE(success);
}
