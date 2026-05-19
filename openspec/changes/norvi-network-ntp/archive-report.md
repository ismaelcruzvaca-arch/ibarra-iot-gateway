# Archive Report: Norvi Network and NTP Integration

This document completes and seals the SDD cycle for the resilient connection state machine, NTP synchronization, mTLS security setup, and MQTT publishing on the Norvi monitor node.

## Final Implementation Deliverables

### 1. dependencies & compiler configuration
* Updated [platformio.ini](file:///c:/Users/Ismael.Cruz/Downloads/ibarra-iot-gateway/ibarra-iot-gateway/edge_nodes/norvi_monitor/platformio.ini) with `knolleary/PubSubClient` and `bblanchon/ArduinoJson`.
* Added `-I.` flag to compiler options to locate configuration files properly.

### 2. credentials & certificates
* Created [secrets.h](file:///c:/Users/Ismael.Cruz/Downloads/ibarra-iot-gateway/ibarra-iot-gateway/edge_nodes/norvi_monitor/include/secrets.h) holding dummy SSID, passwords, broker IP, and certs.
* Added `secrets.h` to the root [.gitignore](file:///c:/Users/Ismael.Cruz/Downloads/ibarra-iot-gateway/ibarra-iot-gateway/.gitignore) to ensure it is never tracked.

### 3. Payload Serialization
* Implemented [PayloadSerializer.h](file:///c:/Users/Ismael.Cruz/Downloads/ibarra-iot-gateway/ibarra-iot-gateway/edge_nodes/norvi_monitor/src/PayloadSerializer.h) supporting both Arduino targets and native test compilation.
* Expanded [test_main.cpp](file:///c:/Users/Ismael.Cruz/Downloads/ibarra-iot-gateway/ibarra-iot-gateway/edge_nodes/norvi_monitor/test/test_main.cpp) with the `test_payload_serialization_structure` test case.

### 4. Connection State Machine & Publishing Task
* Completely redesigned `vTaskNetworking` inside [main.cpp](file:///c:/Users/Ismael.Cruz/Downloads/ibarra-iot-gateway/ibarra-iot-gateway/edge_nodes/norvi_monitor/src/main.cpp).
* Setup resilient Wi-Fi state machine, blocking NTP check, secure mTLS credentials configuration, and Last Will and Testament.
* Implemented queued telemetry extraction and publish flow.
* Stack size for `vTaskNetworking` increased to `8192` bytes.

All artifacts are persisted under `openspec/changes/norvi-network-ntp/`.
This concludes the Sub-Épica 1.3 cycle.
