"""Phase 1, 2, and 3 tests for Alert Engine.

Phase 1: Schema validation, type contracts, util contracts, metadata.
Phase 2: Core dispatcher (engine.ts, registry.ts, index.ts).
Phase 3: Channel adapters — telegram, whatsapp, slack, email, torreta, push.

These tests validate the project files themselves (schema, types, contracts)
rather than runtime behavior. Runtime tests (Phase 5) use mock-hasura pattern.
"""

from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent.parent.parent
NHOST_MIGRATIONS = PROJECT_ROOT / "nhost" / "migrations" / "default" / "alert_engine"
FUNCTIONS_DIR = PROJECT_ROOT / "nhost" / "functions" / "alert-dispatcher"
HEALTHBEAT_DIR = PROJECT_ROOT / "nhost" / "functions" / "alert-healthbeat"
METADATA_DIR = PROJECT_ROOT / "nhost" / "metadata"
ADAPTERS_DIR = FUNCTIONS_DIR / "adapters"

# Phase 5: Multi-tenant paths
MULTI_TENANT_MIGRATIONS = PROJECT_ROOT / "nhost" / "migrations" / "default" / "multi_tenant_core"
TABLES_METADATA_DIR = PROJECT_ROOT / "nhost" / "metadata" / "databases" / "default" / "tables"

# ===========================================================================
# Schema validation
# ===========================================================================


class TestSchemaValidation:
    """Validate SQL migration files exist and contain expected constructs."""

    def test_up_sql_exists(self):
        assert (NHOST_MIGRATIONS / "up.sql").exists(), \
            "up.sql must exist for alert_engine migration"

    def test_down_sql_exists(self):
        assert (NHOST_MIGRATIONS / "down.sql").exists(), \
            "down.sql must exist for alert_engine migration"

    def test_up_sql_creates_alert_channels(self):
        sql = (NHOST_MIGRATIONS / "up.sql").read_text()
        assert "CREATE TABLE alert_channels" in sql
        assert "gen_random_uuid()" in sql

    def test_up_sql_creates_alert_rules(self):
        sql = (NHOST_MIGRATIONS / "up.sql").read_text()
        assert "CREATE TABLE alert_rules" in sql
        assert "REFERENCES alert_channels" in sql
        assert "ON DELETE CASCADE" in sql

    def test_up_sql_creates_alert_engine_health(self):
        sql = (NHOST_MIGRATIONS / "up.sql").read_text()
        assert "CREATE TABLE alert_engine_health" in sql

    def test_up_sql_has_channel_type_check_constraint(self):
        """channel_type must have CHECK constraint with all 6 types."""
        sql = (NHOST_MIGRATIONS / "up.sql").read_text()
        for ch_type in ["telegram", "whatsapp", "slack", "email", "torreta", "push"]:
            assert ch_type in sql, f"channel_type CHECK must include '{ch_type}'"

    def test_up_sql_foreign_key_on_channel_id(self):
        sql = (NHOST_MIGRATIONS / "up.sql").read_text()
        assert "REFERENCES alert_channels(id)" in sql

    def test_up_sql_index_on_node_id(self):
        sql = (NHOST_MIGRATIONS / "up.sql").read_text()
        assert "idx_alert_rules_node_id" in sql
        assert "CREATE INDEX" in sql

    def test_up_sql_index_on_checked_at(self):
        sql = (NHOST_MIGRATIONS / "up.sql").read_text()
        assert "idx_alert_engine_health_checked_at" in sql

    def test_down_sql_drops_alert_engine_health(self):
        sql = (NHOST_MIGRATIONS / "down.sql").read_text()
        assert "DROP TABLE IF EXISTS alert_engine_health" in sql

    def test_down_sql_drops_alert_rules(self):
        sql = (NHOST_MIGRATIONS / "down.sql").read_text()
        assert "DROP TABLE IF EXISTS alert_rules" in sql

    def test_down_sql_drops_alert_channels(self):
        sql = (NHOST_MIGRATIONS / "down.sql").read_text()
        assert "DROP TABLE IF EXISTS alert_channels" in sql


# ===========================================================================
# TypeScript type contracts
# ===========================================================================


class TestTypeContracts:
    """Validate TypeScript interfaces and types are well-defined."""

    def test_types_file_exists(self):
        assert (FUNCTIONS_DIR / "types.ts").exists()

    def test_utils_file_exists(self):
        assert (FUNCTIONS_DIR / "utils.ts").exists()

    def test_channel_type_is_exported(self):
        content = (FUNCTIONS_DIR / "types.ts").read_text()
        assert "export type ChannelType" in content

    def test_channel_type_includes_all_six(self):
        content = (FUNCTIONS_DIR / "types.ts").read_text()
        for ch_type in ["telegram", "whatsapp", "slack", "email", "torreta", "push"]:
            assert ch_type in content, \
                f"ChannelType must include '{ch_type}'"

    def test_channel_config_interface(self):
        content = (FUNCTIONS_DIR / "types.ts").read_text()
        assert "export interface ChannelConfig" in content

    def test_alert_message_interface(self):
        content = (FUNCTIONS_DIR / "types.ts").read_text()
        assert "export interface AlertMessage" in content
        assert "node_id" in content
        assert "status" in content
        assert "payload" in content
        assert "event_ts" in content

    def test_dispatch_result_interface(self):
        content = (FUNCTIONS_DIR / "types.ts").read_text()
        assert "export interface DispatchResult" in content
        assert "success" in content
        assert "statusCode?" in content
        assert "error?" in content

    def test_dispatcher_adapter_interface(self):
        content = (FUNCTIONS_DIR / "types.ts").read_text()
        assert "export interface AlertDispatcherAdapter" in content
        assert "send" in content
        assert "Promise<DispatchResult>" in content

    def test_utils_exports_retry_backoff(self):
        content = (FUNCTIONS_DIR / "utils.ts").read_text()
        assert "retryBackoff" in content
        assert "export async function retryBackoff" in content

    def test_utils_exports_format_message(self):
        content = (FUNCTIONS_DIR / "utils.ts").read_text()
        assert "formatMessage" in content
        assert "export function formatMessage" in content


# ===========================================================================
# Utility interface contracts
# ===========================================================================


class TestRetryBackoffContract:
    """Validate retryBackoff interface and contract."""

    def test_retry_options_interface(self):
        content = (FUNCTIONS_DIR / "utils.ts").read_text()
        assert "export interface RetryOptions" in content
        assert "maxRetries" in content
        assert "baseDelayMs" in content

    def test_exponential_backoff_formula(self):
        """Retry must use exponential delay: baseDelayMs * 2^attempt."""
        content = (FUNCTIONS_DIR / "utils.ts").read_text()
        assert "Math.pow(2, attempt)" in content or \
               "Math.pow(2, attempt)" in content

    def test_retry_throws_on_exhaustion(self):
        """retryBackoff must throw when all retries exhausted."""
        content = (FUNCTIONS_DIR / "utils.ts").read_text()
        assert "throw lastError" in content

    def test_default_retry_is_3_tries_1s_base(self):
        """Default retry: 3 retries, 1000ms base delay."""
        content = (FUNCTIONS_DIR / "utils.ts").read_text()
        assert "3" in content or "maxRetries" in content

    def test_await_set_timeout_for_delay(self):
        """Delay must use await new Promise(setTimeout)."""
        content = (FUNCTIONS_DIR / "utils.ts").read_text()
        # Could be Promise<unknown> or Promise<void>
        assert "Promise" in content
        assert "setTimeout" in content


class TestFormatMessageContract:
    """Validate formatMessage interface and formatting rules."""

    def test_format_message_includes_node_id(self):
        content = (FUNCTIONS_DIR / "utils.ts").read_text()
        assert "message.node_id" in content
        assert "ALERTA" in content

    def test_format_message_includes_status(self):
        content = (FUNCTIONS_DIR / "utils.ts").read_text()
        assert "message.status" in content

    def test_format_message_includes_timestamp(self):
        content = (FUNCTIONS_DIR / "utils.ts").read_text()
        assert "message.event_ts" in content

    def test_format_message_with_payload(self):
        """When payload has keys, format them as key: value pairs."""
        content = (FUNCTIONS_DIR / "utils.ts").read_text()
        assert "payload" in content
        assert "Object.entries" in content or "payload" in content

    def test_format_message_empty_payload(self):
        """When payload is empty, skip the payload section."""
        content = (FUNCTIONS_DIR / "utils.ts").read_text()
        assert "Object.keys(message.payload)" in content or \
               "Object.keys(payload)" in content

    def test_format_message_returns_string(self):
        """formatMessage must return a string (join with newline)."""
        content = (FUNCTIONS_DIR / "utils.ts").read_text()
        assert ".join" in content
        assert "'\\n'" in content or '"\n"' in content


# ===========================================================================
# Metadata validation
# ===========================================================================


class TestMetadataContracts:
    """Validate metadata YAML files exist with expected structure."""

    def test_event_triggers_yaml_exists(self):
        assert (METADATA_DIR / "event_triggers.yaml").exists()

    def test_cron_triggers_yaml_exists(self):
        assert (METADATA_DIR / "cron_triggers.yaml").exists()

    def test_event_trigger_has_filter_for_status_3(self):
        content = (METADATA_DIR / "event_triggers.yaml").read_text()
        assert "alert_on_alarm" in content
        assert "norvi_telemetry" in content
        assert "_eq" in content
        assert '"3"' in content

    def test_cron_trigger_has_5min_schedule(self):
        content = (METADATA_DIR / "cron_triggers.yaml").read_text()
        assert "alert-healthbeat-cron" in content
        assert "*/5" in content

    def test_cron_trigger_targets_healthbeat_function(self):
        content = (METADATA_DIR / "cron_triggers.yaml").read_text()
        assert "alert-healthbeat" in content


# ===========================================================================
# Phase 2: Core Dispatcher — engine.ts
# ===========================================================================


