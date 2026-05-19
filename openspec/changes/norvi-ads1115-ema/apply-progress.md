# Apply Progress: ADS1115 & EMA Filter Integration

This document tracks the implementation progress of the ADS1115 I2C analog acquisition and the EMA filter on the Norvi monitor node.

## Progress Summary

All tasks from the planning phase have been successfully implemented and verified statically.

### Completed Work Units

1. **Dependency Configuration (`platformio.ini`)**
   - Appended `Adafruit ADS1X15`, `Adafruit BusIO`, `Wire`, and `SPI` libraries to `lib_deps`.

2. **EMA Filter Software Module (`src/EMAFilter.h`)**
   - Implemented `EMAFilter` C++ class.
   - Configured constructor with default $\alpha = 0.1$.
   - Designed mathematical step update: $y[n] = \alpha \cdot x[n] + (1 - \alpha) \cdot y[n-1]$.
   - Handled initial condition $y[0] = x[0]$ on first call to eliminate startup lag.
   - Implemented `getValue()`, `setAlpha(float)`, and `reset()` functions.

3. **Unit Tests & Verification (`test/test_main.cpp`)**
   - Expanded the test suite to include the 5 specified TDD test cases:
     - `test_ema_initialization`
     - `test_ema_step_update`
     - `test_ema_alpha_bounds`
     - `test_ema_convergence`
     - `test_ema_reset`
   - Registered and configured all test cases for execution in both desktop (`main()`) and embedded ESP32 (`setup()`) test runners.

4. **Hardware & Task Integration (`src/main.cpp`)**
   - Included all required headers (`Wire.h`, `Adafruit_ADS1X15.h`, `EMAFilter.h`).
   - Declared global `ads` and `emaFilter` instances.
   - Initialized hardware I2C bus on GPIO pins 21 (SDA) and 22 (SCL) via `Wire.begin(21, 22)`.
   - Initialized ADS1115 ADC at address `0x48`, configured gain to `GAIN_ONE` ($\pm 4.096\text{ V}$ range), and added startup verification checks.
   - Implemented polling task `vTaskAnalog` that reads channel A0, calculates voltage, applies the EMA filter, logs metrics to Serial, and delays for 100ms.
   - Pinned `vTaskAnalog` to Core 1 (`APP_CPU`) using FreeRTOS `xTaskCreatePinnedToCore` with priority `1`.
