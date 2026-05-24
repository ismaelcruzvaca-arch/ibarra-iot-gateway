#include "thermal_monitor.hpp"

#include <unistd.h>

namespace gema {
namespace vision {

ThermalMonitor::ThermalMonitor(
    std::string zone_path,
    std::chrono::seconds interval,
    DegradeCallback callback,
    void* user_data) noexcept
    : zone_path_(std::move(zone_path))
    , interval_(interval)
    , callback_(callback)
    , user_data_(user_data)
    , prev_time_(std::chrono::steady_clock::now())
{}

ThermalMonitor::~ThermalMonitor()
{
    stop();
}

bool ThermalMonitor::start()
{
    if (running_.exchange(true)) {
        return true;
    }
    thread_ = std::make_unique<std::thread>(
        &ThermalMonitor::monitor_loop, this);
    return true;
}

void ThermalMonitor::stop()
{
    if (!running_.exchange(false)) {
        return;
    }
    if (thread_ && thread_->joinable()) {
        thread_->join();
    }
    thread_.reset();
}

ThermalMonitor::State ThermalMonitor::current_state() const
{
    std::lock_guard lock(state_mtx_);
    return state_;
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

int ThermalMonitor::read_temp()
{
    std::ifstream f(zone_path_);
    if (!f.is_open()) {
        return -1;
    }
    int millicelsius = -1;
    f >> millicelsius;
    if (millicelsius < 0) {
        return -1;
    }
    return millicelsius / 1000;  // convert to °C
}

ThermalMonitor::Level ThermalMonitor::evaluate_level(
    int temp, double /*rate*/) const
{
    using namespace thermal_thresholds;

    if (temp >= EMERGENCY_STOP)  return Level::EMERGENCY;
    if (temp >= ACTION_SKIP)     return Level::CRITICAL;
    if (temp >= ACTION_REDUCE_RES) return Level::HOTTER;
    if (temp >= WARN_THROTTLE)   return Level::HOT;
    if (temp >= MONITOR_LOG)     return Level::WARM;
    return Level::OK;
}

void ThermalMonitor::monitor_loop()
{
    while (running_.load()) {
        int temp = read_temp();

        if (temp >= 0) {
            auto now = std::chrono::steady_clock::now();
            double dt = std::chrono::duration<double>(now - prev_time_).count();
            double rate = (dt > 0.0)
                ? (static_cast<double>(temp - prev_temp_) / dt)
                : 0.0;

            Level level = evaluate_level(temp, rate);

            State new_state{temp, rate, level};

            // Detect level transitions for the callback.
            Level old_level;
            {
                std::lock_guard lock(state_mtx_);
                old_level = state_.level;
                state_ = new_state;
            }

            if (level != old_level && callback_) {
                callback_(new_state, user_data_);
            }

            prev_temp_ = temp;
            prev_time_ = now;
        }

        // Sleep in 100 ms chunks for responsive shutdown.
        for (int i = 0; i < 50; ++i) {
            if (!running_.load()) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

}  // namespace vision
}  // namespace gema