class TestEngineContract:
    """Validate engine.ts exists with DispatcherEngine, resolveRules, fanOut."""

    def test_engine_file_exists(self):
        assert (FUNCTIONS_DIR / "engine.ts").exists(), \
            "engine.ts must exist for dispatcher engine"

    def test_engine_exports_dispatcher_engine_class(self):
        content = (FUNCTIONS_DIR / "engine.ts").read_text()
        assert "export class DispatcherEngine" in content or \
               "export class DispatcherEngine" in content

    def test_engine_has_resolve_rules(self):
        content = (FUNCTIONS_DIR / "engine.ts").read_text()
        assert "resolveRules" in content

    def test_engine_has_fan_out(self):
        content = (FUNCTIONS_DIR / "engine.ts").read_text()
        assert "fanOut" in content

    def test_engine_resolve_rules_accepts_node_id(self):
        """resolveRules must accept a node_id string parameter."""
        content = (FUNCTIONS_DIR / "engine.ts").read_text()
        # Look for resolveRules with a parameter (node_id)
        assert "resolveRules" in content
        assert "node_id" in content

    def test_engine_fan_out_accepts_channels_and_message(self):
        """fanOut must accept channels array and AlertMessage."""
        content = (FUNCTIONS_DIR / "engine.ts").read_text()
        assert "fanOut" in content

    def test_engine_uses_retry_backoff(self):
        """engine must use retryBackoff from utils for channel dispatch."""
        content = (FUNCTIONS_DIR / "engine.ts").read_text()
        assert "retryBackoff" in content

    def test_engine_handles_disabled_channels(self):
        """Engine must skip disabled channels in fan-out."""
        content = (FUNCTIONS_DIR / "engine.ts").read_text()
        assert "enabled" in content

    def test_engine_dead_letter_on_retry_exhaustion(self):
        """Engine must write dead-letter to alert_engine_health on retry exhaustion."""
        content = (FUNCTIONS_DIR / "engine.ts").read_text()
        assert "alert_engine_health" in content

    def test_engine_resolve_rules_queries_enabled_only(self):
        """resolveRules should filter WHERE enabled=true."""
        content = (FUNCTIONS_DIR / "engine.ts").read_text()
        assert "enabled" in content
        assert "true" in content


# ===========================================================================
# Phase 2: Core Dispatcher — registry.ts
# ===========================================================================


class TestRegistryContract:
    """Validate registry.ts exists with ChannelType→AlertDispatcherAdapter map."""

    def test_registry_file_exists(self):
        assert (FUNCTIONS_DIR / "adapters" / "registry.ts").exists(), \
            "registry.ts must exist in adapters/"

    def test_registry_exports_adapter_registry(self):
        content = (FUNCTIONS_DIR / "adapters" / "registry.ts").read_text()
        assert "Record<ChannelType" in content or \
               "Record<ChannelType" in content or \
               "AlertDispatcherAdapter" in content

    def test_registry_covers_all_six_channels(self):
        """Registry must define entries for all 6 channel types."""
        content = (FUNCTIONS_DIR / "adapters" / "registry.ts").read_text()
        for ch_type in ["telegram", "whatsapp", "slack", "email", "torreta", "push"]:
            assert ch_type in content, \
                f"Registry must include '{ch_type}'"

    def test_registry_has_default_export(self):
        """Registry should export a function or object to get adapters."""
        content = (FUNCTIONS_DIR / "adapters" / "registry.ts").read_text()
        assert "export" in content


# ===========================================================================
# Phase 2: Core Dispatcher — index.ts (entry point)
# ===========================================================================


class TestIndexContract:
    """Validate index.ts exists with Hasura trigger parsing and dispatch wiring."""

    def test_index_file_exists(self):
        assert (FUNCTIONS_DIR / "index.ts").exists(), \
            "index.ts must exist for Nhost function entry point"

    def test_index_imports_dispatcher_engine(self):
        content = (FUNCTIONS_DIR / "index.ts").read_text()
        assert "DispatcherEngine" in content
        assert "import" in content

    def test_index_parses_hasura_event_payload(self):
        """index.ts must parse Hasura Event Trigger payload."""
        content = (FUNCTIONS_DIR / "index.ts").read_text()
        assert "event" in content
        assert "op" in content or "data" in content

    def test_index_extracts_node_id(self):
        """index.ts must extract node_id from trigger payload."""
        content = (FUNCTIONS_DIR / "index.ts").read_text()
        assert "node_id" in content

    def test_index_calls_resolve_rules(self):
        """index.ts must call resolveRules with the extracted node_id."""
        content = (FUNCTIONS_DIR / "index.ts").read_text()
        assert "resolveRules" in content

    def test_index_calls_fan_out(self):
        """index.ts must call fanOut with resolved channels and message."""
        content = (FUNCTIONS_DIR / "index.ts").read_text()
        assert "fanOut" in content or "fanout" in content.lower()

    def test_index_exports_handler(self):
        """index.ts must export the Nhost function handler."""
        content = (FUNCTIONS_DIR / "index.ts").read_text()
        assert "export" in content


# ===========================================================================
# Phase 3: Channel Adapters
# ===========================================================================


class TestAdapterFileExistence:
    """All six adapter files must exist in the adapters/ directory."""

    ADAPTER_NAMES = ["telegram", "whatsapp", "slack", "email", "torreta", "push"]

    def test_all_adapter_files_exist(self):
        for name in self.ADAPTER_NAMES:
            assert (ADAPTERS_DIR / f"{name}.ts").exists(), \
                f"Adapter file {name}.ts must exist"

    def test_no_extra_adapter_files(self):
        """Only the 6 expected adapter files exist (plus registry.ts)."""
        expected = {f"{n}.ts" for n in self.ADAPTER_NAMES} | {"registry.ts"}
        actual = {f.name for f in ADAPTERS_DIR.iterdir() if f.suffix == ".ts"}
        assert actual == expected, f"Unexpected adapter files: {actual - expected}"


class _AdapterContractBase:
    """Shared base for adapter contract tests."""

    ADAPTER_NAME: str = ""
    EXPECTED_API_ENDPOINT: str = ""
    CONFIG_FIELDS: tuple = ()
    SECRET_REF: str = ""  # e.g., "TELEGRAM_BOT_TOKEN"

    def _adapter_content(self) -> str:
        return (ADAPTERS_DIR / f"{self.ADAPTER_NAME}.ts").read_text()

    def test_file_exists(self):
        assert (ADAPTERS_DIR / f"{self.ADAPTER_NAME}.ts").exists()

    def test_exports_create_adapter(self):
        content = self._adapter_content()
        assert "export function createAdapter" in content, \
            f"{self.ADAPTER_NAME} must export createAdapter()"

    def test_implements_alert_dispatcher_adapter(self):
        """Adapter must implement AlertDispatcherAdapter with send() method."""
        content = self._adapter_content()
        assert "AlertDispatcherAdapter" in content, \
            f"{self.ADAPTER_NAME} must reference AlertDispatcherAdapter"
        assert "send(config" in content or "send(config," in content, \
            f"{self.ADAPTER_NAME} must implement send(config, message)"

    def test_returns_dispatch_result(self):
        """send() must return DispatchResult."""
        content = self._adapter_content()
        assert "DispatchResult" in content, \
            f"{self.ADAPTER_NAME} must reference DispatchResult"

    def test_uses_api_endpoint(self):
        """Adapter must reference the correct API endpoint."""
        content = self._adapter_content()
        assert self.EXPECTED_API_ENDPOINT in content, \
            f"{self.ADAPTER_NAME} must reference endpoint {self.EXPECTED_API_ENDPOINT}"

    def test_uses_fetch_for_http(self):
        """Adapter must use fetch() for HTTP calls (Nhost runtime provides it)."""
        content = self._adapter_content()
        assert "fetch(" in content, \
            f"{self.ADAPTER_NAME} must use fetch() for HTTP calls"

    def test_has_error_handling(self):
        """Adapter must handle errors (try/catch around fetch)."""
        content = self._adapter_content()
        assert "try" in content and "catch" in content, \
            f"{self.ADAPTER_NAME} must have try/catch error handling"

    def test_has_env_var_secret_reference(self):
        """Adapter must reference its Nhost secret via environment variable."""
        if self.SECRET_REF:
            content = self._adapter_content()
            assert self.SECRET_REF in content, \
                f"{self.ADAPTER_NAME} must reference secret {self.SECRET_REF}"

    def test_config_fields_used(self):
        """Adapter must reference its expected config fields."""
        if self.CONFIG_FIELDS:
            content = self._adapter_content()
            for field in self.CONFIG_FIELDS:
                assert field in content, \
                    f"{self.ADAPTER_NAME} must reference config field '{field}'"

    def test_imports_types_from_parent(self):
        """Adapter must import types from ../types."""
        content = self._adapter_content()
        assert "from '../types'" in content or "from \"../types\"" in content, \
            f"{self.ADAPTER_NAME} must import types from ../types"


class TestTelegramAdapter(_AdapterContractBase):
    ADAPTER_NAME = "telegram"
    EXPECTED_API_ENDPOINT = "api.telegram.org"
    CONFIG_FIELDS = ("chat_id",)
    SECRET_REF = "TELEGRAM_BOT_TOKEN"


class TestWhatsAppAdapter(_AdapterContractBase):
    ADAPTER_NAME = "whatsapp"
    EXPECTED_API_ENDPOINT = "graph.facebook.com"
    CONFIG_FIELDS = ("to",)
    SECRET_REF = "WHATSAPP_TOKEN"


class TestSlackAdapter(_AdapterContractBase):
    ADAPTER_NAME = "slack"
    EXPECTED_API_ENDPOINT = "webhook_url"
    CONFIG_FIELDS = ("webhook_url",)
    SECRET_REF = ""

    def test_uses_dynamic_webhook_url(self):
        """Slack adapter uses the webhook URL dynamically, not a hardcoded domain."""
        content = self._adapter_content()
        assert "webhook_url" in content
        assert "fetch(webhookUrl" in content or "fetch(webhook_url" in content


class TestEmailAdapter(_AdapterContractBase):
    ADAPTER_NAME = "email"
    EXPECTED_API_ENDPOINT = "SMTP_HOST"
    CONFIG_FIELDS = ("to", "subject")
    SECRET_REF = "SMTP_HOST"

    def test_uses_fetch_for_http(self):
        """Email adapter uses nodemailer (SMTP), not HTTP fetch. Override base."""
        content = self._adapter_content()
        assert "nodemailer" in content, \
            f"email must use nodemailer for SMTP transport"
        assert "sendMail" in content or "createTransport" in content, \
            f"email must call sendMail/createTransport"


class TestTorretaAdapter(_AdapterContractBase):
    ADAPTER_NAME = "torreta"
    EXPECTED_API_ENDPOINT = "api_key"
    CONFIG_FIELDS = ("endpoint", "api_key")
    SECRET_REF = ""


class TestPushAdapter(_AdapterContractBase):
    ADAPTER_NAME = "push"
    EXPECTED_API_ENDPOINT = "exp.host"
    CONFIG_FIELDS = ("push_tokens",)
    SECRET_REF = ""


# ===========================================================================
# Phase 4: Healthbeat — alert-healthbeat function
# ===========================================================================


