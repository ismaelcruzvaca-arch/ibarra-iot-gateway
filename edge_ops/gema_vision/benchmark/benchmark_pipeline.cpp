/**
 * @file benchmark_pipeline.cpp
 * @brief Full vision pipeline orchestration with per-stage timing.
 *
 * Simulates the complete capture → process → infer → fuse pipeline
 * that runs on the RV1106, with each stage independently timed via
 * BenchmarkCollector + StageTimer RAII.
 *
 * Pipeline stages:
 *   1. Sensor readout         (simulated 16 ms — IMX296 @ 60 fps)
 *   2. ISP / debayer          (simulated 8  ms — hardware ISP)
 *   3. Crop engine            (simulated 3  ms — ROI extraction)
 *   4. OCR relief inference   (mock NPU  ~20 ms, parallel with ink + defect)
 *   5. OCR ink inference      (mock NPU  ~20 ms, parallel)
 *   6. Defect detection       (mock NPU  ~40 ms, parallel)
 *   7. Presence check         (mock NPU   ~7 ms, parallel)
 *   8. L*a*b* calculation     (simulated 2  ms — CPU colour validation)
 *   9. Fusion & verdict       (simulated 1  ms — merge results)
 *
 * Stages 4-7 are conceptually parallel (the NPU processes 4 models
 * concurrently on different crops), but in this benchmark they run
 * sequentially on the mock since there is no real NPU scheduler.
 * This still gives a conservative upper-bound estimate.
 *
 * On real RV1106 hardware, the NPU can pipeline overlapping requests
 * via rknn_run() on separate input buffers, reducing the inference
 * wall-clock to roughly max(latency) rather than sum(latency).
 */

#include "benchmark/benchmark_stages.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

namespace gema {
namespace vision {

// ---------------------------------------------------------------------------
// Forward declarations from mock_stages.cpp
// ---------------------------------------------------------------------------

uint64_t mock_ocr_relief(BenchmarkCollector& collector);
uint64_t mock_ocr_ink(BenchmarkCollector& collector);
uint64_t mock_defect_detection(BenchmarkCollector& collector);
uint64_t mock_presence_check(BenchmarkCollector& collector);

// ---------------------------------------------------------------------------
// Simulated stages
// ---------------------------------------------------------------------------

/**
 * @brief Simulate sensor readout via MIPI CSI-2.
 *
 * IMX296 at 60 fps → ~16.67 ms per frame.
 */
static uint64_t simulated_sensor_readout(BenchmarkCollector& collector)
{
    uint64_t elapsed = 16000;  // 16 ms
    std::this_thread::sleep_for(std::chrono::microseconds(elapsed));
    collector.record_stage("sensor_readout", elapsed);
    return elapsed;
}

/**
 * @brief Simulate ISP and debayer (hardware blocks on RV1106).
 *
 * The Rockchip ISP handles this in ~6-10 ms depending on resolution.
 */
static uint64_t simulated_isp_debayer(BenchmarkCollector& collector)
{
    uint64_t elapsed = 8000;  // 8 ms
    std::this_thread::sleep_for(std::chrono::microseconds(elapsed));
    collector.record_stage("isp_debayer", elapsed);
    return elapsed;
}

/**
 * @brief Simulate Crop Engine — extract ROIs from the full frame.
 *
 * Three crops: OCR relief (320×64), OCR ink (320×64),
 * defect detection (224×224), presence (112×112).
 */
static uint64_t simulated_crop_engine(BenchmarkCollector& collector)
{
    uint64_t elapsed = 3000;  // 3 ms total for 4 crops
    std::this_thread::sleep_for(std::chrono::microseconds(elapsed));
    collector.record_stage("crop_engine", elapsed);
    return elapsed;
}

/**
 * @brief Simulate L*a*b* colour calculation (CPU, ~2 ms).
 */
static uint64_t simulated_lab_calculation(BenchmarkCollector& collector)
{
    uint64_t elapsed = 2000;  // 2 ms
    std::this_thread::sleep_for(std::chrono::microseconds(elapsed));
    collector.record_stage("lab_calculation", elapsed);
    return elapsed;
}

/**
 * @brief Simulate Fusion & Verdict (CPU, ~1 ms).
 */
static uint64_t simulated_fusion(BenchmarkCollector& collector)
{
    uint64_t elapsed = 1000;  // 1 ms
    std::this_thread::sleep_for(std::chrono::microseconds(elapsed));
    collector.record_stage("fusion", elapsed);
    return elapsed;
}

// ---------------------------------------------------------------------------
// Pipeline orchestrator
// ---------------------------------------------------------------------------

/**
 * @brief Run one iteration of the full vision pipeline.
 *
 * @param collector  BenchmarkCollector to record all stage timings.
 * @return           Total wall-clock time for this iteration (microseconds).
 */
uint64_t run_pipeline_iteration(BenchmarkCollector& collector)
{
    auto iter_start = std::chrono::high_resolution_clock::now();

    // Stage 1: Sensor readout
    simulated_sensor_readout(collector);

    // Stage 2: ISP / debayer
    simulated_isp_debayer(collector);

    // Stage 3: Crop engine
    simulated_crop_engine(collector);

    // Stages 4-7: Parallel inference (sequential in mock, see notes above)
    // On real hardware, these would be dispatched to the NPU concurrently.
    mock_ocr_relief(collector);
    mock_ocr_ink(collector);
    mock_defect_detection(collector);
    mock_presence_check(collector);

    // Stage 8: L*a*b* colour calculation
    simulated_lab_calculation(collector);

    // Stage 9: Fusion & verdict
    simulated_fusion(collector);

    auto iter_end = std::chrono::high_resolution_clock::now();
    uint64_t total = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            iter_end - iter_start).count());

    collector.record_stage("total_pipeline", total);
    return total;
}

}  // namespace vision
}  // namespace gema
