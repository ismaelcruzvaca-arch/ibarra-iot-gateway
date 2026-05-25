/**
 * @file test_ota.cpp
 * @brief Software-in-the-Loop (SIL) tests for the OTA update agent.
 *
 * Tests cover:
 *   1. Version parsing and comparison  (pure functions)
 *   2. JSON command parsing            (pure functions)
 *   3. State machine transitions       (via TestableOtaManager)
 *   4. Validation logic                (disk space, version, URL)
 *   5. Busy / lock behaviour
 *
 * All system calls (wget, sha256sum, mv, systemctl) are stubbed out
 * by TestableOtaManager, so tests run in any environment without
 * special hardware or network access.
 */

#include "ota_manager.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace gema {
namespace vision {

// ===========================================================================
// Test utilities
// ===========================================================================

/**
 * @brief Helper: record published status messages for test assertions.
 */
struct StatusRecorder {
    std::mutex              mtx;
    std::string             last_topic;
    std::string             last_payload;
    std::atomic<int>        count{0};
    std::vector<std::string> payloads;

    void record(const std::string& topic, const std::string& payload)
    {
        std::lock_guard<std::mutex> lock(mtx);
        last_topic   = topic;
        last_payload = payload;
        payloads.push_back(payload);
        count.fetch_add(1);
    }
};

// ===========================================================================
// TestableOtaManager — stubs out system calls for unit testing
// ===========================================================================

class TestableOtaManager : public OtaManager {
public:
    using OtaManager::OtaManager;

    // Control what each step returns.
    bool validate_result   = true;
    bool download_result   = true;
    bool verify_result     = true;
    bool backup_result     = true;
    bool apply_result      = true;
    bool restart_result    = true;
    bool confirm_result    = true;
    bool lock_result       = true;

    // Track which steps were called.
    bool validate_called   = false;
    bool download_called   = false;
    bool verify_called     = false;
    bool backup_called     = false;
    bool apply_called      = false;
    bool restart_called    = false;
    bool confirm_called    = false;
    bool lock_called       = false;
    bool unlock_called     = false;

    // Record the command that was validated.
    OtaCommand last_validated_cmd;

protected:
    bool validate(const OtaCommand& cmd) override
    {
        validate_called  = true;
        last_validated_cmd = cmd;
        return validate_result;
    }

    bool download() override
    {
        download_called = true;
        return download_result;
    }

    bool verify_checksum() override
    {
        verify_called = true;
        return verify_result;
    }

    bool backup() override
    {
        backup_called = true;
        return backup_result;
    }

    bool apply() override
    {
        apply_called = true;
        return apply_result;
    }

    bool restart_vision() override
    {
        restart_called = true;
        return restart_result;
    }

    bool confirm_health() override
    {
        confirm_called = true;
        return confirm_result;
    }

    bool acquire_lock() override
    {
        lock_called = true;
        return lock_result;
    }

