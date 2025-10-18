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

    // Initialize state machine
    _connState = CONN_IDLE;
    _connStateStart = 0;
    _connTimeout = 0;
    _mac = "";
    _lastDisconnect = 0;
}

bool AcaiaArduinoBLE::init(String mac)
{
    // Don't restart if already connecting or connected
    if (_connState != CONN_IDLE && _connState != CONN_FAILED)
    {
        // Silently reject - this is called frequently from loop
        return false;
    }


    Serial.print("AcaiaArduinoBLE Library v");
    Serial.print(LIBRARY_VERSION);
    Serial.println(" - Starting non-blocking connection...");

    _mac = mac;
    _lastPacket = 0;
    _connected = false;

    // Start BLE scan
    if (mac == "")
    {
        BLE.scan();
    }
    else if (!BLE.scanForAddress(mac))
    {
        Serial.print("Failed to start scan for ");
        Serial.println(mac);
        return false;
    }

    Serial.println("BLE scan started (non-blocking)");
    transitionTo(CONN_SCANNING, 1000);  // 1 second scan timeout
    return true;  // Returns immediately - call update() repeatedly to continue
}

bool AcaiaArduinoBLE::tare()
{
    // Use flag check instead of .connected() which blocks
    if (!_connected)
    {
        Serial.println("tare failed: not connected");
        return false;
    }

    if (_write.writeValue((_type == GENERIC ? TARE_GENERIC : TARE_ACAIA), (_type == GENERIC ? 1 : 6)))
    {
        Serial.println("tare write successful");
        return true;
    }
    else
    {
        _connected = false;
        Serial.println("tare write failed");
        return false;
    }
}

bool AcaiaArduinoBLE::startTimer()
{
    // Use flag check instead of .connected() which blocks
    if (!_connected)
    {
        Serial.println("start timer failed: not connected");
        return false;
    }

    if (_write.writeValue(START_TIMER, 7))
    {
        Serial.println("start timer write successful");
        return true;
    }
    else
    {
        _connected = false;
        Serial.println("start timer write failed");
        return false;
    }
}

bool AcaiaArduinoBLE::stopTimer()
{
    // Use flag check instead of .connected() which blocks
    if (!_connected)
    {
        Serial.println("stop timer failed: not connected");
        return false;
    }

    if (_write.writeValue(STOP_TIMER, 7))
    {
        Serial.println("stop timer write successful");
        return true;
    }
    else
    {
        _connected = false;
        Serial.println("stop timer write failed");
        return false;
    }
}

bool AcaiaArduinoBLE::resetTimer()
{
    // Use flag check instead of .connected() which blocks
    if (!_connected)
    {
        Serial.println("reset timer failed: not connected");
        return false;
    }

    if (_write.writeValue(RESET_TIMER, 7))
    {
        Serial.println("reset timer write successful");
        return true;
    }
    else
    {
        _connected = false;
        Serial.println("reset timer write failed");
        return false;
    }
}

