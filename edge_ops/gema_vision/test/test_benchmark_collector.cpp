/**
 * @file test_benchmark_collector.cpp
 * @brief Unit tests for BenchmarkCollector, BenchmarkReport, and StageTimer.
 *
 * Strict TDD: test written before production code exists in benchmark/
 * to drive the API design and verify behavior.
 */

#include "benchmark/benchmark_stages.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <sstream>
#include <thread>
#include <vector>

namespace gema {
namespace vision {

// ===========================================================================
// BenchmarkStage unit tests
// ===========================================================================

TEST(BenchmarkStageTest, DefaultConstruction)
{
    BenchmarkStage s;
    EXPECT_EQ(s.name, "");
    EXPECT_EQ(s.min_us, 0ull);
    EXPECT_EQ(s.max_us, 0ull);
    EXPECT_DOUBLE_EQ(s.avg_us, 0.0);
    EXPECT_EQ(s.count, 0u);
}

TEST(BenchmarkStageTest, AccumulateSingleValue)
{
    BenchmarkStage s;
    s.name = "test_stage";
    s.accumulate(100);
    EXPECT_EQ(s.min_us, 100ull);
    EXPECT_EQ(s.max_us, 100ull);
    EXPECT_DOUBLE_EQ(s.avg_us, 100.0);
    EXPECT_EQ(s.count, 1u);
}

TEST(BenchmarkStageTest, AccumulateMultipleValues)
{
    BenchmarkStage s;
    s.name = "multi_stage";
    s.accumulate(100);
    s.accumulate(200);
    s.accumulate(50);
    EXPECT_EQ(s.min_us, 50ull);    // min of {100, 200, 50}
    EXPECT_EQ(s.max_us, 200ull);   // max of {100, 200, 50}
    EXPECT_DOUBLE_EQ(s.avg_us, (100.0 + 200.0 + 50.0) / 3.0);
    EXPECT_EQ(s.count, 3u);
}

TEST(BenchmarkStageTest, AccumulateUpdatesRunningStats)
{
    BenchmarkStage s;
    s.name = "running";
    s.accumulate(42);
    // Check that avg_us is computed from accumulated total, not reset
    double expected_avg = 42.0;
    EXPECT_DOUBLE_EQ(s.avg_us, expected_avg);
    EXPECT_EQ(s.min_us, 42ull);
    EXPECT_EQ(s.max_us, 42ull);

    s.accumulate(84);
    expected_avg = (42.0 + 84.0) / 2.0;
    EXPECT_DOUBLE_EQ(s.avg_us, expected_avg);
    EXPECT_EQ(s.min_us, 42ull);
    EXPECT_EQ(s.max_us, 84ull);
}

// ===========================================================================
// BenchmarkReport unit tests
// ===========================================================================

TEST(BenchmarkReportTest, DefaultReportHasNoStages)
{
    BenchmarkReport rpt;
    EXPECT_EQ(rpt.total_us, 0ull);
    EXPECT_EQ(rpt.sample_count, 0u);
    EXPECT_TRUE(rpt.per_stage.empty());
}

TEST(BenchmarkReportTest, ReportWithOneStage)
{
    BenchmarkReport rpt;
    BenchmarkStage stage;
    stage.name = "single";
    stage.min_us = 10;
    stage.max_us = 20;
    stage.avg_us = 15.0;
    stage.count = 5;
    rpt.per_stage["single"] = stage;
    rpt.total_us = 75;
    rpt.sample_count = 5;

    EXPECT_EQ(rpt.sample_count, 5u);
    EXPECT_EQ(rpt.total_us, 75ull);
    ASSERT_EQ(rpt.per_stage.size(), 1u);
    EXPECT_EQ(rpt.per_stage.at("single").avg_us, 15.0);
}

// ===========================================================================
// BenchmarkCollector unit tests
// ===========================================================================

TEST(BenchmarkCollectorTest, NewCollectorHasNoStages)
{
    BenchmarkCollector collector;
    auto rpt = collector.get_report();
    EXPECT_EQ(rpt.sample_count, 0u);
    EXPECT_TRUE(rpt.per_stage.empty());
}

TEST(BenchmarkCollectorTest, RecordSingleStage)
{
    BenchmarkCollector collector;
    collector.record_stage("inference", 1234);
    auto rpt = collector.get_report();

    ASSERT_EQ(rpt.per_stage.size(), 1u);
    const auto& s = rpt.per_stage.at("inference");
    EXPECT_EQ(s.min_us, 1234ull);
    EXPECT_EQ(s.max_us, 1234ull);
    EXPECT_DOUBLE_EQ(s.avg_us, 1234.0);
    EXPECT_EQ(s.count, 1u);
}

TEST(BenchmarkCollectorTest, RecordMultipleStages)
{
    BenchmarkCollector collector;
    collector.record_stage("crop", 500);
    collector.record_stage("infer", 3000);
    collector.record_stage("crop", 700);

    auto rpt = collector.get_report();

    ASSERT_EQ(rpt.per_stage.size(), 2u);

    // crop stage: 2 samples
    {
        const auto& s = rpt.per_stage.at("crop");
        EXPECT_EQ(s.min_us, 500ull);
        EXPECT_EQ(s.max_us, 700ull);
        EXPECT_DOUBLE_EQ(s.avg_us, 600.0);
        EXPECT_EQ(s.count, 2u);
    }

    // infer stage: 1 sample
    {
        const auto& s = rpt.per_stage.at("infer");
        EXPECT_EQ(s.min_us, 3000ull);
        EXPECT_EQ(s.max_us, 3000ull);
        EXPECT_DOUBLE_EQ(s.avg_us, 3000.0);
        EXPECT_EQ(s.count, 1u);
    }
}

TEST(BenchmarkCollectorTest, ConcurrentRecording)
{
    BenchmarkCollector collector;
    constexpr int kThreads = 4;
    constexpr int kRecordsPerThread = 250;
    std::vector<std::thread> workers;

    for (int t = 0; t < kThreads; ++t) {
        workers.emplace_back([&collector, t]() {
            for (int i = 0; i < kRecordsPerThread; ++i) {
                collector.record_stage("parallel", static_cast<uint64_t>(i * (t + 1)));
            }
        });
    }

    for (auto& w : workers) {
        w.join();
    }

    auto rpt = collector.get_report();
    ASSERT_EQ(rpt.per_stage.size(), 1u);
    const auto& s = rpt.per_stage.at("parallel");
    EXPECT_EQ(s.count, static_cast<unsigned>(kThreads * kRecordsPerThread));
    // min should be 0 (first call with i=0), max should be (kRecordsPerThread-1) * kThreads
    EXPECT_EQ(s.min_us, 0ull);
    EXPECT_GT(s.max_us, 0ull);
}

// ===========================================================================
// StageTimer RAII tests
// ===========================================================================

TEST(StageTimerTest, TimerRecordsDuration)
{
    BenchmarkCollector collector;
    {
        StageTimer timer("sleepy", collector);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }  // timer destructor records elapsed time

    auto rpt = collector.get_report();
    ASSERT_EQ(rpt.per_stage.size(), 1u);
    const auto& s = rpt.per_stage.at("sleepy");
    EXPECT_EQ(s.count, 1u);
    // Should be at least 5000 microseconds
    EXPECT_GE(s.min_us, 5000ull);
}

TEST(StageTimerTest, MultipleTimersRecordIndependentDurations)
{
    BenchmarkCollector collector;
    {
        StageTimer t1("stage_a", collector);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    {
        StageTimer t2("stage_b", collector);
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
    }

    auto rpt = collector.get_report();
    ASSERT_EQ(rpt.per_stage.size(), 2u);
    EXPECT_EQ(rpt.per_stage.at("stage_a").count, 1u);
    EXPECT_EQ(rpt.per_stage.at("stage_b").count, 1u);
    EXPECT_GE(rpt.per_stage.at("stage_a").min_us, 2000ull);
    EXPECT_GE(rpt.per_stage.at("stage_b").min_us, 3000ull);
}

// ===========================================================================
// JSON serialisation tests
// ===========================================================================

TEST(BenchmarkReportJsonTest, EmptyReportJson)
{
    BenchmarkCollector collector;
    auto rpt = collector.get_report();
    std::string json = rpt.to_json();
    // Should contain valid JSON structure even when empty
    EXPECT_NE(json.find("\"per_stage\""), std::string::npos);
    EXPECT_NE(json.find("\"total_us\""), std::string::npos);
    EXPECT_NE(json.find("\"sample_count\""), std::string::npos);
}

TEST(BenchmarkReportJsonTest, ReportJsonHasCorrectValues)
{
    BenchmarkCollector collector;
    collector.record_stage("test_stage", 100);
    collector.record_stage("test_stage", 200);
    auto rpt = collector.get_report();
    std::string json = rpt.to_json();

    // Verify structure
    EXPECT_NE(json.find("\"test_stage\""), std::string::npos);
    EXPECT_NE(json.find("\"count\": 2"), std::string::npos);
    EXPECT_NE(json.find("\"min_us\": 100"), std::string::npos);
    EXPECT_NE(json.find("\"max_us\": 200"), std::string::npos);
}

}  // namespace vision
}  // namespace gema