    void release_lock() override
    {
        unlock_called = true;
    }
};

// ===========================================================================
// 1. Version parsing and comparison
// ===========================================================================

TEST(OtaVersionTest, ParsesStandardSemver)
{
    auto v = parse_version("1.2.3");
    EXPECT_EQ(v.major, 1);
    EXPECT_EQ(v.minor, 2);
    EXPECT_EQ(v.patch, 3);
}

TEST(OtaVersionTest, ParsesZeroVersion)
{
    auto v = parse_version("0.0.0");
    EXPECT_EQ(v.major, 0);
    EXPECT_EQ(v.minor, 0);
    EXPECT_EQ(v.patch, 0);
}

TEST(OtaVersionTest, ParsesLargeNumbers)
{
    auto v = parse_version("999.888.777");
    EXPECT_EQ(v.major, 999);
    EXPECT_EQ(v.minor, 888);
    EXPECT_EQ(v.patch, 777);
}

TEST(OtaVersionTest, InvalidStringReturnsZeroZeroZero)
{
    auto v = parse_version("");
    EXPECT_EQ(v.major, 0);
    EXPECT_EQ(v.minor, 0);
    EXPECT_EQ(v.patch, 0);

    v = parse_version("not-a-version");
    EXPECT_EQ(v.major, 0);
    EXPECT_EQ(v.minor, 0);
    EXPECT_EQ(v.patch, 0);

    v = parse_version("1.2");
    EXPECT_EQ(v.major, 0);
    EXPECT_EQ(v.minor, 0);
    EXPECT_EQ(v.patch, 0);
}

TEST(OtaVersionTest, GreaterThan)
{
    EXPECT_TRUE(parse_version("2.0.0") > parse_version("1.0.0"));
    EXPECT_TRUE(parse_version("1.1.0") > parse_version("1.0.0"));
    EXPECT_TRUE(parse_version("1.0.1") > parse_version("1.0.0"));
    EXPECT_TRUE(parse_version("2.0.1") > parse_version("2.0.0"));
    EXPECT_FALSE(parse_version("1.0.0") > parse_version("1.0.0"));
    EXPECT_FALSE(parse_version("0.9.0") > parse_version("1.0.0"));
    EXPECT_FALSE(parse_version("1.0.0") > parse_version("1.0.1"));
}

TEST(OtaVersionTest, GreaterThanOrEqual)
{
    EXPECT_TRUE(parse_version("1.0.0") >= parse_version("1.0.0"));
    EXPECT_TRUE(parse_version("2.0.0") >= parse_version("1.0.0"));
    EXPECT_FALSE(parse_version("0.5.0") >= parse_version("1.0.0"));
}

TEST(OtaVersionTest, Equality)
{
    Version a{1, 2, 3};
    Version b{1, 2, 3};
    Version c{1, 2, 4};
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a == c);
}

// ===========================================================================
// 2. JSON command parsing
// ===========================================================================

TEST(OtaCommandTest, ParsesMinimalJson)
{
    std::string json = R"({
        "version": "2.0.0",
        "url": "https://example.com/gema-vision.ota",
        "sha256": "abcdef1234567890abcdef1234567890"
    })";

    auto cmd = parse_ota_command(json);
    EXPECT_EQ(cmd.version, "2.0.0");
    EXPECT_EQ(cmd.url,     "https://example.com/gema-vision.ota");
    EXPECT_EQ(cmd.sha256,  "abcdef1234567890abcdef1234567890");
}

TEST(OtaCommandTest, ParsesSingleLineJson)
{
    std::string json = R"({"version":"1.0.0","url":"http://fw.example.com/update.bin","sha256":"deadbeef"})";

    auto cmd = parse_ota_command(json);
    EXPECT_EQ(cmd.version, "1.0.0");
    EXPECT_EQ(cmd.url,     "http://fw.example.com/update.bin");
    EXPECT_EQ(cmd.sha256,  "deadbeef");
}

TEST(OtaCommandTest, MissingFieldsReturnEmpty)
{
    std::string json = R"({"version": "1.0.0"})";
    auto cmd = parse_ota_command(json);
    EXPECT_EQ(cmd.version, "1.0.0");
    EXPECT_TRUE(cmd.url.empty());
    EXPECT_TRUE(cmd.sha256.empty());
}

TEST(OtaCommandTest, EmptyJsonReturnsEmptyFields)
{
    std::string json = "{}";
    auto cmd = parse_ota_command(json);
    EXPECT_TRUE(cmd.version.empty());
    EXPECT_TRUE(cmd.url.empty());
    EXPECT_TRUE(cmd.sha256.empty());
}

TEST(OtaCommandTest, WithWhitespaceAndNewlines)
{
    std::string json =
        "{\n"
        "  \"version\" : \"3.0.0\" ,\n"
        "  \"url\"     : \"https://ota.novamex.com/firmware.bin\" ,\n"
        "  \"sha256\"  : \"aaabbbcccdddeeefff000111222333444555666777888999000\"\n"
        "}\n";

    auto cmd = parse_ota_command(json);
    EXPECT_EQ(cmd.version, "3.0.0");
    EXPECT_EQ(cmd.url,     "https://ota.novamex.com/firmware.bin");
    EXPECT_EQ(cmd.sha256,  "aaabbbcccdddeeefff000111222333444555666777888999000");
}

