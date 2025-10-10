# Gravimetric Shots

> **Precision espresso control through gravimetric flow profiling**

A BLE-connected espresso scale controller with predictive shot ending, built on the LilyGO T-Display-S3-Long ESP32 platform.

[![PlatformIO](https://img.shields.io/badge/PlatformIO-ESP32--S3-orange.svg)](https://platformio.org/)
[![Hardware](https://img.shields.io/badge/Hardware-LilyGO%20T--Display--S3--Long-blue.svg)](https://www.lilygo.cc/products/t-display-s3-long)
[![License](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)

---

## 📖 What is Gravimetric Shots?

**Gravimetric Shots** is an embedded espresso controller that uses real-time weight data from Acaia BLE scales to automate shot profiling. It predicts the optimal shot endpoint using linear regression and controls a solenoid valve via relay to achieve precise extraction targets.

**Key Innovation:** Eliminates manual shot stopping by monitoring flow rate deceleration and predicting the final weight before it occurs.

**Tested Configuration:** La Marzocco Micra + Acaia Lunar 2021

---

## ✨ Features

### Core Functionality
- 📊 **Real-time BLE Scale Integration** - Connects to Acaia Lunar, Pyxis, Pearl S scales
- 🎯 **Predictive Shot Ending** - Linear regression algorithm predicts final weight 1-2 seconds early
- ⚡ **Relay Automation** - Controls solenoid valve on GPIO 48 for hands-free operation
- 🖥️ **Touch UI** - LVGL-based interface on 180×640 portrait display
- 📈 **Shot History Tracking** - Records up to 1000 datapoints per session
- 💾 **Persistent Settings** - NVS-based preferences storage
- 🔋 **Battery Monitoring** - Real-time voltage display and power management

### Reliability Features
- 🔄 **Connection Watchdog** - 5-second timeout for lost BLE connections
- 🛡️ **Automatic Recovery** - Detects and reconnects on dropped connections
- 📡 **Packet Timing Tracking** - Monitors communication health

---

## 🚀 Quick Start

### Prerequisites
- **Hardware:** [LilyGO T-Display-S3-Long](https://www.lilygo.cc/products/t-display-s3-long) (ESP32-S3R8, 16MB Flash, 8MB PSRAM)
- **Scale:** Acaia Lunar, Pyxis, or Pearl S
- **IDE:** [PlatformIO](https://platformio.org/) (recommended) or Arduino IDE
- **Optional:** Relay module for solenoid valve control

### Installation (PlatformIO)

1. **Clone the repository:**
   ```bash
   git clone https://github.com/SongKeat2901/Gravimetric-Shots.git
   cd Gravimetric-Shots
   ```

2. **Open in Visual Studio Code:**
   - Install [VS Code](https://code.visualstudio.com/) and [PlatformIO extension](https://platformio.org/install/ide?install=vscode)
   - `File` → `Open Folder` → select `Gravimetric-Shots` directory
   - PlatformIO will auto-install dependencies

3. **Build and Upload:**
   - Click ✔ (Build) in PlatformIO toolbar
   - Connect T-Display-S3-Long via USB
   - Click → (Upload)
   - Click 🔌 (Serial Monitor) to view debug output

4. **First Run:**
   - Power on your Acaia scale
   - Touch "Connect" on the display
   - Select your scale from the BLE scan list
   - Set your target weight (e.g., 36.0g for a 2:1 ratio)
   - Start pulling your shot!

---

## 🎬 Usage Guide

### Basic Operation

1. **Power On:** Display shows main screen with battery voltage
2. **Connect Scale:** Touch "Connect" → Select scale from BLE scan
3. **Tare Scale:** Place portafilter, touch "Tare" button
4. **Set Target:** Adjust target weight using +/- buttons (default: 36.0g)
5. **Start Shot:** Pull shot on espresso machine
6. **Automatic Stop:** Relay triggers when predicted weight reaches target
7. **Manual Override:** Touch "Stop" button anytime to abort

### Shot Profiling Algorithm

The controller uses **predictive linear regression** to determine shot endpoint:

1. **Monitoring Phase:** Tracks weight every 250ms during extraction
2. **Flow Rate Analysis:** Calculates first derivative (g/s) to detect deceleration
3. **Prediction:** Projects final weight based on current flow rate trend
4. **Early Trigger:** Stops shot 1-2 seconds before target to account for post-stop drips

**Example:** For a 36g target, the relay may trigger at 34.2g if flow rate indicates 1.8g more drips will occur after valve closure.

---

## 🔧 Hardware Requirements

### Main Board
| Component | Specification |
|-----------|--------------|
| **MCU** | ESP32-S3R8 (dual-core @ 240MHz) |
| **Flash** | 16MB |
| **PSRAM** | 8MB (OPI) |
| **Display** | 180×640 QSPI TFT (AXS15231B) |
| **Touch** | Capacitive I2C |
| **USB** | USB-C (JTAG upload) |
| **Battery** | LiPo charging circuit (voltage monitoring on GPIO 8) |

### Supported Scales
- ✅ **Acaia Lunar** (USB-Micro pre-2021 & USB-C 2021+)
- ✅ **Acaia Pyxis**
- ✅ **Acaia Pearl S**

### Optional Hardware
- **Relay Module:** 5V relay (GPIO 48) for solenoid valve control
- **Solenoid Valve:** 3-way valve for espresso machine integration

### Pin Assignments
```cpp
// Defined in src/pins_config.h
#define PIN_BAT_VOLT   8   // Battery voltage (ADC)
#define RELAY1        48   // Relay control output
#define PIN_LCD_BL    38   // Display backlight
#define PIN_TOUCH_RES 21   // Touch reset
```

---

## 📦 Installation (Alternative Methods)

### Arduino IDE Setup

**⚠️ PlatformIO is strongly recommended.** Arduino IDE requires manual library installation.

1. **Clone repository:**
   ```bash
   git clone https://github.com/SongKeat2901/Gravimetric-Shots.git
   ```

2. **Copy vendored libraries:**
   - Copy all folders from `lib/` to Arduino libraries folder:
     - Windows: `C:\Users\YourName\Documents\Arduino\libraries\`
     - macOS: `~/Documents/Arduino/libraries/`
     - Linux: `~/Arduino/libraries/`

3. **Open sketch:**
   - Open `src/GravimetricShots.ino` in Arduino IDE

4. **Board configuration:**
   - **Board:** "ESP32S3 Dev Module"
   - **USB CDC On Boot:** "Enabled"
   - **Flash Size:** "16MB (128Mb)"
   - **Partition Scheme:** "Huge APP (3MB No OTA/1MB SPIFFS)"
   - **PSRAM:** "OPI PSRAM"

5. **Upload:**
   - Select COM port
   - Click Upload
   - If upload fails, see **Troubleshooting** below

---

## 🗂️ Project Structure

```txt
Gravimetric-Shots/
├── src/
│   ├── GravimetricShots.ino    # Main application (1080 lines)
│   ├── AXS15231B.cpp/.h        # Display driver (AXS15231B)
│   ├── pins_config.h            # Hardware pin definitions
│   └── img/                     # UI assets (test images)
├── lib/
│   ├── AcaiaArduinoBLE/        # Custom BLE library (v2.1.2+)
│   ├── ArduinoBLE/             # Vendored ArduinoBLE
│   ├── lvgl/                   # Vendored LVGL v8.3.0-dev
│   ├── ui/                      # Custom LVGL UI components
│   └── lv_conf.h                # LVGL configuration
├── board/
│   └── T-Display-Long.json     # PlatformIO board definition
├── platformio.ini               # Build configuration
├── README.md                    # This file
└── ACKNOWLEDGMENTS.md           # Full attribution history
```

---

## 📊 Technical Details

### Software Stack
- **Framework:** Arduino-ESP32
- **Graphics:** LVGL 8.3.0-dev (vendored, DO NOT UPGRADE)
- **BLE:** ArduinoBLE + Custom AcaiaArduinoBLE fork
- **Storage:** ESP32 NVS (Non-Volatile Storage)

### Memory Usage (Current Build)
- **Flash:** 774,761 bytes (24.6% of 3.1MB)
- **RAM:** 36,980 bytes (11.3% of 327KB)

### Build Environment
```ini
[env:gravimetric_shots]
platform = espressif32
board = T-Display-Long
framework = arduino
src_dir = src
lib_deps =
    # All dependencies vendored in lib/ folder
```

---

## 🛠️ Troubleshooting

### Upload Fails or USB Not Detected

**Manual Boot Mode Entry:**
1. Connect board via USB cable
2. Press and hold **BOOT** button
3. While holding BOOT, press **RST** button
4. Release **RST** button
5. Release **BOOT** button
6. Click Upload in IDE

### Display Not Working After Upload

- Check vendored LVGL version matches `lv_conf.h` (must be v8.3.0-dev)
- Verify `src_dir = src` in `platformio.ini`
- Ensure `lib_deps` is empty (all libraries are vendored)

### BLE Connection Drops During Shot

- Connection watchdog will auto-reconnect within 5 seconds
- Check scale battery level (low battery causes disconnections)
- Reduce distance between ESP32 and scale (< 2 meters)

### LED Flashing When No Battery Connected

- This is normal behavior when powered via USB only
- To disable: `PMU.disableStatLed();` (but disables charging indicator)

---

## 🙏 Acknowledgments

### Primary Credit

**This project is based on [tatemazer/AcaiaArduinoBLE](https://github.com/tatemazer/AcaiaArduinoBLE)** ⭐

**Tate Mazer** (2023-present) created and maintains the definitive Arduino/ESP32 Acaia library:
- Supports 5+ scale types (Lunar, Pyxis, Pearl S, BooKoo Themis, etc.)
- Active development with regular updates and bug fixes
- Community support via [Discord](https://discord.gg/NMXb5VYtre)
- Hardware development (V3.1 PCB for scale integration)

### Community Contributors (Upstream)

**8 years of reverse engineering** by the espresso community:
- **h1kari** (2015) - Initial Acaia protocol reverse engineering
- **bpowers** (2016) - Python implementation
- **AndyZap** (2017) - ESP8266 Arduino port
- **lucapinello** (2018) - ESP32 migration
- **frowin** (2020s) - Protocol refinements
- **Pio Baettig** - Generic scale support, Felicita Arc
- **philgood** - BooKoo Themis support
- **Jochen Niebuhr, RP** - Testing and contributions

### Hardware

- **LilyGO / Xinyuan-LilyGO** - [T-Display-S3-Long](https://github.com/Xinyuan-LilyGO/T-Display-S3-Long) hardware design and display drivers

### This Fork's Modifications

**Scope:** Specialized fork for embedded LVGL UI integration

**What was added:**
1. LVGL touch UI integration with BLE event loop
2. Predictive shot ending algorithm (linear regression)
3. Relay control for espresso machine automation
4. Connection watchdog merged from upstream v3.1.4
5. Serial.print fixes for LVGL timer compatibility

**Testing:** LIMITED - La Marzocco Micra + Acaia Lunar 2021 only

**Important:** For general Acaia scale integration, use [Tate's upstream library](https://github.com/tatemazer/AcaiaArduinoBLE) - it's more robust, widely tested, and actively supported.

**Complete attribution:** See [ACKNOWLEDGMENTS.md](ACKNOWLEDGMENTS.md)

---

## 📚 Additional Documentation

- **[ACKNOWLEDGMENTS.md](ACKNOWLEDGMENTS.md)** - Full attribution history and community credits
- **[ACAIA_BLE_PROTOCOL_RESEARCH.md](ACAIA_BLE_PROTOCOL_RESEARCH.md)** - 8 years of reverse engineering history
- **[IMPLEMENTATION_COMPARISON.md](IMPLEMENTATION_COMPARISON.md)** - Technical analysis of v2.1.2 vs v3.1.4
- **[FIX_SUMMARY.md](FIX_SUMMARY.md)** - Display freeze bug fix (Oct 2025)
- **[CLAUDE.md](CLAUDE.md)** - Project status and development notes

---

## 🤝 Contributing

Contributions are welcome! Please note:

1. **For BLE library improvements:** Contribute to [tatemazer/AcaiaArduinoBLE](https://github.com/tatemazer/AcaiaArduinoBLE) upstream
2. **For UI/hardware integration:** Submit PRs to this repository
3. **Testing:** Include your hardware setup (machine + scale model) in PR description

### Development Workflow

```bash
# Fork and clone
git clone https://github.com/YourUsername/Gravimetric-Shots.git
cd Gravimetric-Shots

# Create feature branch
git checkout -b feature/your-feature-name

# Make changes and test
pio run --target upload
pio device monitor

# Commit and push
git add -A
git commit -m "feat: Description of your feature"
git push origin feature/your-feature-name

# Create Pull Request on GitHub
```

---

## 📄 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

**Upstream Libraries:**
- [AcaiaArduinoBLE](https://github.com/tatemazer/AcaiaArduinoBLE) - MIT License (Tate Mazer)
- [LVGL](https://github.com/lvgl/lvgl) - MIT License
- [ArduinoBLE](https://github.com/arduino-libraries/ArduinoBLE) - LGPL 2.1

---

## 🔗 Resources

### Project Resources
- **GitHub Repository:** https://github.com/SongKeat2901/Gravimetric-Shots
- **Hardware Docs:** https://github.com/Xinyuan-LilyGO/T-Display-S3-Long
- **Upstream BLE Library:** https://github.com/tatemazer/AcaiaArduinoBLE
- **Discord Community:** https://discord.gg/NMXb5VYtre (Tate's server)

### Development Resources
- **PlatformIO Docs:** https://docs.platformio.org
- **LVGL Docs:** https://docs.lvgl.io
- **ESP32-S3 Datasheet:** https://www.espressif.com/en/products/socs/esp32-s3
- **BLE Reverse Engineering Guide:** https://reverse-engineering-ble-devices.readthedocs.io

### ESP32 General Examples
- [BLE Examples](https://github.com/espressif/arduino-esp32/tree/master/libraries/BLE)
- [WiFi Examples](https://github.com/espressif/arduino-esp32/tree/master/libraries/WiFi)
- [SPIFFS Examples](https://github.com/espressif/arduino-esp32/tree/master/libraries/SPIFFS)

---

## ⚠️ Disclaimer

**This is a hobbyist project for personal use.**

- Not certified for commercial espresso equipment
- Use at your own risk when modifying espresso machines
- Ensure proper electrical isolation when integrating relays
- Test thoroughly before relying on automated shot control
- Author is not responsible for damaged equipment or bad espresso ☕️

---

**Built with ❤️ for the espresso community**

*Pull better shots, one gram at a time.*
