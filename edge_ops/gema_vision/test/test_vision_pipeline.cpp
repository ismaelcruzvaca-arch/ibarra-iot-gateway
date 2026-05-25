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
 *
 * Also validates DataCollectorEngine frame capture, mode switching,
 * and MQTT status publishing.
 */

#include "color_validator.hpp"
#include "data_collector_engine.hpp"
#include "frame_pool.hpp"
#include "inference_orchestrator.hpp"
#include "mock_engine.hpp"
#include "mock_flash_trigger.hpp"
#include "ocr_engine.hpp"
#include "postproc_dispatcher.hpp"
#include "spatial_calibrator.hpp"
#include "thread_safe_queue.hpp"
#include "visual_primitive.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace gema {
namespace vision {

// ===========================================================================
// FramePool tests — acquire_shared() with custom deleter
// ===========================================================================

class FramePoolTest : public ::testing::Test {
protected:
    FramePool pool_{640, 480, CV_8UC3};
};

TEST_F(FramePoolTest, AcquireSharedReturnsValidBuffer)
{
    auto frame = pool_.acquire_shared();
    ASSERT_NE(frame, nullptr);
    EXPECT_EQ(frame->rows, 480);
    EXPECT_EQ(frame->cols, 640);
    EXPECT_EQ(frame->type(), CV_8UC3);
}

TEST_F(FramePoolTest, AcquireSharedReleasesOnScopeExit)
{
    size_t acquired_count = 0;
    {
        auto frame = pool_.acquire_shared();
        ASSERT_NE(frame, nullptr);
        // Count how many buffers we can acquire after this one goes out
        // of scope — the custom deleter should release it.
    }
    // After scope exit, the buffer should be available again.
    // Acquire all 4 pool buffers — should succeed without spinning.
    for (size_t i = 0; i < FramePool::kPoolSize; ++i) {
        auto frame = pool_.acquire_shared();
        ASSERT_NE(frame, nullptr);
        ++acquired_count;
    }
    EXPECT_EQ(acquired_count, FramePool::kPoolSize);
}

TEST_F(FramePoolTest, AcquireSharedMultipleAreDistinct)
{
    auto a = pool_.acquire_shared();
    auto b = pool_.acquire_shared();
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    // Pointers must be distinct (different pool slots).
    EXPECT_NE(a.get(), b.get());
}

TEST_F(FramePoolTest, CustomDeleterReturnsBufferToPool)
{
    // Acquire all 4 buffers.
    std::vector<std::shared_ptr<cv::Mat>> frames;
    for (size_t i = 0; i < FramePool::kPoolSize; ++i) {
        frames.push_back(pool_.acquire_shared());
    }
    ASSERT_EQ(frames.size(), FramePool::kPoolSize);

    // Release one by resetting.
    frames[0].reset();
    // Now we should be able to acquire one more (the released slot).
    auto recycled = pool_.acquire_shared();
    ASSERT_NE(recycled, nullptr);
    EXPECT_EQ(recycled->rows, 480);
    EXPECT_EQ(recycled->cols, 640);
}

// ===========================================================================
// Mock MQTT client — counts publishes and captures last topic+payload
// ===========================================================================

class MockMqttClient final : public MqttClient {
public:
    bool publish(const std::string& topic,
                 const std::string& payload) override
    {
        publish_count_.fetch_add(1);
        std::lock_guard<std::mutex> lock(mtx_);
        last_topic_ = topic;
        payloads_.push_back(payload);
        return true;
    }

    /** @brief Total number of publish() calls. */
    std::atomic<int> publish_count_{0};

    /** @brief Topic from the most recent publish() call. */
    std::string last_topic_;

