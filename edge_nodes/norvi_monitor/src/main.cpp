#include <Arduino.h>

#ifndef INPUT_PIN
#define INPUT_PIN 18
#endif

// Struct matching specification
struct ProductionEvent {
    uint32_t cycleCount;      // Monotonically increasing counter for production cycles
    unsigned long timestamp;  // Local timestamp of the event in milliseconds
    uint8_t pinId;            // The hardware ID of the digital pin that triggered the event
};

// Queue handle
QueueHandle_t productionEventQueue = NULL;

// ISR variables
volatile int64_t lastInterruptTime = 0;
volatile uint32_t globalCycleCount = 0;

// ISR decorated with IRAM_ATTR
void IRAM_ATTR inputISR() {
    int64_t currentTime = esp_timer_get_time();
    // 50ms software debounce (50,000 microseconds)
    if (currentTime - lastInterruptTime >= 50000) {
        lastInterruptTime = currentTime;
        globalCycleCount++;

        ProductionEvent event;
        event.cycleCount = globalCycleCount;
        event.timestamp = currentTime / 1000;
        event.pinId = INPUT_PIN;

        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        xQueueSendFromISR(productionEventQueue, &event, &xHigherPriorityTaskWoken);
        if (xHigherPriorityTaskWoken) {
            portYIELD_FROM_ISR();
        }
    }
}

// Task Networking (pinned to Core 0)
void vTaskNetworking(void *pvParameters) {
    ProductionEvent event;
    while (true) {
        if (xQueueReceive(productionEventQueue, &event, portMAX_DELAY) == pdTRUE) {
            Serial.printf("[Networking] Dequeued event: Pin ID: %u, Cycle Count: %u, Timestamp: %lu ms\n",
                          event.pinId, event.cycleCount, event.timestamp);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// Task Application (pinned to Core 1)
void vTaskApplication(void *pvParameters) {
    while (true) {
        Serial.println("[Application] Active and processing logic...");
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

void setup() {
    Serial.begin(115200);
    while (!Serial) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    Serial.println("[System] Initializing Norvi Concurrent Monitor...");

    // Create FreeRTOS Queue for 50 elements
    productionEventQueue = xQueueCreate(50, sizeof(ProductionEvent));
    if (productionEventQueue == NULL) {
        Serial.println("[System] Error: Failed to create productionEventQueue!");
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    // Configure Pin and Attach Interrupt
    pinMode(INPUT_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(INPUT_PIN), inputISR, FALLING);
    Serial.printf("[System] Configured Input Pin: %d with FALLING interrupt\n", INPUT_PIN);

    // Create & Pin Tasks
    // Pinned to Core 0 (PRO_CPU)
    xTaskCreatePinnedToCore(
        vTaskNetworking,
        "vTaskNetworking",
        4096,
        NULL,
        1,
        NULL,
        0
    );
    Serial.println("[System] Task 'vTaskNetworking' pinned to Core 0");

    // Pinned to Core 1 (APP_CPU)
    xTaskCreatePinnedToCore(
        vTaskApplication,
        "vTaskApplication",
        4096,
        NULL,
        1,
        NULL,
        1
    );
    Serial.println("[System] Task 'vTaskApplication' pinned to Core 1");

    Serial.println("[System] Initialization complete.");
}

void loop() {
    // Keep loop empty, delay to yield control
    vTaskDelay(pdMS_TO_TICKS(10000));
}
