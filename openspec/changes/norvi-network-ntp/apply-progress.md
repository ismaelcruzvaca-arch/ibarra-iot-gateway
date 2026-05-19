# Implementation Progress: Norvi Network and NTP Integration

All implementation tasks have been successfully completed and verified via static code reviews.

## Phase 1: Environment & Dependencies
- [x] Add the required libraries to the `lib_deps` section of `edge_nodes/norvi_monitor/platformio.ini`:
  - `knolleary/PubSubClient`
  - `bblanchon/ArduinoJson`
- [x] Define mock placeholders for network credentials and SSL certificates under `edge_nodes/norvi_monitor/include/secrets.h` (reused existing header with `WIFI_SSID`, `WIFI_PASSWORD`, `MQTT_BROKER_IP`, `CA_CERT`, `CLIENT_CERT`, `CLIENT_KEY`).

## Phase 2: Payload Serialization Helper (TDD)
- [x] Create `edge_nodes/norvi_monitor/src/PayloadSerializer.h` containing `serializePayload` to serialize a `ProductionEvent` and a calendar timestamp into the Sparkplug B Lite / Universal JSON format.
- [x] Add the test case `test_payload_serialization_structure` to `edge_nodes/norvi_monitor/test/test_main.cpp`.
- [x] Integrate and register tests in both Arduino `setup()` and native `main()` test runners.

## Phase 3: Resilient Wi-Fi & NTP Synchronization (Core 0)
- [x] Increase the stack size of `vTaskNetworking` to `8192` bytes in `main.cpp`'s `xTaskCreatePinnedToCore` instantiation.
- [x] Implement the connection state machine loop inside `vTaskNetworking` running on Core 0.
- [x] Integrate non-blocking Wi-Fi reconnect timer checks (`WL_CONNECTED` poll / disconnect timeout).
- [x] Configure NTP via `configTime(0, 0, "pool.ntp.org")` when WiFi connects.
- [x] Implement a blocking check loop inside `vTaskNetworking` that prevents MQTTS connection until `time(nullptr)` returns a year > 2024.

## Phase 4: mTLS MQTTS Client & Decoupled Publishing
- [x] Configure `WiFiClientSecure` using the mock keys/certs from `secrets.h`.
- [x] Configure `PubSubClient` to use the secure client, `MQTT_BROKER_IP`, and port 8883.
- [x] Set up Last Will and Testament (LWT) on `novamex/ibarra/production/norvi_001` with `node_health = OFFLINE`.
- [x] When connected, publish `node_health = ONLINE` containing initial/current state.
- [x] In `vTaskNetworking`, dequeue events, format them with UTC timestamps (accounting for queue processing latency), serialize them, and publish them asynchronously.
