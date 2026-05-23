#pragma once

namespace gema {
namespace vision {

/**
 * @brief Abstract interface for the hardware GPIO flash trigger.
 *
 * Designed for the **producer thread** (high-priority, real-time).
 *
 * The physical layer is an inductive sensor → optocoupler → GPIO
 * pin on the RV1106 SoC.  This interface decouples the orchestration
 * logic from the low-level `/dev/gpiochipN` or `libgpiod` calls.
 *
 * ## Timing contract
 *
 * At 90 strokes-per-minute (1.5 Hz) the strobe window is ~667 ms.
 * wait_for_flash() MUST wake within **microseconds** of the rising
 * edge to guarantee that the camera shutter fires while the product
 * is centred in the field of view.
 *
 * A jitter >1 ms will cause motion-blurred or empty frames.
 */
class FlashTrigger {
public:
    virtual ~FlashTrigger() = default;

    /**
     * @brief Block the calling thread until a hardware trigger is
     *        received from the GPIO line.
     *
     * This is a **blocking, not polling** primitive.  The implementation
     * SHOULD use an edge-triggered interrupt or epoll on the GPIO chip
     * file descriptor so the thread sleeps until the event arrives
     * (zero CPU waste).
     *
     * @note MUST be signal-safe: if the orchestrator shuts down while
     *       this method is blocked, the implementation MUST unblock
     *       promptly (e.g. via a self-pipe trick or a cancel fd).
     */
    virtual void wait_for_flash() = 0;
};

}  // namespace vision
}  // namespace gema
