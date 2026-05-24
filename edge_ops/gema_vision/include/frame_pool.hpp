#pragma once

#include <opencv2/opencv.hpp>

#include <atomic>
#include <cstddef>
#include <vector>

namespace gema {
namespace vision {

/**
 * @brief Pre-allocated pool of OpenCV Mat buffers to eliminate
 *        malloc/free fragmentation in the hot inference loop.
 *
 * ## Problem
 *
 * In the consumer loop, every frame creates a new `cv::Mat`
 * allocation (~900 KB for 640×480×3).  On an RV1106 with 128 MB RAM
 * and uClibc (dlmalloc), repeated alloc/free cycles fragment the heap
 * within days, causing OOM or crashes.
 *
 * ## Solution
 *
 * Pre-allocate N buffers at startup and reuse them in a ring-buffer
 * pattern.  The producer writes into the next free buffer, the
 * consumer reads from the next filled buffer — no heap allocations
 * in the hot path.
 *
 * ## Thread safety
 *
 * - `acquire()` — called by the producer thread.  Returns the next
 *   free buffer index.  Blocks if all buffers are in flight.
 * - `release()` — called by the consumer thread after processing.
 *   Returns the buffer to the pool.
 *
 * With 4 buffers and a 1.5 Hz frame rate, each buffer is recycled
 * every ~2.7 s — well within the ~15 s watchdog timeout.
 */
class FramePool {
public:
    static constexpr size_t kPoolSize = 4;

    FramePool(int width, int height, int type)
    {
        for (size_t i = 0; i < kPoolSize; ++i) {
            pool_.emplace_back(cv::Mat::zeros(height, width, type));
            available_[i].store(true, std::memory_order_relaxed);
        }
    }

    /**
     * @brief Acquire the next available buffer index.
     *
     * Spins until a buffer is free.  In practice the wait is
     * sub-microsecond because the consumer releases buffers faster
     * than the producer (1.5 Hz) acquires them.
     *
     * @return Index into the pool (0 .. kPoolSize-1).
     */
    size_t acquire() noexcept
    {
        while (true) {
            for (size_t i = 0; i < kPoolSize; ++i) {
                bool expected = true;
                if (available_[i].compare_exchange_weak(
                        expected, false,
                        std::memory_order_acquire,
                        std::memory_order_relaxed)) {
                    return i;
                }
            }
            // All buffers in flight — spin.  On RV1106 this should
            // never happen with 4 buffers at 1.5 Hz.
            std::this_thread::yield();
        }
    }

    /**
     * @brief Return a buffer to the pool after consumption.
     */
    void release(size_t index) noexcept
    {
        available_[index].store(true, std::memory_order_release);
    }

    /** @brief Direct reference to the pre-allocated buffer. */
    cv::Mat& operator[](size_t index) noexcept { return pool_[index]; }
    const cv::Mat& operator[](size_t index) const noexcept { return pool_[index]; }

private:
    std::vector<cv::Mat> pool_{kPoolSize};
    std::atomic<bool>    available_[kPoolSize];
};

}  // namespace vision
}  // namespace gema
