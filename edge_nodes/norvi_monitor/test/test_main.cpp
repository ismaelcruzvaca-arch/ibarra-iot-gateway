#include <unity.h>
#include "../src/EMAFilter.h"
#include "../src/PayloadSerializer.h"


struct ProductionEvent {
    uint32_t cycleCount;
    unsigned long timestamp;
    uint8_t pinId;
};

struct SpooledEvent {
    uint32_t cycleCount;
    time_t epochTime;
};

// Pure function for debounce logic to test
bool check_debounce(uint64_t current_us, uint64_t last_us, uint64_t debounce_us) {
    return (current_us - last_us) >= debounce_us;
}

void test_production_event_struct_members(void) {
    ProductionEvent event;
    event.cycleCount = 100;
    event.timestamp = 123456;
    event.pinId = 18;

    TEST_ASSERT_EQUAL_UINT32(100, event.cycleCount);
    TEST_ASSERT_EQUAL_UINT32(123456, event.timestamp);
    TEST_ASSERT_EQUAL_UINT8(18, event.pinId);
}

void test_spooled_event_struct_members(void) {
    SpooledEvent event;
    event.cycleCount = 100;
    event.epochTime = 1680000000;

    TEST_ASSERT_EQUAL_UINT32(100, event.cycleCount);
    TEST_ASSERT_EQUAL(4, sizeof(event.cycleCount));
    TEST_ASSERT_GREATER_OR_EQUAL(4, sizeof(event.epochTime));
}

void test_debounce_logic_pass(void) {
    // 50ms is 50000us. Difference is 50000us (exact boundary)
    TEST_ASSERT_TRUE(check_debounce(50000, 0, 50000));
    // Difference is 50001us (> 50ms)
    TEST_ASSERT_TRUE(check_debounce(50001, 0, 50000));
}

void test_debounce_logic_block(void) {
    // Difference is 49999us (< 50ms)
    TEST_ASSERT_FALSE(check_debounce(49999, 0, 50000));
    // Difference is 0us
    TEST_ASSERT_FALSE(check_debounce(0, 0, 50000));
}

void test_ema_initialization(void) {
    EMAFilter filter(0.1f);
    TEST_ASSERT_EQUAL_FLOAT(1234.5f, filter.filter(1234.5f));
    TEST_ASSERT_EQUAL_FLOAT(1234.5f, filter.getValue());
}

void test_ema_step_update(void) {
    EMAFilter filter(0.1f);
    filter.filter(100.0f); // initialized to 100
    TEST_ASSERT_EQUAL_FLOAT(110.0f, filter.filter(200.0f));
}

void test_ema_alpha_bounds(void) {
    // Test alpha = 1.0 (no filtering)
    EMAFilter filter1(1.0f);
    TEST_ASSERT_EQUAL_FLOAT(150.0f, filter1.filter(150.0f));
    TEST_ASSERT_EQUAL_FLOAT(250.0f, filter1.filter(250.0f));
    TEST_ASSERT_EQUAL_FLOAT(350.0f, filter1.filter(350.0f));

    // Test alpha = 0.0 (total filtering, constant output)
    EMAFilter filter0(0.0f);
    TEST_ASSERT_EQUAL_FLOAT(100.0f, filter0.filter(100.0f));
    TEST_ASSERT_EQUAL_FLOAT(100.0f, filter0.filter(200.0f));
    TEST_ASSERT_EQUAL_FLOAT(100.0f, filter0.filter(300.0f));
}

void test_ema_convergence(void) {
    EMAFilter filter(0.1f);
    filter.filter(0.0f); // initialized to 0.0
    for (int i = 0; i < 100; i++) {
        filter.filter(100.0f);
    }
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 100.0f, filter.getValue());
}

void test_ema_reset(void) {
    EMAFilter filter(0.1f);
    filter.filter(150.0f);
    filter.filter(200.0f);
    filter.reset();
    TEST_ASSERT_EQUAL_FLOAT(50.0f, filter.filter(50.0f));
}

void test_payload_serialization_structure(void) {
#ifdef ARDUINO
    String outputJson = serializePayload(42, "2026-05-19T21:20:00Z", "norvi_test_01");
#else
    std::string outputJson = serializePayload(42, "2026-05-19T21:20:00Z", "norvi_test_01", 3600);
#endif

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, outputJson);
    TEST_ASSERT_TRUE(err == DeserializationError::Ok);

    // ── Required top-level fields ────────────────────────────────────────────
    TEST_ASSERT_EQUAL_STRING("norvi_test_01", doc["device_id"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("norvi", doc["device_type"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("2026-05-19T21:20:00Z", doc["timestamp"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("ONLINE", doc["node_health"].as<const char*>());

    // ── metrics[0] — production_cycle ────────────────────────────────────────
    TEST_ASSERT_EQUAL_STRING("production_cycle", doc["metrics"][0]["name"].as<const char*>());
    TEST_ASSERT_EQUAL_INT(42, doc["metrics"][0]["value"].as<int>());
    TEST_ASSERT_EQUAL_STRING("count", doc["metrics"][0]["unit"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("2026-05-19T21:20:00Z", doc["metrics"][0]["timestamp"].as<const char*>());

    // ── metrics[1] — uptime_s ────────────────────────────────────────────────
    TEST_ASSERT_EQUAL_STRING("uptime_s", doc["metrics"][1]["name"].as<const char*>());
#ifdef ARDUINO
    TEST_ASSERT_GREATER_THAN(0, doc["metrics"][1]["value"].as<uint32_t>());
#else
    TEST_ASSERT_EQUAL_INT(3600, doc["metrics"][1]["value"].as<int>());
#endif
    TEST_ASSERT_EQUAL_STRING("s", doc["metrics"][1]["unit"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("2026-05-19T21:20:00Z", doc["metrics"][1]["timestamp"].as<const char*>());
}


#ifdef ARDUINO
#include <Arduino.h>
void setup() {
    UNITY_BEGIN();
    RUN_TEST(test_production_event_struct_members);
    RUN_TEST(test_spooled_event_struct_members);
    RUN_TEST(test_debounce_logic_pass);
    RUN_TEST(test_debounce_logic_block);
    RUN_TEST(test_ema_initialization);
    RUN_TEST(test_ema_step_update);
    RUN_TEST(test_ema_alpha_bounds);
    RUN_TEST(test_ema_convergence);
    RUN_TEST(test_ema_reset);
    RUN_TEST(test_payload_serialization_structure);
    UNITY_END();
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}
#else
int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_production_event_struct_members);
    RUN_TEST(test_spooled_event_struct_members);
    RUN_TEST(test_debounce_logic_pass);
    RUN_TEST(test_debounce_logic_block);
    RUN_TEST(test_ema_initialization);
    RUN_TEST(test_ema_step_update);
    RUN_TEST(test_ema_alpha_bounds);
    RUN_TEST(test_ema_convergence);
    RUN_TEST(test_ema_reset);
    RUN_TEST(test_payload_serialization_structure);
    return UNITY_END();
}
#endif

