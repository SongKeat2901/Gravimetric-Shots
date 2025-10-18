# FreeRTOS Dual-Core Implementation - Testing & Validation Checklist

**Date Created:** October 17, 2025
**Implementation:** Option D - FreeRTOS Task Architecture
**Status:** Initial implementation complete, awaiting comprehensive validation

---

## System Architecture Overview

```
Core 0 (BLE/WiFi)          Core 1 (Application/UI)
==================         ====================
bleTaskFunction()          loop()
  ├─ BLE operations          ├─ LVGL UI updates
  ├─ Scale connection        ├─ Touch handling
  ├─ Weight readings         ├─ Display sleep/wake
  └─ Heartbeat/watchdog      └─ Status updates

         ↕ (Mutex-protected shared data)
     bleData + bleDataMutex
```

**Key Changes:**
- BLE operations moved to Core 0 task (can block without affecting UI)
- Main loop (Core 1) handles ONLY UI operations
- Thread-safe communication via mutex-protected shared data
- Command queue for main loop → BLE task commands

---

## ✅ PRE-VALIDATION CHECKLIST

### Code Review
- [x] BLE task pinned to Core 0 ✅ (line 1671)
- [x] Main loop verified on Core 1 ✅ (confirmed in serial output)
- [x] LVGL calls REMOVED from BLE task ✅ (lines 617-625, 1168-1169)
- [x] Mutex protection on all shared data access ✅ (lines 631-692)
- [x] Stack size set to 20KB ✅ (line 1671)
- [x] Watchdog timeout increased to 8 seconds ✅ (AcaiaArduinoBLE.h:29)
- [x] Display sleep I2C protection ✅ (lines 592-628)
- [x] LVGL initialization guard ✅ (lines 1298-1301)

### Build Verification
- [x] Production build successful ✅
- [x] Debug build with WebSerial successful ✅
- [ ] Flash usage acceptable (<80%)
- [ ] RAM usage acceptable (<80%)

### Initial Boot Test
- [x] System boots without crashes ✅
- [x] Core assignments correct (BLE=0, UI=1) ✅
- [x] Stack watermark reports appearing ✅
- [x] No immediate crashes ✅

---

## 🧪 PHASE 1: UI RESPONSIVENESS TESTS

### Test 1.1: Touch During BLE Connection
**Goal:** Verify main objective - UI remains responsive during 8-15 second BLE connection

**Prerequisites:**
- Scale powered OFF (forces long connection timeout)
- Fresh boot

**Test Steps:**
1. Power on ESP32, wait for main screen
2. Scale should attempt connection (8-15 second timeout expected)
3. **DURING CONNECTION ATTEMPT:**
   - Touch navigation buttons
   - Attempt to open settings screen
   - Try tare/start/stop buttons
   - Scroll through UI elements

**Success Criteria:**
- [ ] UI responds immediately to touch (<100ms perceived)
- [ ] No UI freezing or lag
- [ ] Buttons visually respond (press animation)
- [ ] Screen transitions work smoothly
- [ ] LVGL timer handler continues running

**Metrics to Record:**
- Touch response time (subjective: Excellent/Good/Poor)
- Any UI freezes? (Yes/No, duration)
- Any visual glitches? (Yes/No, description)

