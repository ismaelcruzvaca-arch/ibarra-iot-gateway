# Optical Capture — Delta Specification

## Modified Capability: `optical-capture`

The existing `optical-capture` capability (documented in `docs/hardware_specs/001-optical-capture.md`) currently defines two illumination regimes. This delta spec adds the **third regime** (coaxial bright-field), updates the sensor table to reflect the new regime, and introduces the **RV1106 throughput benchmark** requirement for the complete multi-model pipeline.

## Type of Change

| Aspect | Value |
|--------|-------|
| **Capability** | optical-capture (MODIFIED) |
| **New Regime** | §3.3 Coaxial Bright-Field (ink-printed codes on metallized substrates) |
| **Total Regimes** | 3 (was 2) |
| **New Cross-Cutting** | RV1106 throughput benchmark requirement |
| **Affected Docs** | `docs/hardware_specs/001-optical-capture.md`, `docs/adr/003-vision-multi-head-pipeline.md` |

## Modified Requirements

### Requirement: Sensor Table — Add Third Regime Column

The sensor assignment table in `001-optical-capture.md` §1 MUST be extended to include the third regime:

| Sensor | Shutter | Low-Angle DF (embossed OCR) | Dome Lighting (colorimetry) | Coaxial BF (ink OCR) |
|--------|---------|-----------------------------|-----------------------------|----------------------|
| IMX296 | Global | ✓ Primary | — | ✓ Primary |
| AR0234CS | Global | — | ✓ Primary | ✓ Alternative |

#### Scenario: Sensor table shows all three regimes

- GIVEN the updated `001-optical-capture.md`
- WHEN the sensor table in §1 is inspected
- THEN it MUST document IMX296 as primary for both low-angle DF and coaxial BF, AR0234CS as primary for dome lighting and alternative for coaxial BF

### Requirement: §3.3 Coaxial Bright-Field (New Section)

The existing spec MUST have a new §3.3 inserted after §3.2 with the following content:

> ### 3.3 Coaxial Bright-Field (OCR en Códigos Impresos)
>
> - **Técnica:** Luz azul (~460 nm) coaxial a través de un beamsplitter 50/50 no polarizante. La luz incide perpendicular a la superficie del envase.
> - **Polarización:** Cruzada (polarizador lineal en el emisor + analizador rotado 90° en la cámara) para eliminar el glare especular del metal brillante.
> - **Propósito:** Crear contraste en códigos impresos con tinta sobre superficies metalizadas (espejadas). El mecanismo de contraste es absorbancia de la tinta vs. reflexión del metal, no sombras geométricas.
> - **Longitud de Onda:** 460 nm ± 20 nm (azul). La tinta negra/oscura tiene máxima absorbancia en azul-violeta; el aluminio metalizado tiene reflectancia plana en el espectro visible.
> - **Beamsplitter:** Placa 50/50 montada a 45° entre el lente y el objeto. La cámara y el objeto están en el mismo eje óptico; el iluminador entra por el puerto lateral.
> - **Trigger:** GPIO independiente (pin dedicado, diferente de los regímenes 3.1 y 3.2) para permitir captura selectiva y secuenciación.
> - **Strobe:** Duración ≤ 1 ms (target 10–100 μs). Potencia compensada por la pérdida del beamsplitter (> 75% de pérdida óptica total).
> - **Sensor Primario:** IMX296. Alternativa: AR0234CS si se requiere FOV más amplio.
> - **Resultado:** Una imagen donde la tinta impresa aparece oscura sobre fondo metalizado oscuro (glare suprimido), ideal para OCR de caracteres impresos.
> - **Referencia Cruzada:** Ver especificación detallada en `openspec/changes/optical-capture-enhancement/specs/coaxial-brightfield-illumination/spec.md`.

#### Scenario: §3.3 exists in the updated spec

- GIVEN the updated `001-optical-capture.md`
- WHEN §3.3 is inspected
- THEN it MUST contain the coaxial bright-field description with wavelength, polarization, beamsplitter, trigger, strobe, and sensor requirements as specified above

#### Scenario: §3.3 references the detailed coaxial spec

- GIVEN the updated `001-optical-capture.md`
- WHEN §3.3 is inspected
- THEN it MUST contain a cross-reference to `openspec/changes/optical-capture-enhancement/specs/coaxial-brightfield-illumination/spec.md`

### Requirement: Different Wavelengths Per Substrate

The updated spec MUST note that coaxial bright-field may require different wavelengths depending on the substrate and ink chemistry. The 460 nm blue is the default for black/dark ink on aluminum metallization. Alternative wavelengths SHALL be considered when:

- The ink is transparent or low-absorbance at 460 nm (e.g., light colors, UV-fluorescent inks)
- The substrate has a non-aluminum metallization with different spectral reflectance
- Regulatory or customer requirements specify a different wavelength

#### Scenario: Wavelength flexibility is documented

- GIVEN the updated `001-optical-capture.md` §3.3
- WHEN the section is inspected for wavelength guidance
- THEN it MUST state that 460 nm is the DEFAULT for black ink on aluminum, and that alternative wavelengths SHALL be evaluated for non-standard inks or substrates

## New Cross-Cutting Requirement: RV1106 Throughput Benchmark

### Requirement: Benchmark Methodology

The RV1106 (0.5 TOPS NPU) MUST be benchmarked running the complete multi-model vision pipeline to validate that the pipeline fits within the line-speed budget (60+ ppm → ≤ 1 s per frame). Each stage MUST be measured independently.