    /** @brief Ordered list of all payloads received. */
    std::vector<std::string> payloads_;

private:
    std::mutex mtx_;
};

// ===========================================================================
// Test fixture
// ===========================================================================

class VisionPipelineTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        publish_count_ = 0;
    }

    void TearDown() override {}

    std::atomic<int> publish_count_{0};

    // Uncalibrated calibrator (no H loaded — no-op by design).
    SpatialCalibrator calibrator_;

    // No-op dispatcher (no OCR/color engines configured).
    PostProcessDispatcher dispatcher_{nullptr, nullptr};
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

    ThreadSafeQueue<std::shared_ptr<cv::Mat>> queue;
    MockEngine                               engine(kMockLatencyMs);
    MockMqttClient                           mqtt_client;
    // MockFlashTrigger is exercised by the producer (separate test);
    // here we test the consumer in isolation.

    InferenceOrchestrator orchestrator(engine, queue, mqtt_client, calibrator_, dispatcher_);

    // ---- Act -------------------------------------------------------------
    orchestrator.start();

    // Inject frames from the test thread (simulating the GPIO producer).
    for (int i = 0; i < kNumFrames; ++i) {
        queue.push(std::make_shared<cv::Mat>(cv::Mat::zeros(640, 480, CV_8UC3)));
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
    ThreadSafeQueue<std::shared_ptr<cv::Mat>> queue;
    MockEngine                               engine(20);
    MockMqttClient                           mqtt_client;

    InferenceOrchestrator orchestrator(engine, queue, mqtt_client, calibrator_, dispatcher_);

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
    ThreadSafeQueue<std::shared_ptr<cv::Mat>> queue;
    MockEngine                               engine(5);
    MockMqttClient                           mqtt_client;

    InferenceOrchestrator orchestrator(engine, queue, mqtt_client, calibrator_, dispatcher_);

    // Start twice.
    orchestrator.start();
    orchestrator.start();  // second start MUST be a no-op.

    queue.push(std::make_shared<cv::Mat>(cv::Mat::zeros(640, 480, CV_8UC3)));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    orchestrator.stop();
    orchestrator.stop();  // second stop MUST be a no-op (no crash).

    EXPECT_EQ(orchestrator.frames_processed(), 1);
    EXPECT_EQ(mqtt_client.publish_count_.load(), 1);
}

// ===========================================================================
// DataCollectorEngine Tests
// ===========================================================================

/**
 * @brief Test fixture that provides a temp directory and a fresh engine.
 *
 * Each test gets:
 *   - A unique temporary directory (auto-cleaned on destruction).
 *   - A DataCollectorEngine pointed at that directory.
 *   - A MockMqttClient for verifying status messages.
 */
class DataCollectorTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        // Create a unique temp directory for this test.
        auto tmp = std::filesystem::temp_directory_path();
        test_dir_ = tmp / "gema_dc_test_XXXXXX";
        // mkdtemp-like: append a unique suffix.
        auto uid = std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
        test_dir_ = tmp / ("gema_dc_test_" + uid);
        std::filesystem::create_directories(test_dir_);

        engine_ = std::make_unique<DataCollectorEngine>(
            &mqtt_, test_dir_.string());
    }

    void TearDown() override
    {
        engine_.reset();
        // Clean up temp files.
        std::filesystem::remove_all(test_dir_);
    }

    /** @brief Return the number of .jpg files in the test output directory. */
    int jpeg_count() const
    {
        int count = 0;
        for (auto& entry : std::filesystem::directory_iterator(test_dir_)) {
            if (entry.path().extension() == ".jpg") {
                ++count;
            }
        }
        return count;
    }

    std::filesystem::path           test_dir_;
    MockMqttClient                  mqtt_;
    std::unique_ptr<DataCollectorEngine> engine_;
};

// ---------------------------------------------------------------------------
// Test: IDLE mode discards frames
// ---------------------------------------------------------------------------

TEST_F(DataCollectorTest, IdleModeDiscardsFrames)
{
    ASSERT_EQ(engine_->mode(), DataCollectorEngine::Mode::IDLE);

    cv::Mat dummy = cv::Mat::zeros(640, 480, CV_8UC3);
    for (int i = 0; i < 5; ++i) {
        engine_->infer(dummy);
    }

    EXPECT_EQ(engine_->capture_count(), 0);
    EXPECT_EQ(jpeg_count(), 0);
    EXPECT_EQ(mqtt_.publish_count_.load(), 0);
}

// ---------------------------------------------------------------------------
// Test: CALIBRATION mode saves frames as JPEG
// ---------------------------------------------------------------------------

TEST_F(DataCollectorTest, CalibrationModeSavesFrames)
{
    engine_->set_mode(DataCollectorEngine::Mode::CALIBRATION);

    cv::Mat dummy = cv::Mat::zeros(640, 480, CV_8UC3);
    for (int i = 0; i < 5; ++i) {
        engine_->infer(dummy);
    }

    EXPECT_EQ(engine_->capture_count(), 5);
    EXPECT_EQ(jpeg_count(), 5);

    // Verify files are valid JPEGs by checking magic bytes.
    for (auto& entry : std::filesystem::directory_iterator(test_dir_)) {
        if (entry.path().extension() == ".jpg") {
            std::ifstream f(entry.path(), std::ios::binary);
            char magic[2]{};
            f.read(magic, 2);
            // JPEG magic: 0xFF 0xD8
            EXPECT_EQ(static_cast<uint8_t>(magic[0]), 0xFF);
            EXPECT_EQ(static_cast<uint8_t>(magic[1]), 0xD8);
        }
    }
}

