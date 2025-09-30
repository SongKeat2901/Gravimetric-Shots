# Gravimetric Shots

**BLE-Connected Gravimetric Espresso Scale Controller**

A smart espresso shot controller that connects to Acaia scales via Bluetooth, monitors extraction weight in real-time, and automatically controls solenoid valves for precise shot stopping. Built for the LilyGO T-Display-S3-Long hardware platform.

![Hardware](images/ArduinoIDE.jpg)

---

## Features

### 🎯 Core Functionality
- **BLE Scale Integration**: Connects to Acaia scales (Lunar, Pearl S, Pyxis) via Bluetooth Low Energy
- **Real-time Monitoring**: Live weight tracking and shot timer display
- **Predictive Control**: Linear regression algorithm predicts shot endpoint for automated stopping
- **Relay Output**: GPIO 48 controls solenoid valve for precise flow control
- **Drip Compensation**: 3-second delay accounts for post-shot dripping

### 🖥️ User Interface
- **Touch-based LVGL UI**: Interactive controls on 180×640 portrait display
- **Weight Target Setting**: Touch controls to adjust target weight (0-100g)
- **Visual Feedback**: Real-time weight, timer, and status displays
- **Brightness Control**: Adjustable backlight with persistent storage

### 📊 Advanced Features
- **Shot History**: Tracks up to 1000 data points per shot
- **Trend Analysis**: Uses 10-sample moving window for shot prediction
- **Flush Mode**: Dedicated 5-second flush cycle support
- **Preferences Storage**: Saves target weight, offset, and settings to flash

---

## Hardware

