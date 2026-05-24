#pragma once

#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>

namespace gema {
namespace vision {

/**
 * @brief Thread-safe, bounded/unbounded queue for the
 *        Producer-Consumer pattern.
 *
 * Type parameter T should be cheap to move (e.g. cv::Mat, shared_ptr).
 *
 * ## Bounded mode
 *
 * If `max_size > 0`, the queue drops the oldest item when a new item
 * is pushed and the queue is full (drop-oldest policy).  This prevents
 * unbounded memory growth on the RV1106 (128–256 MB RAM) when the
 * consumer is temporarily slower than the producer.
 *
 * ## Shutdown
 *
 * Call `shutdown()` to signal that no more items will be pushed.
 * The consumer will see `pop()` return a default-constructed T after
 * the queue is drained.  Use `is_shutdown()` to distinguish a
 * drained queue from a genuine empty frame.
 *
 * @tparam T  Value type stored in the queue.
 */
template <typename T>
class ThreadSafeQueue {
public:
    /**
     * @param max_size  Maximum number of items.  0 = unbounded.
     *                  Default: 8 frames at 1.5 Hz × ~5 s latency.
     */
    explicit ThreadSafeQueue(size_t max_size = 8) noexcept
        : max_size_(max_size)
    {}

    // Non-copiable, non-movable (protects the mutex).
    ThreadSafeQueue(const ThreadSafeQueue&) = delete;
    ThreadSafeQueue& operator=(const ThreadSafeQueue&) = delete;
    ThreadSafeQueue(ThreadSafeQueue&&) = delete;
    ThreadSafeQueue& operator=(ThreadSafeQueue&&) = delete;

    // ------------------------------------------------------------------
    // Producer API
    // ------------------------------------------------------------------

    /**
     * @brief Push an item into the queue.
     *
     * If the queue is bounded and full, the oldest item is dropped
     * (drop-oldest policy).
     *
     * @param item  Rvalue-reference — moved into the internal container.
     */
    void push(T&& item)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (max_size_ > 0 && queue_.size() >= max_size_) {
                // Drop oldest to make room.
                queue_.pop();
            }
            queue_.push(std::move(item));
        }
        cv_.notify_one();
    }

    /**
     * @brief Push a const-ref item into the queue (copies).
     */
    void push(const T& item)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (max_size_ > 0 && queue_.size() >= max_size_) {
                queue_.pop();
            }
            queue_.push(item);
        }
        cv_.notify_one();
    }

    // ------------------------------------------------------------------
    // Consumer API
    // ------------------------------------------------------------------

    /**
     * @brief Block until an item is available, then return it.
     *
     * @return T  The front item (move-constructed).
     *
     * If the queue has been shut down AND drained, returns a
     * default-constructed T.  Check `is_shutdown()` to distinguish
     * from a genuine empty/null item.
     */
    T pop()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this]() { return !queue_.empty() || done_; });

        if (queue_.empty()) {
            // done_ flag set and queue drained — return default.
            return T{};
        }

        T item = std::move(queue_.front());
        queue_.pop();
        return item;
    }

    /**
     * @brief Attempt a non-blocking dequeue.
     *
     * @return std::optional<T>  The item if available, std::nullopt
     *                           if the queue is empty.
     */
    std::optional<T> try_pop()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) {
            return std::nullopt;
        }
        T item = std::move(queue_.front());
        queue_.pop();
        return item;
    }

    // ------------------------------------------------------------------
    // Lifetime
    // ------------------------------------------------------------------

    /**
     * @brief Signal the queue that no more items will be pushed.
     *
     * Wakes all waiting consumers so they can drain and exit.
     */
    void shutdown()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            done_ = true;
        }
        cv_.notify_all();
    }

    /** @brief True after shutdown() has been called. */
    bool is_shutdown() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return done_;
    }

    /**
     * @brief Check if the queue is empty.
     */
    bool empty() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    /**
     * @brief Current number of items in the queue.
     */
    size_t size() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<T> queue_;
    bool done_ = false;
    size_t max_size_;
};

}  // namespace vision
}  // namespace gema