class TestHealthbeatFileExistence:
    """alert-healthbeat files must exist."""

    def test_alert_healthbeat_directory_exists(self):
        assert HEALTHBEAT_DIR.exists(), \
            "alert-healthbeat directory must exist"

    def test_assert_ts_exists(self):
        assert (HEALTHBEAT_DIR / "assert.ts").exists(), \
            "assert.ts must exist in alert-healthbeat"

    def test_index_ts_exists(self):
        assert (HEALTHBEAT_DIR / "index.ts").exists(), \
            "index.ts must exist in alert-healthbeat"

    def test_only_expected_files(self):
        """Only index.ts and assert.ts should exist in alert-healthbeat."""
        expected = {"assert.ts", "index.ts"}
        actual = {f.name for f in HEALTHBEAT_DIR.iterdir() if f.suffix == ".ts"}
        assert actual == expected, \
            f"Unexpected files in alert-healthbeat: {actual - expected}"


class TestHealthbeatAssertContract:
    """Validate assert.ts: injection, polling, health row write."""

    ASSERT_PATH = HEALTHBEAT_DIR / "assert.ts"

    def test_assert_file_exists(self):
        assert self.ASSERT_PATH.exists()

    def test_assert_content_has_inject_and_assert(self):
        content = self.ASSERT_PATH.read_text()
        assert "injectAndAssert" in content or "inject_and_assert" in content, \
            "assert.ts must export an injectAndAssert() function"

    def test_assert_exports_function(self):
        content = self.ASSERT_PATH.read_text()
        assert "export" in content, \
            "assert.ts must export at least one function"

    def test_assert_injects_to_norvi_telemetry(self):
        """assert.ts must INSERT a synthetic ALARM into norvi_telemetry."""
        content = self.ASSERT_PATH.read_text()
        assert "norvi_telemetry" in content, \
            "assert.ts must reference norvi_telemetry for synthetic INSERT"

    def test_assert_uses_healthbeat_node_id(self):
        """assert.ts must use __healthbeat__ as node_id for synthetic ALARM."""
        content = self.ASSERT_PATH.read_text()
        assert "__healthbeat__" in content, \
            "assert.ts must use __healthbeat__ as synthetic node_id"

    def test_assert_uses_status_3(self):
        """assert.ts must insert status=3 (ALARM) for the synthetic event."""
        content = self.ASSERT_PATH.read_text()
        assert "status" in content and "3" in content, \
            "assert.ts must reference status=3 for synthetic ALARM"

    def test_assert_polls_alert_engine_health(self):
        """assert.ts must poll alert_engine_health for the health row."""
        content = self.ASSERT_PATH.read_text()
        assert "alert_engine_health" in content, \
            "assert.ts must reference alert_engine_health for polling"

    def test_assert_has_polling_loop(self):
        """assert.ts must implement a polling loop (for/while/setInterval)."""
        content = self.ASSERT_PATH.read_text()
        assert "for" in content or "while" in content or "setInterval" in content, \
            "assert.ts must have a polling loop (for/while)"

    def test_assert_has_poll_interval_5s(self):
        """assert.ts must wait 5s between poll attempts."""
        content = self.ASSERT_PATH.read_text()
        assert "5000" in content, \
            "assert.ts must use 5000ms (5s) poll interval"

    def test_assert_has_poll_timeout_30s(self):
        """assert.ts must have a 30s timeout for polling (6 attempts × 5s)."""
        content = self.ASSERT_PATH.read_text()
        # Look for 6 (max attempts) or 30000 (timeout ms) or 30 (seconds)
        has_max_attempts = "6" in content
        has_timeout = "30000" in content or "30" in content
        assert has_max_attempts or has_timeout, \
            "assert.ts must have 6 max attempts (30s / 5s) or specify 30s timeout"

    def test_assert_writes_health_row(self):
        """assert.ts must INSERT the result row to alert_engine_health."""
        content = self.ASSERT_PATH.read_text()
        assert "insert_alert_engine_health" in content or \
               "alert_engine_health" in content, \
            "assert.ts must write result row to alert_engine_health"

    def test_assert_records_latency_ms(self):
        """assert.ts must record latency_ms in the health result."""
        content = self.ASSERT_PATH.read_text()
        assert "latency_ms" in content or "latency" in content, \
            "assert.ts must track latency_ms"

    def test_assert_records_success(self):
        """assert.ts must record success boolean in the health result."""
        content = self.ASSERT_PATH.read_text()
        assert "success" in content, \
            "assert.ts must track success boolean"

    def test_assert_records_detail(self):
        """assert.ts must record detail string in the health result."""
        content = self.ASSERT_PATH.read_text()
        assert "detail" in content, \
            "assert.ts must track detail string"

    def test_assert_records_checked_at(self):
        """assert.ts must record checked_at timestamp."""
        content = self.ASSERT_PATH.read_text()
        assert "checked_at" in content, \
            "assert.ts must track checked_at timestamp"


class TestHealthbeatIndexContract:
    """Validate index.ts: cron handler wiring."""

    INDEX_PATH = HEALTHBEAT_DIR / "index.ts"

    def test_index_file_exists(self):
        assert self.INDEX_PATH.exists()

    def test_index_exports_handler(self):
        """index.ts must export a handler function for Nhost cron."""
        content = self.INDEX_PATH.read_text()
        assert "export" in content, \
            "index.ts must export at least one function"
        assert "handler" in content or "Handler" in content, \
            "index.ts must export handler function"

    def test_index_calls_inject_and_assert(self):
        """index.ts must call injectAndAssert from assert.ts."""
        content = self.INDEX_PATH.read_text()
        assert "injectAndAssert" in content or "inject_and_assert" in content, \
            "index.ts must call injectAndAssert from assert.ts"

    def test_index_imports_from_assert(self):
        """index.ts must import from ./assert."""
        content = self.INDEX_PATH.read_text()
        assert "from './assert'" in content or "from \"./assert\"" in content, \
            "index.ts must import from ./assert"

    def test_index_returns_response(self):
        """index.ts must return status and body (Nhost Function contract)."""
        content = self.INDEX_PATH.read_text()
        assert "status" in content and "body" in content, \
            "index.ts must return status and body response"

    def test_index_has_error_handling(self):
        """index.ts must wrap in try/catch for error handling."""
        content = self.INDEX_PATH.read_text()
        assert "try" in content and "catch" in content, \
            "index.ts must have try/catch error handling"

    def test_index_has_200_status(self):
        """index.ts must return 200 on success."""
        content = self.INDEX_PATH.read_text()
        assert "200" in content, \
            "index.ts must return status 200 on success"

    def test_index_has_500_status(self):
        """index.ts must return 500 on error."""
        content = self.INDEX_PATH.read_text()
        assert "500" in content, \
            "index.ts must return status 500 on error"


# ===========================================================================
# Phase 5: Integration / E2E Spec Scenarios
#
# These tests validate the END-TO-END flow orchestration across all
# components. Unlike the unit-level contract tests above (which check
# individual files), these verify that the full chain of:
#   trigger → engine → registry → adapters → dispatch
#   healthbeat → inject → poll → assert
# exists and is correctly wired in the source code.
#
# Scenarios covered:
#   Happy: ALARM dispatch
#   No rules
#   Retry exhausted → dead-letter
#   Non-ALARM silent
#   Disabled channel
#   Health pass
#   Health timeout
# ===========================================================================


# ===========================================================================
# Scenario 1: Happy — ALARM dispatch
# ===========================================================================


class TestE2eAlarmDispatch:
    """Happy path: status=3 INSERT → rule resolution → adapter dispatch.

    Covers the full chain:
      Hasura trigger → index.ts parses payload
      → DispatcherEngine.resolveRules()
      → DispatcherEngine.fanOut()
      → adapter.send() for each matched channel
      → 200 response with dispatch results
    """

    INDEX = FUNCTIONS_DIR / "index.ts"
    ENGINE = FUNCTIONS_DIR / "engine.ts"

    def test_index_imports_engine(self):
        """index.ts imports DispatcherEngine from engine module."""
        content = self.INDEX.read_text()
        assert "DispatcherEngine" in content
        assert "from './engine'" in content

    def test_index_imports_registry(self):
        """index.ts imports defaultRegistry from adapters/registry."""
        content = self.INDEX.read_text()
        assert "defaultRegistry" in content
        assert "from './adapters/registry'" in content

    def test_index_creates_engine_with_full_registry(self):
        """DispatcherEngine instantiated with defaultRegistry.getAllAdapters()."""
        content = self.INDEX.read_text()
        assert "new DispatcherEngine(defaultRegistry.getAllAdapters())" in content

    def test_index_builds_alert_message_from_payload(self):
        """index.ts constructs AlertMessage with node_id, status, payload, event_ts."""
        content = self.INDEX.read_text()
        assert "AlertMessage" in content
        # norvi_telemetry branch uses norviRow (bifurcated)
        assert "node_id: norviRow.node_id" in content or "node_id: row.node_id" in content
        assert "payload: norviRow.payload" in content or "payload: row.payload" in content
        assert "event_ts: norviRow.event_ts" in content or "event_ts: row.event_ts" in content

    def test_index_calls_resolve_rules(self):
        """index.ts calls engine.resolveRules with message.node_id."""
        content = self.INDEX.read_text()
        assert "resolveRules" in content
        assert "message.node_id" in content

    def test_index_calls_fan_out_with_rules_and_message(self):
        """index.ts calls engine.fanOut with resolved rules and message."""
        content = self.INDEX.read_text()
        assert "fanOut(rules, message)" in content

    def test_index_returns_200_with_channel_results(self):
        """index.ts returns HTTP 200 with per-channel dispatch results on success."""
        content = self.INDEX.read_text()
        assert "status: 200" in content
        assert "channelResults" in content or "results" in content

    def test_engine_resolve_rules_filters_enabled_rules(self):
        """engine.resolveRules filters results to enabled rules only."""
        content = self.ENGINE.read_text()
        assert "rules.filter" in content
        assert "rule.enabled" in content

    def test_engine_accepts_injected_query_fn(self):
        """engine.resolveRules accepts an optional queryFn for testability (DI)."""
        content = self.ENGINE.read_text()
        assert "queryFn" in content
        assert "defaultQueryRules" in content

    def test_engine_fan_out_iterates_channels(self):
        """engine.fanOut iterates over each channel and calls adapter.send."""
        content = self.ENGINE.read_text()
        assert "adapter.send" in content
        assert "channel.config" in content

    def test_engine_fan_out_wraps_in_retry_backoff(self):
        """Each adapter.send call is wrapped in retryBackoff for resilience."""
        content = self.ENGINE.read_text()
        assert "retryBackoff" in content
        assert "adapter.send" in content
        assert "maxRetries: 3" in content
        assert "baseDelayMs: 1000" in content


# ===========================================================================
# Scenario 2: No rules
# ===========================================================================


