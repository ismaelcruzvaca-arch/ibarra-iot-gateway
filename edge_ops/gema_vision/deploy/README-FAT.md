# GEMA Vision — Factory Acceptance Test (FAT) Protocol

## Overview

The FAT suite validates that a physical RV1106 camera board is fully
functional before installation on the production line.  It consists of:

| Script | Purpose | Destructive? |
|--------|---------|-------------|
| `fat_test.sh` | 9 non-destructive hardware tests | No |
| `fat_wdt_test.sh` | Watchdog reset certification | **Yes — reboots the board** |

## Prerequisites

- Board powered on and reachable via SSH
- Micro SD card or eMMC with the production Buildroot image flashed
- **GPIO loopback jumper** connecting:
  - Output pin (GPIO1_A0 / physical pin 24) → Input pin (GPIO1_A1 / physical pin 23)
- MQTT broker reachable on the network (default: 192.168.1.100)
- `gema-hw-diag` binary in `/userdata/bin/` (compiled by CI/CD pipeline)

## Running the Tests

### Step 1: Non-destructive tests

```bash
# Copy the FAT suite to the board
scp deploy/fat_test.sh deploy/gema-hw-diag root@camara:/tmp/
ssh root@camara

# Run as root
cd /tmp
chmod +x fat_test.sh
./fat_test.sh --broker 192.168.1.100
```

### Step 2: Interpret results

```
[1/9] GPIO Input (Trigger)         PASS  pin 23 readable
[2/9] GPIO Loopback (Actuator)     PASS  out=24 ↔ in=23 (jumper required)
[3/9] HW Diagnostics (NPU+RGA)     PASS  NPU + RGA ok
[4/9] MIPI Camera                  PASS  camera sensor detected
[5/9] DRAM Integrity               PASS  10 MB pattern test passed
[6/9] eMMC/NAND Storage            PASS  write+read+verify ok
[7/9] MQTT Smoke Test              PASS  broker 192.168.1.100 ok
[8/9] Network Link                 PASS  link detected
[9/9] Thermal Baseline             PASS  42°C (normal)

★★★  CAMARA LISTA PARA INSTALAR  ★★★
```

**If any test FAILs**, diagnose based on the error message:
- **GPIO**: Check jumper connection, verify pin numbers in the board datasheet
- **NPU/RGA**: Reflash the system image; if persists, RMA the board
- **MIPI Camera**: Reseat the ribbon cable, verify with `i2cdetect -y 4`
- **DRAM**: Memory soldering defect — RMA
- **eMMC**: Bad block — reformat or RMA
- **MQTT**: Check network connectivity and broker address
- **Network**: Verify Ethernet cable or WiFi configuration
- **Thermal**: ≥ 65°C at idle indicates missing thermal pad — RMA

**SKIP** tests are non-critical or depend on optional hardware:
- `memtester` not installed → install via Buildroot `make memtester`
- `mosquitto_*` not installed → install via Buildroot
- No thermal zone → board variant without temperature sensor

### Step 3: Watchdog Certification (destructive)

**Only run this if Step 1 passed all 9 tests.**

```bash
# On the target board:
chmod +x fat_wdt_test.sh
./fat_wdt_test.sh

# The board will reset in ~15 seconds.  Your SSH session dies.
# Wait for the board to reboot (~30 s), then SSH back and run:
./fat_wdt_test.sh --verify

# Expected output:
# ★★★  WATCHDOG CERTIFICATION PASSED  ★★★
# The board was reset by the WDT and recovered correctly.
```

If `--verify` reports FAIL:
- The watchdog device `/dev/watchdog` may not be enabled in the kernel
- Check `dmesg | grep watchdog` for driver messages
- Verify the device tree enables the RV1106 WDT

## Test Architecture

```
fat_test.sh (9 tests, non-destructive)
│
├── [1] GPIO Input        gpioget 1 23
├── [2] GPIO Loopback     gpioset 1 24=1 → gpioget 1 23=1
├── [3] HW Diagnostics    gema-hw-diag (NPU dlopen + RGA fopen)
├── [4] MIPI Camera       i2cdetect + /dev/video0 + rkipc
├── [5] DRAM Integrity    memtester 10M 1
├── [6] eMMC Storage      dd + sha256sum
├── [7] MQTT Smoke Test   mosquitto_pub → mosquitto_sub
├── [8] Network Link      /sys/class/net/*/carrier
└── [9] Thermal Baseline  /sys/class/thermal/thermal_zone0/temp

fat_wdt_test.sh (1 test, destructive — run separately)
└── [10] WDT Reset        flag → /dev/watchdog → reboot → verify flag
```

## CI/CD Integration

The `gema-hw-diag` binary is automatically cross-compiled for armhf by
the CI/CD pipeline (GitHub Actions) along with `gema-vision` and
`gema-ota`.  It is available as a build artifact and should be included
in the OTA release tarball or copied to `/userdata/bin/` during
factory flashing.

To build locally:
```bash
cmake -B build -DCMAKE_TOOLCHAIN_FILE=toolchain-arm-rockchip.cmake
cmake --build build --target gema-hw-diag
```
