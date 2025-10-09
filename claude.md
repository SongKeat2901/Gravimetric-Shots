# Gravimetric Shots - Project Status

## ✅ Project Migration COMPLETE!

**Project Name:** Gravimetric Shots
**Repository:** https://github.com/SongKeat2901/Gravimetric-Shots
**Hardware:** LilyGO T-Display-S3-Long (ESP32-S3, 180x640 display)
**Status:** Production Ready ✨

---

## ⚠️ CRITICAL FIX APPLIED (Oct 9, 2025)

### 🐛 Display Freeze Issue - RESOLVED
**Problem:** After refactor (commit b6cecdb), display froze at boot
**Root Cause:** Git-downloaded LVGL v8.3.0 (release) ≠ vendored LVGL v8.3.0-dev
**Impact:** Build succeeded but display failed at runtime (silent failure)

### ✅ Solution: Reverted to Vendored Libraries
- Removed Git library dependencies from platformio.ini
- Kept vendored LVGL v8.3.0-dev (matches lv_conf.h configuration)
- Moved code from examples/lvgl_demo/ → src/
- Updated src_dir to point to src/
- **Result:** Display working, all functionality restored ✅

### 📝 Key Lesson Learned
> **LVGL configuration files are version-specific!**
>
> v8.3.0-**dev** (development) ≠ v8.3.0 (stable release)
>
> Even with same version number, -dev suffix means different APIs/config

**See FIX_SUMMARY.md for complete technical analysis**

---

## 🎉 Completed Refactoring (Sep 30, 2025) - REVERTED

### ❌ Phase 1: Library Management (ROLLED BACK)
- ~~Updated platformio.ini with Git library references~~ ← REVERTED
- ~~ArduinoBLE v1.4.1 from Git~~ ← REVERTED
- ~~LVGL v8.3.0 from Git~~ ← REVERTED (caused display freeze)
- ~~Removed vendored ArduinoBLE and LVGL (~360k lines)~~ ← RESTORED
- Kept custom AcaiaArduinoBLE (v2.1.2+) in lib/ ← STILL KEPT
- Build tested successfully ← YES (but display broken)

### ✅ Phase 1 CORRECTED: Vendored Library Management
- **Restored vendored LVGL v8.3.0-dev** from lib/ folder (~180k lines)
- **Restored vendored ArduinoBLE** from lib/ folder (~9k lines)
- Kept custom AcaiaArduinoBLE (v2.1.2+) in lib/
- Updated platformio.ini: empty lib_deps, src_dir = src
- Build AND runtime both successful ✅

### ✅ Phase 2: Git Repository Restructure
- Removed upstream remote (Xinyuan-LilyGO)
- Updated .gitignore for new library structure
- Git-managed libs excluded from repo

### ✅ Phase 3: Project Identity
- Updated description: "Gravimetric Shots - BLE Espresso Scale Controller"
- Renamed gravimetric.ino → GravimetricShots.ino
- Added comprehensive project header with authorship
- Moved src from examples/lvgl_demo to src/

### ✅ Phase 4: Documentation
- Completely rewrote README.md
- Project-focused documentation (not hardware docs)
- Proper LilyGO attribution maintained
- Multi-computer workflow documented

### ✅ Phase 5: GitHub Migration
- Created new repository: Gravimetric-Shots
- Pushed all refactored code
- Updated origin remote → new repo
- Archived old T-Display-S3-Long fork
- Backup created on Desktop

### ✅ Hardware Updates
- Updated PIN_BAT_VOLT: GPIO 2 → 8 (LilyGO correction)
- Verified RELAY1: GPIO 48 (already correct)
- Display driver: Up to date with upstream

---

## 📚 Current Project Structure

```
Gravimetric-Shots/
├── src/
│   ├── GravimetricShots.ino    # Main application (1080 lines)
│   ├── AXS15231B.cpp/.h        # Display driver (AXS15231B)
│   ├── pins_config.h            # Hardware pin definitions
│   └── src/                     # UI assets (test images)
├── lib/
│   ├── AcaiaArduinoBLE/        # Custom BLE library (v2.1.2+)
│   ├── ui/                      # Custom LVGL UI components
│   └── lv_conf.h                # LVGL configuration
├── board/
│   └── T-Display-Long.json     # PlatformIO board definition
├── examples/lvgl_demo/         # Original examples (reference)
├── platformio.ini               # Build configuration
├── README.md                    # Project documentation
└── claude.md                    # This file
```

---

## 🔧 Technology Stack

### Hardware
- **MCU:** ESP32-S3R8 @ 240MHz (dual-core)
- **Flash:** 16MB
- **PSRAM:** 8MB (OPI)
- **Display:** 180×640 QSPI TFT (AXS15231B)
- **Touch:** Capacitive I2C
- **Relay:** GPIO 48

### Software Libraries
**Vendored (In lib/ folder):**
- LVGL v8.3.0-dev (~180k lines)
- ArduinoBLE (~9k lines)
- lv_conf.h (configured for v8.3.0-dev)

**Custom (In Repo):**
- AcaiaArduinoBLE v2.1.2+custom (modified with watchdog)
- Custom LVGL UI components (lib/ui/)