bool AcaiaArduinoBLE::heartbeat()
{
    // Use flag check instead of .connected() which blocks
    if (!_connected)
    {
        Serial.println("heartbeat failed: not connected");
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
    // Simple flag check - no blocking calls
    // Disconnection detected by packet timeout in newWeightAvailable()
    return _connected;
}

bool AcaiaArduinoBLE::newWeightAvailable()
{
    // Check for connection timeout (no blocking calls)
    if (_lastPacket && millis() - _lastPacket > MAX_PACKET_PERIOD_MS)
    {
        Serial.println("Connection timeout - no packets received!");

        // Reset state to trigger reconnection
        _connected = false;
        _connState = CONN_FAILED;  // Let state machine's failure handler deal with cleanup
        _lastPacket = 0;  // Clear to prevent repeated timeout detection

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

// -----------------------------------------------------------------------------
// Non-Blocking State Machine Helper Methods
// -----------------------------------------------------------------------------

void AcaiaArduinoBLE::transitionTo(ConnectionState newState, unsigned long timeout)
{
    _connState = newState;
    _connStateStart = millis();
    _connTimeout = timeout;
}

bool AcaiaArduinoBLE::isConnecting()
{
    return _connState != CONN_IDLE &&
           _connState != CONN_CONNECTED;
}

ConnectionState AcaiaArduinoBLE::getConnectionState()
{
    return _connState;
}

const char* AcaiaArduinoBLE::getStateString()
{
    switch (_connState) {
        case CONN_IDLE: return "Idle";
        case CONN_SCANNING: return "Scanning";
        case CONN_CONNECTING: return "Connecting";
        case CONN_DISCOVERING: return "Discovering";
        case CONN_SUBSCRIBING: return "Subscribing";
        case CONN_IDENTIFYING: return "Identifying";
        case CONN_BATTERY: return "Battery";
        case CONN_NOTIFICATIONS: return "Notifications";
        case CONN_CONNECTED: return "Connected";
        case CONN_FAILED: return "Failed";
        default: return "Unknown";
    }
}

// Main state machine update - call from loop()
bool AcaiaArduinoBLE::update()
{
    // Always reset watchdog
    esp_task_wdt_reset();

    // Keep LVGL responsive
    unsigned long currentMillis = millis();
    if (currentMillis - lastLvUpdate >= lvUpdateInterval)
    {
        lastLvUpdate = currentMillis;
        LVGLTimerHandlerRoutine();
    }

    // Check for state timeout
    if (_connState != CONN_IDLE && _connState != CONN_CONNECTED && _connState != CONN_FAILED)
    {
        if (millis() - _connStateStart > _connTimeout)
        {
            Serial.print("State timeout: ");
            Serial.println(getStateString());

            BLE.stopScan();
            if (_pendingPeripheral)
            {
                _pendingPeripheral.disconnect();
                _pendingPeripheral = BLEDevice();
            }
            transitionTo(CONN_FAILED, 0);
            return false;
        }
    }

    // Execute current state
    switch (_connState)
    {
        case CONN_IDLE:
            return false;

        case CONN_SCANNING:
            stateScanning();
            break;

        case CONN_CONNECTING:
            stateConnecting();
            break;

        case CONN_DISCOVERING:
            stateDiscovering();
            break;

        case CONN_SUBSCRIBING:
            stateSubscribing();
            break;

        case CONN_IDENTIFYING:
            stateIdentifying();
            break;

        case CONN_BATTERY:
            stateBattery();
            break;

        case CONN_NOTIFICATIONS:
            stateNotifications();
            break;

        case CONN_CONNECTED:
            return true;

        case CONN_FAILED:
            Serial.println("Connection failed - cleaning up and restarting scan");

            // Stop any ongoing scan
            BLE.stopScan();

            // Disconnect any existing peripheral connection
            if (_pendingPeripheral)
            {
                Serial.println("Disconnecting old peripheral");
                _pendingPeripheral.disconnect();
            }

            // Clear ALL connection state (including characteristic objects)
            _pendingPeripheral = BLEDevice();
            _read = BLECharacteristic();
            _write = BLECharacteristic();
            _connected = false;
            _lastPacket = 0;
            _packetPeriod = 0;
            _lastHeartBeat = 0;

            // Small delay to let BLE stack process disconnection
            delay(100);

            // Restart scan (DON'T call BLE.end()/begin() - that crashes on ESP32-S3!)
            // Just restart the scan with the existing BLE stack
            if (_mac == "")
            {
                BLE.scan();
            }
            else if (!BLE.scanForAddress(_mac))
            {
                Serial.print("Failed to restart scan for ");
                Serial.println(_mac);
                transitionTo(CONN_IDLE, 0);
                return false;
            }

            Serial.println("Scan restarted (non-blocking reconnect)");
            transitionTo(CONN_SCANNING, 10000);  // 10 second scan timeout for retry
            return false;
    }

    return _connState == CONN_CONNECTED;
}

// Request battery synchronously during init (before weight notifications start)
bool AcaiaArduinoBLE::requestBatterySync()
{
    // Build battery request packet
    byte payload[16] = {0};
    byte bytes[5 + sizeof(payload)];
    bytes[0] = HEADER1;
    bytes[1] = HEADER2;
    bytes[2] = 6; // get setting command
    int cksum1 = 0;
    int cksum2 = 0;

    for (int i = 0; i < sizeof(payload); i++)
    {
        byte val = payload[i] & 0xFF;
        bytes[3 + i] = val;
        if (i % 2 == 0)
            cksum1 += val;
        else
            cksum2 += val;
    }

    bytes[sizeof(payload) + 3] = (cksum1 & 0xFF);
    bytes[sizeof(payload) + 4] = (cksum2 & 0xFF);

    // Send request
    if (!_write.writeValue(bytes, 21))
    {
        Serial.println("battery request failed");
        return false;
    }

    Serial.println("battery request sent");

    // Wait for response (max 1 second)
    unsigned long start = millis();
    byte input[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

    while (millis() - start < 1000)
    {
        // Reset watchdog to prevent timeout during battery wait
        esp_task_wdt_reset();

        if (_read.valueUpdated() && _read.readValue(input, 13) && input[2] == 8)
        {
            _currentBattery = input[4] & 0x7f;
            Serial.print("Battery level received: ");
            Serial.print(_currentBattery);
            Serial.println("%");

            // FLUSH any remaining BLE notifications before starting weight updates
            delay(50);  // Give time for any in-flight notifications to arrive
            while (_read.valueUpdated())
            {
                _read.readValue(input, 13);  // Consume and discard
            }
            Serial.println("BLE buffer flushed");

            return true;
        }

        // Keep LVGL responsive during wait
        unsigned long currentMillis = millis();
        if (currentMillis - lastLvUpdate >= lvUpdateInterval)
        {
            lastLvUpdate = currentMillis;
            LVGLTimerHandlerRoutine();
        }
    }

    Serial.println("battery response timeout (continuing anyway)");

    // DON'T flush on timeout - might consume first weight packets
    // The flush was too aggressive and discarded incoming weight notifications
    // Serial.println("BLE buffer flushed (timeout)");

    return false;
}

// -----------------------------------------------------------------------------
// Non-Blocking State Machine State Handlers
// -----------------------------------------------------------------------------

void AcaiaArduinoBLE::stateScanning()
{
    BLEDevice peripheral = BLE.available();

    if (peripheral && isScaleName(peripheral.localName()))
    {
        BLE.stopScan();
        _pendingPeripheral = peripheral;
        Serial.print("Scale found: ");
        Serial.println(peripheral.localName());
        transitionTo(CONN_CONNECTING, 5000);  // 5s connect timeout
    }
    // Timeout handled by update()
}

void AcaiaArduinoBLE::stateConnecting()
{
    Serial.println("Connecting ...");
    if (_pendingPeripheral.connect())
    {
        Serial.println("Connected");
        transitionTo(CONN_DISCOVERING, 5000);  // 5s discover timeout
    }
    else
    {
        Serial.println("Connection failed!");
        _pendingPeripheral = BLEDevice();  // Clear
        transitionTo(CONN_FAILED, 0);
    }
}

void AcaiaArduinoBLE::stateDiscovering()
{
    Serial.println("Discovering attributes ...");

    // Reset watchdog before blocking ArduinoBLE call (can take 3-10 seconds)
    esp_task_wdt_reset();

    if (_pendingPeripheral.discoverAttributes())
    {
        Serial.println("Attributes discovered");
        transitionTo(CONN_SUBSCRIBING, 5000);  // 5s subscribe timeout
    }
    else
    {
        Serial.println("Attribute discovery failed!");
        _pendingPeripheral.disconnect();
        _pendingPeripheral = BLEDevice();
        transitionTo(CONN_FAILED, 0);
    }
}

void AcaiaArduinoBLE::stateSubscribing()
{
    // Reset watchdog before characteristic operations (can take 1-2 seconds)
    esp_task_wdt_reset();

    // Determine scale type
    if (_pendingPeripheral.characteristic(READ_CHAR_OLD_VERSION).canSubscribe())
    {
        Serial.println("Old version Acaia Detected");
        _type = OLD;
        _write = _pendingPeripheral.characteristic(WRITE_CHAR_OLD_VERSION);
        _read = _pendingPeripheral.characteristic(READ_CHAR_OLD_VERSION);
    }
    else if (_pendingPeripheral.characteristic(READ_CHAR_NEW_VERSION).canSubscribe())
    {
        Serial.println("New version Acaia Detected");
        _type = NEW;
        _write = _pendingPeripheral.characteristic(WRITE_CHAR_NEW_VERSION);
        _read = _pendingPeripheral.characteristic(READ_CHAR_NEW_VERSION);
    }
    else if (_pendingPeripheral.characteristic(READ_CHAR_GENERIC).canSubscribe())
    {
        Serial.println("Generic Scale Detected");
        _type = GENERIC;
        _write = _pendingPeripheral.characteristic(WRITE_CHAR_GENERIC);
        _read = _pendingPeripheral.characteristic(READ_CHAR_GENERIC);
    }
    else
    {
        Serial.println("unable to determine scale type");
        _pendingPeripheral.disconnect();
        _pendingPeripheral = BLEDevice();
        transitionTo(CONN_FAILED, 0);
        return;
    }

    // Subscribe to notifications
    if (!_read.canSubscribe())
    {
        Serial.println("unable to subscribe to READ");
        transitionTo(CONN_FAILED, 0);
    }
    else if (!_read.subscribe())
    {
        Serial.println("subscription failed");
        transitionTo(CONN_FAILED, 0);
    }
    else
    {
        Serial.println("subscribed!");
        transitionTo(CONN_IDENTIFYING, 2000);  // 2s identify timeout
    }
}

void AcaiaArduinoBLE::stateIdentifying()
{
    // Reset watchdog before BLE write operation
    esp_task_wdt_reset();

    if (_write.writeValue(IDENTIFY, 20))
    {
        Serial.println("identify write successful");
        transitionTo(CONN_BATTERY, 2000);  // 2s battery timeout
    }
    else
    {
        Serial.println("identify write failed");
        transitionTo(CONN_FAILED, 0);
    }
}

void AcaiaArduinoBLE::stateBattery()
{
    // Reset watchdog before battery request (can block up to 1 second)
    esp_task_wdt_reset();

    // Use existing requestBatterySync() (has its own timeout and watchdog resets inside)
    // This still blocks but only for ~1 second max
    requestBatterySync();  // Returns immediately or after response/timeout
    transitionTo(CONN_NOTIFICATIONS, 2000);  // 2s notification timeout
}

void AcaiaArduinoBLE::stateNotifications()
{
    // Reset watchdog before final write operation
    esp_task_wdt_reset();

    if (_write.writeValue(NOTIFICATION_REQUEST, 14))
    {
        Serial.println("notification request write successful");
        _connected = true;
        _packetPeriod = 0;
        transitionTo(CONN_CONNECTED, 0);
        Serial.println("Scale fully connected!");
    }
    else
    {
        Serial.println("notification request write failed");
        transitionTo(CONN_FAILED, 0);
    }
}

// REMOVED: getBattery() and updateBattery() - they crash when weight notifications are active
// Battery is now requested during init() via requestBatterySync() before notifications start

int AcaiaArduinoBLE::batteryValue()
{
    return _currentBattery;
}
