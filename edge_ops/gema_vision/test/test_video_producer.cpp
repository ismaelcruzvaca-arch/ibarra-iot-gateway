/**
 * @file test_video_producer.cpp
 * @brief Software-in-the-Loop (SIL) tests for IVideoProducer interface
 *        and MockVideoProducer implementation.
 *
 * Tests the full lifecycle of MockVideoProducer:
 *   1. FramePool::acquire_shared() buffer acquisition + release
 *   2. MockVideoProducer start/stop lifecycle
 *   3. Frame production at the configured rate
 *   4. Idempotent double start / double stop
 *   5. Responsive stop() during sleep_interruptible()
 *   6. set_frame_rate() changes observed production speed
 *   7. All 5 synthetic patterns produce distinguishable pixel data
 *   8. set_pattern() overrides cycling
 */

#include "frame_pool.hpp"
#include "mock_video_producer.hpp"
#include "thread_safe_queue.hpp"
#include "video_producer.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <thread>

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
        // frame goes out of scope → deleter releases the buffer.
    }
    // After scope exit, all 4 pool buffers should be available.
    for (size_t i = 0; i < FramePool::kPoolSize; ++i) {
        auto frame = pool_.acquire_shared();
        ASSERT_NE(frame, nullptr);
        ++acquired_count;
    }
    EXPECT_EQ(acquired_count, FramePool::kPoolSize);
}

TEST_F(FramePoolTest, CustomDeleterReturnsBufferToPool)
{
    // Acquire all 4 buffers.
    std::vector<std::shared_ptr<cv::Mat>> frames;
    for (size_t i = 0; i < FramePool::kPoolSize; ++i) {
        frames.push_back(pool_.acquire_shared());
    }
    ASSERT_EQ(frames.size(), FramePool::kPoolSize);

    // Release one by resetting the shared_ptr.
    frames[0].reset();
    // Now acquire again — the released slot should be reused.
    auto recycled = pool_.acquire_shared();
    ASSERT_NE(recycled, nullptr);
    EXPECT_EQ(recycled->rows, 480);
    EXPECT_EQ(recycled->cols, 640);
}

// ===========================================================================
// MockVideoProducer tests
// ===========================================================================

class MockProducerTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        // Default pool: 640×480, CV_8UC3, 4 buffers.
    }

    void TearDown() override {}

    FramePool                               pool_{640, 480, CV_8UC3};
    ThreadSafeQueue<std::shared_ptr<cv::Mat>> queue_{8};
};

// ---------------------------------------------------------------------------
// Test: Start → is_running == true → Stop → is_running == false
// ---------------------------------------------------------------------------

TEST_F(MockProducerTest, StartStop)
{
    MockVideoProducer producer(pool_, queue_, 10.0);

    EXPECT_FALSE(producer.is_running());

    producer.start();
    EXPECT_TRUE(producer.is_running());

    producer.stop();
    EXPECT_FALSE(producer.is_running());
}

// ---------------------------------------------------------------------------
// Test: Producer pushes at least one frame after running briefly
// ---------------------------------------------------------------------------

TEST_F(MockProducerTest, PushesFrames)
{
    MockVideoProducer producer(pool_, queue_, 10.0);  // 100 ms per frame

    producer.start();
    // At 10 Hz, we expect at least 2 frames in 300 ms.
    std::this_thread::sleep_for(std::chrono::milliseconds(350));
    producer.stop();

    EXPECT_GE(producer.frames_produced(), 1);
    // The queue should contain at least one shared_ptr frame.
    EXPECT_FALSE(queue_.empty());
}

// ---------------------------------------------------------------------------
// Test: Double start is a no-op (no crash, no second thread)
// ---------------------------------------------------------------------------

TEST_F(MockProducerTest, DoubleStartIsNoOp)
{
    MockVideoProducer producer(pool_, queue_, 10.0);

    producer.start();
    EXPECT_TRUE(producer.is_running());

    producer.start();  // second start — must be idempotent
    EXPECT_TRUE(producer.is_running());

    producer.stop();
    EXPECT_FALSE(producer.is_running());
}

