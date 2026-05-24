/**
 * @file test_vision_pipeline.cpp
 * @brief Software-in-the-Loop (SIL) integration test for the
 *        C++ vision inference pipeline.
 *
 * Validates the entire Producer-Consumer threading model:
 *
 *   1. Instantiate REAL InferenceOrchestrator with MOCK dependencies.
 *   2. Inject frames directly into ThreadSafeQueue (simulating the
 *      GPIO-driven producer).
 *   3. Verify that the consumer loop correctly processes every frame,
 *      publishes results via MqttClient, and shuts down cleanly
 *      without deadlocks or dropped frames.
 */

#include "inference_orchestrator.hpp"
#include "mock_engine.hpp"
#include "mock_flash_trigger.hpp"
#include "thread_safe_queue.hpp"
#include "visual_primitive.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>

namespace gema {
namespace vision {

// ===========================================================================
// Mock MQTT client — counts publishes
// ===========================================================================

class MockMqttClient final : public MqttClient {
public:
    bool publish(const std::string& /*topic*/,
                 const std::string& /*payload*/) override
    {
        publish_count_.fetch_add(1);
        return true;
    }

    std::atomic<int> publish_count_{0};
};

// ===========================================================================
// Test fixture
// ===========================================================================

class VisionPipelineTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        // Reset state before each test.
        publish_count_ = 0;
    }

    void TearDown() override
    {
        // Orchestrator is destroyed automatically.
    }

    std::atomic<int> publish_count_{0};
};

// ===========================================================================
// Test: Consumer processes all injected frames
// ===========================================================================

TEST_F(VisionPipelineTest, ConsumerProcessesAllFrames)
{
    // ---- Arrange ---------------------------------------------------------
    constexpr int kNumFrames        = 5;
    constexpr int kMockLatencyMs    = 20;   // per-frame NPU simulation
    constexpr int kProcessingBudget = 500;  // ms safety margin

    ThreadSafeQueue<cv::Mat> queue;
    MockEngine              engine(kMockLatencyMs);
    MockMqttClient          mqtt_client;
    // MockFlashTrigger is exercised by the producer (separate test);
    // here we test the consumer in isolation.

    InferenceOrchestrator orchestrator(engine, queue, mqtt_client);

    // ---- Act -------------------------------------------------------------
    orchestrator.start();

    // Inject frames from the test thread (simulating the GPIO producer).
    for (int i = 0; i < kNumFrames; ++i) {
        queue.push(cv::Mat::zeros(640, 480, CV_8UC3));
    }

    // Wait for all frames to be consumed.
    // Budget: 5 frames × 20 ms = 100 ms; we allow 500 ms for safety.
    std::this_thread::sleep_for(
        std::chrono::milliseconds(kProcessingBudget));

    orchestrator.stop();

    // ---- Assert ----------------------------------------------------------
    EXPECT_EQ(orchestrator.frames_processed(),
              static_cast<uint64_t>(kNumFrames));
    EXPECT_EQ(mqtt_client.publish_count_.load(), kNumFrames);
}

// ===========================================================================
// Test: Zero frames — no crash, no publish
// ===========================================================================

TEST_F(VisionPipelineTest, NoFramesNoCrash)
{
    ThreadSafeQueue<cv::Mat> queue;
    MockEngine              engine(20);
    MockMqttClient          mqtt_client;

    InferenceOrchestrator orchestrator(engine, queue, mqtt_client);

    orchestrator.start();
    // Push nothing — queue stays empty.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    orchestrator.stop();

    EXPECT_EQ(orchestrator.frames_processed(), 0);
    EXPECT_EQ(mqtt_client.publish_count_.load(), 0);
}

// ===========================================================================
// Test: Injected frame is parsed and produces two primitives
// ===========================================================================

TEST_F(VisionPipelineTest, MockEngineReturnsTwoPrimitives)
{
    // Verify that MockEngine produces the expected canned output.
    MockEngine engine(1);  // minimal latency for unit test
    cv::Mat dummy = cv::Mat::zeros(640, 480, CV_8UC3);
    auto batch = engine.infer(dummy);

    ASSERT_EQ(batch.size(), 2);
    EXPECT_EQ(batch[0].class_id, 0);  // DEFECT
    EXPECT_EQ(batch[1].class_id, 1);  // OCR_ZONE
    EXPECT_FLOAT_EQ(batch[0].confidence, 0.95f);
    EXPECT_FLOAT_EQ(batch[1].confidence, 0.88f);
}

// ===========================================================================
// Test: Double start / stop is idempotent
// ===========================================================================

TEST_F(VisionPipelineTest, DoubleStartStopIsIdempotent)
{
    ThreadSafeQueue<cv::Mat> queue;
    MockEngine              engine(5);
    MockMqttClient          mqtt_client;

    InferenceOrchestrator orchestrator(engine, queue, mqtt_client);

    // Start twice.
    orchestrator.start();
    orchestrator.start();  // second start MUST be a no-op.

    queue.push(cv::Mat::zeros(640, 480, CV_8UC3));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    orchestrator.stop();
    orchestrator.stop();  // second stop MUST be a no-op (no crash).

    EXPECT_EQ(orchestrator.frames_processed(), 1);
    EXPECT_EQ(mqtt_client.publish_count_.load(), 1);
}

}  // namespace vision
}  // namespace gema
