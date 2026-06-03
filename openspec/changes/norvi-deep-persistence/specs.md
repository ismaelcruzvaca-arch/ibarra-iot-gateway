# Technical Specifications: Norvi Deep Persistence (Spooling)

## Immutable Constraints

| Area | Constraint | Status |
|---|---|---|
| **Filesystem** | MUST use `LittleFS`. `SPIFFS` is strictly forbidden due to poor wear leveling. | **IMMUTABLE** |
| **Auto-Format** | `LittleFS.begin(true)` MUST be used to automatically format the filesystem if mounting fails. | **IMMUTABLE** |
| **File Path** | Offline events MUST be appended to `/spool.dat`. | **IMMUTABLE** |
| **Non-Blocking I/O** | Flash read/write loops MUST NOT exceed chunks of 10 events per cycle and MUST yield or delay to avoid triggering Watchdogs. | **IMMUTABLE** |
| **Data Format** | Data MUST be serialized in raw binary format to `/spool.dat`. | **IMMUTABLE** |
| **Recovery Integrity**| Spooled events MUST retain their chronological accuracy and be published before any new real-time events. | **IMMUTABLE** |

---

## State Machine Integration

The `vTaskNetworking` task will incorporate the spooling mechanism across its states:

### 1. Offline Spooling (Disconnect/Connecting States)
When current state is **NOT** `STATE_CONNECTED`:
* The task polls `productionEventQueue` periodically.
* If an event is received, it calculates the **UTC Epoch Time** (if NTP has been synchronized at least once during this boot lifecycle).
* A specialized `SpooledEvent` struct containing the absolute Unix epoch timestamp and the `cycleCount` is written to `/spool.dat` in append mode (`"a"`).
* *Note*: If NTP has never synced, the event is either discarded or saved with a special marker to prevent sending 1970 epochs.

### 2. Recovery Flush (`STATE_CONNECTED`)
When the state machine enters `STATE_CONNECTED`:
* Check `LittleFS.exists("/spool.dat")`.
* If true, open the file in read mode (`"r"`).
* Read up to 10 `SpooledEvent` structs per loop iteration.
* Serialize each using `PayloadSerializer` (passing the stored absolute epoch time converted to ISO 8601).
* Publish via MQTT.
* `vTaskDelay` to allow other tasks (like analog sampling on Core 1) uninterrupted execution.
* Repeat until EOF.
* Close and `LittleFS.remove("/spool.dat")`.
* Only after deletion does the system resume reading from the live `productionEventQueue`.

---

## Data Structures

```cpp
// Struct used for Flash Storage to ensure absolute time is preserved across potential reboots
struct SpooledEvent {
    uint32_t cycleCount;
    time_t epochTime; // Absolute UTC epoch timestamp
};
```

## Unit Test (TDD) Specifications

### Test Case 1: Spooler File Operations
* **Given**: A simulated `LittleFS` environment or mock.
* **When**: Multiple `SpooledEvent` structs are appended, and then read back.
* **Then**: The read data perfectly matches the written data, and file removal successfully clears the storage.
* **Note**: In the ESP32 platform, LittleFS tests might require hardware or specific mock setups. Static structural verification will be prioritized if execution is restricted.
