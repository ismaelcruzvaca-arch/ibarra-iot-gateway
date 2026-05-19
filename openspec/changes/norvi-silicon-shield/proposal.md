# Proposal: Norvi Silicon Shield

This proposal details the implementation of hardware-level software protections to ensure maximum stability and data integrity for the Norvi monitor under adverse electrical conditions.

## Quick Path

1. **Task Watchdog Timer (TWDT)**: A 5-second panic-enabled Watchdog will monitor all active FreeRTOS tasks (`vTaskNetworking`, `vTaskApplication`, `vTaskAnalog`). If any task locks up (e.g., stuck I2C bus or stalled MQTT loop), the system will trigger a hard reset to recover autonomously.
2. **Extreme Brownout Detector (BOD)**: Voltage sags can cause Flash corruption (ruining the LittleFS spool) and garbage ADC readings. We will elevate the Brownout Detector's threshold so the ESP32 resets aggressively if the supply drops near 2.80V, prioritizing data safety over uptime.
3. **Hardware Security Documentation**: Burning eFuses for Flash Encryption (AES-256) and Secure Boot V2 is a physical, irreversible process. No C++ code will manage this. Instead, a dedicated `SECURITY.md` file will instruct operators on how to execute the `espefuse.py` commands in the production line prior to firmware flashing.

---

## Architecture Updates

### Task Watchdog Integration
*   The `<esp_task_wdt.h>` header will be imported into `main.cpp`.
*   In `setup()`, the TWDT will be initialized globally with `esp_task_wdt_init(5, true)`.
*   Every pinned task will subscribe to the watchdog using `esp_task_wdt_add(NULL)` upon initialization.
*   The core loops of all tasks will invoke `esp_task_wdt_reset()` in each iteration, proving they are alive and non-blocking.

### Brownout Detector Configuration
*   We will interact with the ESP32 RTC Control Registers (e.g., `soc/rtc_cntl_reg.h`) in `setup()` to forcefully configure the BOD threshold to a stringent voltage level, overriding default framework relaxed behaviors.

### Production Security Playbook
*   A new file, `SECURITY.md`, will be generated containing the precise ESP-IDF / esptool instructions to enforce Secure Boot V2 and AES-256 Flash Encryption via physical eFuse manipulation.

## Risks & Tradeoffs

*   **Aggressive Watchdogs**: If a network timeout or file flush unexpectedly exceeds 5 seconds, the system will panic.
    *   *Mitigation*: We already designed the `vTaskNetworking` spool flush to operate in tiny chunks with `vTaskDelay` interleaving. This guarantees the watchdog is fed predictably.
*   **Irreversible eFuses**: Executing the `SECURITY.md` commands physically locks the ESP32.
    *   *Mitigation*: The documentation will explicitly warn operators that this is a one-way trip and must only be executed on production silicon.
