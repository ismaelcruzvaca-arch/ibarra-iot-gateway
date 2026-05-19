# Implementation Tasks: Norvi Silicon Shield

This document breaks down the integration of the TWDT, BOD, and hardware security documentation into atomic implementation steps.

## Phase 1: Task Watchdog Timer (TWDT)
- [ ] In `main.cpp`, import `#include <esp_task_wdt.h>`.
- [ ] In `setup()`, invoke `esp_task_wdt_init(5, true)` to configure a global 5-second panic timeout.
- [ ] In `vTaskNetworking`, add `esp_task_wdt_add(NULL)` before the infinite loop, and `esp_task_wdt_reset()` inside the loop.
- [ ] In `vTaskApplication`, add `esp_task_wdt_add(NULL)` before the infinite loop, and `esp_task_wdt_reset()` inside the loop.
- [ ] In `vTaskAnalog`, add `esp_task_wdt_add(NULL)` before the infinite loop, and `esp_task_wdt_reset()` inside the loop.

## Phase 2: Brownout Detector (BOD) Enhancement
- [ ] In `main.cpp`, import `#include "soc/soc.h"` and `#include "soc/rtc_cntl_reg.h"`.
- [ ] In `setup()`, directly manipulate the `RTC_CNTL_BROWN_OUT_REG` to elevate the threshold to the ~2.80V mark. (Setting the specific bits per ESP32 technical reference manual or framework macros).

## Phase 3: Hardware Security Playbook (Documentation)
- [ ] Create `edge_nodes/norvi_monitor/SECURITY.md`.
- [ ] Document the Secure Boot V2 signing process and `espefuse.py` command.
- [ ] Document the Flash Encryption AES-256 eFuse burning command.
- [ ] Document the JTAG/UART download mode disablement commands.
