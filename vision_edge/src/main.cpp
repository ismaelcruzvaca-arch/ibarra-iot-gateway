#include "engine.hpp"
#include "watchdog.hpp"
#include "mqtt_client.hpp"
#include <iostream>
#include <thread>
#include <chrono>

int main() {
    std::cout << "Starting Vision Edge Node..." << std::endl;

    vision::watchdog::Watchdog watchdog;
    watchdog.start();

    vision::mqtt::MqttClient mqtt("ssl://mosquitto:8883", "vision_edge_01");
    mqtt.connect();

    // Simulated image defect buffer
    std::vector<float> defect_buffer = {0.1f, 0.5f, 0.9f, 0.8f, 0.2f};
    
    // Main processing loop
    for (int i = 0; i < 10; ++i) {
        watchdog.heartbeat(); // Pet the watchdog
        
        // Process buffer
        float subpixel_defect = vision::engine::processDefectBuffer(defect_buffer, 2);
        
        // Publish telemetry
        std::string payload = "{\"defect_subpixel\": " + std::to_string(subpixel_defect) + "}";
        mqtt.publish("novamex/ibarra/quality/vision_001", payload);
        
        // Simulate frame delay
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }

    watchdog.stop();
    mqtt.disconnect();
    
    std::cout << "Vision Edge Node finished successfully." << std::endl;
    return 0;
}