// ---------------------------------------------------------------------------
// Test: MQTT status published on start and during capture
// ---------------------------------------------------------------------------

TEST_F(DataCollectorTest, MqttStatusOnCalibrationStart)
{
    engine_->set_mode(DataCollectorEngine::Mode::CALIBRATION);

    ASSERT_GE(mqtt_.publish_count_.load(), 1);
    EXPECT_EQ(mqtt_.last_topic_, "novamex/vision/status");
    EXPECT_EQ(mqtt_.payloads_[0], "calibration_started");
}

// ---------------------------------------------------------------------------
// Test: Auto-stop at 200 frames
// ---------------------------------------------------------------------------

TEST_F(DataCollectorTest, AutoStopAtLimit)
{
    constexpr int kLimit = 200;
    engine_->set_mode(DataCollectorEngine::Mode::CALIBRATION);

    cv::Mat dummy = cv::Mat::zeros(640, 480, CV_8UC3);
    for (int i = 0; i < kLimit + 10; ++i) {
        engine_->infer(dummy);
    }

    // Must NOT exceed the limit.
    EXPECT_EQ(engine_->capture_count(), kLimit);
    EXPECT_EQ(jpeg_count(), kLimit);

    // Must have auto-returned to IDLE.
    EXPECT_EQ(engine_->mode(), DataCollectorEngine::Mode::IDLE);
}

// ---------------------------------------------------------------------------
// Test: Set mode to CALIBRATION and back to IDLE
// ---------------------------------------------------------------------------

TEST_F(DataCollectorTest, SetModeBackToIdle)
{
    engine_->set_mode(DataCollectorEngine::Mode::CALIBRATION);
    EXPECT_EQ(engine_->mode(), DataCollectorEngine::Mode::CALIBRATION);

    cv::Mat dummy = cv::Mat::zeros(640, 480, CV_8UC3);
    engine_->infer(dummy);
    EXPECT_EQ(engine_->capture_count(), 1);

    engine_->set_mode(DataCollectorEngine::Mode::IDLE);
    EXPECT_EQ(engine_->mode(), DataCollectorEngine::Mode::IDLE);

    // Subsequent frames should NOT increment the counter.
    for (int i = 0; i < 5; ++i) {
        engine_->infer(dummy);
    }
    EXPECT_EQ(engine_->capture_count(), 1);
    EXPECT_EQ(jpeg_count(), 1);
}

// ===========================================================================
// SpatialCalibrator Tests
// ===========================================================================

class SpatialCalibratorTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        // Identity homography: pixel == mm (1:1 mapping, no skew).
        // Useful for deterministic test assertions.
        identity_H_ = {
            1.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 1.0f
        };
    }

    /** Build a simple primitive for testing. */
    VisualPrimitive make_primitive(int x, int y, int w, int h) const
    {
        VisualPrimitive p;
        p.bbox = cv::Rect(x, y, w, h);
        return p;
    }

    std::vector<float> identity_H_;
};

// ---------------------------------------------------------------------------
// Test: Default state — uncalibrated
// ---------------------------------------------------------------------------

TEST_F(SpatialCalibratorTest, DefaultStateIsUncalibrated)
{
    SpatialCalibrator cal;
    EXPECT_FALSE(cal.is_calibrated());
}

// ---------------------------------------------------------------------------
// Test: Load valid homography → calibrated
// ---------------------------------------------------------------------------

TEST_F(SpatialCalibratorTest, LoadValidHomography)
{
    SpatialCalibrator cal;
    EXPECT_TRUE(cal.load_from_vector(identity_H_));
    EXPECT_TRUE(cal.is_calibrated());
}

// ---------------------------------------------------------------------------
// Test: Load invalid (empty) vector → not calibrated
// ---------------------------------------------------------------------------

TEST_F(SpatialCalibratorTest, LoadInvalidEmpty)
{
    SpatialCalibrator cal;
    EXPECT_FALSE(cal.load_from_vector({}));
    EXPECT_FALSE(cal.is_calibrated());
}

// ---------------------------------------------------------------------------
// Test: Load degenerate (zero determinant) → not calibrated
// ---------------------------------------------------------------------------

TEST_F(SpatialCalibratorTest, LoadDegenerateMatrix)
{
    SpatialCalibrator cal;
    // All zeros → determinant = 0 → degenerate.
    EXPECT_FALSE(cal.load_from_vector({0,0,0, 0,0,0, 0,0,0}));
    EXPECT_FALSE(cal.is_calibrated());
}

