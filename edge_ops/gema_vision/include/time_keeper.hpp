#pragma once

#include <chrono>
#include <atomic>
#include <cstdint>
#include <ctime>

namespace gema {
namespace vision {

/**
 * @brief Centralised time management for the vision pipeline.
 *
 * Solves the "1970 problem" — on RV1106 without an RTC battery, the
 * system clock boots at 1970-01-01 and may take ~30-60 s to sync via
 * NTP over HaLow.
 *
 * ## Design
 *
 *   - **Monotonic clock** is used for all internal measurements
 *     (frame intervals, watchdog timing, queue depths).
 *   - **Realtime clock** is ONLY published externally (MQTT, file
 *     timestamps) AFTER NTP has confirmed synchronisation.
 *   - A sanity check (year >= 2025) prevents garbage timestamps
 *     from leaking into production data.
 *
 * ## Usage
 *
 * ```cpp
 * auto& tk = TimeKeeper::instance();
 *
 * // Internal timing — always safe, always correct
 * int64_t now_ms = tk.monotonic_ms();
 *
 * // External timestamp — value 0 + flag if clock is not synced
 * int64_t wall = tk.realtime_ms();
 * bool   sync  = tk.is_synced();
 * ```
 */
class TimeKeeper {
public:
    static TimeKeeper& instance()
    {
        static TimeKeeper inst;
        return inst;
    }

    // ------------------------------------------------------------------
    // Sync status
    // ------------------------------------------------------------------

    /**
     * @brief Mark the clock as synchronised by NTP.
     *
     * Call this after chrony has confirmed its first `makestep` or
     * after the sanity check `clock_gettime(CLOCK_REALTIME) >= 2025`
     * passes.
     */
    void mark_synced()
    {
        synced_.store(true, std::memory_order_release);
        last_sync_monotonic_ms_ = monotonic_ms();
    }

    /** @brief True after NTP has confirmed first sync. */
    bool is_synced() const
    {
        return synced_.load(std::memory_order_acquire);
    }

    /** @brief Seconds since the last NTP sync.  -1 if never synced. */
    int64_t unsynced_seconds() const
    {
        auto last = last_sync_monotonic_ms_.load(std::memory_order_relaxed);
        if (last == 0) return -1;
        return (monotonic_ms() - last) / 1000;
    }

    // ------------------------------------------------------------------
    // Clock sources
    // ------------------------------------------------------------------

    /**
     * @brief Monotonic time since an arbitrary epoch (steady, never
     *        jumps).  Use for all internal measurements.
     */
    static int64_t monotonic_ms()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count();
    }

    /**
     * @brief Wall-clock time in ms since Unix epoch.
     *
     * @return  The realtime in ms, or **0** if the clock has not been
     *          synchronised.  Consumers MUST check is_synced() before
     *          using the returned value.
     */
    int64_t realtime_ms() const
    {
        if (!synced_.load(std::memory_order_acquire)) {
            return 0;
        }
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
    }

    // ------------------------------------------------------------------
    // Sanity check
    // ------------------------------------------------------------------

    /**
     * @brief Quick check: is the system clock reasonably set?
     *
     * Returns true if CLOCK_REALTIME is >= 2025-01-01.
     * This is a cheaper alternative to waiting for NTP when the clock
     * was restored from fake-hwclock and is likely already correct.
     */
    static bool clock_is_sane()
    {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        return ts.tv_sec >= 1735689600LL;  // 2025-01-01 00:00:00 UTC
    }

private:
    TimeKeeper() = default;

    std::atomic<bool>   synced_{false};
    std::atomic<int64_t> last_sync_monotonic_ms_{0};
};

}  // namespace vision
}  // namespace gema
