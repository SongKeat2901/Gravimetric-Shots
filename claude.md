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
- Moved code from src/ → src/
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
- Moved src from src to src/

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

## 🙏 Acknowledgments & Attributions

### **Based on Excellent Community Work**

This project builds upon **8 years of community reverse engineering**:

```
h1kari (2015) → bpowers (2016) → AndyZap (2017) → lucapinello (2018) →
frowin (2020s) → **Tate Mazer (2023-present)** ⭐ ← PRIMARY CREDIT
```

### **Primary Credit: Tate Mazer**

**This project is based on [tatemazer/AcaiaArduinoBLE](https://github.com/tatemazer/AcaiaArduinoBLE)**

Tate's ongoing work (2023-present):
- Created and maintains the definitive Arduino/ESP32 Acaia library
- Supports 5+ scale types (Lunar, Pyxis, Pearl S, BooKoo Themis, etc.)
- Active development: Regular updates, bug fixes, new features
- Hardware development: V3.1 PCB for scale integration
- Community support: Discord server, issue tracking
- Connection watchdog (v3.1.2-3.1.3)
- Debug mode for troubleshooting

**Discord:** https://discord.gg/NMXb5VYtre

### **Community Contributors (to Upstream)**

- **Pio Baettig:** Generic scale support, Felicita Arc
- **philgood:** BooKoo Themis support (#18)
- **Jochen Niebuhr:** Lunar 2019 contributions
- **RP:** BooKoo contributions
- **Discord Community:** Testing, feedback, validation

### **This Fork's Specific Modifications**

**Scope:** Specialized fork for embedded LVGL UI, NOT general improvement

**Testing:** LIMITED - LM Micra + Acaia Lunar only

**What was added:**
1. **LVGL Integration** - UI timer handling during BLE operations
2. **Serial.print Fix** - Commented out blocking calls for LVGL compatibility
3. **Merged Upstream** - Connection watchdog from Tate's v3.1.4
4. **Issue #7** - Documented ESP32-S3 17-byte packet behavior

**What was NOT tested:**
- ❌ Other scales (Pyxis, Pearl S, BooKoo Themis)
- ❌ Other machines (Gaggia, Rancilio, E61, etc.)
- ❌ Multiple scales
- ❌ Long-term production use beyond personal setup

### **Important Disclaimer**

**This fork serves ONE specific use case: Embedded LVGL touch UI**

**For general scale integration, use [Tate's upstream library](https://github.com/tatemazer/AcaiaArduinoBLE):**
- More scales supported ✅
- Actively maintained ✅
- Community tested ✅
- Discord support ✅
- Broader hardware validation ✅

**See [ACKNOWLEDGMENTS.md](ACKNOWLEDGMENTS.md) for complete attribution.**

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

**Current Commit:** 27fcbcb (watchdog fix) + Oct 16 non-blocking implementation
**Date:** Oct 16, 2025
**Build Status:** ✅ Working
**Flash:** 783,929 bytes (24.9% of 3.1MB)
**RAM:** 37,196 bytes (11.4% of 327KB)

**Recent Changes:**
- Implemented non-blocking BLE connection state machine (+1,816 bytes)
- Fixed watchdog timeout during scale connection
- Refactored battery request into clean state machine
- Added BLE buffer flush for reliable weight data flow
- Fixed macOS extended attributes build issues

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
- **AcaiaArduinoBLE:** Contains your personal modifications (v2.1.2+custom)
  - **Full documentation:** [IMPLEMENTATION_COMPARISON.md](IMPLEMENTATION_COMPARISON.md)
  - **Changes from upstream:** [lib/AcaiaArduinoBLE/CUSTOM_MODIFICATIONS.md](lib/AcaiaArduinoBLE/CUSTOM_MODIFICATIONS.md)
  - **Protocol research:** [ACAIA_BLE_PROTOCOL_RESEARCH.md](ACAIA_BLE_PROTOCOL_RESEARCH.md)
  - Upstream v3.1.4 improvements already merged (Oct 1, 2025)
  - Your implementation is MORE ROBUST than upstream (see comparison docs)

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

# Troubleshooting builds on macOS
xattr -rc .pio              # Clear extended attributes (fixes SCons errors)
pio run --target clean      # Clean build artifacts
pio run -j 1                # Single-threaded build (avoid race conditions)
```

---

## 🔧 Build System Issues & Solutions

### macOS Extended Attributes Build Failure (Oct 16, 2025)

**Problem:** PlatformIO builds fail with SCons database errors on macOS

**Symptoms:**
- `FileNotFoundError: .sconsign311.tmp` - SCons can't write build database
- `undefined reference to 'loop()'` - Linker can't find compiled .ino file
- Intermittent build failures (sometimes succeeds, sometimes fails)
- Build directories disappear or are incomplete
- `firmware.bin` not created despite "SUCCESS" message

**Root Cause:**
macOS adds extended attributes (metadata) to files and directories:
- **What:** Extended attributes like `com.apple.provenance`, `com.apple.quarantine`
- **When:** File downloads, external drive copies, git operations, PlatformIO package downloads
- **Impact:** SCons build system can't write dependency tracking files (`.d` files, `.sconsign311.tmp`)
- **Result:** Compilation succeeds but linking fails, or build database corruption

**Diagnosis:**
```bash
# Check for extended attributes (look for @ symbol after permissions)
ls -la .pio/
# Example output showing problem:
# drwxr-xr-x@ 3 user staff   96 Oct 16 20:46 .pio/build
#           ^ this @ indicates extended attributes are present

ls -la src/*.ino
# -rw-r--r--@ 1 user staff 39467 Oct 16 20:36 src/GravimetricShots.ino
#           ^ extended attributes on source file

# View specific attributes
xattr -l .pio
```

**Solution:**
```bash
# Remove extended attributes from .pio directory (recursive, clear all)
xattr -rc .pio

# Then build normally
pio run --target upload
```

**Prevention Strategy 1: Build Wrapper Script**
Create `build.sh` in project root:
```bash
#!/bin/bash
# build.sh - Automatically clears extended attributes before building
xattr -rc .pio 2>/dev/null  # Clear attributes (suppress errors if none exist)
~/.platformio/penv/bin/platformio run "$@"
```

Make executable: `chmod +x build.sh`

Usage: `./build.sh --target upload`

**Prevention Strategy 2: Git Hook (Automatic)**
Add to `.git/hooks/post-checkout` and `.git/hooks/post-merge`:
```bash
#!/bin/bash
# Auto-clear extended attributes after git operations
xattr -rc .pio 2>/dev/null || true
xattr -rc src 2>/dev/null || true
```

Make executable: `chmod +x .git/hooks/post-checkout .git/hooks/post-merge`

**Why Extended Attributes Keep Returning:**
- PlatformIO downloads packages → macOS quarantine flag added
- Git operations on external repos → provenance tracking added
- Copying files between drives → metadata preserved
- Must clear attributes after these operations

**Key Insight:**
This is a **macOS-specific issue**. Linux and Windows don't have this extended attributes system, so builds work normally on those platforms. If you're experiencing build failures on macOS but CI/CD or other developers' builds work fine, extended attributes are likely the cause.

**Timeline of Discovery:**
- **Problem:** Non-blocking state machine implementation built successfully once (783,929 bytes)
- **Failure:** Subsequent builds failed with SCons errors
- **Discovery:** `ls -la` showed `@` flags on `.pio/` directory and files
- **Solution:** `xattr -rc .pio` cleared attributes → build succeeded
- **Result:** Firmware uploaded successfully, non-blocking implementation deployed

**Alternative Workarounds (if xattr doesn't work):**
```bash
# Nuclear option: Delete entire .pio directory and rebuild from scratch
rm -rf .pio
pio run

# Use Arduino IDE instead (doesn't use SCons)
# Use esp-idf directly (bypasses PlatformIO entirely)
```

**Reference Links:**
- Apple Developer: [Extended Attributes Overview](https://developer.apple.com/library/archive/documentation/FileManagement/Conceptual/FileSystemProgrammingGuide/FileSystemDetails/FileSystemDetails.html)
- PlatformIO Issue Tracker: Similar SCons issues reported by macOS users
- This issue first encountered: Oct 16, 2025 during non-blocking implementation

---

## 📞 Support & Resources

### Project Documentation
- **Main Repository:** https://github.com/SongKeat2901/Gravimetric-Shots
- **Protocol Research:** [ACAIA_BLE_PROTOCOL_RESEARCH.md](ACAIA_BLE_PROTOCOL_RESEARCH.md) - 8-year history of reverse engineering
- **Implementation Analysis:** [IMPLEMENTATION_COMPARISON.md](IMPLEMENTATION_COMPARISON.md) - Why your code is more robust
- **Custom Modifications:** [lib/AcaiaArduinoBLE/CUSTOM_MODIFICATIONS.md](lib/AcaiaArduinoBLE/CUSTOM_MODIFICATIONS.md) - v2.1.2 vs v3.1.4

### External Resources
- **Hardware Docs:** https://github.com/Xinyuan-LilyGO/T-Display-S3-Long
- **PlatformIO:** https://docs.platformio.org
- **LVGL:** https://docs.lvgl.io
- **Upstream AcaiaArduinoBLE:** https://github.com/tatemazer/AcaiaArduinoBLE
- **Your Issue #7:** https://github.com/tatemazer/AcaiaArduinoBLE/issues/7
- **BLE Reverse Engineering Guide:** https://reverse-engineering-ble-devices.readthedocs.io

---

## ✨ What Makes This Project Unique

1. **Custom Acaia BLE Implementation** - Enhanced reliability and features
2. **Predictive Shot Control** - Linear regression for precise ending
3. **Touch UI** - Native LVGL interface on portrait display
4. **Hardware Integration** - Direct relay control for automation
5. **Open Source** - Full code available for espresso enthusiasts

---

**Status:** Ready for production use and community contributions! ☕️