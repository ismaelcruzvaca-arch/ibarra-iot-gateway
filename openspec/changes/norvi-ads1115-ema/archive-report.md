# Archive Report: ADS1115 & EMA Filter Integration

This document completes and seals the SDD cycle for the ADS1115 ADC and DSP EMA filter integration on the Norvi monitor node.

## Final Implementation State

The following deliverables have been successfully deployed and verified:

### 1. platformio.ini
Updated dependency configuration:
* `adafruit/Adafruit ADS1X15`
* `adafruit/Adafruit BusIO`
* `Wire`
* `SPI`

### 2. src/EMAFilter.h
C++ Exponential Moving Average filter implementation:
* Initialization logic preventing startup lag: $y[0] = x[0]$.
* State functions: `filter(float)`, `getValue()`, `reset()`, `setAlpha(float)`.

### 3. test/test_main.cpp
Comprehensive TDD tests:
* `test_ema_initialization` (Lag avoidance)
* `test_ema_step_update` (Math correctness)
* `test_ema_alpha_bounds` (Alpha boundary limits $0.0$ and $1.0$)
* `test_ema_convergence` (Convergence over iterations)
* `test_ema_reset` (State cleanup)

### 4. src/main.cpp
Integration layout:
* Hardware I2C initialized on standard ESP32 pins GPIO 21/22.
* ADS1115 instance began at I2C address `0x48` and configured with `GAIN_ONE`.
* Dedicated acquisition task `vTaskAnalog` running on Core `1` (APP_CPU) every 100ms.

All artifacts are persisted under `openspec/changes/norvi-ads1115-ema/`.
This concludes the Sub-Épica 1.2 cycle.
