#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace gema {
namespace vision {

// ===========================================================================
// Version — semantic versioning (MAJOR.MINOR.PATCH)
// ===========================================================================

/**
 * @brief Lightweight semver struct.
 *
 * Constructed via parse_version() and compared with operator> / operator>=.
 * A default-constructed Version is 0.0.0 (invalid / unset).
 */
struct Version {
    int major = 0;
    int minor = 0;
    int patch = 0;

    bool operator==(const Version& other) const noexcept
    {
        return major == other.major
            && minor == other.minor
            && patch == other.patch;
    }
};

/** @brief Strictly greater-than semver comparison. */
bool operator>(const Version& a, const Version& b);

/** @brief Greater-than-or-equal semver comparison. */
bool operator>=(const Version& a, const Version& b);

/**
 * @brief Parse a "MAJOR.MINOR.PATCH" string.
 * @return  Version, or {0,0,0} on parse failure.
 */
Version parse_version(const std::string& str);

// ===========================================================================
// OtaCommand — parsed from JSON MQTT payload
// ===========================================================================

/**
 * @brief Structured OTA command received via MQTT.
 *
 * The cloud publishes a JSON payload on `novamex/vision/ota/update`:
 * @code{.json}
 * { "version": "2.0.0", "url": "https://.../artifact.ota", "sha256": "abc..." }
 * @endcode
 */
struct OtaCommand {
    std::string version;        // semver string, e.g. "2.0.0"
    std::string url;            // download URL for the OTA artifact
    std::string sha256;         // expected SHA-256 checksum (hex)
    std::string signature;      // Ed25519 signature of the manifest (base64)
    std::string next_public_key; // next public key for rotation (base64 PEM)
};

/**
 * @brief Parse a JSON OTA command string.
 *
 * @param json_str  Raw JSON payload (must contain "version", "url", "sha256").
 * @return Populated OtaCommand — missing fields are empty strings.
 *
 * @note Minimal parser — no external JSON library needed.
 *       Only handles flat objects with string values.
 */
OtaCommand parse_ota_command(const std::string& json_str);

// ===========================================================================
// OtaManager — reactive OTA update state machine
// ===========================================================================

/**
 * @brief Over-The-Air update agent for the RV1106.
 *
 * Designed as a reactive component: the main application (or a simple
 * MQTT listener) calls on_ota_command() when a message arrives on
 * `novamex/vision/ota/update`.
 *
 * ## State machine
 *
 *   IDLE → VALIDATING → DOWNLOADING → VERIFYING → BACKING_UP
 *       → APPLYING → RESTARTING → CONFIRMING → SUCCESS
 *         ↓            ↓             ↓           ↓
 *       FAILED        FAILED       FAILED      FAILED
 *
 * Any step can transition to FAILED on error.  On SUCCESS, the new
 * binary has been applied and the vision service has been restarted.
 *
 * ## Thread safety
 *
 * on_ota_command() is **not** re-entrant — call it from one thread
 * at a time (e.g. an MQTT callback or a FIFO reader loop).
 * state() is atomic and safe to read from any thread.
 *
 * ## Locking
 *
 * A POSIX flock() on /userdata/ota_tmp/ota.lock prevents concurrent
 * OTA operations from multiple processes.
 */
class OtaManager {
public:
    /** OTA state machine states. */
    enum class State : uint8_t {
        IDLE,          ///< Waiting for command
        VALIDATING,    ///< Checking version, disk space, compatibility
        DOWNLOADING,   ///< Downloading artifact via wget
        VERIFYING,     ///< SHA-256 checksum verification
        BACKING_UP,    ///< Backing up current binary
        APPLYING,      ///< Atomic mv + triple-flush
        RESTARTING,    ///< Restarting vision service
        CONFIRMING,    ///< Post-restart health check
        SUCCESS,       ///< Update complete
        FAILED         ///< Update failed (terminal)
    };

    /**
     * @brief Callback for publishing status via MQTT.
     *
     * Signature: (topic, payload)
     * Called after each state transition with a status message on
     * `novamex/vision/ota/status`.
     */
    using PublishCallback = std::function<void(const std::string& topic,
                                               const std::string& payload)>;

    /**
     * @brief Construct the OTA manager.
     * @param publish_cb  Callback for MQTT status publishing.
     */
    explicit OtaManager(PublishCallback publish_cb);

    virtual ~OtaManager();

    // Non-copiable, non-movable.
    OtaManager(const OtaManager&) = delete;
    OtaManager& operator=(const OtaManager&) = delete;
    OtaManager(OtaManager&&) = delete;
    OtaManager& operator=(OtaManager&&) = delete;

    // ------------------------------------------------------------------
    // Public API
    // ------------------------------------------------------------------

    /**
     * @brief Process an incoming OTA command.
     *
     * Parses the JSON, validates, and runs the state machine.
     * Must be called from a single thread (not re-entrant).
     *
     * @param json_cmd  Raw JSON payload from MQTT.
     */
    void on_ota_command(const std::string& json_cmd);

    /** @brief Current state machine state (thread-safe). */
    State state() const noexcept { return state_.load(); }

    /** @brief Human-readable string for the current state. */
    std::string state_string() const;

    // ------------------------------------------------------------------
    // Test helpers
    // ------------------------------------------------------------------

    /** @brief Override the current running version (for testing). */
    void set_current_version(const Version& v) { current_version_ = v; }

    /** @brief Return the current running version. */
    Version current_version() const { return current_version_; }

    // ------------------------------------------------------------------
    // Key management
    // ------------------------------------------------------------------

