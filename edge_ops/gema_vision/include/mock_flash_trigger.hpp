#pragma once

#include "flash_trigger.hpp"

#include <chrono>
#include <thread>

namespace gema {
namespace vision {

/**
 * @brief Software-in-the-Loop (SIL) mock flash trigger.
 *
 * Simulates the GPIO inductive sensor edge at the production line
 * frequency.  Used by GTest to validate the producer-consumer
 * handshake WITHOUT physical hardware.
 *
 * ## Timing model
 *
 *   - wait_for_flash() blocks for `interval_ms` (default 666 ms),
 *     which corresponds to 90 strokes/minute (1 / 1.5 Hz).
 *
 * This allows SIL integration tests to validate that the orchestrator
 * correctly paces its inference loop to match the real machine cadence.
 */
class MockFlashTrigger final : public FlashTrigger {
public:
    /**
     * @param interval_ms  Simulated strobe interval in milliseconds.
     *                     Default 666 ms ≈ 90 strokes / minute.
     */
    explicit MockFlashTrigger(int interval_ms = 666)
        : interval_ms_(interval_ms)
    {}

    void wait_for_flash() override
    {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(interval_ms_));
    }

private:
    int interval_ms_;
};

}  // namespace vision
}  // namespace gema
