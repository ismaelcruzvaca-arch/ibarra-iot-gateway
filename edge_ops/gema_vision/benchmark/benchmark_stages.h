#pragma once

/**
 * @file benchmark_stages.h
 * @brief Data structures and utilities for per-stage pipeline benchmarking.
 *
 * RAII + thread-safe collector designed for measuring latency of each
 * stage in the vision pipeline (sensor readout, ISP, crop, inference,
 * L*a*b* calculation, fusion) on the RV1106 NPU hardware.
 *
 * All time measurements use std::chrono::high_resolution_clock.
 */

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>

namespace gema {
namespace vision {

// ===========================================================================
// BenchmarkStage — per-stage statistics
// ===========================================================================

/**
 * @brief Running statistics for a single pipeline stage.
 *
 * Accumulates min/max/avg/count across multiple calls to `accumulate()`.
 * Thread-safe only when used via BenchmarkCollector's mutex.
 */
struct BenchmarkStage {
    /** Human-readable stage name (e.g. "sensor_readout", "ocr_relief"). */
    std::string name;

    /** Minimum observed latency in microseconds. */
    uint64_t min_us = 0;

    /** Maximum observed latency in microseconds. */
    uint64_t max_us = 0;

    /** Running average latency in microseconds. */
    double avg_us = 0.0;

    /** Number of samples accumulated. */
    unsigned count = 0;

    /**
     * @brief Accumulate one latency sample into running stats.
     * @param elapsed_us  Measured latency in microseconds.
     */
    void accumulate(uint64_t elapsed_us) noexcept
    {
        if (count == 0) {
            min_us = elapsed_us;
            max_us = elapsed_us;
            avg_us = static_cast<double>(elapsed_us);
        } else {
            min_us = std::min(min_us, elapsed_us);
            max_us = std::max(max_us, elapsed_us);
            // Running average avoids overflow for large counts.
            avg_us = avg_us + (static_cast<double>(elapsed_us) - avg_us)
                             / static_cast<double>(count + 1);
        }
        ++count;
    }
};

// ===========================================================================
// BenchmarkReport — snapshot of all stages
// ===========================================================================

/**
 * @brief Immutable snapshot of benchmark results.
 *
 * Produced by BenchmarkCollector::get_report() after a measurement run.
 */
struct BenchmarkReport {
    /** Per-stage statistics keyed by stage name. */
    std::unordered_map<std::string, BenchmarkStage> per_stage;

    /** Cumulative wall-clock time across all stages (last sample). */
    uint64_t total_us = 0;

    /** Total number of complete pipeline iterations measured. */
    unsigned sample_count = 0;

    /**
     * @brief Serialise this report to a minimal JSON string.
     *
     * Hand-rolled to avoid external JSON dependency (no nlohmann/json).
     */
    std::string to_json() const
    {
        std::ostringstream os;
        os << "{\n";
        os << "  \"sample_count\": " << sample_count << ",\n";
        os << "  \"total_us\": " << total_us << ",\n";
        os << "  \"per_stage\": {\n";

        bool first = true;
        for (const auto& [name, stage] : per_stage) {
            if (!first) {
                os << ",\n";
            }
            first = false;
            os << "    \"" << name << "\": {\n";
            os << "      \"count\": " << stage.count << ",\n";
            os << "      \"min_us\": " << stage.min_us << ",\n";
            os << "      \"max_us\": " << stage.max_us << ",\n";
            os << "      \"avg_us\": " << stage.avg_us << "\n";
            os << "    }";
        }
        os << "\n  }\n";
        os << "}\n";
        return os.str();
    }
};

// ===========================================================================
// BenchmarkCollector — thread-safe statistics aggregator
// ===========================================================================

/**
 * @brief Thread-safe collector that accumulates per-stage latency samples.
 *
 * Designed to be shared across multiple threads (e.g. parallel inference
 * stages) via the mutex-protected `record_stage()` method.
 *
 * Usage:
 * @code
 *   BenchmarkCollector collector;
 *   collector.record_stage("sensor_readout", 16234);
 *   collector.record_stage("ocr_relief",    21500);
 *   auto report = collector.get_report();
 *   std::cout << report.to_json() << std::endl;
 * @endcode
 */
class BenchmarkCollector {
public:
    /**
     * @brief Record a single latency sample for a stage.
     *
     * @param name       Stage identifier (e.g. "crop_engine").
     * @param elapsed_us Measured wall-clock time in microseconds.
     */
    void record_stage(const std::string& name, uint64_t elapsed_us)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stages_[name].name = name;
        stages_[name].accumulate(elapsed_us);
    }

    /**
     * @brief Produce a snapshot report of all collected data.
     *
     * Thread-safe. Returns a copy of the current statistics.
     */
    BenchmarkReport get_report() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        BenchmarkReport rpt;
        rpt.per_stage = stages_;
        // Compute totals from the accumulated data.
        for (const auto& [name, stage] : stages_) {
            if (stage.count > rpt.sample_count) {
                rpt.sample_count = stage.count;
            }
        }
        return rpt;
    }

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, BenchmarkStage> stages_;
};

// ===========================================================================
// StageTimer — RAII scope timer
// ===========================================================================

/**
 * @brief RAII helper that measures elapsed time and records it to a
 *        BenchmarkCollector on destruction.
 *
 * Usage:
 * @code
 *   {
 *       StageTimer timer("ocr_inference", collector);
 *       run_inference();  // measured automatically
 *   }  // timer destructor calls collector.record_stage()
 * @endcode
 */
class StageTimer {
public:
    /**
     * @brief Start timing a stage.
     *
     * @param name      Stage name (passed to record_stage).
     * @param collector Target collector for the measurement.
     */
    StageTimer(const std::string& name, BenchmarkCollector& collector)
        : name_(name)
        , collector_(collector)
        , start_(std::chrono::high_resolution_clock::now())
    {}

    /**
     * @brief Stop timing and record the elapsed duration.
     *
     * Guaranteed to record even if the scope exits via exception.
     */
    ~StageTimer()
    {
        auto end = std::chrono::high_resolution_clock::now();
        auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
            end - start_).count();
        collector_.record_stage(name_, static_cast<uint64_t>(elapsed_us));
    }

    // Non-copiable, non-movable.
    StageTimer(const StageTimer&) = delete;
    StageTimer& operator=(const StageTimer&) = delete;
    StageTimer(StageTimer&&) = delete;
    StageTimer& operator=(StageTimer&&) = delete;

private:
    std::string name_;
    BenchmarkCollector& collector_;
    std::chrono::time_point<std::chrono::high_resolution_clock> start_;
};

}  // namespace vision
}  // namespace gema
