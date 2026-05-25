#pragma once

#include <string>

namespace gema {
namespace vision {

/**
 * @brief Abstract interface for frame producers.
 *
 * Each implementation acquires frames from a FramePool and pushes
 * them to a ThreadSafeQueue. The producer runs its own internal
 * thread with start()/stop() lifecycle.
 */
class IVideoProducer {
public:
    virtual ~IVideoProducer() = default;

    /// Start the producer thread.
    virtual void start() = 0;

    /// Gracefully stop the producer thread (joins).
    virtual void stop() = 0;

    /// True if the producer thread is running.
    virtual bool is_running() const = 0;

    /**
     * @brief Set the target frame rate.
     * @param fps  Frames per second (0 = unlimited / max speed).
     *
     * Only affects mock/simulated producers. Hardware-triggered
     * producers (GPIO / V4L2) ignore this.
     */
    virtual void set_frame_rate(double fps) = 0;
};

}  // namespace vision
}  // namespace gema
