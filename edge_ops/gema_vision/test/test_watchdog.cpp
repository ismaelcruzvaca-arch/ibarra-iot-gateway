/**
 * @file test_watchdog.cpp
 * @brief SIL tests for the hardware Watchdog class.
 *
 * Tests are run against a regular file (not /dev/watchdog) so they
 * work in CI/Docker without special hardware.
 */

#include "watchdog.hpp"

#include <gtest/gtest.h>

#include <fcntl.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

namespace gema {
namespace vision {

// ===========================================================================
// Test fixture
// ===========================================================================

class WatchdogTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        // Create a unique temp file to act as the "watchdog device".
        // We create it as a regular file first since open() with O_WRONLY
        // (which is what /dev/watchdog requires) will fail on a non-existent
        // file.  In production /dev/watchdog is a device node created by
        // the kernel driver.
        auto tmp = std::filesystem::temp_directory_path();
        auto uid = std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
        test_path_ = tmp / ("gema_wdt_test_" + uid);
        std::ofstream touch(test_path_);
        touch << "";
    }

    void TearDown() override
    {
        // Remove the temp file if it still exists.
        std::filesystem::remove(test_path_);
    }

    std::filesystem::path test_path_;
};

// ===========================================================================
// Test: Watchdog starts, pings, and stops cleanly
// ===========================================================================

TEST_F(WatchdogTest, StartPingStop)
{
    // Use a 100 ms interval for fast testing.
    Watchdog wd(test_path_.string(), std::chrono::milliseconds(100));

    ASSERT_TRUE(wd.start()) << "start() should succeed even for a regular file";
    ASSERT_TRUE(wd.is_running());

    // Let it run for ~500 ms → expect 4-6 pings.
    std::this_thread::sleep_for(std::chrono::milliseconds(550));

    wd.stop();
    EXPECT_FALSE(wd.is_running());

    // Must have pinged at least 3 times.
    EXPECT_GE(wd.ping_count(), 3);

    // Verify the file exists and has data in it (the ping bytes).
    ASSERT_TRUE(std::filesystem::exists(test_path_));
    EXPECT_GT(std::filesystem::file_size(test_path_), 0);
}

// ===========================================================================
// Test: Stop without start is a no-op (no crash)
// ===========================================================================

TEST_F(WatchdogTest, StopWithoutStartIsNoOp)
{
    Watchdog wd(test_path_.string());
    // No start() called.
    EXPECT_NO_THROW(wd.stop());
    EXPECT_FALSE(wd.is_running());
    EXPECT_EQ(wd.ping_count(), 0);
}

// ===========================================================================
// Test: Double start is idempotent
// ===========================================================================

TEST_F(WatchdogTest, DoubleStartIsIdempotent)
{
    Watchdog wd(test_path_.string(), std::chrono::milliseconds(200));
    ASSERT_TRUE(wd.start());
    ASSERT_TRUE(wd.start());  // second start should be a no-op

    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    wd.stop();

    EXPECT_GE(wd.ping_count(), 1);
}

// ===========================================================================
// Test: Ping returns false when device doesn't exist (graceful failure)
// ===========================================================================

TEST_F(WatchdogTest, PingFailsGracefully)
{
    // Path to a non-existent directory — start() will fail.
    Watchdog wd("/nonexistent/watchdog");
    EXPECT_FALSE(wd.start());
    EXPECT_FALSE(wd.is_running());
    EXPECT_EQ(wd.ping_count(), 0);

    // stop() should be a safe no-op.
    EXPECT_NO_THROW(wd.stop());
}

// ===========================================================================
// Test: Manual ping writes to the device
// ===========================================================================

TEST_F(WatchdogTest, ManualPingWritesToDevice)
{
    Watchdog wd(test_path_.string());
    ASSERT_TRUE(wd.start());

    // Manually ping 3 times (in addition to the keepalive thread pings).
    for (int i = 0; i < 3; ++i) {
        EXPECT_TRUE(wd.ping());
    }

    wd.stop();

    // File should have at least 3 bytes (one per ping).
    EXPECT_GE(std::filesystem::file_size(test_path_), 3);
}

// ===========================================================================
// Test: Destructor stops the watchdog cleanly
// ===========================================================================

TEST_F(WatchdogTest, DestructorStopsCleanly)
{
    {
        Watchdog wd(test_path_.string(), std::chrono::milliseconds(50));
        ASSERT_TRUE(wd.start());
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        // wd goes out of scope — destructor calls stop()
    }

    // File should exist with data.
    EXPECT_TRUE(std::filesystem::exists(test_path_));
    EXPECT_GT(std::filesystem::file_size(test_path_), 0);
}

}  // namespace vision
}  // namespace gema
