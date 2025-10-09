# AcaiaArduinoBLE Custom Modifications

**Base Version:** v2.1.2 (from tatemazer/AcaiaArduinoBLE)
**Upstream Latest:** v3.1.4
**Status:** Custom modified version for Gravimetric Shots project

---

## 🔍 Key Differences: Your v2.1.2 vs Upstream v3.1.4

### 1. **Constructor & Debug Support**

**Upstream v3.1.4:**
```cpp
AcaiaArduinoBLE(bool debug);  // Constructor with debug parameter
bool _debug;                   // Debug flag member
```

**Your Version:**
```cpp
AcaiaArduinoBLE();            // Simple constructor, no debug param
// No debug flag
```

**Impact:** Upstream added extensive debug logging capabilities. Your version is cleaner for production use.

---

### 2. **Connection Watchdog (CRITICAL NEW FEATURE)**

**Upstream v3.1.4:**
```cpp
#define MAX_PACKET_PERIOD_MS 5000
long _packetPeriod;
long _lastPacket;

// In init():
_lastPacket = 0;
_packetPeriod = 0;

// Monitors packet timing to detect disconnects
```

**Your Version:**
```cpp
// No watchdog implementation
```

**Impact:** 🚨 **IMPORTANT** - Upstream added disconnect detection (v3.1.2-3.1.3). This prevents hanging on lost connections.

---

### 3. **Generic Scale UUIDs**

**Upstream v3.1.4:**
```cpp
#define WRITE_CHAR_GENERIC "ff12"
#define READ_CHAR_GENERIC  "ff11"
byte TARE_GENERIC[6]       = { 0x03, 0x0a, 0x01, 0x00, 0x00, 0x08 };
byte START_TIMER_GENERIC[6] = { 0x03, 0x0a, 0x04, 0x00, 0x00, 0x0a };
byte STOP_TIMER_GENERIC[6]  = { 0x03, 0x0a, 0x05, 0x00, 0x00, 0x0d };
byte RESET_TIMER_GENERIC[6] = { 0x03, 0x0a, 0x06, 0x00, 0x00, 0x0c };
```

**Your Version:**
```cpp
#define WRITE_CHAR_GENERIC "ffe1"
#define READ_CHAR_GENERIC  "ffe1"
byte TARE_GENERIC[1] = {0x54};
// No generic timer commands
```

**Impact:** Different generic scale support. Upstream supports BooKoo Themis, your version uses simpler protocol.

---

### 4. **Library Version Tracking**

**Upstream v3.1.4:**
```cpp
#define LIBRARY_VERSION "3.1.3"

// In init():
Serial.print("AcaiaArduinoBLE Library v");
Serial.print(LIBRARY_VERSION);
Serial.println(" reinitializing...");
```

**Your Version:**
```cpp
// No version tracking
```

**Impact:** Minor - just logging improvement.

---

### 5. **LVGL Integration (YOUR CUSTOM FEATURE)**

**Your Version:**
```cpp
const int lvUpdateInterval = 16;
unsigned long lastLvUpdate = 0;
extern void LVGLTimerHandlerRoutine();

// In init() loop:
unsigned long currentMillis = millis();
if (currentMillis - lastLvUpdate >= lvUpdateInterval) {
    LVGLTimerHandlerRoutine();
    lastLvUpdate = currentMillis;
}
```

**Upstream v3.1.4:**
```cpp
// No LVGL integration
```

**Impact:** ✨ **YOUR CUSTOM FEATURE** - Keeps LVGL UI responsive during BLE connection. This is critical for your touch UI!

---

### 6. **Battery Monitoring**

**Your Version:**
```cpp
bool getBattery();
bool updateBattery();
int batteryValue();
int _currentBattery;
```

**Upstream v3.1.4:**
```cpp
// Battery features removed in v3.x
```

**Impact:** You have battery monitoring, upstream dropped it.

---

### 7. **Initial Weight Value**

**Your Version:**
```cpp
_currentWeight = 999;  // Initialize to 999
```

