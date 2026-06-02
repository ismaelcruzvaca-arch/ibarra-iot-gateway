# Coaxial Bright-Field Illumination Specification

## Purpose

Ink-printed codes (lot codes, expiration dates, 2D DataMatrix) on metallized shiny packaging cannot be read with the existing low-angle dark-field regime because ink-on-metal contrast relies on **absorbance vs. reflectance**, not geometric shadows. A coaxial bright-field regime delivers light perpendicular to the surface via a 50/50 beamsplitter, using cross-polarization to suppress the mirror-like glare of the metal substrate and a blue wavelength (~460 nm) to maximize ink absorbance relative to the metal background.

## Immutable Constraints

| Area | Constraint | Status |
|------|-----------|--------|
| **Beamsplitter** | A 50/50 non-polarizing plate beamsplitter MUST be placed between the lens and the object plane at 45° to the optical axis. | **IMMUTABLE** |
| **Wavelength** | The illumination source MUST be blue, centered at 460 nm ± 20 nm. | **IMMUTABLE** |
| **Polarization** | Cross-polarization MUST be used: a linear polarizer on the emitter side and a crossed analyzer (rotated 90°) on the camera side. | **IMMUTABLE** |
| **Trigger Independence** | The coaxial strobe MUST have its own dedicated GPIO trigger line, independent from the low-angle and dome triggers. | **IMMUTABLE** |
| **Strobe Duration** | The illumination pulse MUST be ≤ 1 ms (target: 10–100 μs) to freeze conveyor motion at line speed. | **IMMUTABLE** |
| **Sensor** | Primary sensor for this regime is IMX296 (global shutter). AR0234CS MAY be used if wider FOV is required, but resolution tradeoff MUST be documented. | **IMMUTABLE** |
| **Optical Axis** | The camera + beamsplitter + object MUST be on the same optical axis (coaxial). The illuminator enters the beamsplitter from the side, at 90° to the camera axis. | **IMMUTABLE** |

## Requirements

### Requirement: Coaxial Optical Path

The system MUST deliver illumination from a blue LED strobe through a 50/50 plate beamsplitter such that the light exits perpendicular to the object surface, reflects back through the beamsplitter, and reaches the sensor with no more than 25% of the emitted power at the sensor plane (50% lost at beamsplitter first pass × 50% lost at beamsplitter second pass, plus Fresnel losses).

#### Scenario: Beamsplitter alignment produces perpendicular illumination

- GIVEN a 50/50 non-polarizing plate beamsplitter mounted at 45° between the lens and the object plane
- WHEN the blue strobe (460 nm) fires from the side port of the beamsplitter
- THEN the illumination on the object plane MUST be perpendicular (0° incidence ± 2°) to the surface over the full FOV

#### Scenario: Coaxial path transmission budget

- GIVEN the optical path: strobe → beamsplitter → object → beamsplitter → sensor
- WHEN the strobe emits at full power
- THEN the total optical transmission from strobe to sensor MUST be ≤ 25% of emitted power (accounting for two 50/50 splits and Fresnel losses)

### Requirement: Cross-Polarized Suppression of Metallized Glare

The emitter polarizer and camera analyzer MUST be rotated 90° relative to each other. On a mirror-like metallized surface with no ink, the reflected light (which preserves polarization) MUST be attenuated by ≥ 99% (extinction ratio ≥ 100:1). On an ink-printed area, the diffuse scattering from the ink layer MUST depolarize the light sufficiently that a detectable signal reaches the sensor.

#### Scenario: Mirror surface is dark (glare suppressed)

- GIVEN a cleaned, unprinted metallized packaging sample placed in the FOV
- WHEN the coaxial strobe fires with cross-polarization active
- THEN the mean pixel value in the captured image over the metal area MUST be ≤ 10 (8-bit, 0–255 scale) — glare is suppressed to near-black

#### Scenario: Ink code is visible against dark metal

- GIVEN a packaging sample with ink-printed code on metallized surface placed in the FOV
- WHEN the coaxial strobe fires with cross-polarization active
- THEN the ink area MUST produce a mean pixel value ≥ 80 (8-bit) and the contrast ratio (ink_pixel_mean / metal_pixel_mean) MUST be ≥ 8:1

### Requirement: Blue Wavelength Absorption

The illumination source MUST be centered at 460 nm ± 20 nm. This wavelength is chosen because:
- Black and dark-color inks typically have peak absorption in the blue-violet range (higher optical density than red/NIR)
- Aluminum metallization has relatively flat reflectance across visible spectrum — the contrast mechanism relies on ink absorbance, not substrate reflectance difference

#### Scenario: Ink absorbance at 460 nm is sufficient

- GIVEN a spectrophotometer measurement of the target ink on metallized film
- WHEN absorbance is measured at 460 nm vs. 630 nm (the red wavelength used for low-angle regime)
- THEN the optical density at 460 nm MUST be ≥ 1.5× the optical density at 630 nm for the same ink

#### Scenario: Blue vs. red contrast comparison

- GIVEN identical packaging samples with ink codes, imaged with blue (460 nm) and red (630 nm) coaxial illumination
- WHEN contrast ratio (ink/metal) is measured for both
- THEN the blue image MUST yield a contrast ratio ≥ 1.3× that of the red image

### Requirement: GPIO Trigger Independence

The coaxial strobe MUST be triggered by its own dedicated GPIO pin on the RV1106, independent from the low-angle dark-field strobe and the dome lighting strobe. This enables:
- The coaxial regime to fire on a separate subset of packages (only those with ink codes detected)
- Independent timing adjustment (strobe duration, delay relative to sensor exposure)
- The ability to sequence coaxial capture immediately after low-angle capture on the same package