#### Pipeline Stages

| # | Stage | Type | Input Resolution | Notes |
|---|-------|------|------------------|-------|
| 1 | OCR relief model | .rknn inference | 320 × 64 | Existing model |
| 2 | OCR ink model | .rknn inference | 320 × 64 | NEW — ink code reading |
| 3 | Defect detection | .rknn inference | 224 × 224 | Existing model |
| 4 | Presence detection | .rknn inference | 112 × 112 | Existing model |
| 5 | Crop engine | C++ pre-processing | Varies | ROI extraction per model |
| 6 | L*a*b* calculations | C++ processing | Varies | Colorimetry on dome image |
| 7 | Fusion / verdict | C++ post-processing | N/A | Combine all model outputs |

#### Scenario: Per-stage latency is measured independently

- GIVEN the RV1106 benchmark harness instrumented with per-stage timestamps
- WHEN the full pipeline runs on N ≥ 100 sample images
- THEN for each stage, the benchmark MUST record: stage name, min/avg/max latency (μs), and count of runs

#### Scenario: Pipeline stages are isolated for measurement

- GIVEN the benchmark harness
- WHEN a single stage is measured
- THEN the timer MUST start immediately before the stage begins and stop immediately after it completes, excluding any scheduling or queue overhead from adjacent stages

#### Scenario: All five .rknn models are benchmarked

- GIVEN the four inference models (OCR-relief, OCR-ink, defects, presence)
- WHEN each model is invoked independently N times
- THEN the benchmark MUST report per-model latency statistics (min/avg/max, μs) and NPU utilization (if measurable via RV1106 perf counters)

### Requirement: Reporting Format

The benchmark report MUST include the following for each pipeline configuration tested:

| Metric | Format | Example |
|--------|--------|---------|
| Stage name | String | `ocr-ink-model` |
| Min latency | Integer (μs) | 12500 |
| Avg latency | Integer (μs) | 14200 |
| Max latency | Integer (μs) | 18900 |
| Std deviation | Integer (μs) | 1200 |
| Samples | Integer | 100 |
| Notes | String | (any anomalies observed) |

The report MUST also include a **total pipeline time** row that sums the average latencies of all stages in sequence (assuming sequential execution) and reports the measured end-to-end time (GPIO trigger → verdict output).

#### Scenario: Report contains per-stage and total metrics

- GIVEN the completed benchmark run
- WHEN the report is generated
- THEN it MUST contain a row for each pipeline stage with min/avg/max latency
- AND a total pipeline row with both summed-average and measured end-to-end latency

#### Scenario: Report identifies outliers

- GIVEN the benchmark dataset
- WHEN any single run exceeds 2× the average for any stage
- THEN the report MUST flag that run as an outlier and include the run index and observed latency

### Requirement: Margin Calculation

The benchmark MUST calculate the line-speed headroom margin as:

`margin_percent = ((1000 - total_pipeline_avg_ms) / total_pipeline_avg_ms) × 100`

where `total_pipeline_avg_ms` is the measured end-to-end average latency in milliseconds. The margin MUST be reported and classified:

| Margin | Classification | Action |
|--------|----------------|--------|
| ≥ 50% | Green | No action needed |
| 25% – 49% | Yellow | Document headroom; monitor |
| 10% – 24% | Orange | Optimize top-3 stages; re-benchmark |
| < 10% | Red | Pipeline redesign required |

#### Scenario: Green margin is confirmed

- GIVEN the benchmark report
- WHEN `total_pipeline_avg_ms` is 500 ms
- THEN `margin_percent` MUST be 100% (classification: Green)
- AND the report MUST state "No action needed"

#### Scenario: Orange margin triggers optimization

- GIVEN the benchmark report
- WHEN `total_pipeline_avg_ms` is 850 ms
- THEN `margin_percent` MUST be 17.6% (classification: Orange)
- AND the report MUST identify the top 3 stages by latency contribution and recommend optimization targets

## Scenarios Summary

| ID | Scenario | Type |
|----|----------|------|
| OC-DELTA-1 | Sensor table shows all three regimes | Existing (modified) |
| OC-DELTA-2 | §3.3 exists in the updated spec | Existing (modified) |
| OC-DELTA-3 | §3.3 references the detailed coaxial spec | Existing (modified) |
| OC-DELTA-4 | Wavelength flexibility is documented | Existing (modified) |
| OC-DELTA-5 | Per-stage latency is measured independently | New (benchmark) |
| OC-DELTA-6 | Pipeline stages are isolated for measurement | New (benchmark) |
| OC-DELTA-7 | All five .rknn models are benchmarked | New (benchmark) |
| OC-DELTA-8 | Report contains per-stage and total metrics | New (benchmark) |
| OC-DELTA-9 | Report identifies outliers | New (benchmark) |
| OC-DELTA-10 | Green margin is confirmed | New (benchmark) |
| OC-DELTA-11 | Orange margin triggers optimization | New (benchmark) |

## Success Criteria

| Criterion | Target | Verification Method |
|-----------|--------|---------------------|
| Sensor table updated | 3 regimes, 2 sensors | Visual inspection of `001-optical-capture.md` |
| §3.3 written | Complete coaxial description | Visual inspection |
| Benchmark methodology defined | Per-stage isolation, N ≥ 100 | Review benchmark plan |
| Report format defined | Per-stage + total + margin | Review report template |
| Pipeline margin ≥ 50% | Green classification | Benchmark execution |