    /**
     * @brief Load the active public key.
     *
     * Precedence:
     *   1. /userdata/ota/trusted_keys/key-active.pub  (file override)
     *   2. kOtaBuiltinPublicKey (built-in fallback)
     *
     * @return PEM-encoded public key, or empty string if none found.
     */
    std::string load_public_key() const;

    /** @brief Load the next-rotation public key. */
    std::string load_next_key() const;

    /** @brief Save next-rotation public key to disk. */
    bool save_next_key(const std::string& pem_base64);

    /**
     * @brief Promote the next key to active (atomic rename).
     *
     * Uses std::rename() which is atomic on the same filesystem —
     * a power loss during promotion leaves either the old key or
     * the new key, never a corrupt half-written file.
     */
    bool promote_next_key();

    /**
     * @brief Verify an Ed25519-signed manifest.
     *
     * Tries the active key first, then the next-key if the active
     * fails (TOFU rotation pattern).
     *
     * On successful verification with the next-key, automatically
     * promotes it to active.
     *
     * @param signature_hex  Hex-encoded Ed25519 signature.
     * @param message        The message that was signed (e.g. the manifest JSON).
     * @return true if verification succeeded with any key.
     */
    bool verify_signature(const std::string& signature_hex,
                          const std::string& message);

protected:
    // ------------------------------------------------------------------
    // Virtual steps (overridable in tests)
    // ------------------------------------------------------------------

    /**
     * @brief Validate the OTA command.
     *
     * Checks:
     *   1. New version > current version (semver)
     *   2. Sufficient disk space via statvfs (>= 50 MB)
     *   3. URL has a valid scheme (http/https)
     *
     * @return true if all checks pass.
     */
    virtual bool validate(const OtaCommand& cmd);

    /**
     * @brief Download the OTA artifact.
     *
     * Uses system("wget -c -O /userdata/ota_tmp/artifact.tmp <URL>").
     * Streams directly to eMMC — never to /tmp/ or RAM.
     *
     * @return true if download succeeded.
     */
    virtual bool download();

    /**
     * @brief Verify the downloaded artifact's SHA-256 checksum.
     *
     * Uses system("sha256sum -c ...") with the expected hash.
     *
     * @return true if checksum matches.
     */
    virtual bool verify_checksum();

    /**
     * @brief Back up the current binary.
     *
     * Copies /userdata/bin/gema-ota → /backup/gema-ota.<version>
     * and generates /backup/gema-ota.<version>.sha256.
     *
     * @return true if backup succeeded.
     */
    virtual bool backup();

    /**
     * @brief Atomically apply the new binary.
     *
     * Uses mv within the same filesystem (/userdata/) for atomicity,
     * then performs a triple-flush: sync() + sync() + blockdev --flushbufs.
     *
     * @return true if apply succeeded.
     */
    virtual bool apply();

    /**
     * @brief Restart the gema-vision service.
     *
     * Calls system("systemctl restart gema-vision").
     *
     * @return true if systemctl succeeded.
     */
    virtual bool restart_vision();

    /**
     * @brief Confirm health after restart.
     *
     * Checks that the gema-vision service is active, then calls
     * system("fw_setenv bootcount 0") to reset the boot counter.
     *
     * @return true if health check passed.
     */
    virtual bool confirm_health();

    // ------------------------------------------------------------------
    // Lock management (overridable for testing)
    // ------------------------------------------------------------------

    /** @brief Acquire an exclusive flock() on the OTA lock file. */
    virtual bool acquire_lock();

    /** @brief Release the OTA lock file. */
    virtual void release_lock();

private:
    /** @brief Transition to a new state and report via MQTT. */
    void set_state(State s);

    /**
     * @brief Report status via the publish callback.
     * @param status   Short status word (e.g. "validating", "downloading").
     * @param message  Human-readable detail message.
     */
    void report_status(const std::string& status, const std::string& message);

    /** @brief Convert a State enum to a human-readable string. */
    static std::string state_to_string(State s);

    // ------------------------------------------------------------------
    // Constants (embedded filesystem paths)
    // ------------------------------------------------------------------

    static constexpr const char* kLockFile    = "/userdata/ota_tmp/ota.lock";
    static constexpr const char* kTempDir     = "/userdata/ota_tmp";
    static constexpr const char* kTempArtifact= "/userdata/ota_tmp/artifact.tmp";
    static constexpr const char* kFinalBinary = "/userdata/ota_tmp/artifact.bin";
    static constexpr const char* kBackupDir   = "/backup";
    static constexpr const char* kBinaryPath  = "/userdata/bin/gema-ota";
    static constexpr const char* kVisionBin   = "/usr/bin/gema-vision";
    static constexpr const char* kTopicStatus = "novamex/vision/ota/status";

    // Key management paths
    static constexpr const char* kKeyActive   = "/userdata/ota/trusted_keys/key-active.pub";
    static constexpr const char* kKeyNext     = "/userdata/ota/trusted_keys/key-next.pub";
    static constexpr const char* kKeyDir      = "/userdata/ota/trusted_keys";

    /** Minimum free disk space required for OTA (50 MB). */
    static constexpr uint64_t kMinDiskBytes  = 50ULL * 1024 * 1024;

    // ------------------------------------------------------------------
    // State
    // ------------------------------------------------------------------

    std::atomic<State>  state_{State::IDLE};
    Version             current_version_;
    OtaCommand          pending_cmd_;
    PublishCallback     publish_cb_;
    int                 lock_fd_{-1};
};

}  // namespace vision
}  // namespace gema
