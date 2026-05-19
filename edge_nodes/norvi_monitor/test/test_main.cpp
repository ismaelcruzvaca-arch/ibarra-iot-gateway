#include <unity.h>

struct ProductionEvent {
    uint32_t cycleCount;
    unsigned long timestamp;
    uint8_t pinId;
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

#ifdef ARDUINO
#include <Arduino.h>
void setup() {
    UNITY_BEGIN();
    RUN_TEST(test_production_event_struct_members);
    RUN_TEST(test_debounce_logic_pass);
    RUN_TEST(test_debounce_logic_block);
    UNITY_END();
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}
#else
int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_production_event_struct_members);
    RUN_TEST(test_debounce_logic_pass);
    RUN_TEST(test_debounce_logic_block);
    return UNITY_END();
}
#endif
