#pragma once

#include "frame_pool.hpp"
#include "thread_safe_queue.hpp"
#include "video_producer.hpp"

#include <opencv2/opencv.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>

namespace gema {
namespace vision {

/**
 * @brief Mock video producer that generates synthetic frames.
 *
 * Acquires buffers from a FramePool, paints one of 5 synthetic
 * patterns, and pushes the shared_ptr to a ThreadSafeQueue.
 * The producer runs its own thread with a configurable frame rate
 * and responsive sleep_interruptible() for fast shutdown.
 *
 * ## Patterns (cycling)
 *
 * | Index | Pattern       | Visual description                   |
 * |-------|---------------|--------------------------------------|
 * | 0     | SOLID_COLOR   | Cycling R, G, B, Y, C, M fill       |
 * | 1     | GRADIENT      | Horizontal colour sweep, shifts      |
 * | 2     | NOISE         | Uniform random pixel values          |
 * | 3     | CHECKERBOARD  | 32×32 alternating black/white cells  |
 * | 4     | TEST_CARD     | Simplified colour bars (7 vertical)  |
 *
 * ## Thread safety
 *
 * - start()/stop() are thread-safe (atomic running_ flag).
 * - set_frame_rate() uses a mutex to protect interval_ updates.
 * - The producer loop reads interval_ under the mutex each iteration.
 */
class MockVideoProducer final : public IVideoProducer {
public:
    /**
     * @brief Construct the mock producer.
     *
     * @param pool   Frame pool to acquire buffers from.
     * @param queue  Queue to push painted frames into.
     * @param fps    Target frame rate (default 1.5 Hz).
     */
    MockVideoProducer(
        FramePool& pool,
        ThreadSafeQueue<std::shared_ptr<cv::Mat>>& queue,
        double fps = 1.5) noexcept
        : pool_(pool)
        , queue_(queue)
        , frame_rate_(fps)
    {
        update_interval();
    }

    ~MockVideoProducer()
    {
        stop();
    }

    // Non-copiable, non-movable (protects thread handle).
    MockVideoProducer(const MockVideoProducer&) = delete;
    MockVideoProducer& operator=(const MockVideoProducer&) = delete;
    MockVideoProducer(MockVideoProducer&&) = delete;
    MockVideoProducer& operator=(MockVideoProducer&&) = delete;

    // ------------------------------------------------------------------
    // IVideoProducer interface
    // ------------------------------------------------------------------

    void start() override
    {
        if (running_.exchange(true)) {
            return;  // already running — idempotent
        }
        frames_produced_.store(0);
        thread_ = std::make_unique<std::thread>(
            &MockVideoProducer::producer_loop, this);
    }

    void stop() override
    {
        if (!running_.exchange(false)) {
            return;  // already stopped — idempotent
        }
        if (thread_ && thread_->joinable()) {
            thread_->join();
        }
        thread_.reset();
    }

    bool is_running() const noexcept override
    {
        return running_.load();
    }

    void set_frame_rate(double fps) override
    {
        std::lock_guard<std::mutex> lock(interval_mutex_);
        frame_rate_ = fps;
        update_interval();
    }

    // ------------------------------------------------------------------
    // Test helpers
    // ------------------------------------------------------------------

    /**
     * @brief Total frames produced since the last start().
     */
    uint64_t frames_produced() const noexcept
    {
        return frames_produced_.load();
    }

    /**
     * @brief Synthetic pattern identifiers.
     */
    enum class MockPattern {
        SOLID_COLOR  = 0,
        GRADIENT      = 1,
        NOISE         = 2,
        CHECKERBOARD  = 3,
        TEST_CARD     = 4,
    };

    /**
     * @brief Override the cycling pattern for deterministic testing.
     *
     * The next frame produced will use the given pattern.  After one
     * frame the cycling resumes (index advances).
     */
    void set_pattern(MockPattern pattern) noexcept
    {
        pattern_index_ = static_cast<int>(pattern);
    }

private:
    // ------------------------------------------------------------------
    // Producer loop
    // ------------------------------------------------------------------

    /**
     * @brief Main producer loop — runs in its own thread.
     *
     * 1. Acquire a buffer from the pool via acquire_shared().
     * 2. Paint the current pattern into the buffer.
     * 3. Push the shared_ptr to the queue (hands ownership to consumer).
     * 4. Increment frame counter.
     * 5. Cycle pattern index (0..4).
     * 6. Sleep for the configured interval (interruptible in 100 ms chunks).
     */
    void producer_loop()
    {
        while (running_.load()) {
            auto frame = pool_.acquire_shared();
            paint_pattern(*frame);
            queue_.push(std::move(frame));

            frames_produced_.fetch_add(1);

            // Cycle through patterns 0..4.
            pattern_index_ = (pattern_index_ + 1) % 5;

            sleep_interruptible(current_interval(), running_);
        }
    }

    // ------------------------------------------------------------------
    // Pattern painting
    // ------------------------------------------------------------------

    /**
     * @brief Paint one of the 5 synthetic patterns into the frame.
     *
     * Dispatches based on the current pattern_index_ (0..4).
     */
    void paint_pattern(cv::Mat& frame)
    {
        switch (static_cast<MockPattern>(pattern_index_ % 5)) {
            case MockPattern::SOLID_COLOR:  paint_solid_color(frame);  break;
            case MockPattern::GRADIENT:     paint_gradient(frame);     break;
            case MockPattern::NOISE:        paint_noise(frame);        break;
            case MockPattern::CHECKERBOARD: paint_checkerboard(frame); break;
            case MockPattern::TEST_CARD:    paint_test_card(frame);    break;
        }
    }

