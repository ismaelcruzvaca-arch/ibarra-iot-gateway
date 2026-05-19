# Norvi Concurrent Skeleton Architecture

This proposal defines the concurrent multi-core firmware skeleton for the ESP32-based Norvi IIOT monitor, enabling high-performance, real-time edge processing. It establishes isolated networking on Core 0 and application logic on Core 1, decoupled via a thread-safe Queue, with interrupt-driven digital inputs and a non-blocking 50ms software debounce.

## Quick Path

1. **Verify setup**: Inspect the proposed `platformio.ini` and `src/main.cpp`.
2. **Deploy to Norvi**: Upload the code using PlatformIO.
3. **Trigger Input**: Connect digital input 1 (GPIO 18) to ground (FALLING edge) to trigger a `ProductionEvent`.
4. **Inspect output**: Verify via Serial Monitor (`115200` baud) that Core 0 dequeues and processes events while Core 1 runs application telemetry concurrently.

## Details

| Topic | Decision |
|-------|----------|
| **Core Pinning** | Pin `vTaskNetworking` to Core 0 (PRO_CPU) and `vTaskApplication` to Core 1 (APP_CPU) to isolate communication latencies from application processing. |
| **Queue Decoupling** | Use a thread-safe FreeRTOS `QueueHandle_t` transmitting `ProductionEvent` structs to pass events from Core 1 (ISR) to Core 0 (Networking). |
| **IRAM Interrupt Handler** | Configure `handleDigitalInputISR` with the `IRAM_ATTR` attribute to run the ISR from fast instruction RAM, minimizing latency. |
| **Debounce Calculation** | Implement a non-blocking 50ms software debounce inside the ISR by comparing microsecond timestamps via `esp_timer_get_time()`. |

---

## Architectural Design

### 1. Dual-Core Allocation (Core Pinning)
The ESP32 microcontroller features two Xtensa 32-bit LX6 cores (Core 0 / PRO_CPU and Core 1 / APP_CPU). 
* **Core 0 (PRO_CPU)** handles the ESP32 WiFi/Bluetooth protocol stack (LwIP). By pinning the `vTaskNetworking` task to Core 0, network latency and blocking API calls (e.g. MQTT publishes, TCP connections) do not affect application-level responsiveness.
* **Core 1 (APP_CPU)** is reserved for user code. Pinning `vTaskApplication` to Core 1 ensures critical local control loops, sensor polling, and data logs are not interrupted by network congestion or protocol retries.

```mermaid
graph TD
    subgraph Core 0 (PRO_CPU)
        NetTask[vTaskNetworking]
        WiFiStack[WiFi & LwIP Stack]
        NetTask --> WiFiStack
    end
    subgraph Core 1 (APP_CPU)
        AppTask[vTaskApplication]
        ISR[IRAM_ATTR ISR]
    end
    ISR -- xQueueSendFromISR --> Queue[(productionEventQueue)]
    Queue -- xQueueReceive (blocks) --> NetTask
```

### 2. Thread-Safe Queue Decoupling
An asynchronous FIFO queue (`productionEventQueue`) handles communication between the ISR / Core 1 and the networking consumer on Core 0. The queue uses standard FreeRTOS thread-safe mechanisms, ensuring no race conditions or state corruption.
The transmitted message is a lightweight `ProductionEvent` struct:
```cpp
struct ProductionEvent {
    unsigned long timestamp; // Millisecond timestamp
    uint32_t eventId;        // Monotonically increasing ID
};
```

### 3. IRAM Interrupt Service Routine (ISR)
To capture rapid production line signals, the digital input pin is configured with an edge-triggered hardware interrupt. The handler is decorated with `IRAM_ATTR` to load the code into the ESP32's fast internal Instruction RAM rather than slower Flash memory.
* **Pin Logic**: Norvi digital inputs utilize optocouplers, causing active-low logic (HIGH when open, LOW when closed/triggered).
* **Trigger Type**: Configured for `FALLING` edges to capture the transition from inactive to active state.

### 4. Non-Blocking 50ms Software Debounce
Physical contacts and sensors suffer from signal bounce. Rather than blocking the processor with a delay (which is illegal and causes crashes in an ISR), we implement a non-blocking software debounce using the ESP32 system timer:
* The ISR retrieves the current system time in microseconds via `esp_timer_get_time()`.
* It compares this to `lastInterruptTime` (stored in microseconds).
* If the difference is less than 50,000 microseconds (50ms), the interrupt is discarded.
* If valid, `lastInterruptTime` is updated, the event counter is incremented, and the event is pushed to the queue.

---

## Proposed Configurations and Code

### `edge_nodes/norvi_monitor/platformio.ini`
```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
monitor_speed = 115200
build_flags = 
    -DCORE_DEBUG_LEVEL=3
    -DINPUT_PIN=18
```

