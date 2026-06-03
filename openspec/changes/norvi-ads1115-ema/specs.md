# Technical Specifications: ADS1115 and EMA Filter

These specifications formalize the integration requirements for the ADS1115 16-bit analog-to-digital converter (ADC) and the Exponential Moving Average (EMA) software filter.

## Immutable Constraints

| Area | Decision | Status |
|---|---|---|
| **Core Pinning** | Polling of the ADC and EMA filter calculation MUST execute on Core 1 (APP_CPU). | **IMMUTABLE** |
| **I2C Pinout** | The hardware I2C bus MUST use SDA = GPIO 21 and SCL = GPIO 22. | **IMMUTABLE** |
| **I2C Address** | The ADS1115 I2C address MUST be configured to `0x48`. | **IMMUTABLE** |
| **ADC Gain** | The ADS1115 GAIN MUST be set to `GAIN_ONE` (giving a range of +/- 4.096V). | **IMMUTABLE** |
| **EMA Initialization** | The filter MUST initialize its state with the first reading to eliminate ramp-up lag. | **IMMUTABLE** |
| **TDD Requirement** | The EMA filter implementation MUST be verified via Unit Tests prior to integration. | **IMMUTABLE** |

---

## Hardware Integration Specifications

### 1. I2C Configuration
* **Bus Speed**: 100 kHz (Standard I2C speed). The default PlatformIO / Arduino speed (100 kHz) is sufficient.
* **Initialization**: The bus must be initialized using `Wire.begin(21, 22)` to ensure correct pins are assigned on the ESP32.
* **ADC Address**: `0x48` (ADDR pin pulled to Ground).

### 2. ADC Acquisition Details
* **Pin Allocation**: Channel A0 of the ADS1115 will be polled.
* **Gain / Scaling**: `GAIN_ONE` is used.
  - Voltage full-scale range: $\pm 4.096\text{ V}$.
  - Resolution: 16-bit ($65,536$ steps, or $32,768$ steps for positive single-ended values).
  - LSB size: $4.096\text{ V} / 32768 = 0.125\text{ mV}$.
  - The maximum safe input voltage is $VDD + 0.3\text{ V} \approx 3.6\text{ V}$. The signal range to be measured is $0\text{ V}$ to $3.3\text{ V}$, which fits perfectly within the $\pm 4.096\text{ V}$ full-scale range without risk of clipping or overvoltage.

---

## EMA Filter Mathematical Specifications

The Exponential Moving Average (EMA) filter is a recursive first-order infinite impulse response (IIR) filter.

### 1. The Recursive Equation
For each discrete time step $n \ge 1$:
$$y[n] = \alpha \cdot x[n] + (1 - \alpha) \cdot y[n-1]$$

Where:
* $x[n]$ is the current raw ADC reading (floating point voltage or raw LSB count).
* $y[n]$ is the current filtered output.
* $y[n-1]$ is the previous filtered output.
* $\alpha$ (Alpha) is the smoothing factor in the range $[0.0, 1.0]$. Default value is `0.1`.

### 2. Initial Condition Handling
To prevent startup lag (which occurs if $y[-1]$ is assumed to be `0.0`), the filter must use the first reading as the starting point:
$$y[0] = x[0]$$
This is done by tracking initialization state:
```cpp
if (!initialized) {
    y = x;
    initialized = true;
} else {
    y = alpha * x + (1 - alpha) * y;
}
```

---

## Unit Test (TDD) Specifications

The `EMAFilter` class must be verified against the following assertions using the Unity test framework.

### Test Case 1: Initial Condition Handling (Zero Lag)
* **Given**: An uninitialized `EMAFilter` with $\alpha = 0.1$.
* **When**: A raw reading of `1234.5` is processed.
* **Then**: The filtered output must be exactly `1234.5`.
* **Assertion**: `TEST_ASSERT_EQUAL_FLOAT(1234.5f, filter.filter(1234.5f))`

### Test Case 2: Mathematical Step Updates
* **Given**: An initialized `EMAFilter` with $y[0] = 100.0$ and $\alpha = 0.1$.
* **When**: A new raw reading of `200.0` is processed.
* **Then**: The filtered output must be exactly `110.0`.
  $$\text{Calculation: } (0.1 \cdot 200.0) + (0.9 \cdot 100.0) = 20.0 + 90.0 = 110.0$$
* **Assertion**: `TEST_ASSERT_EQUAL_FLOAT(110.0f, filter.filter(200.0f))`

### Test Case 3: Alpha Bound = 1.0 (No Filtering)
* **Given**: An `EMAFilter` with $\alpha = 1.0$.
* **When**: Readings of `150.0`, `250.0`, and `350.0` are processed sequentially.
* **Then**: The outputs must exactly match the inputs at each step.
* **Assertion**:
  - `TEST_ASSERT_EQUAL_FLOAT(150.0f, filter.filter(150.0f))`
  - `TEST_ASSERT_EQUAL_FLOAT(250.0f, filter.filter(250.0f))`
  - `TEST_ASSERT_EQUAL_FLOAT(350.0f, filter.filter(350.0f))`

### Test Case 4: Alpha Bound = 0.0 (Total Filtering / Constant Output)
* **Given**: An `EMAFilter` with $\alpha = 0.0$.
* **When**: Initial reading of `100.0` is processed, followed by `200.0` and `300.0`.
* **Then**: The output must remain `100.0` for all steps.
* **Assertion**:
  - `TEST_ASSERT_EQUAL_FLOAT(100.0f, filter.filter(100.0f))`
  - `TEST_ASSERT_EQUAL_FLOAT(100.0f, filter.filter(200.0f))`
  - `TEST_ASSERT_EQUAL_FLOAT(100.0f, filter.filter(300.0f))`

### Test Case 5: Convergence to Steady State
* **Given**: An initialized `EMAFilter` with $\alpha = 0.1$ and initial reading `0.0`.
* **When**: Constant input of `100.0` is processed for 100 iterations.
* **Then**: The final value must converge to `100.0` within an epsilon of `0.001`.
* **Assertion**:
  - Loop 100 times: `filter.filter(100.0f)`
  - `TEST_ASSERT_FLOAT_WITHIN(0.001f, 100.0f, filter.getValue())`

### Test Case 6: Filter Reset
* **Given**: An initialized `EMAFilter` with $\alpha = 0.1$ that has processed multiple readings and has current value `150.0`.
* **When**: `filter.reset()` is called, followed by a new reading of `50.0`.
* **Then**: The output must be exactly `50.0` (confirming it re-initialized and did not blend with the prior `150.0` value).
* **Assertion**:
  - `filter.reset()`
  - `TEST_ASSERT_EQUAL_FLOAT(50.0f, filter.filter(50.0f))`
