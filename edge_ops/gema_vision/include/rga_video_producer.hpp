#pragma once

#ifdef __arm__

// Real hardware implementation for RV1106 RGA + MIPI CSI.
// Will be implemented when hardware is available.

#include "frame_pool.hpp"
#include "thread_safe_queue.hpp"
#include "video_producer.hpp"

#include <string>

namespace gema {
namespace vision {

/**
 * @brief Placeholder for the RGA-based video producer.
 *
 * On RV1106 hardware, this class will drive the RGA (Raster Graphics
 * Accelerator) to capture frames from a MIPI CSI camera and push them
 * to the inference pipeline.  For now it's a compile-time stub that
 * emits a clear error via static_assert.
 */
class RgaVideoProducer final : public IVideoProducer {
public:
    RgaVideoProducer(
        FramePool& /*pool*/,
        ThreadSafeQueue<std::shared_ptr<cv::Mat>>& /*queue*/,
        const std::string& /*camera_device*/ = "/dev/video0")
    {}

    ~RgaVideoProducer() override = default;

    // Non-copiable, non-movable.
    RgaVideoProducer(const RgaVideoProducer&) = delete;
    RgaVideoProducer& operator=(const RgaVideoProducer&) = delete;
    RgaVideoProducer(RgaVideoProducer&&) = delete;
    RgaVideoProducer& operator=(RgaVideoProducer&&) = delete;

    void start() override
    {
        static_assert(false, "RgaVideoProducer not implemented yet");
    }

    void stop() override {}
    bool is_running() const override { return false; }
    void set_frame_rate(double /*fps*/) override {}
};

}  // namespace vision
}  // namespace gema

#else
// Compile-time guard: RgaVideoProducer is only available on ARM targets.
// On x86/Docker builds, attempting to use it causes a compile error.
// This is intentional — the mock producer is used in SIL tests and dev.
#error "RgaVideoProducer requires ARM target (RV1106). Use MockVideoProducer for x86/Docker."
#endif