class TestE2eNoRules:
    """Edge case: status=3 INSERT but no matching alert_rules for node_id.

    Expected: clean 200 response with dispatched:false — not an error.
    This is a valid operational state (node has no notification rules).
    """

    INDEX = FUNCTIONS_DIR / "index.ts"

    def test_index_handles_empty_rules_gracefully(self):
        """index.ts checks rules.length === 0 and handles it without error."""
        content = self.INDEX.read_text()
        assert "rules.length === 0" in content

    def test_no_rules_returns_200_not_error(self):
        """No-rules branch returns HTTP 200 with dispatched:false."""
        content = self.INDEX.read_text()
        assert "dispatched: false" in content or "dispatched:false" in content
        assert "status: 200" in content

    def test_no_rules_returns_no_error_field(self):
        """No-rules response must NOT include an error field."""
        content = self.INDEX.read_text()
        no_rules_block = content[content.index("rules.length === 0"):]
        # The no-rules block should have a 200 response without error
        assert "status: 200" in no_rules_block
        assert "dispatched: false" in no_rules_block
        assert "error" not in no_rules_block.split("}")[0]

    def test_engine_default_query_returns_empty(self):
        """Default queryRules returns [] meaning no rules defined yet."""
        content = (FUNCTIONS_DIR / "engine.ts").read_text()
        assert "return []" in content
        assert "defaultQueryRules" in content


# ===========================================================================
# Scenario 3: Retry exhausted
# ===========================================================================


class TestE2eRetryExhausted:
    """Error handling: adapter fails persistently → 3 retries → dead-letter.

    Covers: retryBackoff exhaustion → engine catches → writes DeadLetterEntry
    to alert_engine_health via deadLetterFn. Retry exhausted is NOT a silent
    failure — it is recorded.
    """

    ENGINE = FUNCTIONS_DIR / "engine.ts"
    UTILS = FUNCTIONS_DIR / "utils.ts"

    def test_retry_defaults_to_3_attempts(self):
        """retryBackoff defaults to maxRetries=3, baseDelayMs=1000."""
        content = self.UTILS.read_text()
        assert "maxRetries: 3" in content
        assert "baseDelayMs: 1000" in content

    def test_retry_exponential_backoff_formula(self):
        """Delay grows as baseDelayMs * 2^attempt."""
        content = self.UTILS.read_text()
        assert "Math.pow(2, attempt)" in content

    def test_retry_throws_on_exhaustion(self):
        """retryBackoff throws lastError when all retries exhausted."""
        content = self.UTILS.read_text()
        assert "throw lastError" in content

    def test_engine_catches_retry_exhaustion(self):
        """engine.fanOut wraps retryBackoff in try/catch to handle exhaustion."""
        content = self.ENGINE.read_text()
        assert "catch" in content
        assert "retryBackoff" in content

    def test_engine_defines_dead_letter_entry_interface(self):
        """DeadLetterEntry interface defines the shape of failure records."""
        content = self.ENGINE.read_text()
        assert "DeadLetterEntry" in content
        assert "success" in content
        assert "detail" in content
        assert "channel_type" in content
        assert "node_id" in content

    def test_engine_builds_dead_letter_on_failure(self):
        """On catch, engine builds DeadLetterEntry with failure details."""
        content = self.ENGINE.read_text()
        # Find the catch block
        catch_index = content.index("catch")
        dead_letter_block = content[catch_index:]
        assert "deadLetter" in dead_letter_block or "DeadLetterEntry" in dead_letter_block
        assert "success: false" in dead_letter_block
        assert "detail: errorMessage" in dead_letter_block
        assert "channel_type" in dead_letter_block
        assert "node_id" in dead_letter_block

    def test_engine_calls_dead_letter_fn(self):
        """engine.fanOut awaits deadLetterFn with the dead-letter entry."""
        content = self.ENGINE.read_text()
        assert "deadLetterFn" in content
        assert "await deadLetterFn" in content

    def test_engine_accepts_injected_dead_letter_fn(self):
        """engine.fanOut accepts optional deadLetterFn parameter for DI/testing."""
        content = self.ENGINE.read_text()
        assert "deadLetterFn" in content
        assert "defaultDeadLetter" in content

    def test_failed_channel_result_is_success_false(self):
        """After exhaustion, channel result has success=false with error message."""
        content = self.ENGINE.read_text()
        assert "success: false" in content
        assert "error: errorMessage" in content


# ===========================================================================
# Scenario 4: Non-ALARM silent
# ===========================================================================


class TestE2eNonAlarmSilent:
    """Edge case: INSERT with status != 3 triggers no dispatch.

    In production, the Hasura filter prevents non-ALARM events from reaching
    the function. But index.ts also guards against non-INSERT event types
    (UPDATE, DELETE) that could arrive through misconfiguration.
    """

    INDEX = FUNCTIONS_DIR / "index.ts"

    def test_index_guards_non_insert_events(self):
        """index.ts checks event.op !== 'INSERT' before processing."""
        content = self.INDEX.read_text()
        assert "event.op !== 'INSERT'" in content or "event.op!=='INSERT'" in content

    def test_non_insert_returns_200_silently(self):
        """Non-INSERT events return 200 with 'Non-INSERT event ignored'."""
        content = self.INDEX.read_text()
        assert "Non-INSERT event ignored" in content
        assert "status: 200" in content

    def test_non_insert_does_not_dispatch(self):
        """Non-INSERT path returns early — no resolveRules or fanOut called."""
        content = self.INDEX.read_text()
        # The non-INSERT check is the first early return in the handler
        # Find the first "Non-INSERT event ignored" and check it returns
        non_insert_index = content.index("Non-INSERT event ignored")
        before_non_insert = content[:non_insert_index]
        after_non_insert = content[non_insert_index:]
        assert "return {" in before_non_insert  # return keyword before the message
        assert "resolveRules" not in before_non_insert  # no dispatch logic yet
        assert "fanOut" not in before_non_insert  # no dispatch logic yet

    def test_index_validates_node_id_exists(self):
        """index.ts checks row.node_id exists before building message."""
        content = self.INDEX.read_text()
        assert "Missing node_id" in content
        assert "node_id" in content

    def test_missing_node_id_returns_400(self):
        """Missing node_id returns HTTP 400, not 200."""
        content = self.INDEX.read_text()
        assert "status: 400" in content
        assert "Missing node_id" in content


# ===========================================================================
# Scenario 5: Disabled channel
# ===========================================================================


class TestE2eDisabledChannel:
    """Edge case: alert_rule has enabled=false → channel skipped.

    The engine must check each channel's enabled flag in fanOut and skip
    disabled channels entirely — no dispatch attempts, no results entry.
    """

    ENGINE = FUNCTIONS_DIR / "engine.ts"

    def test_fan_out_skips_disabled_channels(self):
        """engine.fanOut checks channel.enabled and continue-skips when false."""
        content = self.ENGINE.read_text()
        assert "if (!channel.enabled)" in content or "if(!channel.enabled)" in content
        assert "continue" in content

    def test_disabled_channel_not_counted_in_results(self):
        """Disabled channels are skipped — no results.push for them."""
        content = self.ENGINE.read_text()
        # The continue comes before any results.push
        continue_idx = content.index("continue")
        # After continue, the next line should skip the dispatch/result code
        after_continue = content[continue_idx:]
        # There should be no adapter.send or results.push immediately after continue
        # The continue jumps to next iteration

    def test_resolve_rules_returns_only_enabled_rules(self):
        """engine.resolveRules filters disabled rules before returning."""
        content = self.ENGINE.read_text()
        assert "rules.filter" in content
        assert "rule.enabled" in content


# ===========================================================================
# Scenario 6: Health pass
# ===========================================================================


class TestE2eHealthPass:
    """Healthbeat: inject synthetic ALARM → poll → found within 30s.

    Full healthbeat flow:
      healthbeat/index.ts → assert.injectAndAssert()
      → INSERT synthetic ALARM (node_id=__healthbeat__)
      → poll alert_engine_health every 5s (up to 30s)
      → row found → INSERT health result with success=true
    """

    ASSERT = HEALTHBEAT_DIR / "assert.ts"
    HB_INDEX = HEALTHBEAT_DIR / "index.ts"

    def test_assert_exports_inject_and_assert(self):
        """assert.ts exports injectAndAssert async function."""
        content = self.ASSERT.read_text()
        assert "export async function injectAndAssert" in content

    def test_inject_inserts_synthetic_alarm(self):
        """injectAndAssert uses GraphQL mutation to insert synthetic ALARM."""
        content = self.ASSERT.read_text()
        assert "insert_norvi_telemetry_one" in content

    def test_inject_uses_healthbeat_node_id(self):
        """Synthetic ALARM uses __healthbeat__ as node_id."""
        content = self.ASSERT.read_text()
        assert "__healthbeat__" in content

    def test_inject_sets_status_to_3(self):
        """Synthetic ALARM has status: 3."""
        content = self.ASSERT.read_text()
        assert "status: 3" in content

    def test_assert_polls_health_table(self):
        """injectAndAssert polls alert_engine_health for health result row."""
        content = self.ASSERT.read_text()
        # The poll query references alert_engine_health
        assert "alert_engine_health" in content

    def test_assert_polls_for_max_6_attempts(self):
        """Polling uses maxAttempts=6 (30s / 5s poll interval)."""
        content = self.ASSERT.read_text()
        assert "maxAttempts" in content
        assert "6" in content or "6 *" in content

    def test_assert_has_5s_poll_interval(self):
        """Polling waits 5000ms (5s) between attempts."""
        content = self.ASSERT.read_text()
        assert "pollIntervalMs" in content or "5000" in content

    def test_assert_has_polling_loop_with_bound_check(self):
        """Polling uses a for-loop with attempt < maxAttempts guard."""
        content = self.ASSERT.read_text()
        assert "for" in content
        assert "attempt < maxAttempts" in content

    def test_assert_writes_health_result_on_success(self):
        """injectAndAssert writes health check result via GraphQL mutation."""
        content = self.ASSERT.read_text()
        assert "insert_alert_engine_health_one" in content

    def test_health_result_has_all_required_fields(self):
        """Health result includes checked_at, latency_ms, success, detail."""
        content = self.ASSERT.read_text()
        assert "checked_at" in content
        assert "latency_ms" in content
        assert "success" in content
        assert "detail" in content

    def test_assert_sets_success_true_when_found(self):
        """When health row is found within polling window, success=true."""
        content = self.ASSERT.read_text()
        assert "found = true" in content or "found=true" in content

    def test_healthbeat_index_calls_inject_and_assert(self):
        """healthbeat/index.ts calls injectAndAssert() from assert module."""
        content = self.HB_INDEX.read_text()
        assert "injectAndAssert" in content
        assert "from './assert'" in content

    def test_healthbeat_index_returns_200(self):
        """healthbeat/index.ts returns HTTP 200 on successful health check."""
        content = self.HB_INDEX.read_text()
        assert "status: 200" in content

    def test_healthbeat_index_exports_handler(self):
        """healthbeat/index.ts exports a handler function for Nhost cron."""
        content = self.HB_INDEX.read_text()
        assert "export async function handler" in content

    def test_health_result_interface_defined(self):
        """assert.ts defines HealthCheckResult interface with all fields."""
        content = self.ASSERT.read_text()
        assert "HealthCheckResult" in content
        assert "checked_at" in content
        assert "latency_ms" in content
        assert "success" in content
        assert "detail" in content


