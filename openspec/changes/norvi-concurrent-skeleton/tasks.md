# Implementation Tasks: Norvi Concurrent Skeleton

This document breaks down the implementation of Sub-Épica 1.1 into atomic, sequential work units.

## Phase 1: Environment Initialization
- [x] Initialize the PlatformIO project in `edge_nodes/norvi_monitor/`.
- [x] Create `edge_nodes/norvi_monitor/platformio.ini` configured for `esp32dev`, Arduino framework, `115200` baud rate, and compilation optimizations.

## Phase 2: C++ Application Skeleton & Dependencies
- [x] Create `edge_nodes/norvi_monitor/src/main.cpp`.
- [x] Define the `ProductionEvent` C struct with `cycleCount`, `timestamp`, and `pinId`.
- [x] Declare the global FreeRTOS `QueueHandle_t` initialized to hold exactly 50 elements.
- [x] Define the hardware configuration variables (e.g., `INPUT_PIN = 18`).

## Phase 3: Non-Blocking ISR
- [x] Implement the `IRAM_ATTR` interrupt handler function.
- [x] Implement the 50ms software debounce logic using `esp_timer_get_time()`.
- [x] Within the ISR, construct the `ProductionEvent` and enqueue it asynchronously using `xQueueSendFromISR()`.

## Phase 4: FreeRTOS Tasks & Pinning
- [x] Implement `vTaskNetworking` (Core 0): Add a blocking loop using `xQueueReceive()`, logging dequeued events, and a placeholder delay.
- [x] Implement `vTaskApplication` (Core 1): Add a local processing loop with a placeholder `vTaskDelay`.
- [x] Within `setup()`, configure the input pin with `pinMode()` and `attachInterrupt()` for `FALLING` edges.
- [x] Within `setup()`, instantiate the tasks using `xTaskCreatePinnedToCore()` targeting Core 0 and Core 1 respectively.
- [x] Leave `loop()` practically empty with a long `vTaskDelay()`.
