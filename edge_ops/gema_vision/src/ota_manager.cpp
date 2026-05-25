/**
 * @file ota_manager.cpp
 * @brief OTA update state machine implementation.
 *
 * All destructive operations (download, verify, backup, apply) use
 * CLI tools via system(3) rather than library calls.  This keeps the
 * implementation simple and the binary small — critical for the
 * RV1106's 128 MB RAM and 512 MB eMMC.
 *
 * ## Safety guarantees
 *
 *   - **Atomic apply**: mv within the same filesystem (/userdata/) is
 *     atomic on Linux — the new binary is either fully present or
 *     fully absent.
 *   - **Triple-flush**: sync() + sync() + blockdev --flushbufs after
 *     every write guarantees data reaches the physical medium before
 *     we proceed.
 *   - **flock()**: prevents concurrent OTA operations from multiple
 *     processes (e.g. if two MQTT messages arrive).
 *   - **OOM safety**: OOMScoreAdjust=-900 ensures the OTA agent is
 *     killed before gema-vision (-1000) if memory runs out.
 */

#include "ota_manager.hpp"

#include <sys/file.h>      // flock()
#include <sys/statvfs.h>   // statvfs()
#include <unistd.h>        // sync(), close(), write()

#include <cstdio>           // std::snprintf, system()
#include <cstdlib>          // std::system, std::strtol
#include <cstring>          // std::strerror
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

// ---------------------------------------------------------------------------
// Helper: invoke system() and explicitly discard the result
// ---------------------------------------------------------------------------
// GCC's warn_unused_result attribute on system(3) cannot be silenced by
// static_cast<void> — only by assigning to a variable.
namespace {

[[maybe_unused]] inline int system_ignore(const char* cmd)
{
    int rc = std::system(cmd);
    return rc;  // caller chooses to ignore
}

[[maybe_unused]] inline int system_ignore(const std::string& cmd)
{
    return system_ignore(cmd.c_str());
}

}  // anonymous namespace