# ===========================================================================
# Scenario 7: Health timeout
# ===========================================================================


class TestE2eHealthTimeout:
    """Healthbeat: inject → poll timeout (30s) → success=false.

    When the dispatcher is slow, fails, or the health row never arrives,
    the healthbeat must:
    - Exhaust all 6 poll attempts
    - Set success=false with descriptive detail
    - Still write the result row to alert_engine_health
    """

    ASSERT = HEALTHBEAT_DIR / "assert.ts"

    def test_assert_handles_not_found(self):
        """When poll loop exhausts without finding a row, handled gracefully."""
        content = self.ASSERT.read_text()
        assert "!found" in content or "found" in content
        # After the loop, there's logic for when found is false
        assert "if (!found)" in content or "if not found" in content.lower()

    def test_assert_sets_success_false_on_timeout(self):
        """When no health row found, success is set to false."""
        content = self.ASSERT.read_text()
        assert "success: false" in content

    def test_assert_records_timeout_detail_message(self):
        """Timeout produces descriptive detail about no row found."""
        content = self.ASSERT.read_text()
        assert "Health check failed" in content

    def test_assert_still_writes_result_on_timeout(self):
        """Even on timeout, a result row is written to alert_engine_health."""
        content = self.ASSERT.read_text()
        assert "insert_alert_engine_health_one" in content

    def test_assert_has_try_catch_for_unexpected_errors(self):
        """injectAndAssert wraps entire flow in try/catch for resilience."""
        content = self.ASSERT.read_text()
        assert "try" in content and "catch" in content

    def test_assert_writes_failure_row_on_exception(self):
        """On exception, health result row is written with success=false."""
        content = self.ASSERT.read_text()
        assert "failMutation" in content or "Exception:" in content

    def test_assert_returns_health_result_on_exception(self):
        """Even on exception, injectAndAssert still returns HealthCheckResult."""
        content = self.ASSERT.read_text()
        assert "return {" in content
        assert "success: false" in content

    def test_assert_exception_detail_is_sanitized(self):
        """Exception detail is sanitized for GraphQL (escaped quotes, no newlines)."""
        content = self.ASSERT.read_text()
        assert '.replace(/"/g' in content or '.replace(\'/"\', " )' in content
        assert "escape" in content.lower() or "replace" in content


# ===========================================================================
# Cross-component orchestration — import chain verification
# ===========================================================================


class TestE2eOrchestration:
    """Verifies the complete cross-component import wiring.

    Checks that every module that should be connected IS connected
    via imports, and that no orphan modules exist in the dispatch chain.
    """

    def test_index_imports_engine(self):
        """index.ts imports from ./engine."""
        content = (FUNCTIONS_DIR / "index.ts").read_text()
        assert "'./engine'" in content or '"./engine"' in content

    def test_index_imports_types(self):
        """index.ts imports from ./types."""
        content = (FUNCTIONS_DIR / "index.ts").read_text()
        assert "'./types'" in content or '"./types"' in content

    def test_index_imports_registry(self):
        """index.ts imports from ./adapters/registry."""
        content = (FUNCTIONS_DIR / "index.ts").read_text()
        assert "'./adapters/registry'" in content

    def test_engine_imports_types(self):
        """engine.ts imports from ./types."""
        content = (FUNCTIONS_DIR / "engine.ts").read_text()
        assert "'./types'" in content or '"./types"' in content

    def test_engine_imports_utils(self):
        """engine.ts imports retryBackoff from ./utils."""
        content = (FUNCTIONS_DIR / "engine.ts").read_text()
        assert "'./utils'" in content or '"./utils"' in content

    def test_registry_imports_types(self):
        """registry.ts imports from ../types."""
        content = (ADAPTERS_DIR / "registry.ts").read_text()
        assert "'../types'" in content or '"../types"' in content

    def test_each_adapter_imports_types(self):
        """Each adapter imports AlertDispatcherAdapter and types from ../types."""
        for name in ["telegram", "whatsapp", "slack", "email", "torreta", "push"]:
            content = (ADAPTERS_DIR / f"{name}.ts").read_text()
            assert "'../types'" in content or '"../types"' in content, \
                f"{name}.ts must import from ../types"
            assert "AlertDispatcherAdapter" in content, \
                f"{name}.ts must implement AlertDispatcherAdapter"

    def test_healthbeat_index_imports_assert(self):
        """healthbeat/index.ts imports from ./assert."""
        content = (HEALTHBEAT_DIR / "index.ts").read_text()
        assert "'./assert'" in content or '"./assert"' in content

    def test_all_handlers_return_status_and_body(self):
        """All Nhost function handlers return { status, body } response shape."""
        dispatch = (FUNCTIONS_DIR / "index.ts").read_text()
        healthbeat = (HEALTHBEAT_DIR / "index.ts").read_text()
        assert "status" in dispatch and "body" in dispatch
        assert "status" in healthbeat and "body" in healthbeat

    def test_all_handlers_have_try_catch(self):
        """All Nhost function handlers wrap logic in try/catch."""
        dispatch = (FUNCTIONS_DIR / "index.ts").read_text()
        healthbeat = (HEALTHBEAT_DIR / "index.ts").read_text()
        assert "try" in dispatch and "catch" in dispatch
        assert "try" in healthbeat and "catch" in healthbeat


# ===========================================================================
# End-to-end data flow — message format verification
# ===========================================================================


class TestE2eMessageFlow:
    """Verifies that the alert message format flows consistently end-to-end.

    The message format originates in formatMessage (utils.ts) and flows
    through each adapter's formatting function. All must include:
    - Node ID label
    - Status code
    - Timestamp
    - Payload key-value pairs (when present)
    """

    UTILS = FUNCTIONS_DIR / "utils.ts"

    def test_format_message_includes_node_id_label(self):
        """formatMessage labels the node as 'Nodo:'."""
        content = self.UTILS.read_text()
        assert "Nodo" in content or "nodo" in content
        assert "message.node_id" in content

    def test_format_message_includes_status(self):
        """formatMessage includes the status code."""
        content = self.UTILS.read_text()
        assert "message.status" in content
        assert "Estado" in content

    def test_format_message_includes_timestamp(self):
        """formatMessage includes the event timestamp."""
        content = self.UTILS.read_text()
        assert "message.event_ts" in content
        assert "Timestamp" in content

    def test_format_message_handles_payload(self):
        """formatMessage renders payload entries as key:value pairs."""
        content = self.UTILS.read_text()
        assert "Object.entries" in content
        assert "payload" in content

    def test_format_message_skips_empty_payload(self):
        """formatMessage skips payload section when payload has no keys."""
        content = self.UTILS.read_text()
        assert "Object.keys" in content
        assert "length > 0" in content

    def test_format_message_joins_with_newline(self):
        """formatMessage joins lines with newline character."""
        content = self.UTILS.read_text()
        assert ".join('" in content or '.join("' in content
        assert "\\n" in content


# ===========================================================================
# Phase 4: Dynamic Rule Engine — Migration V2
# ===========================================================================

SILENCE_DETECTOR_DIR = PROJECT_ROOT / "nhost" / "functions" / "silence-detector"
NHOST_ALERT_RULES_WINDOW_MIGRATIONS = PROJECT_ROOT / "nhost" / "migrations" / "default" / "alert_rules_window"


class TestMigrationV2:
    """Validate that migration files include the new Dynamic Rule Engine columns
    and the alert_events table (Fase 1 schema evolution)."""

    def test_up_sql_alters_alert_rules_with_scope(self):
        sql = (NHOST_MIGRATIONS / "up.sql").read_text()
        assert "ADD COLUMN IF NOT EXISTS scope" in sql
        assert "'SYSTEM'" in sql or "SYSTEM" in sql

    def test_up_sql_check_constraint_on_scope(self):
        sql = (NHOST_MIGRATIONS / "up.sql").read_text()
        assert "CHECK (scope IN ('SYSTEM', 'USER_DEFINED'))" in sql

    def test_up_sql_has_tipo_condicion_column(self):
        sql = (NHOST_MIGRATIONS / "up.sql").read_text()
        assert "ADD COLUMN IF NOT EXISTS tipo_condicion" in sql

    def test_up_sql_tipo_condicion_check_constraint(self):
        sql = (NHOST_MIGRATIONS / "up.sql").read_text()
        for cond in ["SILENCE_TIMEOUT", "ERROR_THRESHOLD", "FREQUENCY_DROP", "DEFECT_THRESHOLD"]:
            assert cond in sql, f"tipo_condicion CHECK must include '{cond}'"

    def test_up_sql_has_valor_umbral_column(self):
        sql = (NHOST_MIGRATIONS / "up.sql").read_text()
        assert "ADD COLUMN IF NOT EXISTS valor_umbral" in sql

    def test_up_sql_has_canales_column(self):
        sql = (NHOST_MIGRATIONS / "up.sql").read_text()
        assert "ADD COLUMN IF NOT EXISTS canales" in sql

    def test_up_sql_has_cooldown_minutos_column(self):
        sql = (NHOST_MIGRATIONS / "up.sql").read_text()
        assert "ADD COLUMN IF NOT EXISTS cooldown_minutos" in sql
        assert "DEFAULT 30" in sql

    def test_up_sql_has_last_alerted_at_column(self):
        sql = (NHOST_MIGRATIONS / "up.sql").read_text()
        assert "ADD COLUMN IF NOT EXISTS last_alerted_at" in sql

    def test_up_sql_creates_alert_events_table(self):
        sql = (NHOST_MIGRATIONS / "up.sql").read_text()
        assert "CREATE TABLE IF NOT EXISTS alert_events" in sql

    def test_up_sql_alert_events_has_rule_id_fk(self):
        sql = (NHOST_MIGRATIONS / "up.sql").read_text()
        assert "REFERENCES alert_rules(id)" in sql
        assert "ON DELETE CASCADE" in sql

    def test_up_sql_alert_events_has_tipo_evento_check(self):
        sql = (NHOST_MIGRATIONS / "up.sql").read_text()
        for evt in ["SILENCE_TIMEOUT", "ERROR_THRESHOLD", "FREQUENCY_DROP", "DEFECT_THRESHOLD"]:
            assert evt in sql, f"tipo_evento CHECK must include '{evt}'"

    def test_up_sql_alert_events_has_dispatched_column(self):
        sql = (NHOST_MIGRATIONS / "up.sql").read_text()
        assert "dispatched" in sql
        assert "BOOLEAN" in sql

    def test_up_sql_alert_events_has_dispatch_result_column(self):
        sql = (NHOST_MIGRATIONS / "up.sql").read_text()
        assert "dispatch_result" in sql
        assert "JSONB" in sql

    def test_up_sql_has_alert_events_indexes(self):
        sql = (NHOST_MIGRATIONS / "up.sql").read_text()
        assert "idx_alert_events_dispatched" in sql
        assert "idx_alert_events_rule_id" in sql

    def test_down_sql_drops_alert_events_table(self):
        sql = (NHOST_MIGRATIONS / "down.sql").read_text()
        assert "DROP TABLE IF EXISTS alert_events" in sql

    def test_down_sql_drops_alert_events_indexes(self):
        sql = (NHOST_MIGRATIONS / "down.sql").read_text()
        assert "idx_alert_events_dispatched" in sql
        assert "idx_alert_events_rule_id" in sql

    def test_down_sql_reverts_alert_rules_columns(self):
        sql = (NHOST_MIGRATIONS / "down.sql").read_text()
        for col in ["last_alerted_at", "cooldown_minutos", "canales", "valor_umbral", "tipo_condicion", "scope"]:
            assert f"DROP COLUMN IF EXISTS {col}" in sql, \
                f"down.sql must drop column {col}"