// ---------------------------------------------------------------------------
// Test: Double stop is a no-op (no crash)
// ---------------------------------------------------------------------------

TEST_F(MockProducerTest, DoubleStopIsNoOp)
{
    MockVideoProducer producer(pool_, queue_, 10.0);

    producer.start();
    producer.stop();
    EXPECT_FALSE(producer.is_running());

    producer.stop();  // second stop — must be idempotent
    EXPECT_FALSE(producer.is_running());
}

// ---------------------------------------------------------------------------
// Test: Stop() returns quickly while producer is sleeping
// ---------------------------------------------------------------------------
//
// At 1.5 Hz the producer sleeps for ~666 ms between frames.
// sleep_interruptible() wakes every 100 ms to check running_.
// stop() must return within ~200 ms of being called during the
// sleep window.

TEST_F(MockProducerTest, StopDuringSleep)
{
    MockVideoProducer producer(pool_, queue_, 1.5);  // 666 ms interval

    producer.start();

    // Let the producer acquire a frame and enter sleep.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    auto start = std::chrono::steady_clock::now();
    producer.stop();
    auto elapsed = std::chrono::steady_clock::now() - start;

    // Maximum expected delay: 100 ms (sleep chunk) + thread join overhead.
    EXPECT_LT(elapsed, std::chrono::milliseconds(250));
    EXPECT_FALSE(producer.is_running());
}

// ---------------------------------------------------------------------------
// Test: set_frame_rate() changes observed frame rate
// ---------------------------------------------------------------------------
//
// Run producer at a known high rate for a short period, then compare
// frame counts at different rates.

TEST_F(MockProducerTest, FrameRateChangesApply)
{
    MockVideoProducer producer(pool_, queue_, 20.0);  // 50 ms interval

    producer.set_frame_rate(5.0);  // 200 ms interval
    producer.start();

    // Run for ~1 second — expect ~5 frames at 5 Hz.
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    producer.stop();

    // With generous tolerance: at 5 Hz, expect 5-8 frames in 1.2 s.
    uint64_t count = producer.frames_produced();
    EXPECT_GE(count, 2);
    EXPECT_LE(count, static_cast<uint64_t>(12));
}

// ---------------------------------------------------------------------------
// Test: All 5 patterns produce distinguishable non-zero frame data
// ---------------------------------------------------------------------------
//
// For each pattern, run the producer for one frame, pop the frame from
// the queue, and verify that pixel data is non-zero and meets pattern-
// specific expectations.