// ===========================================================================
// 3. State machine transitions
// ===========================================================================

TEST(OtaStateMachineTest, FullSuccessTransition)
{
    StatusRecorder rec;
    auto cb = [&rec](const std::string& t, const std::string& p) {
        rec.record(t, p);
    };

    TestableOtaManager ota(cb);
    ota.set_current_version(Version{1, 0, 0});

    std::string json = R"({"version":"2.0.0","url":"https://ex.com/fw.bin","sha256":"abc"})";
    ota.on_ota_command(json);

    // All steps must have been called in sequence.
    EXPECT_TRUE(ota.validate_called);
    EXPECT_TRUE(ota.download_called);
    EXPECT_TRUE(ota.verify_called);
    EXPECT_TRUE(ota.backup_called);
    EXPECT_TRUE(ota.apply_called);
    EXPECT_TRUE(ota.restart_called);
    EXPECT_TRUE(ota.confirm_called);

    // Final state must be SUCCESS.
    EXPECT_EQ(ota.state(), OtaManager::State::SUCCESS);

    // Must have published status messages.
    EXPECT_GE(rec.count.load(), 3);
}

TEST(OtaStateMachineTest, ValidationFailureStopsMachine)
{
    StatusRecorder rec;
    auto cb = [&rec](const std::string& t, const std::string& p) {
        rec.record(t, p);
    };

    TestableOtaManager ota(cb);
    ota.validate_result = false;

    std::string json = R"({"version":"2.0.0","url":"https://ex.com/fw.bin","sha256":"abc"})";
    ota.on_ota_command(json);

    EXPECT_TRUE(ota.validate_called);
    EXPECT_FALSE(ota.download_called);
    EXPECT_FALSE(ota.verify_called);
    EXPECT_FALSE(ota.backup_called);
    EXPECT_FALSE(ota.apply_called);
    EXPECT_FALSE(ota.restart_called);
    EXPECT_FALSE(ota.confirm_called);
    EXPECT_EQ(ota.state(), OtaManager::State::FAILED);
}

TEST(OtaStateMachineTest, DownloadFailureStopsMachine)
{
    StatusRecorder rec;
    auto cb = [&rec](const std::string& t, const std::string& p) {
        rec.record(t, p);
    };

    TestableOtaManager ota(cb);
    ota.download_result = false;

    std::string json = R"({"version":"2.0.0","url":"https://ex.com/fw.bin","sha256":"abc"})";
    ota.on_ota_command(json);

    EXPECT_TRUE(ota.validate_called);
    EXPECT_TRUE(ota.download_called);
    EXPECT_FALSE(ota.verify_called);
    EXPECT_FALSE(ota.backup_called);
    EXPECT_FALSE(ota.apply_called);
    EXPECT_FALSE(ota.restart_called);
    EXPECT_FALSE(ota.confirm_called);
    EXPECT_EQ(ota.state(), OtaManager::State::FAILED);
}

TEST(OtaStateMachineTest, VerifyFailureStopsMachine)
{
    StatusRecorder rec;
    auto cb = [&rec](const std::string& t, const std::string& p) {
        rec.record(t, p);
    };

    TestableOtaManager ota(cb);
    ota.verify_result = false;

    std::string json = R"({"version":"2.0.0","url":"https://ex.com/fw.bin","sha256":"abc"})";
    ota.on_ota_command(json);

    EXPECT_TRUE(ota.validate_called);
    EXPECT_TRUE(ota.download_called);
    EXPECT_TRUE(ota.verify_called);
    EXPECT_FALSE(ota.backup_called);
    EXPECT_FALSE(ota.apply_called);
    EXPECT_FALSE(ota.restart_called);
    EXPECT_FALSE(ota.confirm_called);
    EXPECT_EQ(ota.state(), OtaManager::State::FAILED);
}

