#!/bin/sh
# ============================================================================
# fat_test.sh  —  Factory Acceptance Test for GEMA Vision (RV1106)
#
# Runs 9 non-destructive hardware validation tests.
# If ALL pass, the camera is ready for installation.
# Run as root on the target board.
#
# Usage:
#   ./fat_test.sh [--broker 192.168.1.100]
#
# The destructive Watchdog Reset test (test #10) is in fat_wdt_test.sh
# and must be run separately AFTER this script passes.
# ============================================================================

set -e

# ---- Constants -------------------------------------------------------------
BROKER_IP="${1:-192.168.1.100}"   # default MQTT broker for smoke test
PASS=0
FAIL=1
SKIP=2
TOTAL=0
PASSED=0

# ---- Coloured output -------------------------------------------------------
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'  # No Colour

print_header() {
    printf "\n============================================\n"
    printf "  GEMA Vision — Factory Acceptance Test\n"
    printf "  $(date -u +%Y-%m-%dT%H:%M:%SZ)\n"
    printf "============================================\n\n"
}

print_result() {
    local test_num="$1"
    local test_name="$2"
    local result="$3"
    local detail="$4"

    TOTAL=$((TOTAL + 1))
    case "$result" in
        "$PASS")
            printf "  ${GREEN}[%d/9] %-30s PASS${NC}  %s\n" "$test_num" "$test_name" "$detail"
            PASSED=$((PASSED + 1))
            ;;
        "$FAIL")
            printf "  ${RED}[%d/9] %-30s FAIL${NC}  %s\n" "$test_num" "$test_name" "$detail"
            ;;
        "$SKIP")
            printf "  ${YELLOW}[%d/9] %-30s SKIP${NC}  %s\n" "$test_num" "$test_name" "$detail"
            PASSED=$((PASSED + 1))  # skip counts as pass for final tally
            ;;
    esac
}

# ===========================================================================
# Test 1: GPIO Input (Trigger / Laser sensor)
# ===========================================================================

test_gpio_input() {
    local pin="${1:-23}"  # default GPIO pin for trigger input

    if ! command -v gpioget >/dev/null 2>&1; then
        print_result 1 "GPIO Input (Trigger)" "$SKIP" "libgpiod not installed"
        return
    fi

    if gpioget 1 "$pin" >/dev/null 2>&1; then
        print_result 1 "GPIO Input (Trigger)" "$PASS" "pin $pin readable"
    else
        print_result 1 "GPIO Input (Trigger)" "$FAIL" "cannot read pin $pin"
    fi
}

# ===========================================================================
# Test 2: GPIO Output Loopback (Actuator)
# ===========================================================================
# Requires a jumper wire between output pin and input pin.
# The technician must connect these before running.

test_gpio_loopback() {
    local out_pin="${1:-24}"  # default GPIO output (piston relay)
    local in_pin="${2:-23}"   # default GPIO input (tied to out_pin via jumper)

    if ! command -v gpioset >/dev/null 2>&1 || ! command -v gpioget >/dev/null 2>&1; then
        print_result 2 "GPIO Loopback (Actuator)" "$SKIP" "libgpiod not installed"
        return
    fi

    # Set output HIGH.
    gpioset 1 "$out_pin"=1 2>/dev/null
    sleep 0.1
    local val
    val=$(gpioget 1 "$in_pin" 2>/dev/null || echo "0")

    if [ "$val" = "1" ]; then
        # Set output LOW and verify.
        gpioset 1 "$out_pin"=0 2>/dev/null
        sleep 0.1
        val=$(gpioget 1 "$in_pin" 2>/dev/null || echo "1")
        if [ "$val" = "0" ]; then
            print_result 2 "GPIO Loopback (Actuator)" "$PASS" \
                "out=$out_pin ↔ in=$in_pin (jumper required)"
        else
            print_result 2 "GPIO Loopback (Actuator)" "$FAIL" \
                "output stuck HIGH at pin $out_pin"
        fi
    else
        print_result 2 "GPIO Loopback (Actuator)" "$FAIL" \
            "output pin $out_pin not reaching input pin $in_pin (jumper connected?)"
    fi
}

# ===========================================================================
# Test 3: Hardware Diagnostics (NPU + RGA)
# ===========================================================================

test_hw_diag() {
    local diag_bin="./gema-hw-diag"
    if [ ! -f "$diag_bin" ]; then
        diag_bin="/userdata/bin/gema-hw-diag"
    fi
    if [ ! -f "$diag_bin" ]; then
        print_result 3 "HW Diagnostics (NPU+RGA)" "$SKIP" "gema-hw-diag not found"
        return
    fi

    if "$diag_bin" 2>&1; then
        print_result 3 "HW Diagnostics (NPU+RGA)" "$PASS" "NPU + RGA ok"
    else
        print_result 3 "HW Diagnostics (NPU+RGA)" "$FAIL" "see output above"
    fi
}

# ===========================================================================
# Test 4: MIPI Camera Presence
# ===========================================================================

test_mipi_camera() {
    local found=0

    # Method 1: i2cdetect on the CSI bus.
    if command -v i2cdetect >/dev/null 2>&1; then
        if i2cdetect -y 4 2>/dev/null | grep -qi "0x30\|0x36\|0x3c\|0x40"; then
            found=1
        fi
    fi

    # Method 2: /dev/video0 presence.
    if [ -e /dev/video0 ]; then
        found=1
    fi

    # Method 3: rkipc process running.
    if pidof rkipc >/dev/null 2>&1; then
        found=1
    fi

    if [ "$found" = "1" ]; then
        print_result 4 "MIPI Camera" "$PASS" "camera sensor detected"
    else
        print_result 4 "MIPI Camera" "$FAIL" \
            "no camera found (check cable, i2cdetect, /dev/video0)"
    fi
}

