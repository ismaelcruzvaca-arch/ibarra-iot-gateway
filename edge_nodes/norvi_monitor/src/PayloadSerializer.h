#ifndef PAYLOAD_SERIALIZER_H
#define PAYLOAD_SERIALIZER_H

#include <ArduinoJson.h>

#ifdef ARDUINO
#include <Arduino.h>
inline String serializePayload(uint32_t cycleCount, const char* isoTimestamp) {
    JsonDocument doc;
    doc["node_health"] = "ONLINE";
    JsonArray metrics = doc["metrics"].to<JsonArray>();
    JsonObject metric = metrics.add<JsonObject>();
    metric["name"] = "production_cycle";
    metric["value"] = cycleCount;
    metric["timestamp"] = isoTimestamp;

    String output;
    serializeJson(doc, output);
    return output;
}
#else
#include <string>
inline std::string serializePayload(uint32_t cycleCount, const char* isoTimestamp) {
    JsonDocument doc;
    doc["node_health"] = "ONLINE";
    JsonArray metrics = doc["metrics"].to<JsonArray>();
    JsonObject metric = metrics.add<JsonObject>();
    metric["name"] = "production_cycle";
    metric["value"] = cycleCount;
    metric["timestamp"] = isoTimestamp;

    std::string output;
    serializeJson(doc, output);
    return output;
}
#endif

#endif // PAYLOAD_SERIALIZER_H