**Upstream v3.1.4:**
```cpp
_currentWeight = 0;    // Initialize to 0
```

**Impact:** Minor - you use 999 to detect "not yet received" state.

---

## 🎯 Summary of Changes

### Features in Upstream v3.1.4 You're Missing:

1. ✅ **Connection Watchdog** (v3.1.2-3.1.3)
   - Detects lost connections via packet timeout
   - Prevents hanging on disconnect
   - **RECOMMENDED TO ADD**

2. ✅ **Debug Mode** (v3.0.0+)
   - Extensive debug logging
   - Helps troubleshooting
   - Optional to add

3. ✅ **Library Version Tracking** (v3.1.0+)
   - Version string in Serial output
   - Nice to have

4. ✅ **Updated Generic Scale Support** (v3.1.0+)
   - BooKoo Themis support
   - Felicita improvements
   - Only needed if using those scales

### Features You Have That Upstream Doesn't:

1. ✨ **LVGL Integration**
   - Keeps UI responsive during connection
   - **CRITICAL FOR YOUR PROJECT**

2. ✨ **Battery Monitoring**
   - getBattery(), updateBattery(), batteryValue()
   - **YOUR FEATURE**

3. ✨ **Initial Weight = 999**
   - Better "not ready" detection
   - **YOUR DESIGN CHOICE**

---

## 🚀 Recommended Actions

### High Priority:
- [ ] **Add connection watchdog from v3.1.4**
  - Prevents hanging on lost connection
  - Merge `_packetPeriod` and `_lastPacket` logic
  - Keep your LVGL integration

### Medium Priority:
- [ ] **Review generic scale UUID changes**
  - If you use Felicita/BooKoo scales
  - Current UUIDs: `ffe1` vs upstream `ff11/ff12`

### Low Priority:
- [ ] Add library version tracking
- [ ] Add debug mode flag (optional)

### Don't Change:
- ✅ Keep LVGL integration (your custom feature)
- ✅ Keep battery monitoring (your custom feature)
- ✅ Keep initial weight = 999 (your design)

---

## 📋 Change Log Between v2.1.2 → v3.1.4

**v2.3.0 (Jun 2024):**
- Added AUTOTARE constant

**v3.0.0 (Jul 2024):**
- V3 hardware implementation
- Added debug mode support

**v3.1.0 (Nov 2024):**
- Added BooKoo Themis Scale Support
- Fixed auto-reset timer
- Added Pearl S Support

**v3.1.1 (Jan 2025):**
- Lunar 2021 AL008 compatibility

**v3.1.2 (Feb 2025):**
- **Connection watchdog added** 🚨

**v3.1.3 (Jun 2025):**
- **Bug fix: stop brewing if disconnected** 🚨

**v3.1.4 (Sep 2025):**
- Minor refinements

---

## 💡 Integration Strategy

### Option 1: Selective Merge (Recommended)
Keep your version, add only the connection watchdog:

```cpp
// Add to class:
long _packetPeriod;
long _lastPacket;

// Add MAX_PACKET_PERIOD_MS define
#define MAX_PACKET_PERIOD_MS 5000

// In init(), after _connected = true:
_packetPeriod = 0;
_lastPacket = millis();

// In getWeight() or loop:
// Check for timeout and handle disconnect
```

### Option 2: Full Upgrade
Replace with v3.1.4, then re-add your customizations:
- LVGL integration
- Battery monitoring
- Initial weight = 999

**Effort:** High, risk of introducing bugs

### Option 3: Keep As-Is
Stay on your modified v2.1.2
- **Risk:** No disconnect detection

---

## 🔗 Useful Links

- **Upstream Repository:** https://github.com/tatemazer/AcaiaArduinoBLE
- **Upstream v3.1.4:** https://github.com/tatemazer/AcaiaArduinoBLE/releases/tag/v3.1.4
- **Your Version:** lib/AcaiaArduinoBLE/ (modified v2.1.2)

---

**Recommendation:** Add connection watchdog from v3.1.4 while keeping your LVGL and battery features.