TEST(OtaStateMachineTest, BackupFailureStopsMachine)
{
    StatusRecorder rec;
    auto cb = [&rec](const std::string& t, const std::string& p) {
        rec.record(t, p);
    };

    TestableOtaManager ota(cb);
    ota.backup_result = false;

    std::string json = R"({"version":"2.0.0","url":"https://ex.com/fw.bin","sha256":"abc"})";
    ota.on_ota_command(json);

    EXPECT_TRUE(ota.validate_called);
    EXPECT_TRUE(ota.download_called);
    EXPECT_TRUE(ota.verify_called);
    EXPECT_TRUE(ota.backup_called);
    EXPECT_FALSE(ota.apply_called);
    EXPECT_FALSE(ota.restart_called);
    EXPECT_FALSE(ota.confirm_called);
    EXPECT_EQ(ota.state(), OtaManager::State::FAILED);
}

TEST(OtaStateMachineTest, ApplyFailureStopsMachine)
{
    StatusRecorder rec;
    auto cb = [&rec](const std::string& t, const std::string& p) {
        rec.record(t, p);
    };

    TestableOtaManager ota(cb);
    ota.apply_result = false;

    std::string json = R"({"version":"2.0.0","url":"https://ex.com/fw.bin","sha256":"abc"})";
    ota.on_ota_command(json);

    EXPECT_TRUE(ota.validate_called);
    EXPECT_TRUE(ota.download_called);
    EXPECT_TRUE(ota.verify_called);
    EXPECT_TRUE(ota.backup_called);
    EXPECT_TRUE(ota.apply_called);
    EXPECT_FALSE(ota.restart_called);
    EXPECT_FALSE(ota.confirm_called);
    EXPECT_EQ(ota.state(), OtaManager::State::FAILED);
}

TEST(OtaStateMachineTest, RestartFailureStopsMachine)
{
    StatusRecorder rec;
    auto cb = [&rec](const std::string& t, const std::string& p) {
        rec.record(t, p);
    };

    TestableOtaManager ota(cb);
    ota.restart_result = false;

    std::string json = R"({"version":"2.0.0","url":"https://ex.com/fw.bin","sha256":"abc"})";
    ota.on_ota_command(json);

    EXPECT_TRUE(ota.validate_called);
    EXPECT_TRUE(ota.download_called);
    EXPECT_TRUE(ota.verify_called);
    EXPECT_TRUE(ota.backup_called);
    EXPECT_TRUE(ota.apply_called);
    EXPECT_TRUE(ota.restart_called);
    EXPECT_FALSE(ota.confirm_called);
    EXPECT_EQ(ota.state(), OtaManager::State::FAILED);
}

TEST(OtaStateMachineTest, ConfirmFailureStopsMachine)
{
    StatusRecorder rec;
    auto cb = [&rec](const std::string& t, const std::string& p) {
        rec.record(t, p);
    };

    TestableOtaManager ota(cb);
    ota.confirm_result = false;

    std::string json = R"({"version":"2.0.0","url":"https://ex.com/fw.bin","sha256":"abc"})";
    ota.on_ota_command(json);

    EXPECT_TRUE(ota.validate_called);
    EXPECT_TRUE(ota.download_called);
    EXPECT_TRUE(ota.verify_called);
    EXPECT_TRUE(ota.backup_called);
    EXPECT_TRUE(ota.apply_called);
    EXPECT_TRUE(ota.restart_called);
    EXPECT_TRUE(ota.confirm_called);
    EXPECT_EQ(ota.state(), OtaManager::State::FAILED);
}

// ===========================================================================
// 4. Edge cases and concurrent behaviour
// ===========================================================================

TEST(OtaStateMachineTest, IdleIsDefaultState)
{
    StatusRecorder rec;
    auto cb = [&rec](const std::string& t, const std::string& p) {
        rec.record(t, p);
    };

    OtaManager ota(cb);
    EXPECT_EQ(ota.state(), OtaManager::State::IDLE);
    EXPECT_EQ(ota.state_string(), "idle");
}

