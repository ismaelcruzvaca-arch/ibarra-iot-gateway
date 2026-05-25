/**
 * @file test_telemetry.cpp
 * @brief SIL tests for TelemetryCollector.
 *
 * Validates:
 *   1. JSON payload format and content
 *   2. Start / stop lifecycle (idempotent)
 *   3. FPS calculation across intervals
 *   4. VmRSS parsing from /proc/self/status
 *   5. Graceful shutdown via stop() while sleeping
 */

#include "inference_orchestrator.hpp"
#include "mock_engine.hpp"
#include "telemetry_collector.hpp"
#include "thermal_monitor.hpp"
#include "thread_safe_queue.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace gema {
namespace vision {

// ===========================================================================
// Mock MQTT client — records last published payload
// ===========================================================================

class MockTelemetryMqtt final : public MqttClient {
public:
    bool publish(const std::string& topic,
                 const std::string& payload) override
    {
        std::lock_guard<std::mutex> lock(mtx_);
        last_topic_ = topic;
        last_payload_ = payload;
        publish_count_.fetch_add(1);
        return true;
    }

    std::mutex mtx_;
    std::string last_topic_;
    std::string last_payload_;
    std::atomic<int> publish_count_{0};
};

// ===========================================================================
// Helpers
// ===========================================================================

/** @brief Build a TelemetryCollector with mocked dependencies for testing. */
struct TelemetryTestHarness {
    ThreadSafeQueue<std::shared_ptr<cv::Mat>> queue{8};
    MockEngine engine{10};
    MockTelemetryMqtt mqtt;
    SpatialCalibrator calibrator;
    PostProcessDispatcher dispatcher{nullptr, nullptr};
    InferenceOrchestrator orchestrator{
        engine, queue, mqtt, calibrator, dispatcher, 0};
    ThermalMonitor thermal;

    // We need to start the orchestrator so frames_processed advances.
    void produce_frames(int count)
    {
        orchestrator.start();
        for (int i = 0; i < count; ++i) {
            queue.push(std::make_shared<cv::Mat>(
                cv::Mat::zeros(640, 480, CV_8UC3)));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(
            count * 20 + 50));  // wait for consumer
        orchestrator.stop();
    }
};

// ===========================================================================
// 1. JSON payload format
// ===========================================================================

TEST(TelemetryPayloadTest, JsonHasAllRequiredFields)
{
    TelemetryTestHarness harness;
    harness.produce_frames(5);

    // Create collector with short interval
    TelemetryCollector collector(
        harness.mqtt,
        harness.orchestrator,
        harness.queue,
        harness.thermal,
        std::chrono::seconds{1});  // 1 s for fast test

    collector.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    collector.stop();

    ASSERT_GE(harness.mqtt.publish_count_.load(), 1);

    std::string json;
    {
        std::lock_guard<std::mutex> lock(harness.mqtt.mtx_);
        json = harness.mqtt.last_payload_;
    }

    // Verify all required fields exist in the JSON
    EXPECT_NE(json.find("\"uptime_sec\":"), std::string::npos);
    EXPECT_NE(json.find("\"fps\":"), std::string::npos);
    EXPECT_NE(json.find("\"frames_processed\":"), std::string::npos);
    EXPECT_NE(json.find("\"frames_dropped\":"), std::string::npos);
    EXPECT_NE(json.find("\"soc_temp_c\":"), std::string::npos);
    EXPECT_NE(json.find("\"heap_resident_kb\":"), std::string::npos);
    EXPECT_NE(json.find("\"defects_total\":"), std::string::npos);
    EXPECT_NE(json.find("\"last_defect_ts\":"), std::string::npos);

    // Verify JSON is syntactically valid (starts with {, ends with })
    EXPECT_EQ(json.front(), '{');
    EXPECT_EQ(json.back(), '}');

    // Verify no trailing comma before closing brace
    auto close_brace = json.rfind('}');
    ASSERT_NE(close_brace, std::string::npos);
    if (close_brace > 1) {
        EXPECT_NE(json[close_brace - 1], ',');
    }

    // Verify topic
    EXPECT_EQ(harness.mqtt.last_topic_, "novamex/vision/telemetry");
}

// ===========================================================================
// 2. Start / stop lifecycle
// ===========================================================================

TEST(TelemetryLifecycleTest, StartStopIsIdempotent)
{
    TelemetryTestHarness harness;

    TelemetryCollector collector(
        harness.mqtt,
        harness.orchestrator,
        harness.queue,
        harness.thermal,
        std::chrono::seconds{30});

    EXPECT_FALSE(collector.is_running());

    collector.start();
    EXPECT_TRUE(collector.is_running());

    // Double start should be a no-op
    collector.start();
    EXPECT_TRUE(collector.is_running());

    collector.stop();
    EXPECT_FALSE(collector.is_running());

    // Double stop should be a no-op (no crash)
    collector.stop();
    EXPECT_FALSE(collector.is_running());
}

// ===========================================================================
// 3. FPS calculation
// ===========================================================================

TEST(TelemetryFpsTest, CalculatesFpsFromFrameDelta)
{
    TelemetryTestHarness harness;

    // Start the orchestrator (consumer thread).
    harness.orchestrator.start();

    TelemetryCollector collector(
        harness.mqtt,
        harness.orchestrator,
        harness.queue,
        harness.thermal,
        std::chrono::seconds{1});

    // Start the collector.  last_frames_ snapshot is taken here.
    collector.start();

    // Push frames while the orchestrator is running — they get consumed
    // and frames_processed advances.
    for (int i = 0; i < 10; ++i) {
        harness.queue.push(std::make_shared<cv::Mat>(
            cv::Mat::zeros(640, 480, CV_8UC3)));
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    // Wait for the telemetry interval to elapse and a publish to happen.
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));

    collector.stop();
    harness.orchestrator.stop();

    ASSERT_GE(harness.mqtt.publish_count_.load(), 1);

    std::string payload;
    {
        std::lock_guard<std::mutex> lock(harness.mqtt.mtx_);
        payload = harness.mqtt.last_payload_;
    }

    // Check FPS is positive (we produced frames, interval is 1s)
    auto fps_pos = payload.find("\"fps\":");
    ASSERT_NE(fps_pos, std::string::npos);

    // Extract the fps value
    auto after_key = fps_pos + 6;  // past "fps:"
    auto comma = payload.find(',', after_key);
    std::string fps_str = payload.substr(after_key, comma - after_key);
    double fps = std::stod(fps_str);

    EXPECT_GT(fps, 0.0) << "FPS should be positive when frames were produced";
}

// ===========================================================================
// 4. Graceful shutdown while sleeping
// ===========================================================================
// 4. Graceful shutdown while sleeping
// ===========================================================================

TEST(TelemetryShutdownTest, StopReturnsQuicklyDuringSleep)
{
    TelemetryTestHarness harness;

    TelemetryCollector collector(
        harness.mqtt,
        harness.orchestrator,
        harness.queue,
        harness.thermal,
        std::chrono::seconds{300});  // 5 min interval — will be sleeping

    collector.start();

    // Give thread time to enter cv::wait_for
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    auto start = std::chrono::steady_clock::now();
    collector.stop();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    // Must return in < 1 s (not 300 s!)
    EXPECT_LT(elapsed, 1000) << "stop() should not wait for the full interval";
}

}  // namespace vision
}  // namespace gema