#### Scenario: Dedicated GPIO triggers coaxial independently

- GIVEN the RV1106 GPIO pin assigned to coaxial strobe is logically isolated
- WHEN a trigger signal is sent to the coaxial GPIO pin
- THEN the coaxial strobe MUST fire and the other two strobe systems MUST NOT fire
- AND the IMX296 sensor exposure window MUST align with the coaxial strobe pulse

#### Scenario: Sequential multi-regime capture

- GIVEN a package triggers the photoelectric sensor
- WHEN the RV1106 schedules low-angle capture first (GPIO-1), then coaxial capture (GPIO-2), then dome capture (GPIO-3)
- THEN each strobe fires only during its assigned sensor exposure window and no optical cross-talk between regimes is visible in the captured images

### Requirement: Strobe Power Budget Through Beamsplitter

The coaxial strobe MUST deliver sufficient illumination to the sensor after the 75%+ loss through the double beamsplitter path. The strobe peak power MUST be calculated as:

`P_required = P_sensor_needed × (1 / 0.25)` (accounting for two 50/50 splits)

where `P_sensor_needed` is the power required at the IMX296 sensor plane for a proper exposure at the target F#, exposure time ≤ 100 μs, and gain ≤ 12 dB.

#### Scenario: Strobe provides adequate exposure

- GIVEN the coaxial strobe is configured with calculated peak power
- WHEN an image of a white reflectance standard (90%+ reflectivity) is captured at the target F# and exposure time
- THEN the mean pixel value in the center 50% of the image MUST be between 180–220 (8-bit, 0–255 scale)

#### Scenario: Strobe duration is ≤ 1 ms

- GIVEN an oscilloscope connected to the strobe driver output
- WHEN the strobe is triggered
- THEN the optical pulse width at 50% peak power MUST be ≤ 1 ms (measured at the LED terminals)

### Requirement: Sensor Compatibility

The primary sensor for coaxial capture is the IMX296 (global shutter, 1.58 MP, 3.45 μm pixel). The AR0234CS (global shutter, 2.3 MP, 3.0 μm pixel) MAY be used as an alternative when wider FOV is needed, subject to:

- Resolution tradeoff documented in the per-camera configuration
- Lens selection accounts for the different sensor format
- Exposure and gain settings are validated independently for each sensor

#### Scenario: IMX296 captures coaxial images at target resolution

- GIVEN an IMX296 sensor with a lens selected for the coaxial FOV (e.g., 25mm or 35mm focal length for ~50 mm × 30 mm FOV at working distance)
- WHEN the coaxial strobe fires during sensor exposure
- THEN the captured image MUST have sufficient resolution to read 10-mil (0.254 mm) code elements with ≥ 3 pixels per module

#### Scenario: AR0234CS alternative is documented

- GIVEN an AR0234CS sensor used for coaxial capture
- WHEN the per-camera configuration is inspected
- THEN the configuration MUST document: sensor model, lens focal length, FOV dimensions, pixel resolution per code module, and the tradeoff rationale vs. IMX296

### Requirement: Integration with Optical Capture Delta Spec

The coaxial regime MUST be referenced in the updated optical-capture spec as the third illumination regime (Section 3.3). The optical-capture delta spec SHALL define:

- The trigger GPIO mapping (coaxial = GPIO-3 or as assigned in the hardware integration)
- The regime sequencing logic (which packages get which regimes)
- The hardware spec update instructions for `001-optical-capture.md`

#### Scenario: Coaxial regime appears in optical-capture spec

- GIVEN the updated `openspec/specs/optical-capture/spec.md` delta spec
- WHEN the spec is inspected
- THEN it MUST contain a Section 3.3 describing the coaxial bright-field regime with cross-references to this coaxial-brightfield-illumination spec

## Scenarios Summary

| ID | Scenario | Domain |
|----|----------|--------|
| COAX-1 | Beamsplitter alignment produces perpendicular illumination | Optics |
| COAX-2 | Coaxial path transmission budget | Optics |
| COAX-3 | Mirror surface is dark (glare suppressed) | Polarization |
| COAX-4 | Ink code is visible against dark metal | Polarization |
| COAX-5 | Ink absorbance at 460 nm is sufficient | Wavelength |
| COAX-6 | Blue vs. red contrast comparison | Wavelength |
| COAX-7 | Dedicated GPIO triggers coaxial independently | Electronics |
| COAX-8 | Sequential multi-regime capture | Integration |
| COAX-9 | Strobe provides adequate exposure | Power |
| COAX-10 | Strobe duration is ≤ 1 ms | Power |
| COAX-11 | IMX296 captures coaxial images at target resolution | Sensor |
| COAX-12 | AR0234CS alternative is documented | Sensor |
| COAX-13 | Coaxial regime appears in optical-capture spec | Integration |

## Success Criteria

| Criterion | Target | Method |
|-----------|--------|--------|
| Contrast ratio (ink vs. metal) | ≥ 8:1 | Image histogram analysis |
| Character recognition rate | ≥ 99.5% on test samples | OCR model inference on captured images |
| Glare suppression (clean metal) | Mean pixel ≤ 10 (8-bit) | Image histogram analysis |
| Strobe duration | ≤ 1 ms (target 10–100 μs) | Oscilloscope measurement |
| GPIO trigger isolation | No cross-talk between regimes | Sequential capture test |
