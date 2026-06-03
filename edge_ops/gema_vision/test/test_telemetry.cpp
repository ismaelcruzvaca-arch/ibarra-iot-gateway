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

    // Create collector with short interval and a test device_id
    TelemetryCollector collector(
        harness.mqtt,
        harness.orchestrator,
        harness.queue,
        harness.thermal,
        "test_device_01",
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

    // ── New unified schema: verify all 5 required top-level fields ──────────
    EXPECT_NE(json.find("\"device_id\":"), std::string::npos)
        << "Payload must contain device_id";
    EXPECT_NE(json.find("\"device_type\":"), std::string::npos)
        << "Payload must contain device_type";
    EXPECT_NE(json.find("\"timestamp\":"), std::string::npos)
        << "Payload must contain ISO 8601 timestamp";
    EXPECT_NE(json.find("\"node_health\":"), std::string::npos)
        << "Payload must contain node_health";
    EXPECT_NE(json.find("\"metrics\":"), std::string::npos)
        << "Payload must contain metrics array";

    // ── Verify device identity values ──────────────────────────────────────
    EXPECT_NE(json.find("\"test_device_01\""), std::string::npos)
        << "device_id must match the injected value";
    EXPECT_NE(json.find("\"camera\""), std::string::npos)
        << "device_type must be \"camera\"";

    // ── Verify node_health is a valid enum value ────────────────────────────
    EXPECT_NE(json.find("\"ONLINE\""), std::string::npos)
        << "Default node_health should be ONLINE (temp <= 75)";

    // ── Verify flat fields are GONE from root ──────────────────────────────
    // These were previously at the top level; now they live in metrics[].
    EXPECT_EQ(json.find("\"uptime_sec\":"), std::string::npos)
        << "uptime_sec must NOT be a flat root field";
    EXPECT_EQ(json.find("\"fps\":"), std::string::npos)
        << "fps must NOT be a flat root field";
    EXPECT_EQ(json.find("\"last_defect_ts\":"), std::string::npos)
        << "last_defect_ts must NOT be a flat root field";

    // ── Verify metrics[] array structure ───────────────────────────────────
    auto metrics_start = json.find("\"metrics\":[");
    ASSERT_NE(metrics_start, std::string::npos)
        << "metrics must be a JSON array starting with [" << '\n'
        << json;

    // Each metric entry must contain "name" and "value"
    EXPECT_NE(json.find("\"name\":"), std::string::npos)
        << "Each metric entry must have a name field";
    EXPECT_NE(json.find("\"value\":"), std::string::npos)
        << "Each metric entry must have a value field";

    // Some metrics have unit (uptime_sec → "s", soc_temp_c → °C, heap → kB)
    EXPECT_NE(json.find("\"unit\":\"s\""), std::string::npos)
        << "uptime_sec metric must have unit \"s\"";

    // ── Verify JSON structural integrity ───────────────────────────────────
    EXPECT_EQ(json.front(), '{');
    EXPECT_EQ(json.back(), '}');

    auto close_brace = json.rfind('}');
    ASSERT_NE(close_brace, std::string::npos);
    if (close_brace > 1) {
        EXPECT_NE(json[close_brace - 1], ',')
            << "No trailing comma before closing brace";
    }

    // ── Verify topic ───────────────────────────────────────────────────────
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
        "test_device_01",
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
        "test_device_01",
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
    // In the new schema, fps lives in metrics[] as {"name":"fps","value":15.0}
    auto fps_name_pos = payload.find("\"name\":\"fps\"");
    ASSERT_NE(fps_name_pos, std::string::npos);

    // Find the "value" key after the fps name entry
    auto value_pos = payload.find("\"value\":", fps_name_pos);
    ASSERT_NE(value_pos, std::string::npos);
    auto after_key = value_pos + 8;  // past "\"value\":"
    auto comma = payload.find(',', after_key);
    auto brace = payload.find('}', after_key);
    auto end_pos = (comma != std::string::npos && comma < brace) ? comma : brace;
    std::string fps_str = payload.substr(after_key, end_pos - after_key);
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
        "test_device_01",
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