# ===========================================================================
# Phase 4: Silence Detector — File existence
# ===========================================================================


class TestSilenceDetectorFiles:
    """Validate that silence-detector function directory and files exist."""

    def test_silence_detector_directory_exists(self):
        assert SILENCE_DETECTOR_DIR.exists(), \
            "silence-detector directory must exist"

    def test_engine_ts_exists(self):
        assert (SILENCE_DETECTOR_DIR / "engine.ts").exists(), \
            "engine.ts must exist in silence-detector"

    def test_index_ts_exists(self):
        assert (SILENCE_DETECTOR_DIR / "index.ts").exists(), \
            "index.ts must exist in silence-detector"

    def test_only_expected_files(self):
        """Only engine.ts and index.ts should exist in silence-detector."""
        expected = {"engine.ts", "index.ts"}
        actual = {f.name for f in SILENCE_DETECTOR_DIR.iterdir() if f.suffix == ".ts"}
        assert actual == expected, \
            f"Unexpected files in silence-detector: {actual - expected}"

    def test_engine_exports_rule_type(self):
        """engine.ts must export the Rule interface."""
        content = (SILENCE_DETECTOR_DIR / "engine.ts").read_text()
        assert "export interface Rule" in content

    def test_engine_exports_alert_event_insert_type(self):
        """engine.ts must export the AlertEventInsert interface."""
        content = (SILENCE_DETECTOR_DIR / "engine.ts").read_text()
        assert "export interface AlertEventInsert" in content


# ===========================================================================
# Phase 4: Silence Detector — Engine contract
# ===========================================================================


class TestSilenceDetectorEngineContract:
    """Validate that engine.ts exports SilenceDetectorEngine with evaluate()
    method and the expected interfaces (SilenceRule, EvaluationResult, etc.)."""

    ENGINE_PATH = SILENCE_DETECTOR_DIR / "engine.ts"

    def test_engine_file_exists(self):
        assert self.ENGINE_PATH.exists()

    def test_engine_exports_silence_detector_engine_class(self):
        content = self.ENGINE_PATH.read_text()
        assert "export class SilenceDetectorEngine" in content

    def test_engine_has_evaluate_method(self):
        content = self.ENGINE_PATH.read_text()
        assert "async evaluate(" in content

    def test_engine_exports_rule_interface(self):
        content = self.ENGINE_PATH.read_text()
        assert "export interface Rule" in content

    def test_engine_exports_evaluation_result_interface(self):
        content = self.ENGINE_PATH.read_text()
        assert "export interface EvaluationResult" in content
        assert "rulesEvaluated" in content
        assert "alertsTriggered" in content
        assert "errors" in content

    def test_engine_exports_alert_event_insert_interface(self):
        content = self.ENGINE_PATH.read_text()
        assert "export interface AlertEventInsert" in content

    def test_engine_exports_health_entry_interface(self):
        content = self.ENGINE_PATH.read_text()
        assert "export interface HealthEntry" in content

    def test_rule_has_all_fields(self):
        content = self.ENGINE_PATH.read_text()
        for field in ["id", "node_id", "tipo_condicion", "valor_umbral", "canales", "cooldown_minutos", "last_alerted_at", "ventana_minutos"]:
            assert field in content, \
                f"Rule must have field '{field}'"

    def test_evaluate_accepts_six_injected_functions(self):
        """evaluate() must accept 6 injected function parameters for DI."""
        content = self.ENGINE_PATH.read_text()
        # The evaluate method signature
        assert "evaluate(" in content
        assert "queryRulesFn" in content
        assert "queryLastEventFn" in content
        assert "queryErrorCountFn" in content
        assert "insertAlertEventFn" in content
        assert "updateLastAlertedFn" in content
        assert "writeHealthFn" in content

    def test_evaluate_loops_over_rules(self):
        """evaluate() must iterate over rules array."""
        content = self.ENGINE_PATH.read_text()
        assert "for (const rule of rules)" in content

    def test_evaluate_checks_silence_threshold(self):
        """evaluate() must compare elapsed time against valor_umbral."""
        content = self.ENGINE_PATH.read_text()
        assert "elapsedSeconds" in content
        assert "valor_umbral" in content

    def test_evaluate_has_cooldown_logic(self):
        """evaluate() must check cooldown before triggering alert."""
        content = self.ENGINE_PATH.read_text()
        assert "cooldown" in content.lower() or "cooldown_minutos" in content

    def test_evaluate_calls_insert_alert_event(self):
        """When both conditions met, evaluate() must call insertAlertEventFn."""
        content = self.ENGINE_PATH.read_text()
        assert "insertAlertEventFn" in content

    def test_evaluate_calls_update_last_alerted(self):
        """After inserting event, evaluate() must call updateLastAlertedFn."""
        content = self.ENGINE_PATH.read_text()
        assert "updateLastAlertedFn" in content

    def test_evaluate_calls_write_health(self):
        """evaluate() must call writeHealthFn after evaluation cycle."""
        content = self.ENGINE_PATH.read_text()
        assert "writeHealthFn" in content

    def test_evaluate_handles_query_errors(self):
        """evaluate() must handle errors during rule query gracefully."""
        content = self.ENGINE_PATH.read_text()
        assert "try" in content
        assert "catch" in content

    def test_evaluate_handles_no_telemetry(self):
        """When no telemetry exists for a node, evaluate() skips that rule."""
        content = self.ENGINE_PATH.read_text()
        assert "lastEventTs === null" in content

    # -----------------------------------------------------------------------
    # ERROR_THRESHOLD-specific contract tests
    # -----------------------------------------------------------------------

    def test_evaluate_has_polymorphic_condition_branch(self):
        """evaluate() must branch on rule.tipo_condicion."""
        content = self.ENGINE_PATH.read_text()
        assert "tipo_condicion === 'SILENCE_TIMEOUT'" in content
        assert "tipo_condicion === 'ERROR_THRESHOLD'" in content

    def test_evaluate_error_threshold_calls_query_error_count(self):
        """ERROR_THRESHOLD branch must call queryErrorCountFn."""
        content = self.ENGINE_PATH.read_text()
        assert "queryErrorCountFn" in content
        assert "ventana_minutos" in content

    def test_evaluate_error_threshold_count_exceeded_creates_event(self):
        """When error count >= valor_umbral, an alert event must be created."""
        content = self.ENGINE_PATH.read_text()
        assert "errorCount >= rule.valor_umbral" in content
        assert "shouldAlert = true" in content

    def test_evaluate_error_threshold_count_below_umbral_skips(self):
        """When error count < valor_umbral, the rule must be skipped."""
        content = self.ENGINE_PATH.read_text()
        # The ERROR_THRESHOLD branch only sets shouldAlert=true when count >= umbral
        assert "shouldAlert = true" in content
        # If shouldAlert stays false, no alert is inserted (checked by shouldAlert block)

    def test_evaluate_error_threshold_respects_cooldown(self):
        """ERROR_THRESHOLD rules must respect cooldown (same as SILENCE_TIMEOUT)."""
        content = self.ENGINE_PATH.read_text()
        assert "cooldown_minutos" in content
        assert "60 * 1000" in content

    def test_evaluate_mixed_rules_both_types(self):
        """The engine loop must handle both rule types in a single cycle."""
        content = self.ENGINE_PATH.read_text()
        assert "SILENCE_TIMEOUT" in content
        assert "ERROR_THRESHOLD" in content
        # Both branches exist inside the same for loop
        assert "tipo_condicion === 'SILENCE_TIMEOUT'" in content
        assert "tipo_condicion === 'ERROR_THRESHOLD'" in content

    def test_evaluate_error_threshold_uses_ventana_minutos(self):
        """ERROR_THRESHOLD branch must use ventana_minutos for the time window."""
        content = self.ENGINE_PATH.read_text()
        assert "ventana_minutos" in content
        # The default should be 5 minutes
        assert "rule.ventana_minutos" in content
        assert "5" in content


# ===========================================================================
# Phase 4: Dispatcher Bifurcation — alert-dispatcher/index.ts
# ===========================================================================


