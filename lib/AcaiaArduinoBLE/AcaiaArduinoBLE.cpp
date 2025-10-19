/*
  AcaiaArduinoBLE.cpp - Library for connecting to
  an Acaia Scale using the ArduinoBLE library.
  Created by Tate Mazer, December 13, 2023.
  Released into the public domain.

  Adding Generic Scale Support, Pio Baettig
*/
#include "Arduino.h"
#include "AcaiaArduinoBLE.h"
#include <ArduinoBLE.h>
#include "../../src/debug_config.h"  // For thread-safe LOG_*() macros with serialMutex
#include "esp_task_wdt.h"             // For watchdog reset during blocking BLE operations

#define HEADER1 0xef
#define HEADER2 0xdd

byte IDENTIFY[20] = {0xef, 0xdd, 0x0b, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x30, 0x31, 0x32, 0x33, 0x34, 0x9a, 0x6d};
byte HEARTBEAT[7] = {0xef, 0xdd, 0x00, 0x02, 0x00, 0x02, 0x00};
byte NOTIFICATION_REQUEST[14] = {0xef, 0xdd, 0x0c, 0x09, 0x00, 0x01, 0x01, 0x02, 0x02, 0x05, 0x03, 0x04, 0x15, 0x06};
byte START_TIMER[7] = {0xef, 0xdd, 0x0d, 0x00, 0x00, 0x00, 0x00};
byte STOP_TIMER[7] = {0xef, 0xdd, 0x0d, 0x00, 0x02, 0x00, 0x02};
byte RESET_TIMER[7] = {0xef, 0xdd, 0x0d, 0x00, 0x01, 0x00, 0x01};
byte TARE_ACAIA[6] = {0xef, 0xdd, 0x04, 0x00, 0x00, 0x00};
byte TARE_GENERIC[1] = {0x54};

int count = 0;

const int lvUpdateInterval = 16;
unsigned long lastLvUpdate = 0;
extern void LVGLTimerHandlerRoutine();


AcaiaArduinoBLE::AcaiaArduinoBLE()
{
    _currentWeight = 999;
    _connected = false;
    _packetPeriod = 0;
}

bool AcaiaArduinoBLE::init(String mac)
{
    // CRITICAL FIX: Ensure clean BLE state before reconnection
    // ArduinoBLE can retain stale connection state after disconnect,
    // causing subscription to fail on reconnect. Force cleanup here.
    BLE.disconnect();  // Close any existing connection
    delay(500);        // Give BLE stack and scale time to reset (tested: 500ms minimum)

    unsigned long start = millis();
    _lastPacket = 0;

    if (mac == "")
    {
        BLE.scan();
    }
    else if (!BLE.scanForAddress(mac))
    {
        LOG_ERROR("BLE", "❌ Failed to find scale MAC: %s", mac.c_str());
        return false;
    }

    do
    {
        BLEDevice peripheral = BLE.available();
        if (peripheral && isScaleName(peripheral.localName()))
        {
            BLE.stopScan();

            LOG_INFO("BLE", "🔗 Connecting to scale...");
            if (peripheral.connect())
            {
                LOG_INFO("BLE", "✅ Connected to scale");
            }
            else
            {
                LOG_ERROR("BLE", "❌ Failed to connect to scale");
                return false;
            }

            LOG_INFO("BLE", "🔍 Discovering BLE characteristics...");

            // CRITICAL: discoverAttributes() is BLOCKING and can take 1-10+ seconds
            // This causes watchdog timeout if it takes too long. Feed watchdog before/after.
            esp_task_wdt_reset();  // Reset watchdog before blocking call

            unsigned long discovery_start = millis();
            bool discovery_success = peripheral.discoverAttributes();
            unsigned long discovery_time = millis() - discovery_start;

            esp_task_wdt_reset();  // Reset watchdog after blocking call

            if (discovery_success)
            {
                LOG_INFO("BLE", "✅ Characteristics discovered (took %lums)", discovery_time);
            }
            else
            {
                LOG_ERROR("BLE", "❌ Characteristic discovery failed (took %lums)", discovery_time);
                LOG_ERROR("BLE", "    This usually means scale BLE stack is still resetting");
                LOG_ERROR("BLE", "    Will retry on next init() call");
                peripheral.disconnect();
                return false;
            }

            // Determine type of scale
            if (peripheral.characteristic(READ_CHAR_OLD_VERSION).canSubscribe())
            {
                LOG_INFO("BLE", "📊 Old version Acaia detected");
                _type = OLD;
                _write = peripheral.characteristic(WRITE_CHAR_OLD_VERSION);
                _read = peripheral.characteristic(READ_CHAR_OLD_VERSION);
            }
            else if (peripheral.characteristic(READ_CHAR_NEW_VERSION).canSubscribe())
            {
                LOG_INFO("BLE", "📊 New version Acaia detected");
                _type = NEW;
                _write = peripheral.characteristic(WRITE_CHAR_NEW_VERSION);
                _read = peripheral.characteristic(READ_CHAR_NEW_VERSION);
            }
            else if (peripheral.characteristic(READ_CHAR_GENERIC).canSubscribe())
            {
                LOG_INFO("BLE", "📊 Generic scale detected");
                _type = GENERIC;
                _write = peripheral.characteristic(WRITE_CHAR_GENERIC);
                _read = peripheral.characteristic(READ_CHAR_GENERIC);
            }
            else
            {
                LOG_ERROR("BLE", "❌ Unable to determine scale type");
                return false;
            }

            if (!_read.canSubscribe())
            {
                LOG_ERROR("BLE", "❌ Unable to subscribe to READ characteristic");
                return false;
            }
            else if (!_read.subscribe())
            {
                LOG_ERROR("BLE", "❌ Subscription to READ characteristic failed");
                return false;
            }
            else
            {
                LOG_INFO("BLE", "✅ Subscribed to weight notifications");
            }

            if (_write.writeValue(IDENTIFY, 20))
            {
                LOG_DEBUG("BLE", "✅ IDENTIFY command sent");
            }
            else
            {
                LOG_ERROR("BLE", "❌ IDENTIFY command failed");
                return false;
            }
            if (_write.writeValue(NOTIFICATION_REQUEST, 14))
            {
                LOG_DEBUG("BLE", "✅ NOTIFICATION_REQUEST sent");
            }
            else
            {
                LOG_ERROR("BLE", "❌ NOTIFICATION_REQUEST failed");
                return false;
            }
            _connected = true;
            _packetPeriod = 0;
            return true;
        }
        // CRITICAL FIX: LVGL calls REMOVED for dual-core ESP32-S3
        // On single-core systems, LVGLTimerHandlerRoutine() kept UI responsive during 10s scan
        // On dual-core ESP32-S3: Core 0 (BLE) + Core 1 (UI) both calling LVGL → race condition → crashes
        // Core 1 handles all LVGL operations - Core 0 BLE task must NOT touch LVGL
        // Watchdog is fed in bleTaskFunction() every 10ms - no timeout risk

        // Feed watchdog to prevent interrupt watchdog timeout during 10s scan
        // BLE scan takes 10s but interrupt watchdog timeout is 3s
        esp_task_wdt_reset();

    } while (millis() - start < 10000);  // 10s timeout (matches upstream - tested for reconnection)

    LOG_WARN("BLE", "⏱️  Scan timeout - scale not found");
    return false;
}

