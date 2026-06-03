# Technical Specifications: Norvi Network and NTP Integration

These specifications define the network connection state machine, NTP time validation, mTLS configuration, and MQTT serialization rules.

## Immutable Constraints

| Area | Constraint | Status |
|---|---|---|
| **Core Pinning** | `vTaskNetworking` MUST execute on Core 0 (PRO_CPU). | **IMMUTABLE** |
| **NTP Validation** | Initialization of MQTTS MUST be blocked until the system year > 2024. | **IMMUTABLE** |
| **mTLS Security** | Connections to the broker MUST use `WiFiClientSecure` with certificates (Zero Trust). | **IMMUTABLE** |
| **MQTT Port** | The MQTT secure port MUST be set to `8883`. | **IMMUTABLE** |
| **MQTT Topic** | Telemetry payload MUST be published to `novamex/ibarra/production/norvi_001`. | **IMMUTABLE** |
| **Universal JSON** | Every payload MUST contain `node_health` and a `metrics` array (ISO 8601 timestamps). | **IMMUTABLE** |
| **Stack Allocation**| `vTaskNetworking` MUST have at least `8192` bytes stack size. | **IMMUTABLE** |

---

## Connection State Transition Table

The state machine transitions are defined based on network events:

| Current State | Event / Condition | Action | Next State |
|---|---|---|---|
| `STATE_DISCONNECTED` | Task start / Wi-Fi disconnected | Start `WiFi.begin()`, set timers | `STATE_CONNECTING_WIFI` |
| `STATE_CONNECTING_WIFI` | `WiFi.status() == WL_CONNECTED` | Call `configTime()` | `STATE_SYNCING_TIME` |
| `STATE_CONNECTING_WIFI` | Timeout reached (15 seconds) | Disconnect Wi-Fi, set backoff timer | `STATE_DISCONNECTED` |
| `STATE_SYNCING_TIME` | `time(nullptr)` returns year > 2024 | Configure certificates on client | `STATE_CONNECTING_MQTT` |
| `STATE_SYNCING_TIME` | `WiFi.status() != WL_CONNECTED` | Reset NTP connection state | `STATE_DISCONNECTED` |
| `STATE_CONNECTING_MQTT` | `mqttClient.connect() == true` | Publish LWT / `ONLINE` status | `STATE_CONNECTED` |
| `STATE_CONNECTING_MQTT` | Connect failed / Timeout | Log error, start reconnect timer (5s) | `STATE_CONNECTING_MQTT` |
| `STATE_CONNECTING_MQTT` | `WiFi.status() != WL_CONNECTED` | Log disconnect | `STATE_DISCONNECTED` |
| `STATE_CONNECTED` | Dequeued `ProductionEvent` | Serialize to JSON, publish to topic | `STATE_CONNECTED` |
| `STATE_CONNECTED` | `mqttClient.connected() == false` | Log error | `STATE_CONNECTING_MQTT` |
| `STATE_CONNECTED` | `WiFi.status() != WL_CONNECTED` | Log disconnect | `STATE_DISCONNECTED` |

---

## Universal Payload Serialization Specifications

The JSON serialization helper `serializePayload` MUST construct a valid JSON matching:
* Root key `node_health` set to `"ONLINE"`.
* Root key `metrics` containing an array with a single metric object:
  - `name`: `"production_cycle"`
  - `value`: Integer (e.g. `event.cycleCount`)
  - `timestamp`: ISO 8601 formatted UTC time (e.g. `"YYYY-MM-DDTHH:MM:SSZ"`)

Example:
```json
{
  "node_health": "ONLINE",
  "metrics": [
    {
      "name": "production_cycle",
      "value": 12,
      "timestamp": "2026-05-19T21:20:00Z"
    }
  ]
}
```

---

## Unit Test (TDD) Specifications

The following TDD cases will be added to verify payload serialization:

### Test Case 1: JSON Payload Generation Structure
* **Given**: A `ProductionEvent` with `cycleCount = 42` and a Unix epoch time representing `2026-05-19T21:20:00Z`.
* **When**: The payload is serialized.
* **Then**: The resulting JSON must be parseable and match the key-value structures.
* **Assertions**:
  - `TEST_ASSERT_TRUE(deserializeJson(doc, outputJson) == DeserializationError::Ok)`
  - `TEST_ASSERT_EQUAL_STRING("ONLINE", doc["node_health"].as<const char*>())`
  - `TEST_ASSERT_EQUAL_INT(42, doc["metrics"][0]["value"].as<int>())`
  - `TEST_ASSERT_EQUAL_STRING("production_cycle", doc["metrics"][0]["name"].as<const char*>())`
  - `TEST_ASSERT_EQUAL_STRING("2026-05-19T21:20:00Z", doc["metrics"][0]["timestamp"].as<const char*>())`
