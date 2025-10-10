# Acknowledgments

This project would not exist without the incredible work of many individuals and communities who reverse-engineered the Acaia BLE protocol and built the libraries this project depends on.

---

## 🙏 Primary Credit

### **Tate Mazer** (@tatemazer)
**Creator and Primary Developer of AcaiaArduinoBLE**

This project is based on Tate's excellent [AcaiaArduinoBLE](https://github.com/tatemazer/AcaiaArduinoBLE) library, which provides the complete BLE communication implementation for Acaia scales on ESP32 and Arduino platforms.

**Tate's Contributions:**
- Created and maintains AcaiaArduinoBLE library (2023-present)
- Supports multiple scale types: Acaia Lunar, Pearl S, Pyxis, BooKoo Themis
- Developed hardware V3.1 PCB for scale integration
- Connection watchdog and reliability improvements
- Debug mode for troubleshooting
- Active community support via Discord
- Continuous updates and bug fixes

**Without Tate's work, this espresso controller project would not be possible.** ✨

---

## 🌍 Community Contributors to AcaiaArduinoBLE

### **Pio Baettig** (@baettigp)
- Added generic scale support
- Felicita Arc compatibility
- Timer functions (start, stop, reset)

### **philgood** (@philgood)
- BooKoo Themis Scale support (v3.1.0)
- First community pull request contributor

### **Jochen Niebuhr**
- Lunar 2019 contributions
- Testing and validation

### **RP**
- BooKoo scale contributions

### **Discord Community**
- Testing across multiple scale models
- Bug reports and feature requests
- Support and troubleshooting

**Discord:** https://discord.gg/NMXb5VYtre

---

## 📜 Historical Lineage - Acaia BLE Protocol Reverse Engineering

### **The Pioneers** (2015-2018)

#### **h1kari** - Original Reverse Engineer
- First known Python implementation (pre-2016)
- Discovered Acaia Pearl BLE protocol through packet sniffing
- Repository: h1kari/AcaiaScale

#### **bpowers** - JavaScript Port
- Created btscale for Chrome apps (2015-2016)
- Cross-platform implementation (Android, iOS, ChromeOS)
- Inspired many subsequent projects
- Repository: bpowers/btscale

#### **AndyZap** - Methodology Documentation
- Documented Android HCI logging + Wireshark analysis (2017-2018)
- Solved firmware 2.0 protocol changes
- Shared complete reverse engineering methodology
- **Critical contribution:** Made the knowledge accessible to future developers
- Repository: AndyZap/AcaiaLoggerWin10

#### **Luca Pinello** - Python Ecosystem
- Created pyacaia Python library (2016-present)
- Inspired by btscale
- Popular in coffee community (2000+ users)
- Repository: lucapinello/pyacaia

#### **frowin** - ESP32 Gateway Foundation
- Created LunarGateway for ESP32 (pre-2023)
- Documented heartbeat requirements
- Provided foundation for Arduino implementations
- Repository: frowin/LunarGateway

---

## 🛠️ This Project's Modifications

### **SongKeat** (This Fork)

**Scope:** This is a **specialized fork** for embedded UI applications, NOT a general-purpose improvement.

**My Specific Contributions:**
1. **LVGL Integration** - Added UI timer handling during BLE operations to prevent display freeze
2. **Production Validation** - Tested on **one specific setup**:
   - Machine: La Marzocco (LM) Micra espresso machine
   - Scale: Acaia Lunar (USB-C, 2021 version)
   - Display: LilyGO T-Display-S3-Long (180x640 QSPI)
3. **Issue #7 Report** - Documented ESP32-S3 17-byte packet behavior
4. **Serial.print Fix** - Commented out blocking calls in init() for LVGL compatibility

**Important Limitations:**
- ❌ **NOT tested on:** Pyxis, Pearl S, BooKoo Themis, Felicita Arc
- ❌ **NOT tested on:** Other espresso machines or hardware platforms
- ❌ **Single machine validation only:** LM Micra + Acaia Lunar
- ❌ **Upstream has MORE features:** Debug mode, broader scale support, ongoing development