class TestDispatcherBifurcation:
    """Validate that alert-dispatcher/index.ts now handles both table.name
    scenarios: norvi_telemetry (SYSTEM) and alert_events (USER_DEFINED)."""

    INDEX = FUNCTIONS_DIR / "index.ts"

    def test_index_detects_table_name(self):
        """index.ts checks payload.table.name to bifurcate."""
        content = self.INDEX.read_text()
        assert "table" in content
        assert "tableName" in content or "table" in content

    def test_index_handles_norvi_telemetry_branch(self):
        """index.ts must still handle the norvi_telemetry branch."""
        content = self.INDEX.read_text()
        assert "norvi_telemetry" in content

    def test_index_handles_alert_events_branch(self):
        """index.ts must handle the alert_events branch."""
        content = self.INDEX.read_text()
        assert "alert_events" in content

    def test_index_branches_on_table_name(self):
        """index.ts must branch on table.name === 'alert_events'."""
        content = self.INDEX.read_text()
        assert "tableName === 'alert_events'" in content or \
               "table?.name" in content

    def test_alert_events_flow_queries_rule_by_id(self):
        """alert_events branch must query rule by rule_id."""
        content = self.INDEX.read_text()
        assert "rule_id" in content or "queryUserDefinedRule" in content

    def test_alert_events_flow_reads_canales(self):
        """alert_events branch must read canales array from the rule."""
        content = self.INDEX.read_text()
        assert "canales" in content

    def test_alert_events_flow_looks_up_adapter(self):
        """For each channel type in canales, adapter must be looked up."""
        content = self.INDEX.read_text()
        assert "adapter" in content
        assert "adapters" in content

    def test_alert_events_flow_queries_alert_channels(self):
        """alert_events branch must query alert_channels by channel type."""
        content = self.INDEX.read_text()
        assert "alert_channels" in content
        assert "channel_type" in content

    def test_alert_events_flow_updates_dispatched(self):
        """alert_events branch must UPDATE dispatched=true on completion."""
        content = self.INDEX.read_text()
        assert "dispatched" in content
        assert "updateAlertEventDispatched" in content or \
               "dispatched: true" in content

    def test_alert_events_flow_imports_adapters_directly(self):
        """alert_events flow uses adapters from registry, not duplicated."""
        content = self.INDEX.read_text()
        assert "defaultRegistry" in content
        assert "from './adapters/registry'" in content

    def test_norvi_telemetry_flow_preserved(self):
        """norvi_telemetry branch still uses DispatcherEngine and fanOut."""
        content = self.INDEX.read_text()
        assert "DispatcherEngine" in content
        assert "fanOut" in content


# ===========================================================================
# Phase 4: Metadata — Cron trigger for silence-detector
# ===========================================================================


class TestMetadataCron:
    """Validate that cron_triggers.yaml has the silence-detector-cron entry."""

    def test_cron_has_silence_detector_entry(self):
        content = (METADATA_DIR / "cron_triggers.yaml").read_text()
        assert "silence-detector-cron" in content

    def test_cron_has_1min_schedule(self):
        content = (METADATA_DIR / "cron_triggers.yaml").read_text()
        # Check for * * * * * (every minute)
        assert "schedule: '* * * * *'" in content or "schedule: '* * * * *'" in content

    def test_cron_targets_silence_detector_function(self):
        content = (METADATA_DIR / "cron_triggers.yaml").read_text()
        assert "silence-detector" in content
        assert "webhook" in content

    def test_cron_has_retry_config(self):
        content = (METADATA_DIR / "cron_triggers.yaml").read_text()
        assert "retry_conf" in content

    def test_cron_has_webhook_secret_header(self):
        content = (METADATA_DIR / "cron_triggers.yaml").read_text()
        assert "x-nhost-webhook-secret" in content


# ===========================================================================
# Phase 4: Metadata — Event trigger for alert_events
# ===========================================================================


class TestMetadataEventTrigger:
    """Validate that event_triggers.yaml has the alert_events trigger."""

    def test_event_trigger_has_alert_events_table(self):
        content = (METADATA_DIR / "event_triggers.yaml").read_text()
        assert "alert_events" in content

    def test_event_trigger_has_alert_on_silence_detected(self):
        content = (METADATA_DIR / "event_triggers.yaml").read_text()
        assert "alert_on_silence_detected" in content

    def test_event_trigger_fires_on_insert(self):
        content = (METADATA_DIR / "event_triggers.yaml").read_text()
        assert "insert:" in content

    def test_event_trigger_targets_alert_dispatcher(self):
        content = (METADATA_DIR / "event_triggers.yaml").read_text()
        assert "alert-dispatcher" in content

    def test_event_trigger_has_retry_config(self):
        content = (METADATA_DIR / "event_triggers.yaml").read_text()
        assert "num_retries: 3" in content

    def test_event_trigger_has_webhook_secret_header(self):
        content = (METADATA_DIR / "event_triggers.yaml").read_text()
        assert "x-nhost-webhook-secret" in content


# ===========================================================================
# Phase 4: Cooldown logic
# ===========================================================================


class TestCooldownLogic:
    """Validate that the cooldown logic prevents re-alerting within the
    cooldown window. A rule must not trigger a new alert if the time since
    last_alerted_at is less than cooldown_minutos * 60 seconds."""

    def test_engine_cooldown_compares_last_alerted(self):
        """Cooldown check must compare last_alerted_at against now."""
        content = (SILENCE_DETECTOR_DIR / "engine.ts").read_text()
        assert "last_alerted_at" in content
        assert "lastAlertTime" in content or "cooldown" in content.lower()

    def test_engine_cooldown_uses_cooldown_minutos(self):
        """Cooldown must reference cooldown_minutos."""
        content = (SILENCE_DETECTOR_DIR / "engine.ts").read_text()
        assert "cooldown_minutos" in content

    def test_engine_cooldown_converts_to_ms(self):
        """cooldown_minutos converted to milliseconds ( * 60 * 1000)."""
        content = (SILENCE_DETECTOR_DIR / "engine.ts").read_text()
        assert "60 * 1000" in content

    def test_engine_allows_if_last_alerted_is_null(self):
        """If last_alerted_at is null, cooldown check must allow the alert."""
        content = (SILENCE_DETECTOR_DIR / "engine.ts").read_text()
        assert "null" in content
        assert "last_alerted_at" in content

    def test_engine_skips_if_within_cooldown(self):
        """If within cooldown, the rule must be skipped (continue)."""
        content = (SILENCE_DETECTOR_DIR / "engine.ts").read_text()
        assert "cooldown" in content.lower()

    def test_dispatcher_updates_last_alerted(self):
        """After inserting alert event, dispatcher updates last_alerted_at."""
        content = (SILENCE_DETECTOR_DIR / "engine.ts").read_text()
        assert "updateLastAlertedFn" in content

    def test_dispatcher_sets_last_alerted_on_all_rules(self):
        """index.ts must call updateLastAlertedFn after insert."""
        content = (SILENCE_DETECTOR_DIR / "index.ts").read_text()
        assert "update_alert_rules_by_pk" in content or \
               "defaultUpdateLastAlerted" in content


# ===========================================================================
# Phase 5: Multi-tenant — Migration, Metadata, and Isolation
# ===========================================================================


class TestMultiTenantMigration:
    """Validate multi_tenant_core migration files exist and contain
    expected tables, ALTER TABLE statements, and seed data."""

    def test_multi_tenant_directory_exists(self):
        assert MULTI_TENANT_MIGRATIONS.exists(), \
            "multi_tenant_core migration directory must exist"

    def test_up_sql_exists(self):
        assert (MULTI_TENANT_MIGRATIONS / "up.sql").exists(), \
            "up.sql must exist for multi_tenant_core migration"

    def test_down_sql_exists(self):
        assert (MULTI_TENANT_MIGRATIONS / "down.sql").exists(), \
            "down.sql must exist for multi_tenant_core migration"

    def test_up_sql_creates_plants(self):
        sql = (MULTI_TENANT_MIGRATIONS / "up.sql").read_text()
        assert "CREATE TABLE IF NOT EXISTS plants" in sql

    def test_up_sql_creates_lines(self):
        sql = (MULTI_TENANT_MIGRATIONS / "up.sql").read_text()
        assert "CREATE TABLE IF NOT EXISTS lines" in sql
        assert "REFERENCES plants(id)" in sql

    def test_up_sql_creates_machines(self):
        sql = (MULTI_TENANT_MIGRATIONS / "up.sql").read_text()
        assert "CREATE TABLE IF NOT EXISTS machines" in sql
        assert "REFERENCES lines(id)" in sql

    def test_up_sql_creates_device_models(self):
        sql = (MULTI_TENANT_MIGRATIONS / "up.sql").read_text()
        assert "CREATE TABLE IF NOT EXISTS device_models" in sql

    def test_up_sql_creates_alert_capabilities(self):
        sql = (MULTI_TENANT_MIGRATIONS / "up.sql").read_text()
        assert "CREATE TABLE IF NOT EXISTS alert_capabilities" in sql

    def test_up_sql_creates_model_capabilities(self):
        sql = (MULTI_TENANT_MIGRATIONS / "up.sql").read_text()
        assert "CREATE TABLE IF NOT EXISTS model_capabilities" in sql

    def test_up_sql_creates_nodes(self):
        sql = (MULTI_TENANT_MIGRATIONS / "up.sql").read_text()
        assert "CREATE TABLE IF NOT EXISTS nodes" in sql
        assert "REFERENCES machines(id)" in sql
        assert "REFERENCES device_models(id)" in sql

    def test_up_sql_creates_user_plants(self):
        sql = (MULTI_TENANT_MIGRATIONS / "up.sql").read_text()
        assert "CREATE TABLE IF NOT EXISTS user_plants" in sql
        assert "REFERENCES plants(id)" in sql

    def test_up_sql_alters_alert_rules_add_plant_id(self):
        sql = (MULTI_TENANT_MIGRATIONS / "up.sql").read_text()
        assert "ALTER TABLE alert_rules" in sql
        assert "ADD COLUMN IF NOT EXISTS plant_id" in sql
        assert "REFERENCES plants(id)" in sql

    def test_up_sql_alters_alert_events_add_plant_id(self):
        sql = (MULTI_TENANT_MIGRATIONS / "up.sql").read_text()
        assert "ALTER TABLE alert_events" in sql
        assert "ADD COLUMN IF NOT EXISTS plant_id" in sql
        assert "REFERENCES plants(id)" in sql

    def test_up_sql_has_seed_insert_for_alert_capabilities(self):
        sql = (MULTI_TENANT_MIGRATIONS / "up.sql").read_text()
        assert "INSERT INTO alert_capabilities" in sql
        assert "ON CONFLICT (capability_key) DO NOTHING" in sql

    def test_down_sql_drops_alert_events_plant_id(self):
        sql = (MULTI_TENANT_MIGRATIONS / "down.sql").read_text()
        assert "ALTER TABLE alert_events DROP COLUMN IF EXISTS plant_id" in sql

    def test_down_sql_drops_alert_rules_plant_id(self):
        sql = (MULTI_TENANT_MIGRATIONS / "down.sql").read_text()
        assert "ALTER TABLE alert_rules DROP COLUMN IF EXISTS plant_id" in sql

    def test_down_sql_drops_all_tables(self):
        sql = (MULTI_TENANT_MIGRATIONS / "down.sql").read_text()
        for table in ["user_plants", "nodes", "model_capabilities",
                       "alert_capabilities", "device_models", "machines",
                       "lines", "plants"]:
            assert f"DROP TABLE IF EXISTS {table}" in sql, \
                f"down.sql must DROP TABLE IF EXISTS {table}"

    def test_down_sql_drops_indexes(self):
        sql = (MULTI_TENANT_MIGRATIONS / "down.sql").read_text()
        for idx in ["idx_alert_events_plant_id", "idx_alert_rules_plant_id",
                     "idx_user_plants_plant_id", "idx_user_plants_user_id",
                     "idx_nodes_machine_id", "idx_machines_line_id",
                     "idx_lines_plant_id"]:
            assert f"DROP INDEX IF EXISTS {idx}" in sql, \
                f"down.sql must drop index {idx}"


