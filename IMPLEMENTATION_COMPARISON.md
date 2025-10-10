# AcaiaArduinoBLE - LVGL Fork Analysis
## Specialized Fork for Embedded UI Applications

**Date:** October 10, 2025
**Base:** tatemazer/AcaiaArduinoBLE v2.1.2
**Upstream Latest:** v3.1.4+
**Testing:** LIMITED - LM Micra + Acaia Lunar only

---

## ⚠️ IMPORTANT DISCLAIMERS

### **Testing Scope is Extremely Limited**

✅ **TESTED:**
- **Machine:** La Marzocco (LM) Micra espresso machine (1 unit)
- **Scale:** Acaia Lunar USB-C 2021 version (1 unit)
- **Display:** LilyGO T-Display-S3-Long
- **Use Case:** Embedded touch UI application

❌ **NOT TESTED:**
- Acaia Pyxis
- Acaia Pearl S
- BooKoo Themis
- Felicita Arc
- Other espresso machines
- Other hardware platforms
- Multiple simultaneous scales
- Production environments beyond personal use

### **This is NOT a General Improvement**

This fork serves **ONE specific use case:**
- Embedded systems with LVGL touch UI
- ESP32-S3 platform
- Single-machine, single-scale setup

**For general scale integration, use [Tate's upstream library](https://github.com/tatemazer/AcaiaArduinoBLE)** which has:
- Broader hardware testing
- More scale support
- Active community maintenance
- Discord support
- Regular updates

---

## 📊 What This Fork Adds

### **Primary Addition: LVGL Integration**

**Problem Solved:**
- BLE scanning (1000ms timeout) blocks LVGL timer
- Display freezes during scale connection/reconnection
- Touch input becomes unresponsive

**Solution:**
```cpp
// In AcaiaArduinoBLE::init() scanning loop:
unsigned long currentMillis = millis();
if (currentMillis - lastLvUpdate >= lvUpdateInterval) {
    LVGLTimerHandlerRoutine();  // Keep UI alive
    lastLvUpdate = currentMillis;
}
```

**Impact:**
- ✅ UI remains responsive during BLE operations (16ms updates)
- ✅ Touch interface works during scanning
- ✅ Professional user experience for embedded UI

**Limitation:**
- Only useful if you're using LVGL for touch UI
- Adds coupling to external UI framework
- Not relevant for headless/serial-only applications

---

### **Secondary Addition: Serial.print Fix**

**Problem:**
- `init()` is called from `loop()` during reconnection attempts
- `Serial.print()` can block for milliseconds
- Blocks LVGL timer → display freeze

**Solution:**
```cpp
// Commented out in AcaiaArduinoBLE.cpp:
// Serial.print("AcaiaArduinoBLE Library v");
// Serial.print(LIBRARY_VERSION);
// Serial.println(" reinitializing...");
```

**Impact:**
- ✅ Smooth reconnection without display freeze
- ✅ LVGL timer continues uninterrupted

**Limitation:**
- Loses helpful debug output
- Upstream may have addressed this differently
- Only matters for LVGL-based applications

---

### **Merged Features from Upstream v3.1.4**

**Added (Oct 1, 2025):**
- ✅ Connection watchdog (5-second timeout)
- ✅ Packet timing tracking
- ✅ Automatic disconnect detection

**Credit:** These improvements are from **Tate Mazer's upstream work (v3.1.2-3.1.3)**. I simply merged them into this fork.

---

### **Issue #7 Contribution**

**Discovery:**
- ESP32-S3 + Lunar firmware v1.0.0.16
- `valueLength()` returns 17 bytes (not standard 13)
- Only first 13 bytes contain valid data

**Contribution:**
- Reported issue to upstream
- Documented workaround: `readValue(input, 13)`

**Impact:**
- Helped other ESP32-S3 users
- Documented firmware-specific behavior

**Limitation:**
- Not a fix, just a workaround documented in issue tracker
- Upstream may handle this differently
- Only tested on ONE firmware version

---

## 🔍 Feature Comparison

### **What Upstream Has That This Fork Doesn't**

| Feature | Upstream v3.1.4+ | This Fork |
|---------|------------------|-----------|
| **Debug Mode** | ✅ Runtime flag | ❌ None |
| **BooKoo Themis Support** | ✅ Yes | ❌ No |
| **Updated Generic UUIDs** | ✅ ff11/ff12 | ❌ Old ffe1 |
| **Broader Scale Testing** | ✅ Multiple scales | ❌ Lunar only |
| **Active Development** | ✅ Ongoing | ❌ Frozen fork |
| **Community Support** | ✅ Discord | ❌ None |
| **Hardware Development** | ✅ V3.1 PCB | ❌ None |

### **What This Fork Has**

| Feature | Upstream v3.1.4+ | This Fork |
|---------|------------------|-----------|
| **LVGL Integration** | ❌ None | ✅ Yes |
| **Serial.print Fix** | ❌ Still present | ✅ Commented out |
| **Battery Monitoring** | ❌ Removed in v3.x | ✅ Kept |

---

## 🎯 Use Case Matrix

### **When to Use Upstream (tatemazer/AcaiaArduinoBLE)**

✅ **General scale integration**
✅ **Multiple scale support** (Lunar, Pyxis, Pearl S, BooKoo)
✅ **Headless/serial applications**
✅ **Active community support** (Discord)
✅ **Regular updates and bug fixes**
✅ **Hardware development** (V3.1 PCB)
✅ **Production reliability** (tested by community)

### **When to Use This Fork**

✅ **Embedded LVGL touch UI** (primary use case)
✅ **ESP32-S3 platform** (tested hardware)
✅ **LM Micra + Acaia Lunar** (exact same setup)
✅ **Battery level monitoring** (if needed)
✅ **Willing to accept limited testing scope**

⚠️ **WARNING:** If your setup differs from LM Micra + Lunar, consider using upstream instead.

---

## 📈 Honest Assessment

### **What This Fork Does Well**

1. ✅ **LVGL Integration Works** - Tested and production-validated
2. ✅ **Specific Use Case Solved** - Embedded UI applications
3. ✅ **Documented Limitations** - Clear about testing scope
4. ✅ **Merged Upstream Improvements** - Connection watchdog included

### **What This Fork Lacks**

1. ❌ **Limited Testing** - One machine, one scale
2. ❌ **Not Actively Maintained** - Fork frozen at v2.1.2 base
3. ❌ **Missing Upstream Features** - Debug mode, BooKoo support, etc.
4. ❌ **No Community Support** - No Discord, limited issue tracking
5. ❌ **Narrow Hardware Validation** - ESP32-S3 + Lunar only

---

## 🔄 Comparison Summary

### **Upstream (tatemazer/AcaiaArduinoBLE) is Better For:**

- **99% of use cases**
- General scale integration projects
- Multiple scale support
- Headless applications
- Production reliability
- Community support
- Ongoing development

### **This Fork is Better For:**

- **1% of use cases** - Embedded LVGL UI
- ESP32-S3 + touch screen + LVGL
- LM Micra + Acaia Lunar (tested setup)
- Willing to maintain your own fork

---

## 💡 Recommendations

### **If You're Starting a New Project:**

1. **Start with [tatemazer/AcaiaArduinoBLE](https://github.com/tatemazer/AcaiaArduinoBLE)**
   - Join Discord: https://discord.gg/NMXb5VYtre
   - Better tested, more features, active support

2. **Only use this fork if:**
   - You specifically need LVGL integration
   - You have LM Micra + Acaia Lunar (same hardware)
   - You understand the limited testing scope

3. **Consider contributing LVGL support to upstream:**
   - Tate may accept a PR for optional LVGL integration
   - Benefits entire community
   - Better than maintaining separate fork

---

## 🙏 Credit Where Credit is Due

### **Primary Credit: Tate Mazer**

This fork is based on **Tate's excellent work:**
- Created and maintains AcaiaArduinoBLE (2023-present)
- Supports 5+ scale types
- Active community development
- Hardware V3.1 PCB design
- Connection reliability improvements
- Discord community support

**Without Tate's foundation, this fork would not exist.**

### **Community Contributors to Upstream**

- **Pio Baettig:** Generic scale support, Felicita Arc
- **philgood:** BooKoo Themis support
- **Jochen Niebuhr:** Lunar 2019 contributions
- **RP:** BooKoo contributions
- **Discord Community:** Testing and feedback

### **This Fork's Additions (SongKeat)**

- LVGL integration for touch UI
- Serial.print blocking fix
- Limited validation on LM Micra + Lunar
- Issue #7 documentation

---

## 📊 Code Metrics

### **Lines of Code**

| Category | Upstream v3.1.4 | This Fork | Difference |
|----------|-----------------|-----------|------------|
| Header (.h) | ~50 lines | ~66 lines | +16 (LVGL) |
| Implementation (.cpp) | ~280 lines | ~397 lines | +117 (LVGL + battery) |

**Analysis:** +133 lines = LVGL integration + battery monitoring (kept from older version)

### **Memory Usage (This Project)**

- **Flash:** 774,761 bytes (24.6% of 3.1MB) - includes LVGL UI code
- **RAM:** 36,980 bytes (11.3% of 327KB) - minimal overhead
- **AcaiaArduinoBLE Contribution:** ~5-8KB flash

---

## 🔬 Testing Evidence

### **Production Validation**

✅ **Tested:**
- 100+ espresso shots with LM Micra + Lunar
- Connection reliability during shots
- Display responsiveness
- Touch UI interaction
- Battery monitoring

❌ **NOT Tested:**
- Other scales (Pyxis, Pearl S, BooKoo Themis)
- Other machines (Gaggia, Rancilio, E61, etc.)
- Multiple scales simultaneously
- Long-term reliability (months/years)
- Edge cases and error conditions

---

## 🎯 Final Verdict

### **Upstream (tatemazer/AcaiaArduinoBLE): ⭐⭐⭐⭐⭐**

**Recommended for:**
- General use ✅
- Multiple scales ✅
- Active support ✅
- Ongoing development ✅

### **This Fork (SongKeat LVGL): ⭐⭐⭐**

**Recommended for:**
- Embedded LVGL UI ✅
- LM Micra + Lunar ✅
- Limited testing ⚠️
- Frozen development ⚠️

---

## 📞 Support

### **For General Scale Integration:**
- **Use:** [tatemazer/AcaiaArduinoBLE](https://github.com/tatemazer/AcaiaArduinoBLE)
- **Discord:** https://discord.gg/NMXb5VYtre
- **Support:** Active community

### **For This LVGL Fork:**
- **Use:** This repository (Gravimetric-Shots)
- **Support:** Limited - Issues on GitHub only
- **Recommendation:** Consider upstream first

---

## 🤝 Acknowledgments

**Thank you to:**
1. **Tate Mazer** - for creating and maintaining the excellent AcaiaArduinoBLE library
2. **Community contributors** - Pio, philgood, Jochen, RP, Discord members
3. **Historical pioneers** - h1kari, bpowers, AndyZap, lucapinello, frowin

**This fork stands on the shoulders of giants.**

---

## 📝 Conclusion

This fork serves a **specific niche: embedded LVGL UI applications**.

**For most users, [Tate's upstream library](https://github.com/tatemazer/AcaiaArduinoBLE) is the better choice:**
- More scales supported
- Actively maintained
- Community tested
- Broader hardware validation

**Use this fork only if you have the EXACT same use case and are willing to accept limited testing scope.**

---

**Analysis Date:** October 10, 2025
**Purpose:** Technical documentation, not marketing material
**Tone:** Honest assessment of limitations and scope
**Recommendation:** Use upstream for general projects ⭐
