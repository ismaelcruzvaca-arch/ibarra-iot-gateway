/**
 * @file mock_stages.cpp
 * @brief Mock inference stages that simulate realistic NPU latencies.
 *
 * Designed for host-side (x86_64) benchmarking of the pipeline before
 * real NPU hardware is available. Each function uses
 * std::this_thread::sleep_for() to mimic model inference time.
 *
 * Latency defaults are based on RV1106 NPU (0.5 TOPS) estimates:
 *   - OCR models (OCR relief + OCR ink):  ~15-25 ms per 320×64 crop
 *   - Defect detection:                   ~30-50 ms per 224×224 crop
 *   - Presence check:                      ~5-10 ms per 112×112 crop
 *
 * Configurable via env vars:
 *   BENCHMARK_LATENCY_OCR_RELIEF, BENCHMARK_LATENCY_OCR_INK,
 *   BENCHMARK_LATENCY_DEFECT,     BENCHMARK_LATENCY_PRESENCE
 */

#include "benchmark/benchmark_stages.h"

#include <cstdint>
#include <cstdlib>
#include <random>
#include <string>
#include <thread>

namespace gema {
namespace vision {

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

/**
 * @brief Read an env var as microseconds, or fall back to a default range.
 *
 * Expected format: "base_ms" or "base_ms,jitter_ms"
 *   - base_ms:   nominal latency in milliseconds
 *   - jitter_ms: uniform jitter +/- in milliseconds (default 0)
 *
 * @param var_name    Environment variable name.
 * @param default_ms  Default nominal latency in milliseconds.
 * @param jitter_ms   Default jitter range in milliseconds.
 * @return            The chosen latency (base + random jitter) in microseconds.
 */
static uint64_t env_or_default_us(const char* var_name,
                                   uint64_t default_ms,
                                   uint64_t jitter_ms = 0)
{
    const char* val = std::getenv(var_name);
    uint64_t base_ms = default_ms;
    uint64_t jitter = jitter_ms;

    if (val) {
        std::string s(val);
        auto comma = s.find(',');
        if (comma != std::string::npos) {
            base_ms = static_cast<uint64_t>(std::atol(s.substr(0, comma).c_str()));
            jitter  = static_cast<uint64_t>(std::atol(s.substr(comma + 1).c_str()));
        } else {
            base_ms = static_cast<uint64_t>(std::atol(s.c_str()));
        }
    }

    // Apply uniform jitter.
    static thread_local std::mt19937_64 rng(std::random_device{}());
    int64_t jitter_offset = 0;
    if (jitter > 0) {
        std::uniform_int_distribution<int64_t> dist(
            -static_cast<int64_t>(jitter),
            static_cast<int64_t>(jitter));
        jitter_offset = dist(rng);
    }

    // Clamp to prevent negative sleeps.
    int64_t result = static_cast<int64_t>(base_ms * 1000) + jitter_offset * 1000;
    if (result < 1000) result = 1000;  // minimum 1 ms

    return static_cast<uint64_t>(result);
}

// ---------------------------------------------------------------------------
// Mock inference functions
// ---------------------------------------------------------------------------

/**
 * @brief Mock OCR inference for embossed/relief characters.
 *
 * Simulates a .rknn model on a 320×64 crop.
 * Default: ~15-25 ms (20 ms nominal, 5 ms jitter).
 */
uint64_t mock_ocr_relief(BenchmarkCollector& collector)
{
    uint64_t elapsed = env_or_default_us("BENCHMARK_LATENCY_OCR_RELIEF", 20, 5);
    std::this_thread::sleep_for(std::chrono::microseconds(elapsed));
    collector.record_stage("ocr_relief", elapsed);
    return elapsed;
}

/**
 * @brief Mock OCR inference for ink-printed characters on metallised film.
 *
 * Simulates a .rknn model on a 320×64 crop.
 * Default: ~15-25 ms (20 ms nominal, 5 ms jitter).
 */
uint64_t mock_ocr_ink(BenchmarkCollector& collector)
{
    uint64_t elapsed = env_or_default_us("BENCHMARK_LATENCY_OCR_INK", 20, 5);
    std::this_thread::sleep_for(std::chrono::microseconds(elapsed));
    collector.record_stage("ocr_ink", elapsed);
    return elapsed;
}

/**
 * @brief Mock defect detection inference.
 *
 * Simulates a .rknn model on a 224×224 crop.
 * Default: ~30-50 ms (40 ms nominal, 10 ms jitter).
 */
uint64_t mock_defect_detection(BenchmarkCollector& collector)
{
    uint64_t elapsed = env_or_default_us("BENCHMARK_LATENCY_DEFECT", 40, 10);
    std::this_thread::sleep_for(std::chrono::microseconds(elapsed));
    collector.record_stage("defect_detection", elapsed);
    return elapsed;
}

/**
 * @brief Mock presence check inference.
 *
 * Simulates a .rknn model on a 112×112 crop.
 * Default: ~5-10 ms (7 ms nominal, 3 ms jitter).
 */
uint64_t mock_presence_check(BenchmarkCollector& collector)
{
    uint64_t elapsed = env_or_default_us("BENCHMARK_LATENCY_PRESENCE", 7, 3);
    std::this_thread::sleep_for(std::chrono::microseconds(elapsed));
    collector.record_stage("presence_check", elapsed);
    return elapsed;
}

}  // namespace vision
}  // namespace gema