**This fork is optimized for my specific use case (embedded LVGL UI). For general scale integration, use [Tate's upstream library](https://github.com/tatemazer/AcaiaArduinoBLE) which is more actively maintained and supports more scales.**

---

## 🔧 Hardware & Software Dependencies

### **LilyGO T-Display-S3-Long**
**Manufacturer:** LilyGO / Xinyuan-LilyGO
- Hardware design and reference examples
- Display driver (AXS15231B) implementation
- Touch controller integration
- Repository: https://github.com/Xinyuan-LilyGO/T-Display-S3-Long

### **LVGL** (Light and Versatile Graphics Library)
- UI framework v8.3.0-dev
- Touch interface components
- https://lvgl.io

### **ArduinoBLE**
- Arduino Bluetooth Low Energy library
- Core BLE communication stack
- https://github.com/arduino-libraries/ArduinoBLE

---

## 🎖️ Chain of Gratitude

```
Acaia (created the scale hardware)
    ↓
h1kari (2015) - reverse engineered the protocol
    ↓
bpowers (2016) - JavaScript implementation
    ↓
AndyZap (2017) - documented methodology
    ↓
lucapinello (2018) - Python ecosystem
    ↓
frowin (2020s) - ESP32 gateway
    ↓
Tate Mazer (2023-2025) - Arduino/ESP32 library ← PRIMARY CREDIT
    ↓ (+ Community: Pio, philgood, Jochen, RP, Discord)
    ↓
SongKeat (2024-2025) - LVGL fork for embedded UI
```

**Every person in this chain contributed to making this project possible.**

---

## 💬 Acaia's Position

**Important Note:** Acaia has **never officially released** BLE protocol documentation.

- Acaia released closed-source iOS/Android SDKs (binary only, no source code)
- Acaia declined requests to open the protocol (per Beanconqueror community)
- All community implementations are **reverse-engineered**
- Acaia firmware updates sometimes change the protocol

**Reverse engineering was necessary because Acaia chose not to document the public interface of their Bluetooth-enabled products.**

**Reference:** [Reverse Engineering BLE Devices](https://reverse-engineering-ble-devices.readthedocs.io)

---

## 📖 Documentation Credits

### **Research Resources**
- **Reverse Engineering BLE Devices Guide:** https://reverse-engineering-ble-devices.readthedocs.io
  - Methodology for protocol discovery
  - Wireshark analysis techniques
  - APK decompilation guides

### **Coffee Community**
- **Coffee Forums UK:** Protocol discussions and collaboration
- **Home Barista:** Hardware integration discussions
- **Beanconqueror Community:** Multi-platform implementation insights

---

## 🤝 Open Source Philosophy

This project embraces the open-source spirit:

1. **Built on the work of others** - Standing on the shoulders of giants
2. **Share improvements freely** - Give back to the community
3. **Document thoroughly** - Help the next person
4. **Acknowledge contributions** - Credit where credit is due
5. **Collaborate, don't compete** - Community over ego

---

## 📞 Contact & Support

**This Project (Gravimetric Shots):**
- Repository: https://github.com/SongKeat2901/Gravimetric-Shots
- For LVGL integration questions or LM Micra + Lunar setup

**Upstream AcaiaArduinoBLE (Recommended for most users):**
- Repository: https://github.com/tatemazer/AcaiaArduinoBLE
- Discord: https://discord.gg/NMXb5VYtre
- For general scale support, multiple scales, broader hardware compatibility

---

## 🎯 Recommendation

**If you're building a scale integration project:**

1. **Start with Tate's library:** https://github.com/tatemazer/AcaiaArduinoBLE
   - Actively maintained
   - Supports more scales (Lunar, Pyxis, Pearl S, BooKoo Themis)
   - Community support via Discord
   - Better tested across hardware

2. **Use this fork ONLY if:**
   - You need LVGL UI integration
   - You have LM Micra + Acaia Lunar (exact same setup as tested)
   - You understand the limited testing scope

**Tate's library is the definitive community implementation. This is a specialized fork for a specific use case.**

---

## 💝 Thank You

To everyone mentioned above:

**Thank you for:**
- Reverse engineering a closed protocol
- Sharing knowledge freely
- Building excellent libraries
- Supporting the community
- Making projects like this possible

**Special thanks to Tate Mazer** for maintaining AcaiaArduinoBLE and supporting the community. Your work is the foundation this project is built on. 🙏

---

**Note:** If I've missed anyone or misrepresented contributions, please let me know via GitHub issues. Proper attribution is important.

---

**Last Updated:** October 10, 2025
