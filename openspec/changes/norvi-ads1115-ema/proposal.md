# Proposal: ADS1115 Integration and EMA Filter

This proposal outlines the hardware integration of the ADS1115 16-bit analog-to-digital converter (ADC) via I2C and the implementation of a configurable Exponential Moving Average (EMA) filter on the ESP32. The analog sampling and filtering logic are pinned to Core 1 (APP_CPU) to guarantee real-time telemetry processing isolation from Core 0 network communications.

## Quick Path

1. **Configure dependencies**: Add `adafruit/Adafruit ADS1X15` to the PlatformIO configuration.
2. **Implement EMA Filter**: Create `src/EMAFilter.h` with initial condition handling to avoid ramp-up lag.
3. **TDD Verification**: Write Unit Tests for the `EMAFilter` class in `test/test_main.cpp` using Unity.
4. **Setup I2C & ADS1115**: Configure `Wire` (SDA=21, SCL=22) and initialize ADS1115 at address `0x48`.
5. **Periodic Task Pinning**: Deploy a dedicated task `vTaskAnalog` pinned to Core 1 to poll the ADC and update the filter.

## Details

| Topic | Decision |
|---|---|
| **I2C Pinout** | Use standard ESP32 I2C pins: SDA = GPIO 21, SCL = GPIO 22. |
| **I2C Address** | Standard ADS1115 I2C address `0x48` (ADDR pin to GND). |
| **ADC Gain** | Set gain to `GAIN_ONE` (FS range +/- 4.096V) with VDD at 3.3V. |
| **EMA Algorithm** | `Filtered = (Alpha * Raw) + ((1 - Alpha) * Previous)` with default `Alpha = 0.1` (configurable). |
| **Initial State** | Initialize the filter with the first raw ADC reading to eliminate initial ramp-up lag. |
| **Core Pinning** | Execute ADC polling and filtering inside a task pinned to Core 1 (APP_CPU). |

---

## Architectural Design

### 1. Hardware Integration
The ADS1115 is a high-resolution, low-power 16-bit delta-sigma ADC. It communicates via I2C:
* **Connections**:
  - `VDD` to `3.3V`
  - `GND` to `GND`
  - `SDA` to `GPIO 21` (ESP32 hardware I2C SDA)
  - `SCL` to `GPIO 22` (ESP32 hardware I2C SCL)
  - `ADDR` to `GND` (sets I2C address to `0x48`)
* **Library**: The project will utilize the `Adafruit ADS1X15` library, which provides a clean C++ interface to read single-ended and differential channels.
* **Gain & Input Range**: Operating the ESP32 at 3.3V means the ADC's analog inputs should not exceed `3.3V`. A gain configuration of `GAIN_ONE` (FS +/- 4.096V) is chosen. This yields a resolution of 0.125mV per LSB, which is highly accurate and prevents ADC saturation.

### 2. EMA Filter Design
Analog inputs are susceptible to high-frequency noise. An Exponential Moving Average (EMA) filter will be implemented in C++ to smooth the signal without the memory overhead of a standard moving average.
* **Mathematical Formula**:
  $$Filtered_n = \alpha \cdot Raw_n + (1 - \alpha) \cdot Filtered_{n-1}$$
  Where $\alpha$ (Alpha) is the smoothing factor ($0 < \alpha \le 1$). A default of `0.1` will be used.
* **Initial Condition Handling**: To prevent the filter from slowly ramping up from 0 to the actual measured value (which introduces a severe lag on startup), the filter must detect the very first reading and initialize $Filtered_0 = Raw_0$.
* **Implementation Structure**: A header-only C++ class `EMAFilter` will be created at `edge_nodes/norvi_monitor/src/EMAFilter.h` to allow easy inclusion in both the main application and test code.

### 3. Core Pinning & Execution
To prevent network latencies (Core 0, WiFi/MQTT stack) from affecting analog sampling frequencies, the polling and filtering will be executed entirely on Core 1 (APP_CPU).
* **Execution Task**: A dedicated `vTaskAnalog` task running on Core 1 is preferred. This allows scheduling analog polling at a higher frequency (e.g., 100ms) with high temporal consistency (using `vTaskDelay`), without cluttering `vTaskApplication` which is used for slower telemetry checks (currently every 5 seconds).

```mermaid
graph TD
    subgraph Core 0 (PRO_CPU)
        NetTask[vTaskNetworking]
    end
    subgraph Core 1 (APP_CPU)
        AppTask[vTaskApplication]
        AnalogTask[vTaskAnalog]
        
        AnalogTask -->|Read ADC & Apply EMA| ADS1115[(ADS1115 ADC)]
        AnalogTask -->|Log / Store filtered value| Mem[Global/Shared State]
    end
```

---

## Risks, Tradeoffs, and Assumptions

### Risks
* **I2C Bus Contention**: If other I2C sensors are added later, calling `Wire` APIs from multiple tasks could cause bus collisions.
  - *Mitigation*: Restrict I2C access exclusively to the analog polling task, or wrap I2C transactions in a Mutex.
* **ADC Startup Delay**: The ADS1115 requires a small initialization time. If read immediately during boot, it might return stale data or error.
  - *Mitigation*: Ensure `ads.begin()` is checked in `setup()`, and add a short delay before starting the polling task.

### Tradeoffs
* **Floating Point Overhead**: ESP32 has a hardware FPU (Single Precision), so float operations are fast, but using float arithmetic in a tight loop is slightly more expensive than integer/fixed-point math.
  - *Benefit*: High readability and precision, which matches the 16-bit resolution of the ADS1115.

### Assumptions
* **Supply Voltage**: VDD is stable at 3.3V, ensuring the reference voltage does not drift.
* **I2C Pins**: GPIO 21 and 22 are free and not shared with conflicting hardware.

---

## Next Steps
1. **Review Spec**: Verify mathematical correctness and TDD specifications.
2. **Implement EMA Filter Class & Tests**: Write the unit tests and implement the filter.
3. **Integrate and Verify**: Add the ADS1115 library, wire the hardware, and deploy.
