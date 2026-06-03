# Implementation Tasks: Norvi Deep Persistence (Spooling)

This document breaks down the integration of LittleFS spooling into atomic implementation steps.

## Phase 1: Environment & Filesystem Initialization
- [ ] Add `board_build.filesystem = littlefs` to `edge_nodes/norvi_monitor/platformio.ini` if required by the PlatformIO ESP32 builder.
- [ ] In `main.cpp` `setup()`, include `<LittleFS.h>` and call `LittleFS.begin(true)` to mount or format the filesystem.

## Phase 2: Spooler Helper Operations (TDD)
- [ ] Create `edge_nodes/norvi_monitor/src/DataSpooler.h` to encapsulate LittleFS file operations.
  - Implement `appendEvent(const SpooledEvent& event)`: Opens `/spool.dat` in `"a"` mode and writes the struct.
  - Implement `readChunk(SpooledEvent* buffer, size_t maxCount, size_t offset)`: Reads a chunk of events from a given offset.
  - Implement `clearSpool()`: Removes `/spool.dat`.
- [ ] Implement static structure or mock-based unit tests for `DataSpooler` in `test_main.cpp`.

## Phase 3: State Machine Integration (Core 0)
- [ ] Update `vTaskNetworking` in `main.cpp` to monitor the queue during offline states (`STATE_DISCONNECTED`, `STATE_CONNECTING_WIFI`, `STATE_SYNCING_TIME`, `STATE_CONNECTING_MQTT`).
- [ ] In offline states, dequeue events, calculate absolute epoch time (if NTP has successfully synced before), populate a `SpooledEvent`, and call `DataSpooler::appendEvent()`.
- [ ] In `STATE_CONNECTED`, before the live queue polling loop, check if `/spool.dat` exists.
- [ ] If it exists, execute a non-blocking recovery loop:
  - Read chunks of up to 10 `SpooledEvent` structs.
  - Serialize each to Universal JSON and publish.
  - Delay `vTaskDelay(pdMS_TO_TICKS(50))` between chunks.
  - Once EOF is reached, call `clearSpool()`.
- [ ] Ensure that live `productionEventQueue` polling only resumes after the spool is fully flushed and cleared.
