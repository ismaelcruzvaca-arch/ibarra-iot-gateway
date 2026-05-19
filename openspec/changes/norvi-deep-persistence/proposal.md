# Proposal: Norvi Deep Persistence (Spooling)

This proposal outlines the integration of non-volatile storage (LittleFS) to protect telemetry data against network outages on the Norvi monitor node.

## Quick Path

1. **Filesystem**: Utilize `LittleFS` (superior wear-leveling compared to SPIFFS) to mount the flash memory. If mounting fails, automatically format the partition.
2. **Offline Data Spooling**: When the network state is not `STATE_CONNECTED` (e.g., disconnected or connecting), events are extracted from the FreeRTOS queue and appended to a binary file (`/spool.dat`) to free up RAM and prevent data loss.
3. **Recovery Flush**: Upon successfully entering `STATE_CONNECTED`, the system enters a recovery mode. It reads historical events from `/spool.dat` in small, non-blocking chunks, publishes them via MQTT retaining their original timestamps, and deletes the file upon completion.
4. **Non-Blocking I/O**: Flash operations are relatively slow. To prevent starving Core 0 or triggering the Task Watchdog (TWDT), file I/O and publishing loops will process data in batches (e.g., 5-10 events per cycle) interspersed with `vTaskDelay()`.

---

## Architecture Updates

### State Machine Enhancements

The existing `vTaskNetworking` state machine will be augmented:
* **In Offline States** (`STATE_DISCONNECTED`, `STATE_CONNECTING_WIFI`, `STATE_SYNCING_TIME`, `STATE_CONNECTING_MQTT`):
  Instead of letting the `productionEventQueue` fill up and potentially drop events, a background process will periodically poll the queue and append the `ProductionEvent` structs directly to `/spool.dat` in binary format.
* **In `STATE_CONNECTED`**:
  Before processing new events from the live queue, the state machine checks for the existence of `/spool.dat`. If present, it enters a sub-state (e.g., `FLUSHING_SPOOL`) where it reads chunks of events, serializes them using the existing `PayloadSerializer`, publishes them, and yields control. Once the file is fully processed, it is deleted, and normal live queue processing resumes.

### Data Format

Data will be written in raw binary format (append mode `"a"`). Since the `ProductionEvent` struct has a fixed size, it allows for deterministic reading and writing.

```cpp
// Existing struct
struct ProductionEvent {
    uint32_t cycleCount;
    unsigned long timestamp; // Milliseconds since boot
    uint8_t pinId;
};
```
*Note:* The timestamp is relative to boot time. When spooling, we must convert this to a UTC epoch timestamp *before* saving to Flash, because a device reboot would invalidate the relative boot time.

## Risks & Tradeoffs

* **Flash Wear**: Frequent writes to Flash memory degrade its lifespan.
  * *Mitigation*: LittleFS provides wear leveling. We will buffer queue events and write in chunks to minimize filesystem overhead.
* **Timestamp Validity**: If the device boots offline (no NTP sync yet), it cannot generate absolute UTC timestamps for spooled events.
  * *Mitigation*: The NTP block in `STATE_SYNCING_TIME` prevents MQTT connection. If events occur *before* the first NTP sync, we might need a fallback mechanism or discard them if absolute time is strictly required by the backend. We will assume events are buffered with relative times and converted when read, assuming the device doesn't reboot before syncing. If a reboot occurs, relative times are lost. To be completely robust, events generated before the *first* NTP sync might be problematic.