bool AcaiaArduinoBLE::tare()
{
    // CRITICAL: Check if characteristic is still valid
    if (!_write)
    {
        LOG_ERROR("BLE", "❌ Tare failed: write characteristic is NULL");
        _connected = false;
        return false;
    }

    if (_write.writeValue((_type == GENERIC ? TARE_GENERIC : TARE_ACAIA), 20))
    {
        LOG_INFO("BLE", "⚖️  Tare command sent");
        return true;
    }
    else
    {
        _connected = false;
        LOG_ERROR("BLE", "❌ Tare command failed");
        return false;
    }
}

bool AcaiaArduinoBLE::startTimer()
{
    // CRITICAL: Check if characteristic is still valid
    if (!_write)
    {
        LOG_ERROR("BLE", "❌ Start timer failed: write characteristic is NULL");
        _connected = false;
        return false;
    }

    if (_write.writeValue(START_TIMER, 7))
    {
        LOG_DEBUG("BLE", "▶️  Start timer command sent");
        return true;
    }
    else
    {
        _connected = false;
        LOG_ERROR("BLE", "❌ Start timer command failed");
        return false;
    }
}

bool AcaiaArduinoBLE::stopTimer()
{
    // CRITICAL: Check if characteristic is still valid
    if (!_write)
    {
        LOG_ERROR("BLE", "❌ Stop timer failed: write characteristic is NULL");
        _connected = false;
        return false;
    }

    if (_write.writeValue(STOP_TIMER, 7))
    {
        LOG_DEBUG("BLE", "⏸️  Stop timer command sent");
        return true;
    }
    else
    {
        _connected = false;
        LOG_ERROR("BLE", "❌ Stop timer command failed");
        return false;
    }
}

bool AcaiaArduinoBLE::resetTimer()
{
    // CRITICAL: Check if characteristic is still valid
    if (!_write)
    {
        LOG_ERROR("BLE", "❌ Reset timer failed: write characteristic is NULL");
        _connected = false;
        return false;
    }

    if (_write.writeValue(RESET_TIMER, 7))
    {
        LOG_DEBUG("BLE", "🔄 Reset timer command sent");
        return true;
    }
    else
    {
        _connected = false;
        LOG_ERROR("BLE", "❌ Reset timer command failed");
        return false;
    }
}

bool AcaiaArduinoBLE::heartbeat()
{
    // CRITICAL: Check if characteristic is still valid
    // After long runtime or reconnection, _write can become NULL
    // Accessing NULL characteristic causes LoadProhibited crash
    if (!_write)
    {
        LOG_ERROR("BLE", "❌ Heartbeat failed: write characteristic is NULL");
        _connected = false;
        return false;
    }

    if (_write.writeValue(HEARTBEAT, 7))
    {
        _lastHeartBeat = millis();
        return true;
    }
    else
    {
        _connected = false;
        return false;
    }
}

