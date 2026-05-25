#!/bin/sh
# ============================================================================
# fat_wdt_test.sh  —  Watchdog Reset Certification (Destructive Test)
#
# WARNING: This test WILL reset the board.  Run it AFTER fat_test.sh passes.
#
# Procedure:
#   1. Run this script.
#   2. The board will reset via WDT timeout (~15 s after the test starts).
#   3. The script writes a flag file BEFORE triggering the WDT.
#   4. On the next boot, /etc/init.d/S??boot (or similar) checks for the flag.
#   5. If the flag is found and this script detects it, the test passes.
#   6. The flag is automatically cleaned up.
#
# This is a manual certification step — not automated in fat_test.sh
# because a board reset terminates the SSH session.
# ============================================================================

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

FLAG_FILE="/userdata/.fat_wdt_test"
FLAG_EXPECTED="WDT_CERTIFICATION_$(cat /proc/sys/kernel/random/boot_id 2>/dev/null || date +%s)"

echo ""
echo "============================================"
echo "  GEMA Vision — Watchdog Reset Certification"
echo "============================================"
echo ""
echo "${YELLOW}WARNING: This test WILL reset the board.${NC}"
echo "Your SSH session will be terminated."
echo ""
echo "Procedure:"
echo "  1. Run this script."
echo "  2. The board resets in ~15 seconds."
echo "  3. After reboot, SSH back and run:"
echo "     ${GREEN}fat_wdt_test.sh --verify${NC}"
echo ""

# ---- Verify mode (run after reboot) ----------------------------------------
if [ "$1" = "--verify" ]; then
    if [ -f "$FLAG_FILE" ]; then
        stored=$(cat "$FLAG_FILE")
        if [ "$stored" = "$FLAG_EXPECTED" ]; then
            rm -f "$FLAG_FILE"
            echo "${GREEN}★★★  WATCHDOG CERTIFICATION PASSED  ★★★${NC}"
            echo "The board was reset by the WDT and recovered correctly."
            exit 0
        else
            echo "${RED}FAIL: Flag file corrupted.${NC}"
            rm -f "$FLAG_FILE"
            exit 1
        fi
    else
        echo "${RED}FAIL: Flag file not found after reboot.${NC}"
        echo "The watchdog may not have reset the board, or the"
        echo "filesystem was not properly mounted at boot."
        exit 1
    fi
fi

# ---- Write flag and trigger WDT -------------------------------------------
echo "Writing certification flag..."
mkdir -p /userdata
echo "$FLAG_EXPECTED" > "$FLAG_FILE"
sync
sync

echo "Flag written.  Triggering watchdog in 5 seconds..."
echo "After reboot, SSH back and run:  ${GREEN}$0 --verify${NC}"
sleep 5

# Open WDT without sending keepalive → timeout → hardware reset.
# The RV1106 WDT defaults to ~15 s timeout.
echo V > /dev/watchdog 2>/dev/null || true
# If /dev/watchdog is not available, use the sysfs interface.
if [ -w /sys/devices/platform/*.watchdog/watchdog*/timeout ]; then
    echo 15 > /sys/devices/platform/*.watchdog/watchdog*/timeout
    echo 1 > /sys/devices/platform/*.watchdog/watchdog*/pretimeout 2>/dev/null || true
fi

# Loop until reset.
echo "WDT armed — waiting for hardware reset..."
while true; do
    sleep 1
done
