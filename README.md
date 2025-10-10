<h1 align = "center">🌟T-Display-S3-Long🌟</h1>

## ⚠️ Project Note

**This repository contains Gravimetric Shots - a gravimetric espresso controller based on:**

- **Hardware:** LilyGO T-Display-S3-Long (see below for hardware specs)
- **BLE Library:** [tatemazer/AcaiaArduinoBLE](https://github.com/tatemazer/AcaiaArduinoBLE) ⭐

### Acknowledgments

**Primary Credit:**
- **Tate Mazer** - Creator and maintainer of [AcaiaArduinoBLE](https://github.com/tatemazer/AcaiaArduinoBLE)
  - Supports multiple Acaia scales (Lunar, Pyxis, Pearl S, BooKoo Themis)
  - Active community support via [Discord](https://discord.gg/NMXb5VYtre)
  - Regular updates and improvements

**Community Contributors:** Pio Baettig, philgood, Jochen Niebuhr, RP, and Discord community

**Hardware:** LilyGO / Xinyuan-LilyGO for T-Display-S3-Long design

**This Fork:** Adds LVGL integration for embedded touch UI (tested on LM Micra + Acaia Lunar only)

**For general Acaia scale integration, use [Tate's upstream library](https://github.com/tatemazer/AcaiaArduinoBLE)**

**Complete attribution:** See [ACKNOWLEDGMENTS.md](ACKNOWLEDGMENTS.md)

---

## 1️⃣Product

| Product(PinMap)        | SOC        | Flash | PSRAM    | Resolution |
| ---------------------- | ---------- | ----- | -------- | ---------- |
| [T-Display-S3-Long][1] | ESP32-S3R8 | 16MB  | 8MB(OPI) | 180x640    |

| Current consumption    | Working current             | sleep current | sleep mode  |
| ---------------------- | --------------------------- | ------------- | ----------- |
| [T-Display-S3-Long][1] | (240MHz) WiFi On 90~350+ mA | About 1.1mA   | gpio wakeup |

[1]:https://www.lilygo.cc/products/t-display-s3-long


## 2️⃣Features

**Gravimetric Shots** is a BLE-connected espresso scale controller with:

- 📊 **Real-time Weight Monitoring** - Connect to Acaia scales via Bluetooth LE
- 🎯 **Predictive Shot Ending** - Linear regression algorithm for precise shot control
- ⚡ **Relay Control** - Automated solenoid valve control (GPIO 48)
- 🖥️ **Touch UI** - LVGL-based interface on 180×640 portrait display
- 📈 **Shot History** - Track up to 1000 datapoints per session
- 💾 **Preferences Storage** - NVS-based settings persistence
- 🔋 **Battery Monitoring** - Real-time battery voltage display

**Supported Scales:**
- Acaia Lunar (USB-Micro pre-2021 & USB-C 2021+)
- Acaia Pyxis
- Acaia Pearl S

**Tested Configuration:** La Marzocco Micra + Acaia Lunar 2021

## 3️⃣Project Structure

```txt
Gravimetric-Shots/
├── src/
│   ├── GravimetricShots.ino    # Main application (1080 lines)
│   ├── AXS15231B.cpp/.h        # Display driver (AXS15231B)
│   └── pins_config.h            # Hardware pin definitions
├── lib/
│   ├── AcaiaArduinoBLE/        # Custom BLE library (v2.1.2+)
│   ├── ArduinoBLE/             # Vendored ArduinoBLE
│   ├── lvgl/                   # Vendored LVGL v8.3.0-dev
│   ├── ui/                      # Custom LVGL UI components
│   └── lv_conf.h                # LVGL configuration
├── board/
│   └── T-Display-Long.json     # PlatformIO board definition
└── platformio.ini               # Build configuration
```

## 4️⃣ PlatformIO Quick Start (Recommended)

1. Install [Visual Studio Code](https://code.visualstudio.com/) and [Python](https://www.python.org/)
2. Search for the `PlatformIO` plugin in the `VisualStudioCode` extension and install it
3. After the installation is complete, restart `VisualStudioCode`
4. Clone or download this repository:
   ```bash
   git clone https://github.com/SongKeat2901/Gravimetric-Shots.git
   ```
5. In VSCode: `File` → `Open Folder` → select the `Gravimetric-Shots` directory
6. PlatformIO will automatically install all dependencies (this may take a few minutes)
7. Click the (✔) symbol in the lower left corner to compile
8. Connect the T-Display-S3-Long board via USB
9. Click (→) to upload firmware
10. Click (plug symbol) to monitor serial output
11. If upload fails or USB device keeps flashing, see **FAQ** below

## 5️⃣ Arduino IDE Quick Start (Alternative)

**⚠️ PlatformIO is strongly recommended.** Arduino IDE setup is more complex due to vendored libraries.

1. Install [Arduino IDE](https://www.arduino.cc/en/software)
2. Clone this repository:
   ```bash
   git clone https://github.com/SongKeat2901/Gravimetric-Shots.git
   ```
3. Copy all folders from `lib/` to your Arduino libraries folder:
   - Windows: `C:\Users\YourName\Documents\Arduino\libraries\`
   - macOS: `~/Documents/Arduino/libraries/`
   - Linux: `~/Arduino/libraries/`
4. Open `src/GravimetricShots.ino` in Arduino IDE
5. Board configuration:
   - **Board:** "ESP32S3 Dev Module"
   - **USB CDC On Boot:** "Enabled"
   - **Flash Size:** "16MB (128Mb)"
   - **Partition Scheme:** "Huge APP (3MB No OTA/1MB SPIFFS)"
   - **PSRAM:** "OPI PSRAM"
6. Select your board's USB port
7. Click `Upload` and wait for compilation
8. If upload fails, see **FAQ** below

# 6️⃣ ESP32 General Resources

* [BLE Examples](https://github.com/espressif/arduino-esp32/tree/master/libraries/BLE)
* [WiFi Examples](https://github.com/espressif/arduino-esp32/tree/master/libraries/WiFi)
* [SPIFFS Examples](https://github.com/espressif/arduino-esp32/tree/master/libraries/SPIFFS)
* [FFat Examples](https://github.com/espressif/arduino-esp32/tree/master/libraries/FFat)
* [Arduino-ESP32 Libraries](https://github.com/espressif/arduino-esp32/tree/master/libraries)

# 7️⃣ FAQ

1. The board uses USB as the JTAG upload port. When printing serial port information on USB_CDC_ON_BOOT configuration needs to be turned on.
If the port cannot be found when uploading the program or the USB has been used for other functions, the port does not appear.
Please enter the upload mode manually.
   1. Connect the board via the USB cable
   2. Press and hold the BOOT button , While still pressing the BOOT button, press RST
   3. Release the RST
   4. Release the BOOT button
   5. Upload sketch

2. If the above is invalid, burn the [binary file](./firmware/README.MD)  to check whether the hardware is normal
3. The OTG external power supply function requires turning on the PMU OTG enablement ,If the USB input is connected and the OTG is set to output, the battery will not be charged.
   ```c
         PMU.enableOTG();  //Enable OTG Power output
         PMU.disableOTG(); //Disable OTG Power output
   ```
4. Turning the physical switch to OFF will completely disconnect the battery from the motherboard. When charging is required, turn the switch to ON.
5. When the battery is not connected and the USB is plugged in, the board's LED status indicator light will flash. You can use `PMU.disableStatLed();` to turn off the indicator light, but this means that if the battery is connected for charging, the LED light will also be disabled. If you need to enable the charging status indicator, please call `PMU.enableStatLed();`


# 8️⃣ Library Dependencies

All libraries are vendored in the `lib/` directory. **Do not upgrade LVGL** - this project uses a specific v8.3.0-dev build with forced software rotation.

**Vendored Libraries:**
- [LVGL 8.3.0-dev](https://github.com/lvgl/lvgl) - Graphics library (⚠️ DO NOT UPGRADE)
- [ArduinoBLE](https://github.com/arduino-libraries/ArduinoBLE) - Bluetooth LE support
- [AcaiaArduinoBLE v2.1.2+](https://github.com/tatemazer/AcaiaArduinoBLE) - Custom fork with LVGL integration

**External Dependencies (auto-installed by PlatformIO):**
- [XPowersLib](https://github.com/lewisxhe/XPowersLib) - Power management (if needed)

