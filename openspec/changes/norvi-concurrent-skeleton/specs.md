# Technical Specifications: Norvi Concurrent Skeleton

These specifications formalize the concurrent architecture for the Norvi IIOT edge node (Sub-Épica 1.1). The core architectural constraints defined here are immutable.

## Immutable Constraints

| Area | Decision | Status |
|------|----------|--------|
| **Core Pinning** | `vTaskNetworking` MUST be pinned to Core 0 (PRO_CPU). `vTaskApplication` MUST be pinned to Core 1 (APP_CPU). | **IMMUTABLE** |
| **ISR Execution** | The digital input interrupt handler MUST be decorated with `IRAM_ATTR` to execute from internal RAM. | **IMMUTABLE** |
| **Queue Capacity** | The FreeRTOS event queue (`productionEventQueue`) MUST be sized for exactly **50** elements. | **IMMUTABLE** |
| **Software Debounce** | Debouncing MUST be non-blocking, using `esp_timer_get_time()`, discarding pulses under 50ms (50,000 µs). | **IMMUTABLE** |

## Data Structures

The system uses a thread-safe FreeRTOS queue (`QueueHandle_t`) to decouple the ISR (Core 1) from the network consumer (Core 0). 

The C struct passed through the queue MUST exactly match the following signature:

```cpp
struct ProductionEvent {
    uint32_t cycleCount;      // Monotonically increasing counter for production cycles
    unsigned long timestamp;  // Local timestamp of the event in milliseconds
    uint8_t pinId;            // The hardware ID of the digital pin that triggered the event
};
```