float AcaiaArduinoBLE::getWeight()
{
    return _currentWeight;
}

bool AcaiaArduinoBLE::heartbeatRequired()
{
    if (_type == OLD || _type == NEW)
    {
        return (millis() - _lastHeartBeat) > HEARTBEAT_PERIOD_MS;
    }
    else
    {
        return 0;
    }
}

bool AcaiaArduinoBLE::isConnected()
{
    // First check our connection flag
    if (!_connected)
    {
        return false;
    }

    // CRITICAL: Verify characteristics are still valid
    // After long runtime or disconnection, ArduinoBLE can invalidate
    // characteristic handles, leading to NULL pointer crashes
    if (!_write || !_read)
    {
        LOG_WARN("BLE", "⚠️  Characteristics became invalid (write=%p, read=%p)",
                 (void*)&_write, (void*)&_read);
        _connected = false;
        return false;
    }

    return true;
}

bool AcaiaArduinoBLE::newWeightAvailable()
{
    // Check for connection timeout
    if (_lastPacket && millis() - _lastPacket > MAX_PACKET_PERIOD_MS)
    {
        LOG_ERROR("BLE", "⏱️  Connection timeout - no weight packets for %lums", millis() - _lastPacket);
        _connected = false;
        BLE.disconnect();
        return false;
    }

    byte input[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    // input[2] == 12 weight message type && input[4] == 5 is to confirm got weight package
    if (NEW == _type && _read.valueUpdated() && _read.readValue(input, 13) && input[2] == 0x0C && input[4] == 0x05)
    {
        // Grab weight bytes (5 and 6)
        //  apply scaling based on the unit byte (9)
        //  get sign byte (10)
        _currentWeight = (((input[6] & 0xff) << 8) + (input[5] & 0xff)) / pow(10, input[9]) * ((input[10] & 0x02) ? -1 : 1);

        // Track packet timing
        if (_lastPacket)
        {
            _packetPeriod = millis() - _lastPacket;
        }
        _lastPacket = millis();

        return true;
    }
    else if (OLD == _type && _read.valueUpdated() && _read.valueLength() == 10 && _read.readValue(input, 10))
    {
        // Grab weight bytes (2 and 3),
        //  apply scaling based on the unit byte (6)
        //  get sign byte (7)
        _currentWeight = (((input[3] & 0xff) << 8) + (input[2] & 0xff)) / pow(10, input[6]) * ((input[7] & 0x02) ? -1 : 1);

        // Track packet timing
        if (_lastPacket)
        {
            _packetPeriod = millis() - _lastPacket;
        }
        _lastPacket = millis();

        return true;
    }
    else if (GENERIC == _type && _read.valueUpdated() && _read.readValue(input, 13))
    {
        // Grab weight bytes (3-8),
        //  get sign byte (2)
        _currentWeight = (input[2] == 0x2B ? 1 : -1) * ((input[3] - 0x30) * 1000 + (input[4] - 0x30) * 100 + (input[5] - 0x30) * 10 + (input[6] - 0x30) * 1 + (input[7] - 0x30) * 0.1 + (input[8] - 0x30) * 0.01);

        // Track packet timing
        if (_lastPacket)
        {
            _packetPeriod = millis() - _lastPacket;
        }
        _lastPacket = millis();

        return true;
    }
    else
    {
        return false;
    }
}

bool AcaiaArduinoBLE::isScaleName(String name)
{
    String nameShort = name.substring(0, 5);

    return nameShort == "CINCO" || nameShort == "ACAIA" || nameShort == "PYXIS" || nameShort == "LUNAR" || nameShort == "PROCH" || nameShort == "FELIC";
}

// Function to create request payload (setting payload byte [2] = 6)
bool AcaiaArduinoBLE::getBattery()
{
    byte payload[16] = {0};
    byte bytes[5 + sizeof(payload)];
    bytes[0] = HEADER1;
    bytes[1] = HEADER2;
    bytes[2] = 6; // get setting
    int cksum1 = 0;
    int cksum2 = 0;

    for (int i = 0; i < sizeof(payload); i++)
    {
        byte val = payload[i] & 0xFF;
        bytes[3 + i] = val;
        if (i % 2 == 0)
        {
            cksum1 += val;
        }
        else
        {
            cksum2 += val;
        }
    }

    bytes[sizeof(payload) + 3] = (cksum1 & 0xFF);
    bytes[sizeof(payload) + 4] = (cksum2 & 0xFF);

    if (_write.writeValue(bytes, 21))
    {
        return true;
    }
    else
    {
        return false;
    }
}
// Function to get battery value
bool AcaiaArduinoBLE::updateBattery()
{
    byte input[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

    // value length checked to be 14 for fw 1.0.0.16 lunar
    if (_read.valueUpdated() && _read.readValue(input, 13) && input[2] == 8)
    {
        _currentBattery = input[4] & 0x7f;
        return true;
    }
    else
    {
        return false;
    }
}

int AcaiaArduinoBLE::batteryValue()
{
    return _currentBattery;
}
