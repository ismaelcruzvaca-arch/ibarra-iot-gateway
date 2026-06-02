/**
 * @file main.cpp
 * @brief Entry point for the gema-benchmark throughput tool.
 *
 * Usage:
 *   ./gema-benchmark                          # 1000 iterations (default)
 *   BENCHMARK_ITERATIONS=500 ./gema-benchmark # custom count
 *
 * Outputs:
 *   1. Pretty-printed summary table to stdout
 *   2. Machine-readable JSON report to stdout after the table
 *
 * Return code: 0 on success, 1 on error.
 *
 * Designed to run on both x86_64 (development) and RV1106 (target).
 */

#include "benchmark/benchmark_stages.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace gema {
namespace vision {

// ---------------------------------------------------------------------------
// Forward declarations from benchmark_pipeline.cpp
// ---------------------------------------------------------------------------

uint64_t run_pipeline_iteration(BenchmarkCollector& collector);

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/**
 * @brief Parse iteration count from env var or use default.
 */
static unsigned parse_iterations(unsigned default_count = 1000)
{
    const char* val = std::getenv("BENCHMARK_ITERATIONS");
    if (val) {
        long n = std::atol(val);
        if (n > 0 && n <= 100000) {
            return static_cast<unsigned>(n);
        }
        std::cerr << "Warning: BENCHMARK_ITERATIONS=" << val
                  << " out of range [1, 100000], using default "
                  << default_count << std::endl;
    }
    return default_count;
}

/**
 * @brief Print a config banner at startup.
 */
static void print_header(unsigned total_iters, unsigned warmup_iters)
{
    std::cout << "============================================\n";
    std::cout << "  GEMA Vision Pipeline Throughput Benchmark\n";
    std::cout << "============================================\n";
    std::cout << "  Total iterations:  " << total_iters << "\n";
    std::cout << "  Warm-up cycles:    " << warmup_iters << "\n";
    std::cout << "  Mode:              ";
#ifdef __aarch64__
    std::cout << "RV1106 (ARM64)\n";
#else
    std::cout << "Host (x86_64)\n";
#endif
    std::cout << "============================================\n"
              << std::endl;
}

/**
 * @brief Print a pretty summary table of per-stage statistics.
 */
static void print_summary_table(const BenchmarkReport& rpt)
{
    // Collect stages sorted by name for deterministic output.
    std::vector<std::string> keys;
    for (const auto& [name, _] : rpt.per_stage) {
        if (name != "total_pipeline") {
            keys.push_back(name);
        }
    }
    std::sort(keys.begin(), keys.end());

    std::cout << "\n--- Per-Stage Latency ---\n";
    std::cout << std::left << std::setw(22) << "Stage"
              << std::right
              << std::setw(10) << "Count"
              << std::setw(12) << "Min (us)"
              << std::setw(12) << "Avg (us)"
              << std::setw(12) << "Max (us)"
              << "\n";
    std::cout << std::string(68, '-') << "\n";

    uint64_t total_min = 0, total_max = 0;
    double total_avg = 0.0;
    unsigned total_count = 0;

    for (const auto& key : keys) {
        const auto& s = rpt.per_stage.at(key);
        std::cout << std::left << std::setw(22) << s.name
                  << std::right
                  << std::setw(10) << s.count
                  << std::setw(12) << s.min_us
                  << std::setw(12) << static_cast<uint64_t>(s.avg_us)
                  << std::setw(12) << s.max_us
                  << "\n";
        total_min += s.min_us;
        total_max += s.max_us;
        total_avg += s.avg_us;
        total_count += s.count;
    }

    // Total row
    if (rpt.per_stage.count("total_pipeline")) {
        const auto& t = rpt.per_stage.at("total_pipeline");
        std::cout << std::string(68, '-') << "\n";
        std::cout << std::left << std::setw(22) << "TOTAL (wall clock)"
                  << std::right
                  << std::setw(10) << t.count
                  << std::setw(12) << t.min_us
                  << std::setw(12) << static_cast<uint64_t>(t.avg_us)
                  << std::setw(12) << t.max_us
                  << "\n";
    }

    // Headroom check: 60 ppm → 1 frame every 1,000,000 us
    constexpr uint64_t kFrameBudgetUs = 1000000;  // 1 second at 60 ppm
    if (rpt.per_stage.count("total_pipeline")) {
        uint64_t avg_total = static_cast<uint64_t>(
            rpt.per_stage.at("total_pipeline").avg_us);
        double headroom_pct = 100.0 * (1.0 - static_cast<double>(avg_total)
                                        / static_cast<double>(kFrameBudgetUs));
        bool line_speed_ok = avg_total < kFrameBudgetUs;

        std::cout << "\n--- Line Speed Analysis ---\n";
        std::cout << "  Frame budget (60 ppm):  " << kFrameBudgetUs << " us\n";
        std::cout << "  Avg pipeline time:      " << avg_total << " us\n";
        std::cout << "  Headroom:               "
                  << std::fixed << std::setprecision(1) << headroom_pct << "%\n";
        std::cout << "  Line speed OK:          "
                  << (line_speed_ok ? "YES" : "NO — exceeds budget!")
                  << "\n" << std::endl;
    }
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

}  // namespace vision
}  // namespace gema

int main()
{
    using namespace gema::vision;

    constexpr unsigned kWarmupIterations = 10;
    unsigned total_iterations = parse_iterations(1000);

    print_header(total_iterations, kWarmupIterations);

    BenchmarkCollector collector;

    // --- Warm-up phase ---
    std::cout << "Warming up (" << kWarmupIterations << " iterations)..."
              << std::flush;
    for (unsigned i = 0; i < kWarmupIterations; ++i) {
        run_pipeline_iteration(collector);
    }
    // Discard warm-up data by resetting the collector.
    collector = BenchmarkCollector();
    std::cout << " done.\n" << std::endl;

    // --- Measurement phase ---
    std::cout << "Benchmarking (" << total_iterations << " iterations)..."
              << std::flush;
    for (unsigned i = 0; i < total_iterations; ++i) {
        run_pipeline_iteration(collector);

        // Live progress (every 10%).
        if (total_iterations >= 10) {
            unsigned tenth = total_iterations / 10;
            if (tenth > 0 && (i + 1) % tenth == 0) {
                std::cout << " " << ((i + 1) / tenth) * 10 << "%" << std::flush;
            }
        }
    }
    std::cout << " done.\n" << std::endl;

    // --- Report ---
    auto rpt = collector.get_report();
    rpt.sample_count = total_iterations;

    // Compute total_us as the average of total_pipeline samples.
    if (rpt.per_stage.count("total_pipeline")) {
        rpt.total_us = static_cast<uint64_t>(
            rpt.per_stage.at("total_pipeline").avg_us);
    }

    // Print human-readable summary.
    print_summary_table(rpt);

    // Print JSON report.
    std::cout << "--- JSON Report ---\n";
    std::cout << rpt.to_json() << std::endl;

    // Return success.
    return 0;
}