// ---------------------------------------------------------------------------
// Test: Bottom-centre anchor point with identity homography
// ---------------------------------------------------------------------------

TEST_F(SpatialCalibratorTest, BottomCentreAnchorWithIdentity)
{
    SpatialCalibrator cal;
    ASSERT_TRUE(cal.load_from_vector(identity_H_));

    // Primitive at x=100, y=200, width=50, height=80.
    // Anchor point = (100 + 50/2, 200 + 80) = (125, 280)
    VisualPrimitive p = make_primitive(100, 200, 50, 80);

    cal.apply(p);

    EXPECT_TRUE(p.has_physical_coords);
    EXPECT_FLOAT_EQ(p.physical_coords_mm.x, 125.0f);
    EXPECT_FLOAT_EQ(p.physical_coords_mm.y, 280.0f);
}

// ---------------------------------------------------------------------------
// Test: Uncalibrated apply is a no-op
// ---------------------------------------------------------------------------

TEST_F(SpatialCalibratorTest, UncalibratedApplyIsNoOp)
{
    SpatialCalibrator cal;  // NOT calibrated
    VisualPrimitive p = make_primitive(100, 200, 50, 80);

    cal.apply(p);

    EXPECT_FALSE(p.has_physical_coords);
    EXPECT_FLOAT_EQ(p.physical_coords_mm.x, -1.0f);
    EXPECT_FLOAT_EQ(p.physical_coords_mm.y, -1.0f);
}

// ---------------------------------------------------------------------------
// Test: Batch apply calibrates all primitives
// ---------------------------------------------------------------------------

TEST_F(SpatialCalibratorTest, BatchApplyCalibratesAll)
{
    SpatialCalibrator cal;
    ASSERT_TRUE(cal.load_from_vector(identity_H_));

    PrimitiveBatch batch;
    batch.push_back(make_primitive(0, 0, 100, 100));
    batch.push_back(make_primitive(200, 150, 60, 40));

    cal.apply_batch(batch);

    for (const auto& p : batch) {
        EXPECT_TRUE(p.has_physical_coords);
        EXPECT_NE(p.physical_coords_mm.x, -1.0f);
    }
}

// ---------------------------------------------------------------------------
// Test: Load from file (identity matrix in JSON)
// ---------------------------------------------------------------------------

TEST_F(SpatialCalibratorTest, LoadFromFile)
{
    auto tmp = std::filesystem::temp_directory_path();
    auto path = tmp / "test_calibrator_config.json";

    {
        std::ofstream f(path);
        f << R"({
            "homography": [1, 0, 0, 0, 1, 0, 0, 0, 1]
        })";
    }

    SpatialCalibrator cal;
    EXPECT_TRUE(cal.load_from_file(path.string()));
    EXPECT_TRUE(cal.is_calibrated());

    std::filesystem::remove(path);
}

// ---------------------------------------------------------------------------
// Test: Load from file with malformed JSON → graceful failure
// ---------------------------------------------------------------------------

TEST_F(SpatialCalibratorTest, LoadFromFileMalformed)
{
    auto tmp = std::filesystem::temp_directory_path();
    auto path = tmp / "test_calibrator_bad.json";

    {
        std::ofstream f(path);
        f << R"(not json at all)";
    }

    SpatialCalibrator cal;
    EXPECT_FALSE(cal.load_from_file(path.string()));
    EXPECT_FALSE(cal.is_calibrated());

    std::filesystem::remove(path);
}

// ---------------------------------------------------------------------------
// Test: Non-existent file → graceful failure
// ---------------------------------------------------------------------------

TEST_F(SpatialCalibratorTest, LoadFromFileNotFound)
{
    SpatialCalibrator cal;
    EXPECT_FALSE(cal.load_from_file("/nonexistent/config.json"));
    EXPECT_FALSE(cal.is_calibrated());
}

// ===========================================================================
// Post-Processing Tests
// ===========================================================================

// ---------------------------------------------------------------------------
// Test: OcrFilter — MockOcrEngine returns canned text
// ---------------------------------------------------------------------------

TEST(PostProcessTest, MockOcrEngineReturnsCannedText)
{
    MockOcrEngine ocr("LOTE-2405");
    EXPECT_EQ(ocr.name(), "mock_ocr");
    EXPECT_TRUE(ocr.load_model("dummy.rknn"));

    cv::Mat dummy_roi = cv::Mat::zeros(24, 94, CV_8UC3);
    std::string text = ocr.infer(dummy_roi);
    EXPECT_EQ(text, "LOTE-2405");
}

