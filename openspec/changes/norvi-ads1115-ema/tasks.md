# Implementation Tasks: ADS1115 Integration and EMA Filter

This document breaks down the implementation of the ADS1115 ADC integration and the EMA filter into atomic, sequential work units.

## Phase 1: Environment & Dependencies
- [x] Add the required libraries to the `lib_deps` section of `edge_nodes/norvi_monitor/platformio.ini`:
  - `adafruit/Adafruit ADS1X15`
  - `adafruit/Adafruit BusIO`
  - `Wire`
  - `SPI`

## Phase 2: EMA Filter Development (TDD)
- [x] Create `edge_nodes/norvi_monitor/src/EMAFilter.h` as a header-only utility containing the C++ `EMAFilter` class.
- [x] Implement mathematical logic and initialization checks in `EMAFilter::filter(float raw)` to assign the first reading directly to prevent startup lag.
- [x] Implement `EMAFilter::reset()` to set the filter back to an uninitialized state.
- [x] Expand `edge_nodes/norvi_monitor/test/test_main.cpp` to include the EMA Filter unit tests:
  - `test_ema_initialization`: Verifies first reading has zero lag.
  - `test_ema_step_update`: Verifies exact mathematical updates for subsequent readings.
  - `test_ema_alpha_bounds`: Verifies behavior at boundary limits $\alpha = 0.0$ and $\alpha = 1.0$.
  - `test_ema_convergence`: Verifies convergence to steady state.
  - `test_ema_reset`: Verifies correct re-initialization behavior.
- [x] Run PlatformIO tests (`pio test`) to verify all `EMAFilter` tests pass successfully (Red/Green cycle).

## Phase 3: Hardware & Library Setup
- [x] Update `edge_nodes/norvi_monitor/src/main.cpp` to include `<Wire.h>`, `<Adafruit_ADS1X15.h>`, and `"EMAFilter.h"`.
- [x] Declare a global `Adafruit_ADS1115` object and a global `EMAFilter` instance (default $\alpha = 0.1$).
- [x] In `setup()`, call `Wire.begin(21, 22)` to configure ESP32 hardware I2C pins.
- [x] In `setup()`, initialize the ADC via `ads.begin(0x48)` and configure gain via `ads.setGain(GAIN_ONE)`. Ensure startup failure logs an error to the serial console.

## Phase 4: FreeRTOS Task Integration (Core 1)
- [x] Implement the `vTaskAnalog` task function to run analog acquisition.
- [x] Inside `vTaskAnalog`, poll ADC channel A0 every 100ms using `ads.readADC_SingleEnded(0)` and `ads.computeVolts()`.
- [x] Apply the raw reading to the global `EMAFilter` instance.
- [x] Log the raw and filtered voltage readings to the serial console for visualization.
- [x] In `setup()`, create `vTaskAnalog` using `xTaskCreatePinnedToCore()`, setting its priority to `1` and pinning it to Core `1` (APP_CPU).

