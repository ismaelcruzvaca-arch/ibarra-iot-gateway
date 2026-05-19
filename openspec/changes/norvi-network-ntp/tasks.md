# Implementation Tasks: Norvi Network and NTP Integration

This document outlines the sequential steps required to implement the resilient network, NTP synchronization, mTLS security, and MQTT publishing on the Norvi monitor node.

## Phase 1: Environment & Dependencies
- [ ] Add the required libraries to the `lib_deps` section of `edge_nodes/norvi_monitor/platformio.ini`:
  - `knolleary/PubSubClient`
  - `bblanchon/ArduinoJson`
- [ ] Define mock placeholders for network credentials and SSL certificates (SSID, PASSWORD, MQTT_BROKER_IP, CA_CERT, CLIENT_CERT, CLIENT_KEY) under a header `edge_nodes/norvi_monitor/src/NetworkCredentials.h`.

## Phase 2: Payload Serialization Helper (TDD)
- [ ] Create `edge_nodes/norvi_monitor/src/PayloadSerializer.h` containing a helper to serialize a `ProductionEvent` and a calendar timestamp into the Payload Universal JSON string.
- [ ] Add the test case `test_payload_serialization_structure` to `edge_nodes/norvi_monitor/test/test_main.cpp`.
- [ ] Run PlatformIO tests to verify the serialization helper behaves correctly.

## Phase 3: Resilient Wi-Fi & NTP Synchronization (Core 0)
- [ ] Increase the stack size of `vTaskNetworking` to `8192` in `main.cpp`'s `xTaskCreatePinnedToCore` parameters to avoid stack overflow under the TLS handshake.
- [ ] Implement the connection state machine loop inside `vTaskNetworking` in `main.cpp`.
- [ ] Integrate non-blocking Wi-Fi reconnect timer checks (`WL_CONNECTED` poll).
- [ ] Configure NTP via `configTime(0, 0, NTP_SERVER)`.
- [ ] Implement a blocking check loop inside `vTaskNetworking` that prevents MQTTS connection until the year retrieved from `time(nullptr)` is greater than 2024.

## Phase 4: mTLS MQTTS Client & Decoupled Publishing
- [ ] Configure `WiFiClientSecure` using the mock keys/certs from `NetworkCredentials.h`.
- [ ] Configure `PubSubClient` to use the secure client, MQTT_BROKER_IP, and port 8883.
- [ ] Set up Last Will and Testament (LWT) on `novamex/ibarra/production/norvi_001` with `node_health = OFFLINE`.
- [ ] When connected, publish `node_health = ONLINE` and monitor the `productionEventQueue`.
- [ ] In `vTaskNetworking`, dequeue events, format them as Payload Universal, and publish them asynchronously.
