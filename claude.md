# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

---

## Project Overview

**Gravimetric Shots** is an embedded ESP32-S3 espresso controller that uses BLE-connected Acaia scales for real-time gravimetric shot profiling. The system monitors extraction weight, predicts shot endpoint via linear regression, and controls a solenoid valve relay for automated shot stopping.

**Hardware**: LilyGO T-Display-S3-Long (ESP32-S3R8, 240MHz dual-core, 16MB Flash, 8MB PSRAM, 180×640 QSPI display, capacitive touch)

**Tested Setup**: La Marzocco Micra + Acaia Lunar 2021

---

## Build Commands

### Standard Build Workflow
```bash
# Build production firmware (USB Serial + NimBLE only)
pio run

# Build and upload to device
pio run --target upload

# Monitor serial output (115200 baud)
pio device monitor --baud 115200

# Monitor with direct filter (no color codes)
pio device monitor --filter=direct --baud 115200

# Clean build (if needed)
pio run --target clean && pio run
```

### Debug Build with Wireless Monitoring
```bash
# Build debug firmware (includes WiFi + WebSerial)
pio run -e gravimetric_shots_debug --target upload

# Access wireless debug at http://<ESP32-IP>/webserial
# WiFi credentials in src/wifi_credentials.h (not in git)
```

### macOS Build Issues
If builds fail with SCons errors (`FileNotFoundError: .sconsign311.tmp`):
```bash
# Clear extended attributes (macOS-specific issue)
xattr -rc .pio

# Then rebuild
pio run --target upload
```

**Why this happens**: macOS adds extended attributes (com.apple.quarantine, com.apple.provenance) to files from downloads/git operations. SCons can't write build database files with these attributes present. This is macOS-only; Linux/Windows builds work normally.

---

## Critical Architecture Patterns

### 1. **Dual-Core FreeRTOS Task Architecture**

The codebase uses **strict CPU core separation** to avoid race conditions:

- **Core 0**: BLE task (`bleTask()`) - Handles all BLE operations (scan, connect, heartbeat)
- **Core 1**: Main loop - Handles UI (LVGL), touch, display, shot control

**Communication**: Message queue (`bleCommandQueue`) for main→BLE commands, mutex-protected shared data structure (`BLESharedData`) for BLE→main updates.

**CRITICAL RULES**:
- ❌ **NEVER** call BLE functions from main loop - use command queue
- ❌ **NEVER** call LVGL functions from BLE task - LVGL is NOT thread-safe
- ✅ **ALWAYS** acquire `bleDataMutex` before reading/writing `bleData`
- ✅ **ALWAYS** acquire `serialMutex` before `Serial.print()` calls

### 2. **NULL Pointer Race Condition Protection**

**Critical Bug (Fixed Oct 2025)**: Scale can disconnect asynchronously during BLE operations, causing NULL pointer dereferences.

**Pattern**: ALL BLE state machine functions check for NULL pointers BEFORE and AFTER long operations:

```cpp
// Pattern used in all state functions
if (!_pReadChar || !_pWriteChar || !_pClient || !_pClient->isConnected()) {
    LOG_ERROR(TAG, "Scale disconnected during <operation> (race condition prevented)");
    transitionTo(CONN_FAILED, 0);
    return;
}
```

**Why**: `onDisconnect()` callback runs asynchronously and nullifies pointers. Without these checks, code crashes with LoadProhibited exception at address 0x00000020.

**See commit 87d1d35** for full implementation.

### 3. **Serial Buffer Overflow Prevention**

**Critical Bug (Fixed Oct 2025)**: Logging in high-frequency callbacks causes USB CDC buffer overflow → system crashes.

**Pattern**:
- ❌ **NO LOGGING** in notification callbacks (`handleNotification()`) - runs at ~20 Hz
- ❌ **NO LOGGING** in interrupt handlers
- ✅ Use counters + periodic logging in main loop instead

**Why**: USB CDC @ 115200 baud = 11.5 KB/s max. High-frequency logging (20 Hz × 180 bytes/log = 3.6 KB/s) + display refresh (242 Hz) = >52 KB/s >> USB capacity → buffer overflow → crash.

### 4. **Vendored Library Dependencies**

**CRITICAL**: This project uses **vendored LVGL v8.3.0-dev** in `lib/` folder, NOT PlatformIO registry versions.

