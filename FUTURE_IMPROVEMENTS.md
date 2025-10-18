# Future Improvements for Gravimetric Shots

## Non-Blocking BLE Connection (State Machine Approach)

### Current Implementation (v2.1.2+custom)
- **Type:** Blocking
- **Method:** `while` loop with 1-second timeout
- **Watchdog Safety:** ✅ Resets watchdog during scan
- **LVGL Responsiveness:** ✅ Calls `LVGLTimerHandlerRoutine()` periodically
- **Pros:**
  - Simple, easy to understand
  - Single function call returns success/failure
  - Well-tested pattern
- **Cons:**
  - Blocks for up to 1 second per attempt
  - Main loop cannot process other tasks during scan

### Proposed Non-Blocking State Machine

#### Overview
Convert `init()` to a non-blocking state machine that distributes connection work across multiple `loop()` iterations.

#### State Diagram
```
IDLE → SCANNING → CONNECTING → DISCOVERING → SUBSCRIBING → IDENTIFYING →
BATTERY_REQUEST → NOTIFICATIONS → CONNECTED
                ↓
              FAILED (timeout)
```

#### Implementation Plan

**1. Add State Enum and Variables (AcaiaArduinoBLE.h)**
```cpp
enum ConnectionState {
    CONN_IDLE,           // Not connected, not scanning
    CONN_SCANNING,       // BLE scan in progress
    CONN_CONNECTING,     // Found scale, connecting
    CONN_DISCOVERING,    // Discovering BLE attributes
    CONN_SUBSCRIBING,    // Subscribing to notifications
    CONN_IDENTIFYING,    // Sending identify command
    CONN_BATTERY,        // Requesting battery level
    CONN_NOTIFICATIONS,  // Enabling weight notifications
    CONN_CONNECTED,      // Fully connected
    CONN_FAILED          // Connection failed
};

class AcaiaArduinoBLE {
private:
    ConnectionState _connState;
    unsigned long _connStateStart;
    BLEDevice _pendingPeripheral;

    // State machine methods
    void stateScan();
    void stateConnect();
    void stateDiscover();
    void stateSubscribe();
    void stateIdentify();
    void stateBattery();
    void stateNotifications();
};
```

**2. Modify init() to Start State Machine**
```cpp
bool AcaiaArduinoBLE::init(String mac)
{
    if (_connState != CONN_IDLE) {
        Serial.println("Connection already in progress");
        return false;
    }

    // Start BLE scan
    if (mac == "") {
        BLE.scan();
    } else if (!BLE.scanForAddress(mac)) {
        Serial.print("Failed to find ");
        Serial.println(mac);
        return false;
    }

    _connState = CONN_SCANNING;
    _connStateStart = millis();
    _mac = mac;

    Serial.println("Starting BLE scan (non-blocking)...");
    return true;  // Returns immediately!
}
```

**3. Add update() Method for State Machine**
```cpp
// Call this from loop() repeatedly
bool AcaiaArduinoBLE::update()
{
    esp_task_wdt_reset();  // Always reset watchdog

    switch (_connState) {
        case CONN_IDLE:
            return false;  // Not connecting

        case CONN_SCANNING:
            stateScan();
            break;

        case CONN_CONNECTING:
            stateConnect();
            break;

        case CONN_DISCOVERING:
            stateDiscover();
            break;

        case CONN_SUBSCRIBING:
            stateSubscribe();
            break;

        case CONN_IDENTIFYING:
            stateIdentify();
            break;

        case CONN_BATTERY:
            stateBattery();
            break;

        case CONN_NOTIFICATIONS:
            stateNotifications();
            break;

        case CONN_CONNECTED:
            return true;  // Connection complete

        case CONN_FAILED:
            Serial.println("Connection failed");
            _connState = CONN_IDLE;
            return false;
    }

    // Check for timeout (10 seconds total)
    if (millis() - _connStateStart > 10000) {
        Serial.println("Connection timeout");
        _connState = CONN_FAILED;
        return false;
    }

    return _connState == CONN_CONNECTED;
}
```