**Required:** [LilyGO T-Display-S3-Long](https://www.lilygo.cc/products/t-display-s3-long)

| Component | Specification |
|-----------|--------------|
| **MCU** | ESP32-S3R8 (Dual-core Xtensa @ 240MHz) |
| **Flash** | 16MB |
| **PSRAM** | 8MB (Octal SPI) |
| **Display** | 180×640 QSPI TFT (AXS15231B driver) |
| **Touch** | Capacitive touch panel (I2C) |
| **Power** | USB-C, Battery support with charging |
| **GPIO** | Relay output on GPIO 48 |

### Pin Configuration
- **Display QSPI**: CS=12, SCK=17, D0-D3=13,18,21,14, RST=16, BL=1
- **Touch I2C**: SDA=15, SCL=10, INT=11, RST=16
- **Relay Output**: GPIO 48 (5V tolerant, suitable for relay/SSR)
- **Battery Voltage**: GPIO 8 (ADC)

---

## Quick Start

### Prerequisites
- [Visual Studio Code](https://code.visualstudio.com/)
- [PlatformIO IDE extension](https://platformio.org/install/ide?install=vscode)
- Acaia scale (Lunar, Pearl S, or Pyxis recommended)

### Installation

1. **Clone Repository**
   ```bash
   git clone https://github.com/SongKeat2901/T-Display-S3-Long.git
   cd T-Display-S3-Long
   ```

2. **Open in PlatformIO**
   - Open folder in VS Code
   - PlatformIO will automatically install dependencies:
     - ArduinoBLE v1.4.1
     - LVGL v8.3.0
     - Custom AcaiaArduinoBLE (included)

3. **Build & Upload**
   ```bash
   pio run --target upload
   ```
   Or use the PlatformIO toolbar in VS Code.

4. **Upload Mode** (if USB port not detected)
   - Press and hold **BOOT** button
   - Press **RST** button (while holding BOOT)
   - Release **RST** button
   - Release **BOOT** button
   - Upload firmware

---

## Usage

### First Boot
1. Power on the device
2. Scale will automatically scan for nearby Acaia scales
3. Once connected, "Connected" status will appear

### Pulling a Shot
1. **Set Target Weight**: Touch the target weight display to adjust (±1g increments)
2. **Tare Scale**: Press the scale's tare button or use the Flush button (long press)
3. **Start Shot**: Press the **Start** button when ready
   - Relay activates (solenoid opens)
   - Weight and timer display in real-time
   - Predictive algorithm monitors flow rate
4. **Auto-Stop**: When predicted endpoint reached:
   - Relay deactivates (solenoid closes)
   - 3-second drip delay countdown begins
   - Final weight displayed

### Flush Mode
- **Long-press Flush button** (>50ms to debounce)
- 5-second flush cycle runs
- Cannot flush during active shot

### Settings Persistence
- Target weight, offset, and brightness are saved to flash memory
- Restored automatically on reboot

---

## Configuration

### Weight Settings
- **Target Range**: 0-100g (adjustable in 1g increments)
- **Offset Calibration**: Automatically stored when taring
- **Max Offset**: ±5g safety limit

### Shot Parameters
```cpp
constexpr int MIN_SHOT_DURATION_S = 5;   // Minimum shot time
constexpr int MAX_SHOT_DURATION_S = 50;  // Maximum shot time
constexpr int DRIP_DELAY_S = 3;          // Post-shot drip delay
constexpr int N = 10;                     // Trend analysis samples
```

### Debug Mode
Uncomment in `GravimetricShots.ino`:
```cpp
#define ENABLE_DEBUG_LOG 1
```
View logs via Serial Monitor @ 115200 baud.

---

## Library Dependencies

### Git-Managed (Auto-Downloaded)
- **ArduinoBLE** v1.4.1 - Arduino BLE library
- **LVGL** v8.3.0 - Graphics library (config: `lib/lv_conf.h`)

### Custom (Included in Repo)
- **AcaiaArduinoBLE** v2.1.2+ - Custom-modified for reliability
  - Based on [tatemazer/AcaiaArduinoBLE](https://github.com/tatemazer/AcaiaArduinoBLE)
  - Enhanced with personal findings and improvements
- **UI Library** - Custom LVGL UI components (`lib/ui/`)

---

## Development

### Project Structure
```
T-Display-S3-Long/
├── src/
│   ├── GravimetricShots.ino    # Main application
│   ├── AXS15231B.cpp/.h        # Display driver
│   ├── pins_config.h            # Hardware pin definitions
│   └── src/                     # UI assets (test images)
├── lib/
│   ├── AcaiaArduinoBLE/        # Custom BLE library
│   ├── ui/                      # LVGL UI components
│   └── lv_conf.h                # LVGL configuration
├── board/
│   └── T-Display-Long.json     # PlatformIO board definition
├── platformio.ini               # Build configuration
└── README.md
```

### Building
```bash
# Clean build
pio run --target clean

# Build only
pio run

# Upload
pio run --target upload

# Monitor serial
pio device monitor --baud 115200
```

### Multi-Computer Workflow
Libraries are managed via Git references - simply clone and build:
```bash
git clone <your-fork>
pio run
```
PlatformIO automatically downloads ArduinoBLE and LVGL.

---

## Troubleshooting

### Scale Won't Connect
- Ensure scale is powered on and in range
- Try power-cycling both devices
- Check that scale isn't connected to another device
- Some scales require manual pairing mode

### Upload Fails
- Use manual upload mode (see Installation step 4)
- Check USB cable (data cable required, not charge-only)
- Verify correct COM port selected

### Display Issues
- **Display corruption**: Reduce SPI frequency in `pins_config.h`
- **Touch not responding**: Check I2C connections and touch calibration

### Build Errors
```bash
# Clear PlatformIO cache
rm -rf .pio/

# Reinstall dependencies
pio lib install
```

---

## Hardware Attribution

This project is built for the **LilyGO T-Display-S3-Long** development board:
- **Manufacturer**: [LilyGO](https://www.lilygo.cc/)
- **Original Repo**: [Xinyuan-LilyGO/T-Display-S3-Long](https://github.com/Xinyuan-LilyGO/T-Display-S3-Long)
- **Product Page**: https://www.lilygo.cc/products/t-display-s3-long

Display driver (AXS15231B) adapted from LilyGO's examples.

---

## License

MIT License - See LICENSE file for details.

**Third-Party Licenses:**
- LVGL: MIT License
- ArduinoBLE: LGPL v2.1
- AcaiaArduinoBLE: Public Domain (original by Tate Mazer)

---

## Contributing

Contributions welcome! Please:
1. Fork the repository
2. Create a feature branch
3. Test thoroughly with hardware
4. Submit pull request with detailed description

---

## Acknowledgments

- **LilyGO** - Hardware platform and display driver examples
- **Tate Mazer** - Original AcaiaArduinoBLE library
- **LVGL Community** - Graphics library and examples
- **Arduino/Espressif** - ESP32 framework and BLE support

---

## Contact

**Author**: SongKeat
**Project**: Gravimetric Shots
**Repository**: https://github.com/SongKeat2901/T-Display-S3-Long

For hardware questions, refer to [LilyGO's documentation](https://github.com/Xinyuan-LilyGO/T-Display-S3-Long).