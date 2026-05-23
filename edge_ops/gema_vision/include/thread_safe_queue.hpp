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
 * ## Thread model
 *
 *   - **Producer(s)** — call push().  Never blocks (unless the queue
 *     grows huge and the system runs out of memory).
 *   - **Consumer**    — call pop() (blocking) or try_pop() (non-blocking).
 *
 * The queue is NOT bounded by design: at 1.5 Hz × 5 seconds of
 * worst-case inference latency we need ≤8 frames of headroom.
 * An unbounded queue eliminates the risk of dropped frames while
 * the orchestrator catches up after a GC / OS scheduling jitter.
 *
 * @tparam T  Value type stored in the queue.
 */
template <typename T>
class ThreadSafeQueue {
public:
    ThreadSafeQueue() = default;

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
     * @param item  Rvalue-reference — moved into the internal container.
     *
     * Thread-safe, never blocks.  Wakes one waiting consumer.
     */
    void push(T&& item)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
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
     * This is the primary consumer primitive.  The calling thread
     * sleeps until push() signals the condition variable.
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
};

}  // namespace vision
}  // namespace gema