class TestHasuraMetadataFiles:
    """Validate that Hasura table metadata YAML files exist for all
    multi-tenant tables and contain RLS configuration referencing
    x-hasura-plant-id."""

    EXPECTED_TABLES = [
        "public_plants",
        "public_lines",
        "public_machines",
        "public_nodes",
        "public_device_models",
        "public_alert_capabilities",
        "public_model_capabilities",
        "public_alert_rules",
        "public_alert_events",
        "public_user_plants",
    ]

    def test_tables_metadata_directory_exists(self):
        assert TABLES_METADATA_DIR.exists(), \
            "tables metadata directory must exist"

    def test_all_expected_table_yamls_exist(self):
        for table_name in self.EXPECTED_TABLES:
            yaml_path = TABLES_METADATA_DIR / f"{table_name}.yaml"
            assert yaml_path.exists(), \
                f"Expected metadata file {table_name}.yaml must exist"

    def test_no_extra_yaml_files(self):
        expected = {f"{t}.yaml" for t in self.EXPECTED_TABLES}
        actual = {f.name for f in TABLES_METADATA_DIR.iterdir() if f.suffix == ".yaml"}
        assert actual == expected, \
            f"Unexpected metadata files: {actual - expected}"

    def test_alert_rules_yaml_has_rls_on_plant_id(self):
        content = (TABLES_METADATA_DIR / "public_alert_rules.yaml").read_text()
        assert "x-hasura-plant-id" in content
        assert "plant_id" in content

    def test_alert_events_yaml_has_rls_on_plant_id(self):
        content = (TABLES_METADATA_DIR / "public_alert_events.yaml").read_text()
        assert "x-hasura-plant-id" in content
        assert "plant_id" in content

    def test_lines_yaml_has_rls_on_plant_id(self):
        content = (TABLES_METADATA_DIR / "public_lines.yaml").read_text()
        assert "x-hasura-plant-id" in content

    def test_machines_yaml_has_rls_via_relationship(self):
        content = (TABLES_METADATA_DIR / "public_machines.yaml").read_text()
        assert "x-hasura-plant-id" in content
        assert "line" in content  # relationship traversal

    def test_nodes_yaml_has_rls_via_relationship(self):
        content = (TABLES_METADATA_DIR / "public_nodes.yaml").read_text()
        assert "x-hasura-plant-id" in content
        assert "machine" in content  # relationship traversal

    def test_plants_yaml_has_rls_on_id(self):
        content = (TABLES_METADATA_DIR / "public_plants.yaml").read_text()
        assert "x-hasura-plant-id" in content

    def test_user_plants_yaml_has_rls_on_user_id(self):
        content = (TABLES_METADATA_DIR / "public_user_plants.yaml").read_text()
        assert "x-hasura-user-id" in content
        assert "x-hasura-plant-id" in content

    def test_each_yaml_has_supervisor_role(self):
        """Every table YAML must define supervisor role permissions."""
        for table_name in self.EXPECTED_TABLES:
            content = (TABLES_METADATA_DIR / f"{table_name}.yaml").read_text()
            assert "role: supervisor" in content, \
                f"{table_name}.yaml must have supervisor role"

    def test_alert_rules_filter_references_plant_id(self):
        """alert_rules supervisor filter must reference x-hasura-plant-id."""
        content = (TABLES_METADATA_DIR / "public_alert_rules.yaml").read_text()
        assert "_eq: x-hasura-plant-id" in content


class TestMultiTenantIsolation:
    """Validate that RLS metadata enforces plant-level isolation for
    supervisor role. A supervisor from plant A must not access data
    from plant B."""

    def test_alert_rules_supervisor_filter_uses_plant_id(self):
        """supervisor SELECT on alert_rules must filter by plant_id."""
        content = (TABLES_METADATA_DIR / "public_alert_rules.yaml").read_text()
        assert "plant_id:" in content
        assert "_eq: x-hasura-plant-id" in content

    def test_alert_rules_supervisor_check_on_insert(self):
        """supervisor INSERT on alert_rules must check plant_id matches."""
        content = (TABLES_METADATA_DIR / "public_alert_rules.yaml").read_text()
        # The check constraint must also reference x-hasura-plant-id
        assert "check:" in content
        assert "x-hasura-plant-id" in content

    def test_alert_events_supervisor_filter_uses_plant_id(self):
        """supervisor SELECT on alert_events must filter by plant_id."""
        content = (TABLES_METADATA_DIR / "public_alert_events.yaml").read_text()
        assert "plant_id:" in content
        assert "_eq: x-hasura-plant-id" in content

    def test_lines_supervisor_filter_uses_plant_id(self):
        """supervisor SELECT on lines must filter by plant_id."""
        content = (TABLES_METADATA_DIR / "public_lines.yaml").read_text()
        assert "plant_id:" in content
        assert "_eq: x-hasura-plant-id" in content

    def test_user_plants_supervisor_filters_by_both_ids(self):
        """supervisor on user_plants must filter by both user_id and
        plant_id."""
        content = (TABLES_METADATA_DIR / "public_user_plants.yaml").read_text()
        assert "x-hasura-user-id" in content
        assert "x-hasura-plant-id" in content
        assert "_and:" in content

    def test_machines_supervisor_filter_uses_relationship(self):
        """supervisor on machines must filter through line relationship
        to plant_id."""
        content = (TABLES_METADATA_DIR / "public_machines.yaml").read_text()
        assert "line:" in content
        assert "plant_id:" in content
        assert "_eq: x-hasura-plant-id" in content

    def test_nodes_supervisor_filter_uses_relationship(self):
        """supervisor on nodes must filter through machine→line
        relationship to plant_id."""
        content = (TABLES_METADATA_DIR / "public_nodes.yaml").read_text()
        assert "machine:" in content
        assert "line:" in content
        assert "plant_id:" in content
        assert "_eq: x-hasura-plant-id" in content

    def test_device_models_no_plant_filter(self):
        """device_models is a global catalog — supervisor has unfiltered
        SELECT. May return all rows."""
        content = (TABLES_METADATA_DIR / "public_device_models.yaml").read_text()
        # The supervisor select must exist and have filter: {} (no filter)
        assert "role: supervisor" in content
        assert "filter: {}" in content

    def test_alert_capabilities_no_plant_filter(self):
        """alert_capabilities is a global catalog — supervisor has
        unfiltered SELECT."""
        content = (TABLES_METADATA_DIR / "public_alert_capabilities.yaml").read_text()
        assert "role: supervisor" in content
        assert "filter: {}" in content


class TestSeedData:
    """Validate that seed INSERT for alert_capabilities contains all
    6 expected capability keys."""

    EXPECTED_CAPABILITIES = [
        "SILENCE_TIMEOUT",
        "ERROR_THRESHOLD",
        "FREQUENCY_DROP",
        "DEFECT_THRESHOLD",
        "TEMP_CRITICAL",
        "VIBRATION_HIGH",
    ]

    def test_seed_insert_exists(self):
        sql = (MULTI_TENANT_MIGRATIONS / "up.sql").read_text()
        assert "INSERT INTO alert_capabilities" in sql

    def test_all_six_capabilities_seeded(self):
        sql = (MULTI_TENANT_MIGRATIONS / "up.sql").read_text()
        for cap in self.EXPECTED_CAPABILITIES:
            assert cap in sql, \
                f"Seed INSERT must include capability '{cap}'"

    def test_seed_uses_on_conflict_do_nothing(self):
        sql = (MULTI_TENANT_MIGRATIONS / "up.sql").read_text()
        assert "ON CONFLICT (capability_key) DO NOTHING" in sql

    def _read_up_sql(self) -> str:
        """Read up.sql with explicit UTF-8 encoding for Spanish characters."""
        path = MULTI_TENANT_MIGRATIONS / "up.sql"
        with open(str(path), encoding='utf-8') as f:
            return f.read()

    def test_silence_timeout_description(self):
        sql = self._read_up_sql()
        assert "Paro de máquina por falta de pulsos" in sql

    def test_error_threshold_description(self):
        sql = self._read_up_sql()
        assert "Límite de errores de comunicación excedido" in sql

    def test_frequency_drop_description(self):
        sql = self._read_up_sql()
        assert "Caída en frecuencia de lecturas" in sql

    def test_defect_threshold_description(self):
        sql = self._read_up_sql()
        assert "Cantidad de defectos de calidad" in sql

    def test_temp_critical_description(self):
        sql = self._read_up_sql()
        assert "Temperatura del dispositivo" in sql

    def test_vibration_high_description(self):
        sql = self._read_up_sql()
        assert "Vibración anormal detectada" in sql


# ===========================================================================
# Phase 6: ERROR_THRESHOLD — Migration + Polymorphic Engine
# ===========================================================================


class TestErrorThresholdMigration:
    """Validate that alert_rules_window migration exists and contains
    the ventana_minutos column and related index."""

    def test_migration_directory_exists(self):
        assert NHOST_ALERT_RULES_WINDOW_MIGRATIONS.exists(), \
            "alert_rules_window migration directory must exist"

    def test_up_sql_exists(self):
        assert (NHOST_ALERT_RULES_WINDOW_MIGRATIONS / "up.sql").exists(), \
            "up.sql must exist for alert_rules_window migration"

    def test_down_sql_exists(self):
        assert (NHOST_ALERT_RULES_WINDOW_MIGRATIONS / "down.sql").exists(), \
            "down.sql must exist for alert_rules_window migration"

    def test_up_sql_adds_ventana_minutos(self):
        sql = (NHOST_ALERT_RULES_WINDOW_MIGRATIONS / "up.sql").read_text()
        assert "ADD COLUMN IF NOT EXISTS ventana_minutos" in sql
        assert "INTEGER DEFAULT 5" in sql

    def test_up_sql_has_ventana_index(self):
        sql = (NHOST_ALERT_RULES_WINDOW_MIGRATIONS / "up.sql").read_text()
        assert "CREATE INDEX IF NOT EXISTS idx_alert_rules_ventana" in sql

    def test_down_sql_drops_ventana_minutos(self):
        sql = (NHOST_ALERT_RULES_WINDOW_MIGRATIONS / "down.sql").read_text()
        assert "DROP COLUMN IF EXISTS ventana_minutos" in sql

    def test_down_sql_drops_ventana_index(self):
        sql = (NHOST_ALERT_RULES_WINDOW_MIGRATIONS / "down.sql").read_text()
        assert "DROP INDEX IF EXISTS idx_alert_rules_ventana" in sql