### Features
- BLE Acaia scale integration (Lunar, Pearl S, Pyxis)
- Real-time weight monitoring
- Predictive shot ending (linear regression)
- Relay control for solenoid valve
- Touch UI with weight targeting
- Shot history tracking (1000 datapoints)
- Preferences storage (NVS)

---

## ✨ Recent Improvements (Oct 2025)

### Connection Reliability (Oct 1, 2025)
**From upstream AcaiaArduinoBLE v3.1.4:**
- ✅ Connection watchdog (5-second timeout)
- ✅ Packet timing tracking
- ✅ Automatic disconnect on lost connection
- ✅ Library version tracking (v2.1.2+custom)
- ✅ Fixed heartbeatRequired() typo (_type == NEW)

**Impact:** Prevents hanging on lost BLE connection during shots

### Display Stability (Oct 1, 2025)
- ✅ Commented out Serial.print in init()
- ✅ Prevents display freeze during reconnection
- ✅ init() called from loop() - Serial.print blocks LVGL timer

**Impact:** Display remains responsive during scale reconnection

---

## 🚀 Next Steps / Future Enhancements

### Immediate Tasks
- [x] Test build on a fresh clone from new repo ✅
- [x] Verify multi-computer workflow works ✅
- [x] Test with hardware to ensure everything still works ✅
- [x] Fix display freeze issue ✅ (Oct 9, 2025)

### Feature Enhancements (Optional)
- [x] Review AcaiaArduinoBLE v3.1.4 changes ✅ (Oct 1, 2025)
  - Merged connection watchdog improvements
  - Kept custom features (LVGL integration, battery monitoring)
- [ ] Add shot profiles (different target weights/parameters)
- [ ] Implement shot history export (CSV/JSON)
- [ ] Add WiFi connectivity for remote monitoring
- [ ] Create mobile app companion (optional)
- [ ] Add more scale support (Felicita, Timemore, etc.)

### Code Improvements
- [ ] Add unit tests for critical functions
- [ ] Improve error handling and recovery
- [ ] Add over-the-air (OTA) update support
- [ ] Profile memory usage and optimize if needed
- [ ] Add configuration via web interface

### Documentation
- [ ] Add photos/videos of hardware setup
- [ ] Create wiring diagram for relay connection
- [ ] Document shot profiling algorithm in detail
- [ ] Add troubleshooting FAQ based on issues
- [ ] Create build/setup video tutorial

---

## 📊 Project Statistics

**Current Commit:** 3505796 (base) + Oct 9 fixes
**Date:** Oct 9, 2025
**Build Status:** ✅ Working
**Flash:** 774,761 bytes (24.6% of 3.1MB)
**RAM:** 36,980 bytes (11.3% of 327KB)

**Recent Changes:**
- Restored vendored libraries (~190k lines)
- Added connection watchdog (~50 lines)
- Moved to src/ directory structure
- Fixed display initialization bug

---

## 🔄 Multi-Computer Workflow

### On Any Computer:
```bash
# Clone
git clone https://github.com/SongKeat2901/Gravimetric-Shots.git
cd Gravimetric-Shots

# Build (PlatformIO auto-downloads ArduinoBLE & LVGL)
pio run

# Upload
pio run --target upload
```

### Making Changes:
```bash
# Make your changes
git add -A
git commit -m "Description of changes"
git push

# On another computer
git pull
pio run
```

---

## 📝 Important Notes

### Custom Library Maintenance
- **AcaiaArduinoBLE:** Contains your personal modifications
  - Keep track of what you changed from upstream
  - Consider documenting custom changes in the library folder
  - Upstream v3.1.4 available if you want to review updates

### Hardware Attribution
- This project uses LilyGO T-Display-S3-Long hardware
- Display driver adapted from LilyGO's examples
- Always maintain proper attribution in README

### Git Best Practices
- Commit frequently with descriptive messages
- Test builds before pushing
- Use branches for experimental features
- Keep main branch stable

---

## 🛠️ Useful Commands

```bash
# Build
pio run

# Clean build
pio run --target clean && pio run

# Upload
pio run --target upload

# Monitor serial
pio device monitor --baud 115200

# Check dependencies
pio lib list

# Update Git libraries (if needed)
pio lib update

# Check repo status
gh repo view SongKeat2901/Gravimetric-Shots
```

---

## 📞 Support & Resources

**Repository:** https://github.com/SongKeat2901/Gravimetric-Shots
**Hardware Docs:** https://github.com/Xinyuan-LilyGO/T-Display-S3-Long
**PlatformIO:** https://docs.platformio.org
**LVGL:** https://docs.lvgl.io

---

## ✨ What Makes This Project Unique

1. **Custom Acaia BLE Implementation** - Enhanced reliability and features
2. **Predictive Shot Control** - Linear regression for precise ending
3. **Touch UI** - Native LVGL interface on portrait display
4. **Hardware Integration** - Direct relay control for automation
5. **Open Source** - Full code available for espresso enthusiasts

---

**Status:** Ready for production use and community contributions! ☕️