// ---------------------------------------------------------------------------
// Test: MockOcrEngine configurable canned text
// ---------------------------------------------------------------------------

TEST(PostProcessTest, MockOcrEngineConfigurable)
{
    MockOcrEngine ocr("VENCE-2026");
    cv::Mat dummy_roi = cv::Mat::zeros(24, 94, CV_8UC3);
    EXPECT_EQ(ocr.infer(dummy_roi), "VENCE-2026");

    ocr.set_canned("2026-05-24");
    EXPECT_EQ(ocr.infer(dummy_roi), "2026-05-24");
}

// ---------------------------------------------------------------------------
// Test: ColorValidator — glare masking prevents false negatives
// ---------------------------------------------------------------------------

TEST(PostProcessTest, ColorValidatorGlareMasking)
{
    ColorValidator cv;
    cv.configure(2,  // class_id = COLOR_ZONE
                 0, 10,   // red low range
                 170, 180, // red high range
                 70, 50,  // sat_min, val_min
                 0.80f);   // pass_ratio

    // Create a mostly-red ROI (300x300 with a 100x100 white glare spot).
    cv::Mat roi(300, 300, CV_8UC3, cv::Scalar(0, 0, 200));  // BGR = dark red
    // Add a white glare patch (simulates specular reflection on plastic).
    roi(cv::Rect(100, 100, 100, 100)) = cv::Scalar(255, 255, 255);

    auto result = cv.validate(roi);

    // The glare should be masked so the red pixels dominate → PASS.
    EXPECT_TRUE(result.pass) << "Glare should be masked, ratio=" << result.ratio;
    EXPECT_FALSE(result.skipped);
}

// ---------------------------------------------------------------------------
// Test: ColorValidator — all-white ROI skipped (excessive glare)
// ---------------------------------------------------------------------------

TEST(PostProcessTest, ColorValidatorAllWhiteIsSkipped)
{
    ColorValidator cv;
    cv.configure(2, 0, 10, 170, 180, 70, 50, 0.80f);

    cv::Mat roi(100, 100, CV_8UC3, cv::Scalar(255, 255, 255));  // pure white

    auto result = cv.validate(roi);
    EXPECT_TRUE(result.skipped);
    EXPECT_EQ(result.skip_reason, "excessive_glare");
}

// ---------------------------------------------------------------------------
// Test: ColorValidator — unconfigured class passes by default (safety)
// ---------------------------------------------------------------------------

TEST(PostProcessTest, ColorValidatorUnconfiguredPasses)
{
    ColorValidator cv;  // no configure() call
    cv::Mat roi(100, 100, CV_8UC3, cv::Scalar(0, 0, 200));

    auto result = cv.validate(roi);
    EXPECT_TRUE(result.pass);   // safety: unconfigured → pass
    EXPECT_TRUE(result.skipped);
}

// ---------------------------------------------------------------------------
// Test: PostProcessDispatcher dispatches OCR + color by class_id
// ---------------------------------------------------------------------------

TEST(PostProcessTest, DispatcherRoutesByClassId)
{
    MockOcrEngine   ocr("LOTE-001");
    ColorValidator  cv;
    cv.configure(1, 0, 10, 170, 180, 70, 50, 0.80f);
    cv.configure(2, 0, 10, 170, 180, 70, 50, 0.80f);

    PostProcessDispatcher dispatcher(&ocr, &cv);

    // Build a batch with one primitive of each class.
    cv::Mat frame(480, 640, CV_8UC3, cv::Scalar(0, 0, 200));
    PrimitiveBatch batch;

    VisualPrimitive defect;
    defect.class_id = 0;
    defect.bbox = cv::Rect(10, 10, 50, 50);
    batch.push_back(std::move(defect));

    VisualPrimitive ocr_zone;
    ocr_zone.class_id = 1;
    ocr_zone.bbox = cv::Rect(100, 100, 94, 24);
    batch.push_back(std::move(ocr_zone));

    VisualPrimitive color_zone;
    color_zone.class_id = 2;
    color_zone.bbox = cv::Rect(200, 200, 100, 100);
    batch.push_back(std::move(color_zone));

    dispatcher.process(batch, frame);

    // class_id=0: no post-processing.
    EXPECT_TRUE(batch[0].ocr_text.empty());

    // class_id=1: OCR + colour.
    EXPECT_EQ(batch[1].ocr_text, "LOTE-001");

    // class_id=2: colour only.
    EXPECT_TRUE(batch[2].ocr_text.empty());
    EXPECT_TRUE(batch[2].color_pass);
}

}  // namespace vision
}  // namespace gema