**Why**:
- LVGL v8.3.0 (release) ≠ LVGL v8.3.0-dev (development)
- `lv_conf.h` is configured specifically for v8.3.0-dev
- Using wrong version = build succeeds but display freezes at runtime (silent failure)

**Libraries in lib/** (DO NOT replace with git dependencies):
- LVGL v8.3.0-dev (~180k lines)
- ArduinoBLE (~9k lines)
- AcaiaArduinoBLE v2.1.2+custom (modified fork)

**Libraries from PlatformIO** (in platformio.ini lib_deps):
- NimBLE-Arduino @ ^1.4.2
- XPowersLib (power management)
- WebSerial, ESPAsyncWebServer, AsyncTCP (debug builds only)

---

## Custom AcaiaArduinoBLE Library

This project uses a **custom modified fork** of tatemazer/AcaiaArduinoBLE.

**Key Custom Modifications**:
1. **LVGL Integration** - Removed Serial.print() calls that block LVGL timer
2. **NULL Pointer Protection** - Added defensive checks in all state functions
3. **NimBLE Backend** - Uses NimBLE instead of ArduinoBLE for WiFi+BLE coexistence
4. **Watchdog Integration** - Calls esp_task_wdt_reset() in update() loop
5. **Serial Buffer Protection** - Removed all logging from handleNotification()

**Documentation**:
- `lib/AcaiaArduinoBLE/CUSTOM_MODIFICATIONS.md` - Changes vs upstream v3.1.4
- `IMPLEMENTATION_COMPARISON.md` - Why this fork is more robust
- `ACAIA_BLE_PROTOCOL_RESEARCH.md` - 8-year BLE reverse engineering history

**Important**: This fork is **specialized for embedded LVGL UI**, NOT a general-purpose library. For other projects, use [tatemazer/AcaiaArduinoBLE](https://github.com/tatemazer/AcaiaArduinoBLE) upstream.

---

## BLE State Machine Architecture

The AcaiaArduinoBLE library uses a **non-blocking state machine** for connection management:

```
CONN_IDLE → CONN_SCANNING → CONN_CONNECTING → CONN_DISCOVERING →
CONN_SUBSCRIBING → CONN_IDENTIFYING → CONN_BATTERY → CONN_NOTIFICATIONS →
CONN_CONNECTED
                     ↓ (on error/disconnect)
                CONN_FAILED → restart scan
```

**State Settling Delays**: Each state waits 200ms before sending commands to give scale time to process previous commands. This prevents scale disconnects during rapid state transitions.

**Timeout Handling**: Each state has a timeout (5-10 seconds). If timeout occurs without completion, state machine transitions to CONN_FAILED and restarts scan.

**Heartbeat**: Connected state sends heartbeat every 2750ms to keep scale alive. If scale doesn't respond within 8000ms, connection is considered lost.

---

## Diagnostic Logging System

The codebase uses a **tag-based logging system** with configurable verbosity levels (defined in `src/debug_config.h`):

**Log Levels**: ERROR (1) < WARN (2) < INFO (3) < DEBUG (4) < VERBOSE (5)

**Subsystem Tags**:
- `TAG_SYS` - System messages (setup, memory, watchdog)
- `TAG_BLE` - BLE connection and state machine
- `TAG_SCALE` - Scale communication and weight updates
- `TAG_SHOT` - Shot brewing logic
- `TAG_UI` - UI events (touch, display)
- `TAG_RELAY` - Relay control
- `TAG_TASK` - FreeRTOS task monitoring

**Usage**:
```cpp
LOG_ERROR(TAG_BLE, "Connection failed: %s", reason);
LOG_INFO(TAG_SHOT, "Shot started at %.1fg", weight);
LOG_DEBUG(TAG_UI, "Touch detected: (%d, %d)", x, y);
```

**Production Logging Level**: LOG_LOCAL_LEVEL=4 (DEBUG) - Reduced from VERBOSE to prevent USB CDC buffer overflow.

---

## Known Issues and Workarounds

### Issue: System Crashes During BLE Operations
**Symptom**: Guru Meditation Error (LoadProhibited) at address 0x00000020
**Cause**: NULL pointer race condition when scale disconnects asynchronously
**Fix**: NULL pointer checks added in commit 87d1d35 (Oct 2025)
**Pattern**: See "NULL Pointer Race Condition Protection" above

### Issue: Display Freezes at Boot
**Symptom**: Build succeeds but display shows nothing, backlight on
**Cause**: Wrong LVGL version (v8.3.0 release vs v8.3.0-dev)
**Fix**: Use vendored LVGL v8.3.0-dev from lib/ folder
**Never**: Replace vendored LVGL with PlatformIO registry version

### Issue: Build Fails on macOS (SCons Errors)
**Symptom**: `FileNotFoundError: .sconsign311.tmp`, `undefined reference to 'loop()'`
**Cause**: macOS extended attributes prevent SCons from writing build database
**Fix**: `xattr -rc .pio` before building
**Why**: macOS-only issue (Linux/Windows unaffected)

### Issue: Watchdog Timeout During Scale Connection
**Symptom**: Interrupt watchdog timeout during BLE write operations
**Cause**: BLE write + LVGL rendering (230 Hz) + touch I2C competing for CPU
**Fix**: Increased CONFIG_ESP_INT_WDT_TIMEOUT_MS from 300ms → 3000ms
**Config**: In platformio.ini build_flags

---

## Shot Control Algorithm

The predictive shot ending algorithm works in phases:

**Phase 1 - Monitoring** (first 5 seconds):
- Track weight every 250ms
- Build shot history buffer (2000 datapoints max)
- No relay control yet

**Phase 2 - Flow Rate Analysis** (after 5s):
- Calculate first derivative (g/s) from last N samples (N=10)
- Detect flow rate deceleration pattern
- Identify "blooming" phase vs "declining" phase

**Phase 3 - Prediction**:
- Use linear regression on last N datapoints
- Project final weight based on current flow rate
- Account for drip delay (3 seconds) and offset (user-configured)

**Phase 4 - Relay Trigger**:
- Stop shot when: `predicted_weight >= (target - offset - drip_compensation)`
- Typical trigger point: ~2g before target for 36g shots
- Post-stop drips fill the gap to reach exact target

**Tunable Parameters**:
- `goalWeight` - Target weight (default: 44g for 22g in → 44g out = 2:1 ratio)
- `weightOffset` - User adjustment (default: 4.3g)
- `DRIP_DELAY_S` - Post-stop drip period (3 seconds)
- `N` - Samples for regression (10 datapoints)

---

## Testing Guidelines

### Hardware Testing Checklist
- [ ] Build and upload firmware
- [ ] Power on scale, verify BLE connection
- [ ] Test manual tare button
- [ ] Set target weight, verify UI updates
- [ ] Pull test shot, verify relay triggers
- [ ] Power off scale during shot, verify graceful disconnect
- [ ] Verify automatic reconnection after disconnect
- [ ] Check serial monitor for errors/warnings

### Long-term Stability Testing
Run system for 10+ minutes while:
- Monitoring heap memory (should stay >200KB free)
- Monitoring stack usage (should stay >17KB remaining)
- Watching for watchdog resets
- Testing multiple connect/disconnect cycles
- Verifying no memory leaks (min_free should not decrease)

### Debugging Crashes
If system crashes:
1. Check serial output for reset reason (first 1 second after boot)
2. Look for panic dump with register values
3. Use `~/.platformio/packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-addr2line -e .pio/build/gravimetric_shots/firmware.elf <PC_ADDRESS>` to decode crash location
4. Check for NULL pointer dereferences (EXCVADDR near 0x00000000)
5. Review recent BLE state machine logs before crash

---

## Pin Definitions

Hardware pins are defined in `src/pins_config.h`:

**Critical Pins**:
- `RELAY1` = GPIO 48 - Solenoid valve control (active HIGH)
- `PIN_BAT_VOLT` = GPIO 8 - Battery voltage ADC (corrected from GPIO 2)
- `TOUCH_IICSCL` = GPIO 10 - Touch controller I2C clock
- `TOUCH_IICSDA` = GPIO 15 - Touch controller I2C data
- `TOUCH_RES` = GPIO 16 - Touch controller reset

**Display Pins**: See `src/AXS15231B.h` for complete AXS15231B driver configuration

---

## Memory Budget

**Flash Usage**: ~896 KB / 3.1 MB (28.5%)
**RAM Usage**: ~49 KB / 327 KB (14.9%)
**Internal DRAM**: ~208 KB free (critical metric for stability)
**PSRAM**: ~8 MB total, ~7.9 MB free (used for LVGL buffers)

**Critical Threshold**: Internal DRAM < 100 KB free = warning (may cause instability)

**Monitoring**: BLE task logs heap stats every 30 seconds, stack stats every 10 seconds.

---

## Git Workflow

**Branches**:
- `main` - Production-ready code, stable builds only
- Feature branches - For experimental work

**Commit Message Pattern**:
```
<type>: <short description>

<detailed explanation>

<impact/testing notes>

🤖 Generated with [Claude Code](https://claude.com/claude-code)

Co-Authored-By: Claude <noreply@anthropic.com>
```

**Types**: `feat`, `fix`, `refactor`, `docs`, `test`, `chore`

**Important**: Always test builds before pushing to main. This is single-user production code.

---

## External Resources

**Hardware**:
- [LilyGO T-Display-S3-Long](https://github.com/Xinyuan-LilyGO/T-Display-S3-Long) - Hardware documentation
- Display Driver: AXS15231B (custom, in src/)

**Libraries**:
- [LVGL v8 Docs](https://docs.lvgl.io/8.3/) - UI framework
- [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) - BLE stack
- [PlatformIO Docs](https://docs.platformio.org) - Build system

**BLE Scale**:
- [tatemazer/AcaiaArduinoBLE](https://github.com/tatemazer/AcaiaArduinoBLE) - Upstream library (v3.1.4)
- [Tate's Discord](https://discord.gg/NMXb5VYtre) - Community support
- [Your Issue #7](https://github.com/tatemazer/AcaiaArduinoBLE/issues/7) - ESP32-S3 17-byte packet behavior

**Protocol Research**:
- `ACAIA_BLE_PROTOCOL_RESEARCH.md` - 8-year reverse engineering timeline
- `IMPLEMENTATION_COMPARISON.md` - This fork vs upstream analysis

---

## Important Disclaimers

**Hardware Scope**:
- Tested: LM Micra + Acaia Lunar 2021 only
- Untested: Other machines, other scales, multiple scales

**This is NOT a general-purpose library**:
- Specialized fork for embedded LVGL UI on ESP32-S3
- Limited testing beyond personal setup
- For general scale integration, use tatemazer/AcaiaArduinoBLE upstream

**Safety**:
- Controls high-voltage espresso machine via relay
- User assumes all risk for electrical/mechanical modifications
- Always test extensively before production use

---

## Quick Reference: Common Tasks

**Build for first time**:
```bash
git clone https://github.com/SongKeat2901/Gravimetric-Shots.git
cd Gravimetric-Shots
pio run --target upload
pio device monitor --baud 115200
```

**Fix macOS build errors**:
```bash
xattr -rc .pio
pio run --target upload
```

**Add new BLE command**:
1. Add enum to `BLECommand` in GravimetricShots.ino
2. Add handler in `bleTask()` switch statement
3. Add sender function with `xQueueSend(bleCommandQueue, ...)`
4. Test with NULL pointer checks in BLE library if needed

**Modify LVGL UI**:
1. Edit UI in SquareLine Studio (exports to lib/ui/)
2. Add event handlers in `ui_events.c` (calls back to .ino via extern functions)
3. Implement handler in GravimetricShots.ino
4. **NEVER** call BLE functions directly from UI events - use command queue

**Increase logging verbosity**:
1. Edit `src/debug_config.h`
2. Change subsystem log level: `#define <SUBSYSTEM>_LOG_LEVEL LOG_VERBOSE`
3. Rebuild and upload

---

## Archive

Historical documentation has been moved to `archive/` directory to keep the root clean:

**Archived Files**:
- **IMPROVEMENT_PLAN.md** (Oct 1, 2025) - Pre-implementation planning for watchdog integration and optimization
- **FIX_SUMMARY.md** (Oct 9, 2025) - Display freeze fix technical analysis (LVGL version mismatch)
- **FUTURE_IMPROVEMENTS.md** (Oct 16, 2025) - Non-blocking BLE state machine planning (now implemented)
- **TESTING_CHECKLIST.md** (Oct 17, 2025) - FreeRTOS dual-core implementation testing checklist (completed)

**Why Archived**: These documents were critical during development but are now superseded by:
- Implemented features (non-blocking BLE, FreeRTOS architecture)
- Fixed issues (display freeze, connection reliability)
- Current documentation (README.md, CLAUDE.md, technical references)

The archived files are preserved for historical reference and provide valuable context for understanding the development timeline and decision-making process.

---

**Last Updated**: October 2025 (Commit 87d1d35)