**Expected Behavior:**
Previously (before Option D), UI would freeze for 8-15 seconds during connection.
NOW: UI should remain "super fast" (user's words) during entire connection attempt.

---

### Test 1.2: Touch During Active BLE Operations
**Goal:** Verify UI stays responsive during scale communication

**Prerequisites:**
- Scale powered ON and connected
- Weight data streaming

**Test Steps:**
1. While weight data is streaming:
   - Rapidly press tare button (5+ times)
   - Open/close settings screen repeatedly
   - Touch multiple UI elements in quick succession
2. Check serial output for command queue processing

**Success Criteria:**
- [ ] All button presses register
- [ ] UI transitions are smooth
- [ ] No dropped touch events
- [ ] Command queue processes all requests
- [ ] Weight display continues updating

**Metrics to Record:**
- Button response time
- Any delayed commands? (Yes/No)
- Queue depth (check serial output)

---

### Test 1.3: Display Sleep/Wake During BLE Operations
**Goal:** Verify I2C fix works during active BLE connection

**Prerequisites:**
- Scale connected and streaming data
- Display sleep timeout = 5 minutes (line 109)

**Test Steps:**
1. Let system idle for 5+ minutes (display should sleep)
2. Check serial output for I2C errors
3. Touch screen to wake display
4. Verify scale connection maintained
5. Repeat 3 times

**Success Criteria:**
- [ ] Display sleeps after 5 minutes
- [ ] NO I2C errors: `[E][Wire.cpp:513] requestFrom()` ✅
- [ ] Touch wakes display immediately
- [ ] Scale connection NOT dropped
- [ ] Weight data resumes after wake

**Metrics to Record:**
- Any I2C errors? (Count)
- Wake-up time (ms, if noticeable)
- Connection status after wake

---

## 🔌 PHASE 2: CONNECTION STABILITY TESTS

### Test 2.1: Normal Connection Cycle
**Goal:** Verify 8-second watchdog timeout works correctly

**Prerequisites:**
- Scale powered ON
- Fresh boot

**Test Steps:**
1. Power on ESP32
2. Watch serial output during connection
3. Note connection time
4. Let scale run for 10 minutes
5. Check for unexpected disconnects

**Success Criteria:**
- [ ] Connection succeeds within 8 seconds
- [ ] No "State timeout: Scanning" loops
- [ ] No premature disconnects
- [ ] Weight data streams continuously
- [ ] Heartbeat sent every 2.75 seconds

**Metrics to Record:**
- Time to connect (seconds)
- Any timeout errors? (Yes/No)
- Total uptime before disconnect (if any)

---

### Test 2.2: Connection Loss and Recovery
**Goal:** Verify graceful handling of connection loss

**Prerequisites:**
- Scale connected and streaming
- System running for 5+ minutes

**Test Steps:**
1. Turn OFF scale (simulate connection loss)
2. Wait for detection (should be <8 seconds)
3. Check UI feedback (Bluetooth icon, status message)
4. Turn ON scale again
5. Verify auto-reconnection

**Success Criteria:**
- [ ] Disconnection detected within 8 seconds
- [ ] UI shows "Scale Not Connected"
- [ ] Bluetooth icon shows disconnected state
- [ ] Auto-reconnect succeeds
- [ ] Weight data resumes streaming
- [ ] NO crashes during transition

**Metrics to Record:**
- Time to detect disconnect (seconds)
- Time to reconnect (seconds)
- Any errors in serial output?

---

### Test 2.3: Multiple Connect/Disconnect Cycles
**Goal:** Verify state machine handles repeated cycles

**Test Steps:**
1. Connect scale (wait for weight data)
2. Disconnect scale (wait for detection)
3. Reconnect scale (wait for weight data)
4. Repeat 10 times

**Success Criteria:**
- [ ] All 10 cycles complete successfully
- [ ] Connection time consistent (within 20%)
- [ ] No memory leaks (heap stable)
- [ ] Stack watermark stable
- [ ] NO crashes

**Metrics to Record:**
- Average connection time
- Heap free (start vs. end)
- Stack watermark trend
- Any failures? (cycle number, error)

---

### Test 2.4: Heartbeat Timing Validation
**Goal:** Verify heartbeat prevents false disconnections

**Prerequisites:**
- Scale connected
- Weight data streaming
- Access to serial output timestamps

**Test Steps:**
1. Let system run for 10 minutes
2. Monitor serial output for heartbeat messages
3. Calculate heartbeat intervals
4. Verify no disconnections occur

**Success Criteria:**
- [ ] Heartbeat sent every 2.75 seconds (±10%)
- [ ] Last packet time updates regularly
- [ ] NO watchdog timeouts
- [ ] Connection uptime >= 10 minutes

**Metrics to Record:**
- Heartbeat interval (average, min, max)
- Packet period (max observed)
- Any missed heartbeats? (count)

---

## ⏱️ PHASE 3: LONG-TERM STABILITY TESTS

### Test 3.1: 30-Minute Runtime Test
**Goal:** Verify system stability over extended operation

**Prerequisites:**
- Scale connected
- Fresh boot
- Serial monitor running

**Test Steps:**
1. Start system, connect scale
2. Run for 30 minutes WITHOUT interaction
3. Monitor serial output every 5 minutes
4. Record metrics below

**Success Criteria:**
- [ ] System runs for full 30 minutes
- [ ] NO crashes or reboots
- [ ] Weight data continues streaming
- [ ] Stack watermark stable
- [ ] Heap memory stable
- [ ] No watchdog resets

**Metrics to Record (every 5 minutes):**
| Time | Heap Free | Stack Left | Uptime | Errors |
|------|-----------|------------|--------|--------|
| 0    |           | ~18KB      | 0s     |        |
| 5    |           |            |        |        |
| 10   |           |            |        |        |
| 15   |           |            |        |        |
| 20   |           |            |        |        |
| 25   |           |            |        |        |
| 30   |           |            |        |        |

**Red Flags:**
- Heap decreasing trend (memory leak)
- Stack watermark <1000 bytes (overflow risk)
- Increasing error count

---

### Test 3.2: Overnight Stability Test (Optional)
**Goal:** Ultimate stability validation

**Prerequisites:**
- Confidence from 30-minute test
- Scale powered via USB (won't auto-sleep)

**Test Steps:**
1. Start system, connect scale
2. Run overnight (8+ hours)
3. Check state in morning

**Success Criteria:**
- [ ] System still running
- [ ] Scale still connected
- [ ] Weight data still streaming
- [ ] No errors in serial log

**Metrics to Record:**
- Total uptime (hours)
- Final heap free
- Final stack watermark
- Error count (total)

---

## 🧮 PHASE 4: PERFORMANCE METRICS

### Test 4.1: FreeRTOS Task Statistics
**Goal:** Verify balanced core utilization

**Prerequisites:**
- Enable task stats in platformio.ini (if available)
- Or use `uxTaskGetSystemState()` in code

**Metrics to Collect:**
- Core 0 CPU usage (%)
- Core 1 CPU usage (%)
- BLE task stack usage (current/max)
- Main loop execution time (ms per cycle)

**Success Criteria:**
- [ ] Core 0 usage <80% (BLE task)
- [ ] Core 1 usage <80% (UI task)
- [ ] Stack usage <80% (both tasks)
- [ ] Main loop cycle time <50ms

---

### Test 4.2: Memory Profiling
**Goal:** Identify memory usage patterns

**Add to code (temporary):**
```cpp
// In bleTaskFunction() and loop()
Serial.printf("[Heap] Free: %d, Min: %d, Max block: %d\n",
    ESP.getFreeHeap(),
    ESP.getMinFreeHeap(),
    ESP.getMaxAllocHeap());

Serial.printf("[PSRAM] Free: %d, Min: %d\n",
    ESP.getFreePsram(),
    ESP.getMinFreePsram());
```

**Metrics to Record:**
- Heap free (typical)
- Heap min (worst case)
- PSRAM free (typical)
- Largest allocatable block

**Success Criteria:**
- [ ] Heap free >50KB (safety margin)
- [ ] PSRAM free >5MB (plenty available)
- [ ] Min heap not decreasing over time

---

## 🐛 PHASE 5: ERROR HANDLING TESTS

### Test 5.1: WebSocket Queue Overflow
**Goal:** Understand and mitigate WebSocket errors

**Current Observation:**
Many `[E][AsyncWebSocket.cpp:434] _queueMessage(): Too many messages queued` errors but system continues running.

**Test Steps:**
1. Connect to WebSerial web interface
2. Let system run for 5 minutes
3. Count queue overflow errors
4. Check if errors correlate with weight updates

**Success Criteria:**
- [ ] Errors do NOT cause crashes
- [ ] Weight data still streams correctly
- [ ] Errors do NOT accumulate
- [ ] WebSerial still accessible

**Optional Fix (if needed):**
- Reduce WebSerial update frequency
- Increase queue size in debug_config.h
- Add rate limiting to DEBUG_PRINT calls

**Metrics to Record:**
- Error frequency (per minute)
- Impact on weight streaming? (Yes/No)
- WebSerial responsiveness (Good/Poor)

---

### Test 5.2: Watchdog Reset Behavior
**Goal:** Verify watchdog protection works

**Test Steps:**
1. Comment out `esp_task_wdt_reset()` in bleTaskFunction()
2. Build and upload
3. Observe system behavior

**Expected Behavior:**
- System should detect task watchdog timeout
- Should trigger controlled reboot
- Should recover automatically

**Success Criteria:**
- [ ] Watchdog timeout detected
- [ ] System reboots (not hangs)
- [ ] System recovers after reboot

**IMPORTANT:** Re-enable `esp_task_wdt_reset()` after test!

---

## 📊 VALIDATION SUMMARY

### Critical Success Criteria (Must Pass ALL)
- [ ] UI responsive during BLE connection (main objective)
- [ ] No crashes during 30-minute test
- [ ] Display sleep/wake works without I2C errors
- [ ] Connection stable for 10+ minutes
- [ ] Stack watermark healthy (>1KB remaining)

### Performance Goals (Should Pass Most)
- [ ] Touch response time excellent
- [ ] Connection time <8 seconds
- [ ] Disconnect detection <8 seconds
- [ ] Heap memory stable over time
- [ ] Core utilization <80%

### Known Issues (Monitor, Fix Optional)
- [ ] WebSocket queue overflow (non-critical)
- [ ] Serial output collision (fixed with delays)

---

## 🚀 PRODUCTION READINESS DECISION

### Go/No-Go Criteria

**GO (Production Ready):**
- All critical success criteria PASS ✅
- 80%+ performance goals PASS ✅
- Known issues documented and non-blocking ✅
- User confirms "super fast" experience ✅

**NO-GO (Needs More Work):**
- Any critical success criteria FAIL ❌
- Crashes observed ❌
- UI freezing still occurs ❌
- Memory leaks detected ❌

---

## 📝 TESTING NOTES & OBSERVATIONS

### Session Date: ___________

**Tester:** ___________

**Environment:**
- Hardware: LilyGO T-Display-S3-Long
- Scale Model: ___________
- Build: Production / Debug (circle one)
- Flash Used: _____%
- RAM Used: _____%

**Overall Assessment:**
```
Pass / Fail / Needs Investigation (circle one)

Notes:
_________________________________________________________________
_________________________________________________________________
_________________________________________________________________
```

**Issues Found:**
1. _______________________________________________________________
2. _______________________________________________________________
3. _______________________________________________________________

**Recommended Actions:**
1. _______________________________________________________________
2. _______________________________________________________________
3. _______________________________________________________________

---

## 🔍 DEBUGGING REFERENCE

### Key Serial Output Messages

**Normal Operation:**
```
[BLE Task] Running on Core: 0
[BLE Task] Core assignment correct!
[Setup] ... main loop on Core 1
[BLE Task] Stack watermark: 18xxx bytes remaining
4.90 (weight data streaming)
```

**Connection Issues:**
```
State timeout: Scanning
Connection timeout - no packets received!
```

**LVGL Crashes:**
```
Guru Meditation Error: Core 1 panic'ed (LoadProhibited)
EXCVADDR: 0x00000010
```

**I2C Crashes:**
```
[E][Wire.cpp:513] requestFrom(): i2cRead returned Error -1
```

**WebSocket (Non-Critical):**
```
[E][AsyncWebSocket.cpp:434] _queueMessage(): Too many messages queued
```

### Important Code Locations

| Component | File | Line | Purpose |
|-----------|------|------|---------|
| BLE Task | GravimetricShots.ino | 1729-1814 | Core 0 BLE operations |
| Main Loop | GravimetricShots.ino | 1807-1843 | Core 1 UI operations |
| Shared Data | GravimetricShots.ino | 122-188 | Thread-safe communication |
| LVGL Guard | GravimetricShots.ino | 1298-1301 | Prevents early LVGL calls |
| I2C Protection | GravimetricShots.ino | 592-628 | Display sleep safety |
| Watchdog Timeout | AcaiaArduinoBLE.h | 29 | 8-second max packet period |

---

## ✅ POST-VALIDATION ACTIONS

### If ALL Tests Pass:
1. [ ] Update CLAUDE.md with "Production Ready" status
2. [ ] Create git commit: "feat: FreeRTOS dual-core implementation validated"
3. [ ] Tag release: `v3.0.0-nimble+freertos`
4. [ ] Update README with performance metrics
5. [ ] Document lessons learned

### If Issues Found:
1. [ ] Document all failures in TESTING_NOTES.md
2. [ ] Prioritize issues (Critical/High/Medium/Low)
3. [ ] Create GitHub issues for tracking
4. [ ] Plan fixes for next session
5. [ ] Re-test after fixes

---

**End of Testing Checklist**

*Good luck with validation! This represents a significant architectural improvement.* 🚀