### `edge_nodes/norvi_monitor/src/main.cpp`
```cpp
#include <Arduino.h>

// Struct for production events
struct ProductionEvent {
    unsigned long timestamp;
    uint32_t eventId;
};

// Queue configuration
#define QUEUE_LENGTH 50
QueueHandle_t productionEventQueue = NULL;

// Hardware GPIO pin for digital input (default to GPIO 18, typical for Norvi)
#ifndef INPUT_PIN
#define INPUT_PIN 18
#endif

// Debounce settings
#define DEBOUNCE_TIME_US 50000 // 50ms in microseconds
volatile uint64_t lastInterruptTime = 0;
volatile uint32_t eventCounter = 0;

// ISR for digital input with IRAM_ATTR
void IRAM_ATTR handleDigitalInputISR() {
    uint64_t currentTime = esp_timer_get_time();
    if (currentTime - lastInterruptTime > DEBOUNCE_TIME_US) {
        lastInterruptTime = currentTime;
        eventCounter++;

        ProductionEvent event;
        event.timestamp = (unsigned long)(currentTime / 1000);
        event.eventId = eventCounter;

        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        // Write to queue without blocking (non-blocking ISR send)
        xQueueSendFromISR(productionEventQueue, &event, &xHigherPriorityTaskWoken);
        if (xHigherPriorityTaskWoken == pdTRUE) {
            portYIELD_FROM_ISR();
        }
    }
}

// Task handles
TaskHandle_t xNetworkingTaskHandle = NULL;
TaskHandle_t xApplicationTaskHandle = NULL;

// Networking task running on Core 0
void vTaskNetworking(void *pvParameters) {
    ProductionEvent event;
    Serial.println("[Networking] Task started on Core 0");

    for (;;) {
        // Read from queue (blocking wait until an item is available)
        if (xQueueReceive(productionEventQueue, &event, portMAX_DELAY) == pdTRUE) {
            Serial.printf("[Networking] Dequeued event ID: %u at timestamp %lu ms\n", 
                          event.eventId, event.timestamp);
            // Simulate network transmission delay
            vTaskDelay(pdMS_TO_TICKS(100)); 
        }
    }
}

// Application task running on Core 1
void vTaskApplication(void *pvParameters) {
    Serial.println("[Application] Task started on Core 1");

    for (;;) {
        // Simulates local business/processing logic (e.g. sensor polling, status logs)
        Serial.println("[Application] Running local telemetry checks...");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void setup() {
    Serial.begin(115200);
    while (!Serial) {
        delay(10);
    }
    Serial.println("[System] Initializing Norvi Concurrent Skeleton...");

    // Create the event queue
    productionEventQueue = xQueueCreate(QUEUE_LENGTH, sizeof(ProductionEvent));
    if (productionEventQueue == NULL) {
        Serial.println("[System] Error creating event queue!");
        while (1) {
            delay(1000);
        }
    }

    // Configure the digital input pin 
    // INPUT mode is used as Norvi optocoupler boards feature external pullups
    pinMode(INPUT_PIN, INPUT);

    // Attach interrupt to input pin on FALLING edge (active-low transition)
    attachInterrupt(digitalPinToInterrupt(INPUT_PIN), handleDigitalInputISR, FALLING);
    Serial.printf("[System] Interrupt attached to GPIO %d (FALLING edge)\n", INPUT_PIN);

    // Create FreeRTOS tasks pinned to cores
    xTaskCreatePinnedToCore(
        vTaskNetworking,
        "TaskNetworking",
        4096,               // Stack size in words (16KB)
        NULL,               // Parameter
        2,                  // Priority (higher priority to process networking fast)
        &xNetworkingTaskHandle,
        0                   // Core 0 (PRO_CPU)
    );

    xTaskCreatePinnedToCore(
        vTaskApplication,
        "TaskApplication",
        4096,               // Stack size in words (16KB)
        NULL,               // Parameter
        1,                  // Priority
        &xApplicationTaskHandle,
        1                   // Core 1 (APP_CPU)
    );

    Serial.println("[System] Tasks successfully initialized.");
}

void loop() {
    // Put loopTask to sleep. Tasks manage all runtime logic.
    vTaskDelay(pdMS_TO_TICKS(10000));
}
```

---

## Risks, Tradeoffs, and Assumptions

### Risks
* **Queue Overflow**: If events occur faster than Core 0 can transmit them over the network, `productionEventQueue` will fill up, causing newer events to be dropped. 
  * *Mitigation*: The `QUEUE_LENGTH` is set to 50. If needed, this can be increased, and we can log dropped events in the ISR using a volatile counter.
* **Shared Serial Console**: Both tasks write to `Serial` (UART0). Concurrent writes may cause interleaved and corrupted output on the console.
  * *Mitigation*: In production, Serial printing should be minimized or protected with a Mutex if accessed from multiple tasks.

### Tradeoffs
* **FreeRTOS Overheads**: Using multi-core tasks introduces slight scheduling and context-switching overheads compared to a single monolithic loop.
  * *Benefit*: Isolation guarantees that network latencies or connectivity loss do not freeze edge telemetry or interrupt time-sensitive pulse counting.

### Assumptions
* **Norvi Pin Mapping**: Assumes digital input 1 maps to GPIO 18. This is common across several Norvi IIOT-AE models, but must be cross-referenced with the target model's schematic before loading.

---

## Next Steps

1. **Review and Approval**: Main agent and user to review this architectural proposal.
2. **Apply Phase**: Once approved, create the files `platformio.ini` and `src/main.cpp` in `edge_nodes/norvi_monitor/`.
3. **Hardware Test**: Compile and run the firmware on target hardware to verify interrupt execution and core separation.