namespace gema {
namespace vision {

// ===========================================================================
// Version helpers
// ===========================================================================

bool operator>(const Version& a, const Version& b)
{
    if (a.major != b.major) return a.major > b.major;
    if (a.minor != b.minor) return a.minor > b.minor;
    return a.patch > b.patch;
}

bool operator>=(const Version& a, const Version& b)
{
    return a == b || a > b;
}

Version parse_version(const std::string& str)
{
    Version v;
    int n = std::sscanf(str.c_str(), "%d.%d.%d", &v.major, &v.minor, &v.patch);
    if (n < 3) {
        return Version{0, 0, 0};  // invalid → 0.0.0
    }
    return v;
}

// ===========================================================================
// JSON command parser (minimal — no external dependency)
// ===========================================================================

namespace {

/**
 * @brief Extract a quoted string value for a given key from a JSON object.
 *
 * Scans @p json for `"key": "value"` and returns `value`.
 * Returns an empty string if the key is not found or malformed.
 * Only handles flat string values — no nesting, no escapes.
 */
std::string extract_json_string(const std::string& json, const std::string& key)
{
    // Look for `"key":` in the JSON string.
    std::string search = "\"" + key + "\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) return {};

    // Find the colon after the key.
    pos = json.find(':', pos + search.size());
    if (pos == std::string::npos) return {};

    // Skip whitespace between colon and value.
    pos = json.find_first_not_of(" \t\r\n", pos + 1);
    if (pos == std::string::npos || json[pos] != '"') return {};

    // Extract the quoted value.
    auto start = pos + 1;
    auto end   = json.find('"', start);
    if (end == std::string::npos) return {};

    return json.substr(start, end - start);
}

}  // anonymous namespace

OtaCommand parse_ota_command(const std::string& json_str)
{
    OtaCommand cmd;
    cmd.version = extract_json_string(json_str, "version");
    cmd.url     = extract_json_string(json_str, "url");
    cmd.sha256  = extract_json_string(json_str, "sha256");
    return cmd;
}

// ===========================================================================
// OtaManager construction / destruction
// ===========================================================================

OtaManager::OtaManager(PublishCallback publish_cb)
    : publish_cb_(std::move(publish_cb))
{
    // Default to version 0.0.0 unless set_current_version() is called
    // (e.g. by the main application after reading a version file).
    current_version_ = Version{0, 0, 0};

    // Ensure the temp directory exists.
    std::string mkdir_cmd = std::string("mkdir -p ") + kTempDir;
    system_ignore(mkdir_cmd);
}

OtaManager::~OtaManager()
{
    release_lock();
}

// ===========================================================================
// State string helpers
// ===========================================================================

std::string OtaManager::state_string() const
{
    return state_to_string(state_.load());
}

std::string OtaManager::state_to_string(State s)
{
    switch (s) {
        case State::IDLE:        return "idle";
        case State::VALIDATING:  return "validating";
        case State::DOWNLOADING: return "downloading";
        case State::VERIFYING:   return "verifying";
        case State::BACKING_UP:  return "backing_up";
        case State::APPLYING:    return "applying";
        case State::RESTARTING:  return "restarting";
        case State::CONFIRMING:  return "confirming";
        case State::SUCCESS:     return "success";
        case State::FAILED:      return "failed";
        default:                 return "unknown";
    }
}

void OtaManager::set_state(State s)
{
    state_.store(s);
}

// ===========================================================================
// Status reporting
// ===========================================================================

void OtaManager::report_status(const std::string& status,
                               const std::string& message)
{
    if (publish_cb_) {
        publish_cb_(kTopicStatus, status + ": " + message);
    }

    // Also log to stderr for journald.
    std::string log = "[gema-ota] " + status + ": " + message + "\n";
    if (::write(STDERR_FILENO, log.c_str(), log.size()) < 0) {
        // ignore — can't do anything about a failed write to stderr
    }
}

// ===========================================================================
// Lock management
// ===========================================================================

bool OtaManager::acquire_lock()
{
    lock_fd_ = ::open(kLockFile, O_CREAT | O_RDWR, 0666);
    if (lock_fd_ < 0) {
        report_status("error", "Cannot create lock file: " + std::string(std::strerror(errno)));
        return false;
    }

    // LOCK_NB makes this non-blocking — fail immediately if locked.
    if (::flock(lock_fd_, LOCK_EX | LOCK_NB) != 0) {
        ::close(lock_fd_);
        lock_fd_ = -1;
        report_status("error", "Another OTA operation is already in progress");
        return false;
    }

    return true;
}

void OtaManager::release_lock()
{
    if (lock_fd_ >= 0) {
        ::flock(lock_fd_, LOCK_UN);
        ::close(lock_fd_);
        lock_fd_ = -1;
    }
}

// ===========================================================================
// State machine: entry point
// ===========================================================================

void OtaManager::on_ota_command(const std::string& json_cmd)
{
    // Atomically transition from IDLE → VALIDATING.
    // If not IDLE, another OTA is already running.
    State expected = State::IDLE;
    if (!state_.compare_exchange_strong(expected, State::VALIDATING)) {
        report_status("busy", "OTA already in progress — ignoring command");
        return;
    }

    // Parse the JSON command.
    auto cmd = parse_ota_command(json_cmd);
    if (cmd.version.empty() || cmd.url.empty() || cmd.sha256.empty()) {
        set_state(State::FAILED);
        report_status("failed",
            "Invalid OTA command: missing 'version', 'url', or 'sha256'");
        return;
    }

    pending_cmd_ = std::move(cmd);
    report_status("validating",
        "Validating OTA update to version " + pending_cmd_.version);

    // ---- Step 1: VALIDATE -----------------------------------------------
    if (!validate(pending_cmd_)) {
        set_state(State::FAILED);
        report_status("failed", "Validation failed — aborting");
        return;
    }

    // Acquire the lock before starting destructive operations.
    if (!acquire_lock()) {
        set_state(State::FAILED);
        report_status("failed", "Could not acquire OTA lock");
        return;
    }

    // ---- Step 2: DOWNLOAD -----------------------------------------------
    set_state(State::DOWNLOADING);
    report_status("downloading",
        "Downloading " + pending_cmd_.url);
    if (!download()) {
        set_state(State::FAILED);
        report_status("failed", "Download failed");
        release_lock();
        return;
    }

    // ---- Step 3: VERIFY -------------------------------------------------
    set_state(State::VERIFYING);
    report_status("verifying", "Verifying SHA-256 checksum");
    if (!verify_checksum()) {
        set_state(State::FAILED);
        report_status("failed", "Checksum verification failed");
        release_lock();
        return;
    }

    // ---- Step 4: BACKUP -------------------------------------------------
    set_state(State::BACKING_UP);
    report_status("backing_up", "Creating backup of current binary");
    if (!backup()) {
        set_state(State::FAILED);
        report_status("failed", "Backup failed");
        release_lock();
        return;
    }

    // ---- Step 5: APPLY --------------------------------------------------
    set_state(State::APPLYING);
    report_status("applying", "Applying update");
    if (!apply()) {
        set_state(State::FAILED);
        report_status("failed", "Apply failed — system may be inconsistent");
        release_lock();
        return;
    }

    // ---- Step 6: RESTART ------------------------------------------------
    // Lock is held through restart to prevent concurrent OTAs.
    set_state(State::RESTARTING);
    report_status("restarting", "Restarting vision service");
    if (!restart_vision()) {
        set_state(State::FAILED);
        report_status("failed", "Restart failed");
        release_lock();
        return;
    }

    // ---- Step 7: CONFIRM ------------------------------------------------
    set_state(State::CONFIRMING);
    report_status("confirming", "Confirming post-restart health");
    if (!confirm_health()) {
        set_state(State::FAILED);
        report_status("failed", "Health check failed after restart");
        release_lock();
        return;
    }

    // ---- Step 8: SUCCESS ------------------------------------------------
    set_state(State::SUCCESS);
    report_status("success",
        "OTA update to version " + pending_cmd_.version + " applied successfully");

    release_lock();
}

// ===========================================================================
// State machine steps
// ===========================================================================

bool OtaManager::validate(const OtaCommand& cmd)
{
    // 1. Semantic version check — new version MUST be strictly greater.
    Version new_ver = parse_version(cmd.version);
    if (new_ver == Version{0, 0, 0}) {
        report_status("error",
            "Cannot parse version string: \"" + cmd.version + "\"");
        return false;
    }

    if (!(new_ver > current_version_)) {
        report_status("error",
            "New version " + cmd.version
            + " is not greater than current version "
            + std::to_string(current_version_.major) + "."
            + std::to_string(current_version_.minor) + "."
            + std::to_string(current_version_.patch));
        return false;
    }

    // 2. Disk space check — need at least kMinDiskBytes free.
    struct statvfs fs_info;
    if (::statvfs(kTempDir, &fs_info) != 0) {
        report_status("error",
            "Cannot check disk space on " + std::string(kTempDir)
            + ": " + std::strerror(errno));
        return false;
    }

    uint64_t free_bytes = static_cast<uint64_t>(fs_info.f_frsize)
                        * static_cast<uint64_t>(fs_info.f_bfree);
    if (free_bytes < kMinDiskBytes) {
        report_status("error",
            "Insufficient disk space: " + std::to_string(free_bytes)
            + " bytes free, need at least "
            + std::to_string(kMinDiskBytes));
        return false;
    }

    // 3. URL scheme check — must be http or https.
    if (cmd.url.size() < 5 ||
        (cmd.url.substr(0, 4) != "http" && cmd.url.substr(0, 5) != "https")) {
        report_status("error",
            "Invalid URL scheme in: \"" + cmd.url + "\"");
        return false;
    }

    return true;
}

bool OtaManager::download()
{
    // wget -c (continue) supports resuming partial downloads.
    // Output goes directly to /userdata/ota_tmp/ — never to /tmp/ or RAM.
    std::string wget_cmd =
        std::string("wget -c -O ") + kTempArtifact + " " + pending_cmd_.url
        + " > /dev/null 2>&1";

    int rc = std::system(wget_cmd.c_str());
    if (rc != 0) {
        report_status("error",
            "wget failed with exit code " + std::to_string(rc));
        return false;
    }

    // Triple-flush after download.
    ::sync();
    ::sync();
    system_ignore("blockdev --flushbufs /dev/mmcblk0 2>/dev/null || true");

    return true;
}

bool OtaManager::verify_checksum()
{
    // Use sha256sum CLI: echo "<expected> <file>" | sha256sum -c
    std::string cmd =
        std::string("echo \"") + pending_cmd_.sha256 + "  " + kTempArtifact
        + "\" | sha256sum -c -s > /dev/null 2>&1";

    int rc = std::system(cmd.c_str());
    if (rc != 0) {
        report_status("error", "SHA-256 checksum mismatch");
        return false;
    }

    return true;
}

bool OtaManager::backup()
{
    // Build version suffix for the backup filename.
    std::string ver_str =
        std::to_string(current_version_.major) + "."
        + std::to_string(current_version_.minor) + "."
        + std::to_string(current_version_.patch);

    std::string backup_file = std::string(kBackupDir) + "/gema-vision." + ver_str;
    std::string sha256_file = backup_file + ".sha256";

    // cp current binary to backup location.
    std::string cp_cmd =
        std::string("cp ") + kBinaryPath + " " + backup_file
        + " > /dev/null 2>&1";
    int rc = std::system(cp_cmd.c_str());
    if (rc != 0) {
        report_status("error",
            "Backup copy failed (exit code " + std::to_string(rc) + ")");
        return false;
    }

    // Generate SHA-256 for the backup.
    std::string sha_cmd =
        std::string("sha256sum ") + backup_file
        + " > " + sha256_file + " 2>/dev/null";
    rc = std::system(sha_cmd.c_str());
    if (rc != 0) {
        report_status("warning",
            "Could not generate backup SHA-256 (exit code "
            + std::to_string(rc) + ")");
        // Non-fatal — backup file still exists.
    }

    // Triple-flush after backup.
    ::sync();
    ::sync();
    system_ignore("blockdev --flushbufs /dev/mmcblk0 2>/dev/null || true");

    return true;
}

bool OtaManager::apply()
{
    // Atomic mv within the same filesystem.
    // We first mv to the final location under /userdata/ota_tmp/,
    // then rename to the binary path (both on /userdata/).
    std::string mv_cmd =
        std::string("mv ") + kTempArtifact + " " + kFinalBinary
        + " > /dev/null 2>&1";
    int rc = std::system(mv_cmd.c_str());
    if (rc != 0) {
        report_status("error",
            "mv failed (exit code " + std::to_string(rc) + ")");
        return false;
    }

    // Rename to the final binary path (atomic within /userdata/).
    std::string rename_cmd =
        std::string("mv ") + kFinalBinary + " " + kBinaryPath
        + " > /dev/null 2>&1";
    rc = std::system(rename_cmd.c_str());
    if (rc != 0) {
        report_status("error",
            "Final rename failed (exit code " + std::to_string(rc) + ")");
        return false;
    }

    // Make executable.
    system_ignore(std::string("chmod 755 ") + kBinaryPath + " > /dev/null 2>&1");

    // Triple-flush — every write must reach the physical medium.
    ::sync();
    ::sync();
    system_ignore("blockdev --flushbufs /dev/mmcblk0 2>/dev/null || true");

    return true;
}

bool OtaManager::restart_vision()
{
    // systemctl restart gema-vision
    int rc = ::system("systemctl restart gema-vision > /dev/null 2>&1");
    if (rc != 0) {
        report_status("error",
            "systemctl restart failed (exit code " + std::to_string(rc) + ")");
        return false;
    }

    return true;
}

bool OtaManager::confirm_health()
{
    // Check that gema-vision is active.
    int rc = ::system("systemctl is-active gema-vision > /dev/null 2>&1");
    if (rc != 0) {
        report_status("error",
            "gema-vision is not active after restart (exit code "
            + std::to_string(rc) + ")");
        return false;
    }

    // Reset the boot counter so U-Boot doesn't think the new image failed.
    system_ignore("fw_setenv bootcount 0 > /dev/null 2>&1 || true");
    // ^ non-fatal — some systems may not have fw_setenv.

    return true;
}

}  // namespace vision
}  // namespace gema