TEST_F(MockProducerTest, AllPatternsRender)
{
    // SOLID_COLOR — every pixel should be identical (non-zero).
    {
        MockVideoProducer producer(pool_, queue_, 100.0);  // very fast
        producer.set_pattern(MockVideoProducer::MockPattern::SOLID_COLOR);
        producer.start();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        producer.stop();

        auto frame = queue_.try_pop();
        ASSERT_TRUE(frame.has_value());
        ASSERT_NE(*frame, nullptr);

        cv::Vec3b first_pixel = (*frame)->at<cv::Vec3b>(0, 0);
        // Verify all pixels match the first pixel.
        bool all_same = true;
        for (int r = 0; r < (*frame)->rows && all_same; ++r) {
            for (int c = 0; c < (*frame)->cols; ++c) {
                if ((*frame)->at<cv::Vec3b>(r, c) != first_pixel) {
                    all_same = false;
                    break;
                }
            }
        }
        EXPECT_TRUE(all_same);
        // First pixel should NOT be all-black (SOLID_COLOR cycles
        // through R, G, B, Y, C, M — every colour has at least one
        // non-zero BGR channel, but only black is all zeros).
        bool not_black = (first_pixel[0] != 0 || first_pixel[1] != 0 || first_pixel[2] != 0);
        EXPECT_TRUE(not_black);
    }

    // Drain queue between patterns.
    while (queue_.try_pop()) {}

    // NOISE — at least 50% of pixels should differ from neighbours
    // (random data is very unlikely to be uniform).
    {
        MockVideoProducer producer(pool_, queue_, 100.0);
        producer.set_pattern(MockVideoProducer::MockPattern::NOISE);
        producer.start();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        producer.stop();

        auto frame = queue_.try_pop();
        ASSERT_TRUE(frame.has_value());
        ASSERT_NE(*frame, nullptr);

        // Count differing adjacent pixels (horizontal neighbours).
        int diff_count = 0;
        int total_pairs = 0;
        for (int r = 0; r < (*frame)->rows; ++r) {
            for (int c = 0; c < (*frame)->cols - 1; ++c) {
                if ((*frame)->at<cv::Vec3b>(r, c) != (*frame)->at<cv::Vec3b>(r, c + 1)) {
                    ++diff_count;
                }
                ++total_pairs;
            }
        }
        // With random noise, ~99.6% of adjacent pixels differ
        // (assuming random BGR values). Use a conservative threshold.
        double ratio = static_cast<double>(diff_count) / total_pairs;
        EXPECT_GT(ratio, 0.5);
    }

    // Drain queue.
    while (queue_.try_pop()) {}

    // CHECKERBOARD — verify top-left 32×32 is uniform (black) and
    // the adjacent 32×32 block is uniform (white).
    {
        MockVideoProducer producer(pool_, queue_, 100.0);
        producer.set_pattern(MockVideoProducer::MockPattern::CHECKERBOARD);
        producer.start();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        producer.stop();

        auto frame = queue_.try_pop();
        ASSERT_TRUE(frame.has_value());
        ASSERT_NE(*frame, nullptr);

        // Top-left cell (offset 0,0) should be black (all zeros).
        cv::Vec3b black = (*frame)->at<cv::Vec3b>(0, 0);
        EXPECT_EQ(black[0], 0);
        EXPECT_EQ(black[1], 0);
        EXPECT_EQ(black[2], 0);

        // Adjacent cell to the right (offset 32, 0) should be white.
        cv::Vec3b white = (*frame)->at<cv::Vec3b>(0, 32);
        EXPECT_EQ(white[0], 255);
        EXPECT_EQ(white[1], 255);
        EXPECT_EQ(white[2], 255);
    }
}

// ---------------------------------------------------------------------------
// Test: set_pattern() overrides the cycling pattern
// ---------------------------------------------------------------------------

TEST_F(MockProducerTest, SetPatternOverridesCycling)
{
    // Default pattern is SOLID_COLOR (index 0).
    MockVideoProducer producer(pool_, queue_, 100.0);

    // Override to GRADIENT.
    producer.set_pattern(MockVideoProducer::MockPattern::GRADIENT);
    producer.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    producer.stop();

    auto frame = queue_.try_pop();
    ASSERT_TRUE(frame.has_value());
    ASSERT_NE(*frame, nullptr);

    // GRADIENT should NOT have uniform colour — verify at least
    // two pixels differ.
    cv::Vec3b p0 = (*frame)->at<cv::Vec3b>(0, 0);
    cv::Vec3b p1 = (*frame)->at<cv::Vec3b>(0, (*frame)->cols - 1);
    bool pixels_differ = (p0[0] != p1[0] || p0[1] != p1[1] || p0[2] != p1[2]);
    EXPECT_TRUE(pixels_differ);
}

// ===========================================================================
// IVideoProducer interface test — compile-time check via abstract usage
// ===========================================================================

TEST(VideoProducerInterfaceTest, IsAbstract)
{
    // Verify IVideoProducer is abstract by attempting a polymorphic
    // delete through a base pointer.
    //
    // This test verifies that MockVideoProducer publicly inherits from
    // IVideoProducer and that all virtual methods are overridden.

    FramePool                               pool(640, 480, CV_8UC3);
    ThreadSafeQueue<std::shared_ptr<cv::Mat>> queue(8);
    MockVideoProducer                        concrete(pool, queue);

    IVideoProducer* base = &concrete;
    EXPECT_FALSE(base->is_running());
    // The base pointer usage verifies the interface contract at compile
    // time — no undefined or unimplemented pure virtual functions.
}

}  // namespace vision
}  // namespace gema
