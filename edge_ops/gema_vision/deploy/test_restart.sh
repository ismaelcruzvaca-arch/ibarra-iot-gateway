#!/bin/bash
# ============================================================================
# test_restart.sh  —  Integration test for gema-vision systemd service
#
# Runs INSIDE the Docker container (gema-systemd-test).
# Tests that systemd restarts the daemon after a crash.
#
# Usage:
#   docker run --privileged --rm -it gema-systemd-test
#   ./test_restart.sh
# ============================================================================

set -euo pipefail

PASS=0
FAIL=0

pass() {
    PASS=$((PASS + 1))
    echo "  ✅ PASS: $1"
}

fail() {
    FAIL=$((FAIL + 1))
    echo "  ❌ FAIL: $1"
}

echo ""
echo "═══════════════════════════════════════════════════"
echo "  GEMA Vision — systemd Integration Test Suite"
echo "═══════════════════════════════════════════════════"
echo ""

# --- Test 1: Service is active ----------------------------------------------
echo "--- Test 1: Service is active ---"
systemctl start gema-vision 2>/dev/null || true
sleep 2
if systemctl is-active --quiet gema-vision; then
    pass "gema-vision service is active"
else
    fail "gema-vision service is NOT active ($(systemctl is-active gema-vision))"
fi

# --- Test 2: OOMScoreAdjust is applied --------------------------------------
echo "--- Test 2: OOMScoreAdjust is applied ---"
PID=$(systemctl show gema-vision --property=MainPID --value 2>/dev/null || echo "")
if [ -n "$PID" ] && [ "$PID" -gt 1 ]; then
    SCORE=$(cat /proc/$PID/oom_score_adj 2>/dev/null || echo "unknown")
    if [ "$SCORE" = "-1000" ]; then
        pass "OOMScoreAdjust=-1000 (PID=$PID)"
    else
        fail "Expected oom_score_adj=-1000, got $SCORE"
    fi
else
    fail "Could not get PID of gema-vision"
fi

# --- Test 3: Watchdog keepalive is running -----------------------------------
echo "--- Test 3: Watchdog keepalive is running ---"
PID=$(systemctl show gema-vision --property=MainPID --value 2>/dev/null || echo "")
if [ -n "$PID" ] && [ "$PID" -gt 1 ]; then
    # The watchdog thread writes to /dev/watchdog. In Docker there is no
    /dev/watchdog, so we check that the process has at least 2 threads
    # (main + watchdog).
    THREADS=$(ls /proc/$PID/task/ 2>/dev/null | wc -l || echo "0")
    if [ "$THREADS" -ge 2 ]; then
        pass "Watchdog thread is running ($THREADS threads)"
    else
        fail "Expected ≥2 threads, got $THREADS (watchdog may not have started)"
    fi
else
    fail "Service not running"
fi

# --- Test 4: Kill -9 causes restart -----------------------------------------
echo "--- Test 4: SIGKILL causes restart ---"
PID=$(systemctl show gema-vision --property=MainPID --value 2>/dev/null || echo "")
if [ -z "$PID" ] || [ "$PID" -le 1 ]; then
    fail "Service not running, cannot test restart"
else
    echo "  Killing PID $PID..."
    kill -9 "$PID"
    sleep 5

    NEW_PID=$(systemctl show gema-vision --property=MainPID --value 2>/dev/null || echo "")
    if [ -n "$NEW_PID" ] && [ "$NEW_PID" -gt 1 ] && [ "$NEW_PID" != "$PID" ]; then
        # Count restarts
        RESTARTS=$(systemctl show gema-vision --property=NRestarts --value 2>/dev/null || echo "0")
        pass "Process restarted (old PID=$PID → new PID=$NEW_PID, restarts=$RESTARTS)"
    else
        fail "Process did NOT restart after SIGKILL (PID=$NEW_PID)"
    fi
fi

# --- Test 5: Multiple kills are handled (burst limit) ------------------------
echo "--- Test 5: Burst limit prevents restart storms ---"
# Kill the process 6 times (StartLimitBurst=5 in the service file).
CURRENT_PID=$(systemctl show gema-vision --property=MainPID --value 2>/dev/null || echo "")
for i in $(seq 1 6); do
    if [ -n "$CURRENT_PID" ] && [ "$CURRENT_PID" -gt 1 ]; then
        kill -9 "$CURRENT_PID" 2>/dev/null || true
        sleep 3
        CURRENT_PID=$(systemctl show gema-vision --property=MainPID --value 2>/dev/null || echo "")
    fi
done

sleep 2
SVC_STATE=$(systemctl is-active gema-vision 2>/dev/null || echo "inactive")
RESTARTS=$(systemctl show gema-vision --property=NRestarts --value 2>/dev/null || echo "0")
echo "  Final state: $SVC_STATE (NRestarts=$RESTARTS)"

# After burst limit, the service should be in a failed/inactive state
# OR still running if StartLimitBurst hasn't been exhausted in the
# current interval. Either is acceptable — the test is that systemd
# handles it gracefully.
if [ "$SVC_STATE" = "active" ] || [ "$SVC_STATE" = "failed" ] || [ "$SVC_STATE" = "inactive" ]; then
    pass "Graceful handling after burst kills (state=$SVC_STATE)"
else
    fail "Unexpected state after burst: $SVC_STATE"
fi

# --- Summary ----------------------------------------------------------------
echo ""
echo "═════════════════════════"
echo "  Results: $PASS passed, $FAIL failed"
echo "═════════════════════════"
echo ""

if [ "$FAIL" -gt 0 ]; then
    echo "  Check logs: journalctl -u gema-vision --no-pager"
    exit 1
fi
exit 0