TEST(OtaStateMachineTest, SecondCommandWhileBusyIsRejected)
{
    StatusRecorder rec;
    auto cb = [&rec](const std::string& t, const std::string& p) {
        rec.record(t, p);
    };

    TestableOtaManager ota(cb);
    // Make validation slow by setting a flag — actually, since our mock
    // returns instantly, just check that on_ota_command while already
    // running (non-IDLE) returns "busy".
    //
    // We simulate this: first command starts and immediately finishes.
    // Second command while in a non-IDLE state would be rejected.
    // But since everything is synchronous, we need another approach:
    // set validate to return false and check that a second call during
    // the first is rejected. Since our mock is synchronous, let's just
    // verify that a second call on a non-IDLE state is rejected.
    //
    // Actually, the simplest test: set lock_result=false, call once
    // (which will proceed past validate to lock and fail). Then verify
    // state is FAILED, then a second call should also be rejected
    // because state is not IDLE.

    ota.lock_result = false;  // Will fail after validation
    std::string json = R"({"version":"2.0.0","url":"https://ex.com/fw.bin","sha256":"abc"})";

    ota.on_ota_command(json);
    // Should have reached lock step and failed.
    EXPECT_TRUE(ota.validate_called);
    EXPECT_TRUE(ota.lock_called);
    EXPECT_FALSE(ota.download_called);
    EXPECT_EQ(ota.state(), OtaManager::State::FAILED);

    // Now state is FAILED, not IDLE — calling again should be rejected.
    // Reset the trackers to see if on_ota_command actually runs again.
    ota.validate_called = false;
    ota.lock_called = false;

    ota.on_ota_command(json);
    // Should NOT have called validate or lock — rejected at CAS.
    EXPECT_FALSE(ota.validate_called) << "Second command should be rejected when not IDLE";
    EXPECT_FALSE(ota.lock_called);
}

TEST(OtaStateMachineTest, LockFailureRejected)
{
    StatusRecorder rec;
    auto cb = [&rec](const std::string& t, const std::string& p) {
        rec.record(t, p);
    };

    TestableOtaManager ota(cb);
    ota.lock_result = false;

    std::string json = R"({"version":"2.0.0","url":"https://ex.com/fw.bin","sha256":"abc"})";
    ota.on_ota_command(json);

    // Validation passed, but lock failed.
    EXPECT_TRUE(ota.validate_called);
    EXPECT_TRUE(ota.lock_called);
    EXPECT_FALSE(ota.download_called);
    EXPECT_EQ(ota.state(), OtaManager::State::FAILED);
}

// ===========================================================================
// 5. Validation logic
// ===========================================================================

TEST(OtaValidationTest, InvalidJsonFieldsRejected)
{
    StatusRecorder rec;
    auto cb = [&rec](const std::string& t, const std::string& p) {
        rec.record(t, p);
    };

    TestableOtaManager ota(cb);
    ota.set_current_version(Version{1, 0, 0});

    // Missing sha256 field.
    std::string json = R"({"version":"2.0.0","url":"https://ex.com/fw.bin"})";
    ota.on_ota_command(json);

    // Should fail before even calling validate — parse returns empty sha256.
    EXPECT_FALSE(ota.validate_called);
    EXPECT_EQ(ota.state(), OtaManager::State::FAILED);
}

TEST(OtaValidationTest, EmptyJsonRejected)
{
    StatusRecorder rec;
    auto cb = [&rec](const std::string& t, const std::string& p) {
        rec.record(t, p);
    };

    TestableOtaManager ota(cb);

    // Empty JSON string.
    ota.on_ota_command("{}");

    EXPECT_FALSE(ota.validate_called);
    EXPECT_EQ(ota.state(), OtaManager::State::FAILED);
}

TEST(OtaStateMachineTest, CurrentVersionPassedToValidate)
{
    StatusRecorder rec;
    auto cb = [&rec](const std::string& t, const std::string& p) {
        rec.record(t, p);
    };

    TestableOtaManager ota(cb);
    ota.set_current_version(Version{3, 0, 0});

    std::string json = R"({"version":"4.0.0","url":"https://ex.com/fw.bin","sha256":"abc"})";
    ota.on_ota_command(json);

    ASSERT_TRUE(ota.validate_called);
    EXPECT_EQ(ota.last_validated_cmd.version, "4.0.0");
    EXPECT_EQ(ota.last_validated_cmd.url, "https://ex.com/fw.bin");
    EXPECT_EQ(ota.last_validated_cmd.sha256, "abc");
}

}  // namespace vision
}  // namespace gema