**4. Example State Implementation**
```cpp
void AcaiaArduinoBLE::stateScan()
{
    BLEDevice peripheral = BLE.available();

    if (peripheral && isScaleName(peripheral.localName())) {
        BLE.stopScan();
        _pendingPeripheral = peripheral;
        _connState = CONN_CONNECTING;
        _connStateStart = millis();  // Reset timeout for next state
        Serial.println("Scale found, connecting...");
    }

    // Timeout after 1 second
    if (millis() - _connStateStart > 1000) {
        Serial.println("Scan timeout");
        _connState = CONN_FAILED;
    }
}

void AcaiaArduinoBLE::stateConnect()
{
    if (_pendingPeripheral.connect()) {
        Serial.println("Connected");
        _connState = CONN_DISCOVERING;
        _connStateStart = millis();
    } else {
        Serial.println("Connection failed");
        _connState = CONN_FAILED;
    }
}

// ... implement other states similarly
```

**5. Usage in Main Application**
```cpp
void loop() {
    esp_task_wdt_reset();
    LVGLTimerHandlerRoutine();

    // Check scale connection
    if (!scale.connected()) {
        if (!scale.isConnecting()) {
            // Start new connection attempt
            if (now - lastScaleInitAttempt >= SCALE_INIT_RETRY_MS) {
                scale.init();
                lastScaleInitAttempt = now;
            }
        } else {
            // Update connection state machine
            if (scale.update()) {
                Serial.println("Scale connected!");
                firstConnectionNotificationPending = true;
            }
        }
    }

    // Rest of loop logic runs normally
    checkHeartBeat();
    handleFlushingCycle();
    updateWeightDisplay();
    // ...
}
```

#### Benefits

✅ **Truly Non-Blocking**
- Loop continues processing other tasks during connection
- UI remains fully responsive
- No long blocking delays

✅ **Better User Feedback**
- Can update UI with connection progress
- "Scanning... Connecting... Discovering..."
- Progress bar possible

✅ **More Robust**
- Fine-grained timeout control per state
- Can retry individual states without full restart
- Better error recovery

✅ **Watchdog Safe**
- Watchdog reset at top of `update()`
- No risk of timeout regardless of connection duration

#### Trade-offs

❌ **More Complex**
- ~200 lines of code vs. ~100 for blocking version
- State management overhead
- More variables to track

❌ **Different API**
- Requires calling `update()` repeatedly from loop
- Connection status checked differently
- Requires application code changes

❌ **Testing Overhead**
- More states = more test cases
- Edge cases between states
- Timing-dependent behavior

#### Recommendation

**Current Implementation (Blocking with Watchdog) is Sufficient For:**
- Current use case (home espresso setup)
- Scale usually connects in <500ms
- Watchdog prevents reboot issues
- Simple, proven pattern

**Non-Blocking State Machine Makes Sense If:**
- Multiple scales supported simultaneously
- Complex UI animations during connection
- Other time-critical tasks in loop
- Connection progress UI desired
- Building a commercial product

#### Migration Path

1. ✅ **Phase 1 (DONE):** Add watchdog resets to blocking implementation
2. **Phase 2 (Optional):** Prototype state machine in separate branch
3. **Phase 3 (Optional):** A/B test both implementations
4. **Phase 4 (Optional):** Migrate if benefits outweigh complexity

---

## Other Future Improvements

### Shot Profiles
- Save multiple target weights
- Different parameters per coffee
- Quick-select from UI

### Data Export
- CSV export of shot history
- Upload to cloud storage
- Graph generation

### WiFi Integration
- Remote monitoring
- Mobile app control
- OTA updates

### Additional Scale Support
- Felicita Arc (already in upstream)
- Timemore scales
- Generic Bluetooth scales

---

**Document Version:** 1.0
**Last Updated:** Oct 16, 2025
**Status:** Blocking implementation working well, non-blocking optional for future