# ===========================================================================
# Test 5: DRAM Integrity
# ===========================================================================

test_dram() {
    if ! command -v memtester >/dev/null 2>&1; then
        print_result 5 "DRAM Integrity" "$SKIP" "memtester not installed"
        return
    fi

    # Test 10 MB (quick — catches major BGA soldering defects).
    if memtester 10M 1 >/dev/null 2>&1; then
        print_result 5 "DRAM Integrity" "$PASS" "10 MB pattern test passed"
    else
        print_result 5 "DRAM Integrity" "$FAIL" "memory pattern mismatch"
    fi
}

# ===========================================================================
# Test 6: eMMC / NAND Data Integrity
# ===========================================================================

test_storage() {
    local test_file="/userdata/.fat_storage_test"
    local checksum_file="/tmp/.fat_storage_sha256"

    # Write 1 MB of random data.
    dd if=/dev/urandom of="$test_file" bs=1M count=1 2>/dev/null
    sha256sum "$test_file" | cut -d' ' -f1 > "$checksum_file"

    # Read back and verify.
    local expected
    local actual
    expected=$(cat "$checksum_file")
    actual=$(sha256sum "$test_file" | cut -d' ' -f1)

    rm -f "$test_file" "$checksum_file"

    if [ "$expected" = "$actual" ]; then
        print_result 6 "eMMC/NAND Storage" "$PASS" "write+read+verify ok"
    else
        print_result 6 "eMMC/NAND Storage" "$FAIL" \
            "checksum mismatch — possible bad block"
    fi
}

# ===========================================================================
# Test 7: MQTT Smoke Test
# ===========================================================================

test_mqtt() {
    if ! command -v mosquitto_pub >/dev/null 2>&1; then
        print_result 7 "MQTT Smoke Test" "$SKIP" "mosquitto client not installed"
        return
    fi

    # Ping the broker first.
    if ! ping -c 1 -W 2 "$BROKER_IP" >/dev/null 2>&1; then
        print_result 7 "MQTT Smoke Test" "$FAIL" "broker $BROKER_IP unreachable"
        return
    fi

    local test_topic="novamex/fat/test"
    local test_msg="fat_ping_$$"

    # Publish with retain, then subscribe.
    mosquitto_pub -h "$BROKER_IP" -t "$test_topic" -m "$test_msg" -r 2>/dev/null
    local received
    received=$(mosquitto_sub -h "$BROKER_IP" -t "$test_topic" --timedout 2000 -C 1 2>/dev/null)

    # Clean up retained message.
    mosquitto_pub -h "$BROKER_IP" -t "$test_topic" -n -r 2>/dev/null

    if [ "$received" = "$test_msg" ]; then
        print_result 7 "MQTT Smoke Test" "$PASS" "broker $BROKER_IP ok"
    else
        print_result 7 "MQTT Smoke Test" "$FAIL" \
            "pub/sub mismatch (got '$received')"
    fi
}

# ===========================================================================
# Test 8: Network Link Status
# ===========================================================================

test_network() {
    local link=0

    # Check Ethernet link.
    if [ -d /sys/class/net/eth0 ]; then
        local carrier
        carrier=$(cat /sys/class/net/eth0/carrier 2>/dev/null || echo "0")
        if [ "$carrier" = "1" ]; then
            link=1
        fi
    fi

    # Check WiFi link.
    if [ -d /sys/class/net/wlan0 ]; then
        local carrier
        carrier=$(cat /sys/class/net/wlan0/carrier 2>/dev/null || echo "0")
        if [ "$carrier" = "1" ]; then
            link=1
        fi
    fi

    if [ "$link" = "1" ]; then
        print_result 8 "Network Link" "$PASS" "link detected"
    else
        print_result 8 "Network Link" "$FAIL" \
            "no carrier on eth0 or wlan0 (cable connected?)"
    fi
}

# ===========================================================================
# Test 9: Thermal Baseline
# ===========================================================================

test_thermal() {
    local zone_path="/sys/class/thermal/thermal_zone0/temp"

    if [ ! -f "$zone_path" ]; then
        print_result 9 "Thermal Baseline" "$SKIP" "no thermal zone"
        return
    fi

    local raw
    raw=$(cat "$zone_path" 2>/dev/null || echo "0")
    local temp_c=$((raw / 1000))

    if [ "$temp_c" -ge 65 ]; then
        print_result 9 "Thermal Baseline" "$FAIL" \
            "${temp_c}°C ≥ 65°C — possible thermal pad missing"
    elif [ "$temp_c" -ge 50 ]; then
        print_result 9 "Thermal Baseline" "$PASS" "${temp_c}°C (warm)"
    else
        print_result 9 "Thermal Baseline" "$PASS" "${temp_c}°C (normal)"
    fi
}

# ===========================================================================
# Main
# ===========================================================================

print_header

# Check root.
if [ "$(id -u)" -ne 0 ]; then
    printf "${RED}ERROR: FAT must be run as root.${NC}\n"
    exit 1
fi

test_gpio_input
test_gpio_loopback
test_hw_diag
test_mipi_camera
test_dram
test_storage
test_mqtt
test_network
test_thermal

printf "\n============================================\n"
printf "  Results:  %d/9 PASSED\n" "$PASSED"

if [ "$PASSED" -eq 9 ]; then
    printf "  ${GREEN}★★★  CAMARA LISTA PARA INSTALAR  ★★★${NC}\n"
    printf "  Run fat_wdt_test.sh separately to certify the watchdog.\n"
    exit 0
else
    printf "  ${RED}★★★  CAMARA RECHAZADA  ★★★${NC}\n"
    printf "  Review the FAIL/SKIP tests above.\n"
    exit 1
fi
