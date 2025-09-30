# Gravimetric Shots - Refactoring Progress

## Project Overview
**Current Name:** T-Display-S3-Long (fork)
**New Name:** Gravimetric Shots
**Purpose:** BLE-connected gravimetric espresso scale controller
**Hardware:** LilyGO T-Display-S3-Long (ESP32-S3, 180x640 display)

---

## Library Status

### ArduinoBLE
- **Current:** v1.3.6 (vendored in lib/)
- **Latest:** v1.4.1
- **Customized:** ❌ No
- **Action:** Move to Git reference

### AcaiaArduinoBLE
- **Current:** v2.1.2 (vendored in lib/)
- **Latest:** v3.1.4 available
- **Customized:** ✅ Yes - custom modifications based on personal findings
- **Action:** Keep in lib/ folder (committed to repo)

### LVGL
- **Current:** v8.3.0 (vendored in lib/)
- **Customized:** ❌ No (but using custom UI in lib/ui/)
- **Action:** Move to Git reference, keep lib/ui/

### AXS15231B Display Driver
- **Current:** Up to date with upstream (MD5 match)
- **Action:** No update needed

---

## Refactoring Tasks

### Phase 1: Library Management ⏳

- [ ] **Update platformio.ini with Git library references**
  - Add ArduinoBLE v1.4.1 from Git
  - Add LVGL v8.3.0 from Git
  - Document that AcaiaArduinoBLE stays local (customized)

- [ ] **Clean up lib/ folder**
  - Remove lib/ArduinoBLE/ (will use Git reference)
  - Remove lib/lvgl/ (will use Git reference)
  - Keep lib/AcaiaArduinoBLE/ (customized version)
  - Keep lib/ui/ (custom LVGL UI)
  - Keep lib/lv_conf.h (LVGL config)

- [ ] **Test build with new Git dependencies**
  - Clean build directory
  - Verify PlatformIO downloads libraries correctly
  - Ensure compilation succeeds

### Phase 2: Git Repository Restructure ⏳

- [ ] **Remove upstream remote**
  - Remove Xinyuan-LilyGO/T-Display-S3-Long upstream
  - Keep only origin remote

- [ ] **Update .gitignore if needed**
  - Ensure lib/ folder structure is correct
  - ArduinoBLE and lvgl should not be committed

### Phase 3: Project Renaming ⏳

- [ ] **Update platformio.ini**
  - Change description to "Gravimetric Shots - Espresso Scale Controller"
  - Keep board as T-Display-Long (hardware unchanged)

- [ ] **Rename main application file**
  - Rename gravimetric.ino → GravimetricShots.ino
  - Update platformio.ini src_dir if needed

- [ ] **Add project header comments**
  - Add authorship and project description to main file
  - Update copyright/license information

### Phase 4: Documentation ⏳

- [ ] **Rewrite README.md**
  - Project title: "Gravimetric Shots"
  - Description: Gravimetric espresso shot controller
  - Features section: BLE Acaia scale, shot timer, relay control, LVGL UI
  - Hardware attribution: "Built for LilyGO T-Display-S3-Long"
  - Setup instructions specific to this application
  - Remove generic T-Display examples documentation
  - Keep hardware specs as reference

- [ ] **Update code comments**
  - Review and update any "T-Display" references to reflect project purpose
  - Ensure code is well-documented for your application

### Phase 5: GitHub Migration ⏳

- [ ] **Create new GitHub repository**
  - Repository name: Gravimetric-Shots
  - Description: "BLE-connected gravimetric espresso scale controller for ESP32"
  - Topics: esp32, espresso, acaia, ble, gravimetric-scale, lvgl

- [ ] **Update origin remote**
  - Point to new repository URL

- [ ] **Push refactored code**
  - Commit all changes with clear message
  - Push to new repository

- [ ] **Archive old forked repository** (optional)
  - Archive or delete SongKeat2901/T-Display-S3-Long
  - Or update it to point to new repo

---

## Key Decisions Made

✅ **Project Name:** Gravimetric Shots
✅ **Board Config:** Keep as T-Display-Long (no hardware change)
✅ **ArduinoBLE:** Move to Git reference (not customized)
✅ **AcaiaArduinoBLE:** Keep local in lib/ (customized)
✅ **LVGL:** Move to Git reference (but keep custom UI)
✅ **Multi-computer workflow:** Git references for standard libraries

---

## Notes

- Custom AcaiaArduinoBLE modifications need to be preserved
- UI library (lib/ui/) is custom and stays in repo
- Display driver (AXS15231B.cpp/h) is current and stays in examples/
- Hardware remains LilyGO T-Display-S3-Long - this is just software rebranding