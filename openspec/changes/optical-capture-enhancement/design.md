# Design: Optical Capture Enhancement — Coaxial Bright-Field for Ink Codes

## Technical Approach

Add a third coaxial bright-field illumination regime for ink-printed codes on metallized packaging. The existing two regimes (low-angle dark field for embossed OCR, dome for colorimetry) cannot read ink on mirror-like surfaces due to specular glare. A 50:50 plate beamsplitter + cross-polarized blue strobe eliminates reflections while maximizing ink absorbance. Deliver a standalone C++ throughput benchmark on RV1106 **before** the optical hardware ships — validates pipeline headroom for three models.

## Architecture Decisions

### Decision: Beamsplitter Type and Placement

| Option | Tradeoff | Decision |
|--------|----------|----------|
| 50:50 plate beamsplitter | ~50% light loss, no chromatic aberrration | **Selected** — simplest, aligns with ADR-003 multi-head philosophy |
| 30:70 pellicle beamsplitter | Less loss, fragile, expensive | Rejected — production line vibration risk |
| Dichroic beamsplitter | Wavelength-specific, inflexible | Rejected — need broad enough for future regimes |

Place at 45° to optical axis: camera looking through, blue LED array at 90°.

### Decision: Wavelength and Polarization

| Option | Tradeoff | Decision |
|--------|----------|----------|
| Blue 470nm + cross-polarized | Ink absorbs blue strongly, metal reflects 470nm → polarizers extinguish metal glare | **Selected** — max contrast for ink codes on shiny metal |
| White + cross-polarized | Lower contrast, higher strobe power needed | Rejected — ink absorbs broadly but metal reflects more |
| Red 630nm (reuse existing) | Ink may not absorb enough, same wavelength as regime 1 | Rejected — conflicts with existing low-angle dark field |

Cross-polarization: linear polarizer (extinction ratio ≥1000:1) on LED array, analyzer on camera lens, axes crossed 90°.

### Decision: Benchmark Executable Architecture

Standalone C++ executable compiled with `CROSSCOMPILE=ON` for RV1106. Uses `std::chrono::high_resolution_clock` per stage. No external dependencies beyond what vision pipeline already uses (RKNN API, OpenCV). Outputs JSON to stdout — pipeable to analysis scripts.

### Decision: GPIO Trigger Integration

RV1106 has three independent GPIO lines: one per regime. The coaxial strobe uses GPIO2 (currently unused). All three lines share the same photocell interrupt — the active regime is selected by SKU configuration at boot.

## Optical Layout

```
          ┌────────── Camera (IMX296 + analyzer)
          │
          │    ┌───────────────────┐
          │    │  Beamsplitter     │
          │    │  (50:50 plate)    │
   Light  │    │                   │
   path  ─┼───►│◄═════ 45° ═════►│◄──── Target surface
          │    │  ▲ Polarizer      │      (ink code on metal)
          │    └──│────────────────┘
          │       │
          │   Blue LED array
          │   (470nm + polarizer)
          │
     ◄────┴────► Crossed 90°
     Polarizer axes
```

Light path: LED → polarizer → beamsplitter (50% reflected) → target → beamsplitter (50% transmitted) → analyzer → sensor.

## Trigger Integration

| Regime | Sensor | GPIO | Strobe | Wavelength |
|--------|--------|------|--------|------------|
| 1. Low-Angle Dark Field | IMX296 | GPIO0 | Red 630nm | Existing |
| 2. Dome Colorimetry | AR0234CS | GPIO1 | White high-CRI | Existing |
| **3. Coaxial Bright-Field** | IMX296 | **GPIO2** | **Blue 470nm** | **New** |

Timing: photocell triggers all 3 GPIOs in parallel; only the active regime's MOSFET enables its strobe. Pulse width: 50–200µs. Strobe driver: N-channel MOSFET (e.g., IRLZ44N) gate driven by GPIO through a TTL buffer (SN74LVC1G17) for clean switching.

## File Changes

| File | Action | Description |
|------|--------|-------------|
| `docs/hardware_specs/001-optical-capture.md` | Modify | Add §3.3 Coaxial Bright-Field regime |
| `docs/adr/003-vision-multi-head-pipeline.md` | Modify | Add ink-OCR model to model table, reference coaxial regime |
| `edge_ops/vision_pipeline/benchmark/main.cpp` | Create | Standalone throughput benchmark executable |
| `edge_ops/vision_pipeline/benchmark/CMakeLists.txt` | Create | Build target for RV1106 cross-compilation |
| `edge_ops/vision_pipeline/benchmark/stages.h` | Create | Stage interface for measurement hooks |

## Benchmark Architecture

```
main.cpp
  ├── StageTimer (RAII wrapper over high_resolution_clock)
  ├── MeasureSensorReadout(sensor_id, n=1000)  → {min, avg, max} µs
  ├── MeasureDebayer(crop, n=1000)              → {min, avg, max} µs
  ├── MeasureCropEngine(rois, n=1000)           → {min, avg, max} µs
  ├── MeasureRKNNInference(model_path, n=1000)  → {min, avg, max} µs
  ├── MeasureLabCalculation(crop, n=1000)       → {min, avg, max} µs
  └── MeasureFusion(scenario, n=1000)           → {min, avg, max} µs
        │
        ▼
  JSON stdout: {"pipeline":{"total_us":{"min":X,"avg":Y,"max":Z},
                 "headroom_pct": 42.5, "line_speed_ok": true}, ...}
```

Output includes headroom: `(1s - pipeline_avg_us) / 1s * 100` for 60+ ppm target. Reports `line_speed_ok: true/false`.

## Testing Strategy

| Layer | What to Test | Approach |
|-------|-------------|----------|
| Validation | Strobe timing | Oscilloscope on GPIO2 output, confirm 50–200µs pulse |
| Validation | Cross-polarization extinction | Place mirror in target plane; pixel intensity should be <5% of non-polarized |
| Benchmark | Correctness | Run on host with dummy modules first, verify JSON schema |
| Benchmark | Statistical stability | Run 3x batches of 1000; verify variance across runs <5% |
| Doc | ADR-003 model table | Verify ink-OCR model entry present, correct crop resolution |

## Integration Plan

1. **Phase A (Benchmark first)**: Create benchmark executable, compile for RV1106, run on dev board with existing models. Report per-stage latency. Gate decision: if total pipeline time exceeds 900µs (90% budget), proceed to Phase B with sequenced (not parallel) inference.
2. **Phase B (Optical hardware)**: Procure beamsplitter + blue LED array + polarizers. Assemble on optical breadboard. Test with ink-code samples. Iterate on strobe power until image quality matches spec.
3. **Phase C (Docs)**: Update `001-optical-capture.md` §3.3 and ADR-003. Rollback: `git checkout` on both files.

## Validation Plan

Before deployment on line:
1. **Optical bench test**: Place known ink codes on metallized substrate; verify contrast ratio (ink pixels vs metal background) ≥ 4:1.
2. **Cross-polarization check**: Capture same target with and without analyzer; polarized image must show specular highlights reduced by ≥90%.
3. **Strobe timing**: GPIO2 pulse measured with scope; verify within 50–200µs window.
4. **Benchmark report**: Run 3 batches of 1000 frames, confirm standard deviation <5% for all stages.

## Open Questions

- [ ] Does the existing IMX296 housing have space for the beamsplitter cube without increasing working distance?
- [ ] Is an additional GPIO line available on the RV1106 carrier board, or does GPIO2 have a mux conflict?
- [ ] Ink-OCR model resolution: what crop size does the model need? (Impacts benchmark design)
