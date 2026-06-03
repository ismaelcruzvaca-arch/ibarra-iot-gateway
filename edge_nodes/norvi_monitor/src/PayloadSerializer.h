#ifndef PAYLOAD_SERIALIZER_H
#define PAYLOAD_SERIALIZER_H

#include <ArduinoJson.h>

#ifdef ARDUINO
#include <Arduino.h>
inline String serializePayload(uint32_t cycleCount, const char* isoTimestamp,
                                const char* deviceId) {
    JsonDocument doc;
    doc["device_id"] = deviceId;
    doc["device_type"] = "norvi";
    doc["timestamp"] = isoTimestamp;
    doc["node_health"] = "ONLINE";

    // Dynamic metrics[] array — each entry has {name, value, unit, timestamp}.
    JsonArray metrics = doc["metrics"].to<JsonArray>();

    // Metric 1: production_cycle
    JsonObject m1 = metrics.add<JsonObject>();
    m1["name"] = "production_cycle";
    m1["value"] = cycleCount;
    m1["unit"] = "count";
    m1["timestamp"] = isoTimestamp;

    // Metric 2: uptime_s — computed from millis() on the device
    uint32_t uptime_s = millis() / 1000;
    JsonObject m2 = metrics.add<JsonObject>();
    m2["name"] = "uptime_s";
    m2["value"] = uptime_s;
    m2["unit"] = "s";
    m2["timestamp"] = isoTimestamp;

    String output;
    serializeJson(doc, output);
    return output;
}
#else
#include <string>
inline std::string serializePayload(uint32_t cycleCount, const char* isoTimestamp,
                                     const char* deviceId, uint32_t uptime_s = 0) {
    JsonDocument doc;
    doc["device_id"] = deviceId;
    doc["device_type"] = "norvi";
    doc["timestamp"] = isoTimestamp;
    doc["node_health"] = "ONLINE";

    // Dynamic metrics[] array — each entry has {name, value, unit, timestamp}.
    JsonArray metrics = doc["metrics"].to<JsonArray>();

    // Metric 1: production_cycle
    JsonObject m1 = metrics.add<JsonObject>();
    m1["name"] = "production_cycle";
    m1["value"] = cycleCount;
    m1["unit"] = "count";
    m1["timestamp"] = isoTimestamp;

    // Metric 2: uptime_s
    JsonObject m2 = metrics.add<JsonObject>();
    m2["name"] = "uptime_s";
    m2["value"] = uptime_s;
    m2["unit"] = "s";
    m2["timestamp"] = isoTimestamp;

    std::string output;
    serializeJson(doc, output);
    return output;
}
#endif

#endif // PAYLOAD_SERIALIZER_H