    /** @brief Fill frame with a cycling solid colour. */
    static void paint_solid_color(cv::Mat& frame)
    {
        static int color_index = 0;
        const cv::Scalar colors[] = {
            cv::Scalar(0, 0, 255),     // R
            cv::Scalar(0, 255, 0),     // G
            cv::Scalar(255, 0, 0),     // B
            cv::Scalar(0, 255, 255),   // Y
            cv::Scalar(255, 255, 0),   // C
            cv::Scalar(255, 0, 255),   // M
        };
        cv::Scalar color = colors[color_index % 6];
        ++color_index;
        frame.setTo(color);
    }

    /** @brief Horizontal colour sweep that shifts each invocation. */
    static void paint_gradient(cv::Mat& frame)
    {
        static int shift = 0;
        for (int r = 0; r < frame.rows; ++r) {
            for (int c = 0; c < frame.cols; ++c) {
                uchar val = static_cast<uchar>((c + shift) % 256);
                frame.at<cv::Vec3b>(r, c) = cv::Vec3b(
                    val,                     // B
                    static_cast<uchar>(255 - val),  // G
                    static_cast<uchar>((val + 128) % 256)  // R
                );
            }
        }
        shift = (shift + 5) % 256;
    }

    /** @brief Uniform random noise. */
    static void paint_noise(cv::Mat& frame)
    {
        cv::randu(frame, cv::Scalar(0, 0, 0), cv::Scalar(255, 255, 255));
    }

    /** @brief 32×32 alternating black/white checkerboard. */
    static void paint_checkerboard(cv::Mat& frame)
    {
        constexpr int kCellSize = 32;
        for (int r = 0; r < frame.rows; ++r) {
            for (int c = 0; c < frame.cols; ++c) {
                bool is_white = ((r / kCellSize) + (c / kCellSize)) % 2 == 0;
                uchar val = is_white ? 255 : 0;
                frame.at<cv::Vec3b>(r, c) = cv::Vec3b(val, val, val);
            }
        }
    }

    /** @brief Simplified SMPTE-like colour bars (7 vertical bars). */
    static void paint_test_card(cv::Mat& frame)
    {
        constexpr int kNumBars = 7;
        int bar_width = frame.cols / kNumBars;
        const cv::Scalar bars[kNumBars] = {
            cv::Scalar(180, 180, 180),  // Light grey (white with margin)
            cv::Scalar(0,   255, 255),  // Yellow
            cv::Scalar(255, 255, 0),    // Cyan
            cv::Scalar(0,   255, 0),    // Green
            cv::Scalar(255, 0,   255),  // Magenta
            cv::Scalar(0,   0,   180),  // Red (darker)
            cv::Scalar(180, 0,   0),    // Blue (darker)
        };
        for (int r = 0; r < frame.rows; ++r) {
            for (int c = 0; c < frame.cols; ++c) {
                int bar = std::min(c / bar_width, kNumBars - 1);
                frame.at<cv::Vec3b>(r, c) = bars[bar];
            }
        }
    }

    // ------------------------------------------------------------------
    // Sleep helpers
    // ------------------------------------------------------------------

    /**
     * @brief Get the current sleep interval under mutex protection.
     */
    std::chrono::microseconds current_interval()
    {
        std::lock_guard<std::mutex> lock(interval_mutex_);
        return interval_;
    }

    /**
     * @brief Set the sleep interval from the current frame_rate_.
     *
     * Must be called with interval_mutex_ held.
     */
    void update_interval()
    {
        if (frame_rate_ <= 0.0) {
            // Unlimited — minimum sleep (essentially spin).
            interval_ = std::chrono::microseconds::zero();
        } else {
            auto us = static_cast<int64_t>(1'000'000.0 / frame_rate_);
            interval_ = std::chrono::microseconds(std::max<int64_t>(us, 0));
        }
    }

    /**
     * @brief Sleep for the given duration, but wake every 100 ms to
     *        check whether the running flag is still set.
     *
     * This allows stop() to return within ~100 ms of being called,
     * regardless of the frame period (e.g. 666 ms at 1.5 Hz).
     *
     * @param total   Total sleep time.
     * @param running Atomic flag to check between chunks.
     */
    static void sleep_interruptible(
        std::chrono::microseconds total,
        const std::atomic<bool>& running)
    {
        constexpr auto kChunk = std::chrono::milliseconds(100);
        auto remaining = total;
        while (remaining > std::chrono::microseconds::zero() && running.load()) {
            auto sleep_for = std::min(remaining, kChunk);
            std::this_thread::sleep_for(sleep_for);
            remaining -= sleep_for;
        }
    }

    // ------------------------------------------------------------------
    // Members
    // ------------------------------------------------------------------

    FramePool&                                        pool_;
    ThreadSafeQueue<std::shared_ptr<cv::Mat>>&        queue_;
    std::unique_ptr<std::thread>                      thread_;
    std::atomic<bool>                                 running_{false};
    std::atomic<uint64_t>                             frames_produced_{0};
    int                                               pattern_index_{0};
    double                                            frame_rate_;
    std::chrono::microseconds                         interval_;
    std::mutex                                        interval_mutex_;
};

}  // namespace vision
}  // namespace gema
