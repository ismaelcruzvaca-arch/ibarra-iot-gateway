# Technical Specifications: Norvi Silicon Shield

## Immutable Constraints

| Area | Constraint | Status |
|---|---|---|
| **TWDT Initialization** | `esp_task_wdt_init(5, true)` MUST be invoked exactly once in `setup()` to configure a 5-second panic window. | **IMMUTABLE** |
| **Task Registration** | All user tasks (`vTaskNetworking`, `vTaskApplication`, `vTaskAnalog`) MUST invoke `esp_task_wdt_add(NULL)`. | **IMMUTABLE** |
| **Watchdog Feeding** | All user tasks MUST invoke `esp_task_wdt_reset()` within their primary `while(true)` loops. | **IMMUTABLE** |
| **BOD Level** | The Brownout Detector threshold MUST be explicitly set to the 2.80V configuration using ESP-IDF system registry access. | **IMMUTABLE** |
| **Security Docs** | No C++ logic will burn eFuses. A `SECURITY.md` file MUST be written with production-line `espefuse.py` commands. | **IMMUTABLE** |

---

## State Machine & Execution Flow Integration

### Task Watchdog Timer (TWDT)
*   The ESP-IDF header `<esp_task_wdt.h>` handles the FreeRTOS WDT API.
*   Because tasks are pinned to independent cores, failure to feed the WDT implies a severe lockup (e.g., deadlock, stuck I2C bus, or network lock) affecting that specific core.
*   `esp_task_wdt_init(timeout, panic)` with `panic = true` forces a hard ESP32 reboot if any registered task violates the 5-second threshold.
*   The chunked processing inside `STATE_CONNECTED` (reading 10 events per iteration) provides enough execution yield and speed to safely feed the watchdog on Core 0.

### Brownout Detector (BOD) Calibration
The ESP32 possesses programmable Brownout trigger thresholds. By default, it might be too tolerant for sensitive analog acquisitions and Flash writes.
*   We will include the necessary RTC Control Register headers (`soc/soc.h` and `soc/rtc_cntl_reg.h`).
*   In `setup()`, the BOD threshold will be elevated to the highest safety level (approx. 2.80V) via `REG_WRITE` or ESP-IDF's internal structures to guarantee that if voltage sags below secure levels for LittleFS, the processor instantly halts and restarts.

## Production Security Documentation (`SECURITY.md`)

The `SECURITY.md` file will specify:
1.  **Secure Boot V2 Setup**: Instructions on generating a signing key and burning the `SECURE_BOOT_EN` eFuse.
2.  **Flash Encryption (AES-256)**: Instructions on enabling `FLASH_CRYPT_CNT`, utilizing the hardware RNG for the AES key, and disabling plaintext flash access.
3.  **JTAG/UART Disablement**: Commands to burn eFuses that permanently disable JTAG debugging and UART download mode, preventing physical silicon extraction.
