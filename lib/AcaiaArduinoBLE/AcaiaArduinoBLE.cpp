/*
  AcaiaArduinoBLE.cpp - Library for connecting to
  an Acaia Scale using NimBLE library (ESP32 native).
  
  Refactored from ArduinoBLE to NimBLE for better WiFi+BLE coexistence.
  Based on original by Tate Mazer, December 13, 2023.
  NimBLE port: October 2025.
*/

#include "Arduino.h"
#include "AcaiaArduinoBLE.h"
#include "NimBLEDevice.h"
#include "../../src/debug_config.h"  // Professional logging system

// Logging tag for BLE subsystem
static const char* TAG = "BLE";

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

// LVGL timer handler (external)
const int lvUpdateInterval = 16;
unsigned long lastLvUpdate = 0;
extern void LVGLTimerHandlerRoutine();

// Static instance pointer for callbacks
AcaiaArduinoBLE* AcaiaArduinoBLE::_instance = nullptr;

// Scan callback class - detects Acaia scales
class AcaiaScanCallbacks : public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice* advertisedDevice) {
        if (AcaiaArduinoBLE::_instance) {
            String name = String(advertisedDevice->getName().c_str());

            // DIAGNOSTIC: Log ALL BLE devices found during scan
            // This helps verify ESP32 BLE is working and shows what devices are visible
            // Changed to VERBOSE to reduce log spam (50-100+ devices during scan)
            if (name.length() > 0) {
                LOG_VERBOSE(TAG, "BLE device found: '%s' (RSSI: %d dBm, Address: %s)",
                            name.c_str(),
                            advertisedDevice->getRSSI(),
                            advertisedDevice->getAddress().toString().c_str());
            } else {
                LOG_VERBOSE(TAG, "BLE device found: <no name> (RSSI: %d dBm, Address: %s)",
                            advertisedDevice->getRSSI(),
                            advertisedDevice->getAddress().toString().c_str());
            }

            // Check if this is a supported scale
            if (AcaiaArduinoBLE::_instance->isScaleName(name)) {
                LOG_INFO(TAG, "✓ ACAIA SCALE FOUND: %s", name.c_str());
                NimBLEDevice::getScan()->stop();
                // Store device ADDRESS (value copy), not pointer
                // FIX: Storing pointer to advertisedDevice causes crash - it's a temporary object!
                AcaiaArduinoBLE::_instance->_deviceAddress = advertisedDevice->getAddress();
                AcaiaArduinoBLE::_instance->_deviceFound = true;
            }
        }
    }
};

// Client callbacks - handle connection events
class AcaiaClientCallbacks : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient* pClient) {
        LOG_DEBUG(TAG, "NimBLE Client connected");
    }

    void onDisconnect(NimBLEClient* pClient) {
        // DIAGNOSTIC: Log disconnect with timing context
        if (AcaiaArduinoBLE::_instance) {
            unsigned long timeSinceLastPacket = AcaiaArduinoBLE::_instance->_lastPacket ?
                (millis() - AcaiaArduinoBLE::_instance->_lastPacket) : 0;
            unsigned long timeSinceLastHeartbeat = millis() - AcaiaArduinoBLE::_instance->_lastHeartBeat;

            LOG_WARN(TAG, "=== SCALE DISCONNECTED ===");
            LOG_WARN(TAG, "  Last packet: %lums ago", timeSinceLastPacket);
            LOG_WARN(TAG, "  Last heartbeat: %lums ago (period=%dms)",
                     timeSinceLastHeartbeat, HEARTBEAT_PERIOD_MS);
            LOG_WARN(TAG, "  Timeout threshold: %dms", MAX_PACKET_PERIOD_MS);
            LOG_WARN(TAG, "  Disconnect initiated by: %s",
                     (timeSinceLastPacket >= MAX_PACKET_PERIOD_MS) ? "ESP32 (timeout)" : "Scale (remote)");
            LOG_WARN(TAG, "==========================");

            AcaiaArduinoBLE::_instance->_connected = false;
            // CRITICAL: Transition state machine to FAILED on disconnect
            // This prevents continued execution with invalid pointers
            AcaiaArduinoBLE::_instance->transitionTo(CONN_FAILED, 0);
            // Clear characteristic pointers to prevent dangling references
            AcaiaArduinoBLE::_instance->_pReadChar = nullptr;
            AcaiaArduinoBLE::_instance->_pWriteChar = nullptr;
            AcaiaArduinoBLE::_instance->_pService = nullptr;
        } else {
            LOG_WARN(TAG, "NimBLE Client disconnected (no instance context)");
        }
    }
};

static AcaiaScanCallbacks scanCallbacks;
static AcaiaClientCallbacks clientCallbacks;

// Constructor
AcaiaArduinoBLE::AcaiaArduinoBLE()
{
    _instance = this;
    _currentWeight = 999;
    _connected = false;
    _packetPeriod = 0;
    _isBrewing = false;  // Start in idle mode (no weight logging)

    // Initialize state machine
    _connState = CONN_IDLE;
    _connStateStart = 0;
    _connTimeout = 0;
    _mac = "";
    _lastDisconnect = 0;

    // Initialize NimBLE pointers
    _pClient = nullptr;
    _pService = nullptr;
    _pWriteChar = nullptr;
    _pReadChar = nullptr;
    _deviceFound = false;  // Initialize device found flag
    _pScan = nullptr;
}

bool AcaiaArduinoBLE::init(String mac)
{
    // Don't restart if already connecting or connected
    if (_connState != CONN_IDLE && _connState != CONN_FAILED)
    {
        // Silently reject - this is called frequently from loop
        return false;
    }

    LOG_INFO(TAG, "AcaiaArduinoBLE Library v%s - Starting non-blocking connection...", LIBRARY_VERSION);

    _mac = mac;
    _lastPacket = 0;
    _connected = false;
    _deviceFound = false;  // Reset device found flag

    // Get or create scan object
    _pScan = NimBLEDevice::getScan();
    if (!_pScan) {
        LOG_ERROR(TAG, "Failed to get scan object!");
        return false;
    }

    // Configure scan
    _pScan->setAdvertisedDeviceCallbacks(&scanCallbacks, false);
    _pScan->setActiveScan(true);
    _pScan->setInterval(100);
    _pScan->setWindow(99);

    // Start scanning
    LOG_INFO(TAG, "BLE scan started (non-blocking)");
    _pScan->start(0, nullptr, false); // 0 = scan forever until stopped
    
    transitionTo(CONN_SCANNING, 10000);  // 10 second scan timeout
    return true;
}

bool AcaiaArduinoBLE::tare()
{
    // Check for NULL pointers - disconnect can happen asynchronously
    if (!_connected || !_pWriteChar || !_pClient || !_pClient->isConnected())
    {
        LOG_DEBUG(TAG, "tare failed: not connected or NULL pointer");
        return false;
    }

    if (_pWriteChar->writeValue((_type == GENERIC ? TARE_GENERIC : TARE_ACAIA),
                                  (_type == GENERIC ? 1 : 6), true))
    {
        LOG_DEBUG(TAG, "tare write successful");
        return true;
    }
    else
    {
        _connected = false;
        LOG_ERROR(TAG, "tare write failed");
        return false;
    }
}

bool AcaiaArduinoBLE::startTimer()
{
    // Check for NULL pointers - disconnect can happen asynchronously
    if (!_connected || !_pWriteChar || !_pClient || !_pClient->isConnected())
    {
        LOG_DEBUG(TAG, "start timer failed: not connected or NULL pointer");
        return false;
    }

    if (_pWriteChar->writeValue(START_TIMER, 7, true))
    {
        LOG_DEBUG(TAG, "start timer write successful");
        return true;
    }
    else
    {
        _connected = false;
        LOG_ERROR(TAG, "start timer write failed");
        return false;
    }
}

bool AcaiaArduinoBLE::stopTimer()
{
    // Check for NULL pointers - disconnect can happen asynchronously
    if (!_connected || !_pWriteChar || !_pClient || !_pClient->isConnected())
    {
        LOG_DEBUG(TAG, "stop timer failed: not connected or NULL pointer");
        return false;
    }

    if (_pWriteChar->writeValue(STOP_TIMER, 7, true))
    {
        LOG_DEBUG(TAG, "stop timer write successful");
        return true;
    }
    else
    {
        _connected = false;
        LOG_ERROR(TAG, "stop timer write failed");
        return false;
    }
}

bool AcaiaArduinoBLE::resetTimer()
{
    // Check for NULL pointers - disconnect can happen asynchronously
    if (!_connected || !_pWriteChar || !_pClient || !_pClient->isConnected())
    {
        LOG_DEBUG(TAG, "reset timer failed: not connected or NULL pointer");
        return false;
    }

    if (_pWriteChar->writeValue(RESET_TIMER, 7, true))
    {
        LOG_DEBUG(TAG, "reset timer write successful");
        return true;
    }
    else
    {
        _connected = false;
        LOG_ERROR(TAG, "reset timer write failed");
        return false;
    }
}

bool AcaiaArduinoBLE::heartbeat()
{
    // CRITICAL: Check for NULL pointers - scale can disconnect asynchronously
    // onDisconnect() callback sets _pWriteChar = nullptr
    if (!_connected || !_pWriteChar || !_pClient || !_pClient->isConnected())
    {
        LOG_VERBOSE(TAG, "heartbeat failed: not connected or NULL pointer");
        return false;
    }

    // DIAGNOSTIC: Log heartbeat with timing info
    unsigned long timeSinceLastPacket = _lastPacket ? (millis() - _lastPacket) : 0;
    LOG_INFO(TAG, "Sending heartbeat (last packet %lums ago, timeout=%dms)",
             timeSinceLastPacket, MAX_PACKET_PERIOD_MS);

    if (_pWriteChar->writeValue(HEARTBEAT, 7, true))
    {
        _lastHeartBeat = millis();
        LOG_DEBUG(TAG, "Heartbeat sent successfully");
        return true;
    }
    else
    {
        LOG_ERROR(TAG, "Heartbeat write FAILED - disconnecting");
        _connected = false;
        return false;
    }
}

float AcaiaArduinoBLE::getWeight()
{
    return _currentWeight;
}

void AcaiaArduinoBLE::setIsBrewing(bool brewing)
{
    _isBrewing = brewing;
}

bool AcaiaArduinoBLE::heartbeatRequired()
{
    if (_type == OLD || _type == NEW)
    {
        return (millis() - _lastHeartBeat) > HEARTBEAT_PERIOD_MS;
    }
    else
    {
        return false;
    }
}

bool AcaiaArduinoBLE::isConnected()
{
    return _connected;
}

int AcaiaArduinoBLE::batteryValue()
{
    return _currentBattery;
}

// Notification callback - called when scale sends weight data
void AcaiaArduinoBLE::notifyCallback(NimBLERemoteCharacteristic* pRemoteCharacteristic,
                                      uint8_t* pData, size_t length, bool isNotify)
{
    if (_instance) {
        _instance->handleNotification(pData, length);
    }
}

void AcaiaArduinoBLE::handleNotification(uint8_t* pData, size_t length)
{
    // CRITICAL: NO LOGGING IN NOTIFICATION CALLBACK!
    // This function runs at high frequency (~20 Hz when scale is active)
    // Logging causes serial buffer overflow → system crashes after 6-30 seconds
    // Previous crash cause: LOG_VERBOSE + LOG_WARN printing 180 bytes per packet
    // Combined with 242 Hz display refresh = >3000ms blocking → watchdog timeout

    // Parse weight data based on scale type
    if (NEW == _type && length >= 13 && pData[2] == 0x0C && pData[4] == 0x05)
    {
        // New Acaia (Lunar 2021+, Pyxis, and Lunar 2019-2021 transitional)
        _currentWeight = (((pData[6] & 0xff) << 8) + (pData[5] & 0xff)) / pow(10, pData[9]) * ((pData[10] & 0x02) ? -1 : 1);

        // NO LOGGING - causes serial buffer overflow at 20 Hz notification rate

        if (_lastPacket)
        {
            _packetPeriod = millis() - _lastPacket;
        }
        _lastPacket = millis();
    }
    else if (OLD == _type && length >= 10)
    {
        // Old Acaia (Lunar pre-2021)
        _currentWeight = (((pData[3] & 0xff) << 8) + (pData[2] & 0xff)) / pow(10, pData[6]) * ((pData[7] & 0x02) ? -1 : 1);

        // NO LOGGING - causes serial buffer overflow at 20 Hz notification rate

        if (_lastPacket)
        {
            _packetPeriod = millis() - _lastPacket;
        }
        _lastPacket = millis();
    }
    else if (GENERIC == _type && length >= 13)
    {
        // Generic scales (Felicita Arc)
        _currentWeight = (pData[2] == 0x2B ? 1 : -1) *
                        ((pData[3] - 0x30) * 1000 + (pData[4] - 0x30) * 100 +
                         (pData[5] - 0x30) * 10 + (pData[6] - 0x30) * 1 +
                         (pData[7] - 0x30) * 0.1 + (pData[8] - 0x30) * 0.01);

        // NO LOGGING - causes serial buffer overflow at 20 Hz notification rate

        if (_lastPacket)
        {
            _packetPeriod = millis() - _lastPacket;
        }
        _lastPacket = millis();
    }
    else
    {
        // Unrecognized packet - silently ignore
        // NO LOGGING HERE! Previous LOG_WARN printed 180 bytes per packet
        // This caused serial buffer overflow → system crashes
        // Diagnostic: If needed, add counter and log in main loop (not here!)
    }
}

bool AcaiaArduinoBLE::newWeightAvailable()
{
    // DIAGNOSTIC: Log packet timing every 5 seconds
    static unsigned long lastDiagnostic = 0;
    if (_connected && _lastPacket && (millis() - lastDiagnostic > 5000))
    {
        unsigned long timeSincePacket = millis() - _lastPacket;
        LOG_VERBOSE(TAG, "Packet health check: last packet %lums ago (timeout at %dms)",
                    timeSincePacket, MAX_PACKET_PERIOD_MS);
        lastDiagnostic = millis();
    }

    // Check for connection timeout
    if (_lastPacket && millis() - _lastPacket > MAX_PACKET_PERIOD_MS)
    {
        LOG_ERROR(TAG, "Connection timeout - no packets received!");
        LOG_ERROR(TAG, "  Last packet was %lums ago (threshold: %dms)",
                  millis() - _lastPacket, MAX_PACKET_PERIOD_MS);
        _connected = false;
        _connState = CONN_FAILED;
        _lastPacket = 0;
        return false;
    }

    // Weight updates come via notification callback
    // Just check if we received data recently
    return (_lastPacket > 0);
}

bool AcaiaArduinoBLE::isScaleName(String name)
{
    String nameShort = name.substring(0, 5);
    return nameShort == "CINCO" || nameShort == "ACAIA" || nameShort == "PYXIS" || 
           nameShort == "LUNAR" || nameShort == "PROCH" || nameShort == "FELIC";
}

// State machine helpers
void AcaiaArduinoBLE::transitionTo(ConnectionState newState, unsigned long timeout)
{
    _connState = newState;
    _connStateStart = millis();
    _connTimeout = timeout;
}

bool AcaiaArduinoBLE::isConnecting()
{
    return _connState != CONN_IDLE && _connState != CONN_CONNECTED && _connState != CONN_FAILED;
}

ConnectionState AcaiaArduinoBLE::getConnectionState()
{
    return _connState;
}

const char* AcaiaArduinoBLE::getStateString()
{
    switch (_connState) {
        case CONN_IDLE:           return "Idle";
        case CONN_SCANNING:       return "Scanning";
        case CONN_CONNECTING:     return "Connecting";
        case CONN_DISCOVERING:    return "Discovering";
        case CONN_SUBSCRIBING:    return "Subscribing";
        case CONN_IDENTIFYING:    return "Identifying";
        case CONN_BATTERY:        return "Battery";
        case CONN_NOTIFICATIONS:  return "Notifications";
        case CONN_CONNECTED:      return "Connected";
        case CONN_FAILED:         return "Failed";
        case CONN_RECONNECT_DELAY: return "Reconnect Delay";
        default:                  return "Unknown";
    }
}

// State machine handlers - NimBLE implementation

void AcaiaArduinoBLE::stateScanning()
{
    // Check if scan callback found a device
    if (_deviceFound)
    {
        _pScan->stop();
        transitionTo(CONN_CONNECTING, 5000);  // 5s connect timeout
    }
    // Timeout handled by update()
}

void AcaiaArduinoBLE::stateConnecting()
{
    LOG_INFO(TAG, "Connecting ...");

    // Create or get client
    if (!_pClient) {
        _pClient = NimBLEDevice::createClient();
        if (!_pClient) {
            LOG_ERROR(TAG, "Failed to create client!");
            transitionTo(CONN_FAILED, 0);
            return;
        }
        _pClient->setClientCallbacks(&clientCallbacks, false);
        _pClient->setConnectionParams(12, 12, 0, 150);
        _pClient->setConnectTimeout(5);
    }

    // Connect to device using stored address
    if (_pClient->connect(_deviceAddress))
    {
        LOG_INFO(TAG, "Connected");
        transitionTo(CONN_DISCOVERING, 5000);  // 5s discover timeout
    }
    else
    {
        LOG_ERROR(TAG, "Connection failed!");
        _deviceFound = false;  // Reset flag
        transitionTo(CONN_FAILED, 0);
    }
}

void AcaiaArduinoBLE::stateDiscovering()
{
    LOG_DEBUG(TAG, "Discovering services ...");

    // Reset watchdog (can take time)
    esp_task_wdt_reset();

    // Try to get services (NimBLE auto-discovers on connect)
    // Just need to find the right characteristic UUIDs

    transitionTo(CONN_SUBSCRIBING, 5000);  // 5s subscribe timeout
}

void AcaiaArduinoBLE::stateSubscribing()
{
    // Reset watchdog before characteristic operations
    esp_task_wdt_reset();

    LOG_DEBUG(TAG, "Finding characteristics ...");

    // CRITICAL: Check if still connected before accessing characteristics
    // Client can disconnect during previous state transitions
    if (!_pClient || !_pClient->isConnected()) {
        LOG_ERROR(TAG, "Client disconnected during subscribing");
        transitionTo(CONN_FAILED, 0);
        return;
    }

    // Try OLD version first
    _pService = _pClient->getService(NimBLEUUID("00001820-0000-1000-8000-00805f9b34fb"));
    if (_pService) {
        // CRITICAL: Must retrieve characteristics from the service first
        std::vector<NimBLERemoteCharacteristic*>* pChars = _pService->getCharacteristics(true);

        // Check connection after getCharacteristics (can trigger disconnect)
        if (!_pClient->isConnected()) {
            LOG_ERROR(TAG, "Client disconnected during OLD characteristic discovery");
            transitionTo(CONN_FAILED, 0);
            return;
        }

        NimBLERemoteCharacteristic* tempReadChar = _pService->getCharacteristic(NimBLEUUID(READ_CHAR_OLD_VERSION));

        // CRITICAL: Check connection again before committing pointers
        if (!_pClient->isConnected()) {
            LOG_ERROR(TAG, "Client disconnected after getting OLD read characteristic");
            transitionTo(CONN_FAILED, 0);
            return;
        }

        if (tempReadChar && tempReadChar->canNotify()) {
            LOG_INFO(TAG, "Old version Acaia Detected");
            _type = OLD;
            _pReadChar = tempReadChar;
            _pWriteChar = _pService->getCharacteristic(NimBLEUUID(WRITE_CHAR_OLD_VERSION));
        }
    }

    // Try NEW version
    if (!_pReadChar) {
        _pService = _pClient->getService(NimBLEUUID("49535343-fe7d-4ae5-8fa9-9fafd205e455"));
        if (_pService) {
            // CRITICAL: Must retrieve characteristics from the service first
            std::vector<NimBLERemoteCharacteristic*>* pChars = _pService->getCharacteristics(true);

            // Check connection after getCharacteristics (can trigger disconnect)
            if (!_pClient->isConnected()) {
                LOG_ERROR(TAG, "Client disconnected during NEW characteristic discovery");
                transitionTo(CONN_FAILED, 0);
                return;
            }

            NimBLERemoteCharacteristic* tempReadChar = _pService->getCharacteristic(NimBLEUUID(READ_CHAR_NEW_VERSION));

            // CRITICAL: Check connection again before committing pointers
            // Disconnect can happen between getCharacteristics and getCharacteristic
            if (!_pClient->isConnected()) {
                LOG_ERROR(TAG, "Client disconnected after getting read characteristic");
                transitionTo(CONN_FAILED, 0);
                return;
            }

            if (tempReadChar && tempReadChar->canNotify()) {
                LOG_INFO(TAG, "New version Acaia Detected");
                _type = NEW;
                _pReadChar = tempReadChar;
                _pWriteChar = _pService->getCharacteristic(NimBLEUUID(WRITE_CHAR_NEW_VERSION));
            }
        }
    }

    // Try GENERIC
    if (!_pReadChar) {
        _pService = _pClient->getService(NimBLEUUID("0000ffe0-0000-1000-8000-00805f9b34fb"));
        if (_pService) {
            // CRITICAL: Must retrieve characteristics from the service first
            std::vector<NimBLERemoteCharacteristic*>* pChars = _pService->getCharacteristics(true);

            // Check connection after getCharacteristics (can trigger disconnect)
            if (!_pClient->isConnected()) {
                LOG_ERROR(TAG, "Client disconnected during GENERIC characteristic discovery");
                transitionTo(CONN_FAILED, 0);
                return;
            }

            NimBLERemoteCharacteristic* tempReadChar = _pService->getCharacteristic(NimBLEUUID(READ_CHAR_GENERIC));

            // CRITICAL: Check connection again before committing pointers
            if (!_pClient->isConnected()) {
                LOG_ERROR(TAG, "Client disconnected after getting GENERIC read characteristic");
                transitionTo(CONN_FAILED, 0);
                return;
            }

            if (tempReadChar && tempReadChar->canNotify()) {
                LOG_INFO(TAG, "Generic Scale Detected");
                _type = GENERIC;
                _pReadChar = tempReadChar;
                _pWriteChar = _pService->getCharacteristic(NimBLEUUID(WRITE_CHAR_GENERIC));
            }
        }
    }

    if (!_pReadChar || !_pWriteChar) {
        LOG_ERROR(TAG, "Unable to determine scale type or find characteristics");
        _pClient->disconnect();
        transitionTo(CONN_FAILED, 0);
        return;
    }

    // CRITICAL: Verify still connected before subscribing
    // Client can disconnect during characteristic discovery
    if (!_pClient->isConnected()) {
        LOG_ERROR(TAG, "Client disconnected before subscription");
        transitionTo(CONN_FAILED, 0);
        return;
    }

    // Subscribe to notifications
    if (!_pReadChar->subscribe(true, notifyCallback)) {
        LOG_ERROR(TAG, "Subscription failed");
        transitionTo(CONN_FAILED, 0);
        return;
    }

    // CRITICAL: Check for disconnect AFTER subscribe() completes
    // The scale can disconnect asynchronously during subscribe() call
    // onDisconnect() callback sets _pReadChar = nullptr
    // We must verify pointers are still valid before transitioning
    if (!_pReadChar || !_pWriteChar || !_pClient || !_pClient->isConnected()) {
        LOG_ERROR(TAG, "Scale disconnected during subscription (race condition prevented)");
        transitionTo(CONN_FAILED, 0);
        return;
    }

    LOG_INFO(TAG, "Subscribed to notifications");
    transitionTo(CONN_IDENTIFYING, 5000);  // 5s identify timeout
}

void AcaiaArduinoBLE::stateIdentifying()
{
    // CRITICAL: Check for disconnect during settling delay
    // Scale can disconnect asynchronously during the 200ms wait
    if (!_pWriteChar || !_pClient || !_pClient->isConnected()) {
        LOG_ERROR(TAG, "Scale disconnected during identify delay (race condition prevented)");
        transitionTo(CONN_FAILED, 0);
        return;
    }

    // STABILITY FIX: Wait 200ms after subscription before sending identify
    // BLE task runs at 100Hz (10ms loop), so 200ms = ~20 iterations
    // Gives scale adequate time to process the subscription command
    // Without this delay, scale may disconnect during identify (especially on first attempt)
    if (millis() - _connStateStart < 200) {
        return;  // Check again in 10ms (next BLE task loop iteration)
    }

    LOG_DEBUG(TAG, "Sending identify ...");

    if (!_pWriteChar->writeValue(IDENTIFY, 20, true)) {
        LOG_ERROR(TAG, "Identify write failed");
        transitionTo(CONN_FAILED, 0);
        return;
    }

    LOG_DEBUG(TAG, "Identify sent");
    transitionTo(CONN_BATTERY, 2000);  // 2s battery timeout
}

void AcaiaArduinoBLE::stateBattery()
{
    // CRITICAL: Check for disconnect during settling delay
    // Scale can disconnect asynchronously during the 200ms wait
    if (!_pWriteChar || !_pClient || !_pClient->isConnected()) {
        LOG_ERROR(TAG, "Scale disconnected during battery delay (race condition prevented)");
        transitionTo(CONN_FAILED, 0);
        return;
    }

    // STABILITY FIX: Wait 200ms after identify command before battery request
    // BLE task runs at 100Hz (10ms loop), so 200ms = ~20 iterations
    // Gives scale time to process the identify command
    if (millis() - _connStateStart < 200) {
        return;  // Check again in 10ms (next BLE task loop iteration)
    }

    LOG_DEBUG(TAG, "Requesting battery (skipping for now) ...");
    // Battery request implementation can be added later
    transitionTo(CONN_NOTIFICATIONS, 2000);
}

void AcaiaArduinoBLE::stateNotifications()
{
    // CRITICAL: Check for disconnect during settling delay
    // Scale can disconnect asynchronously during the 200ms wait
    if (!_pWriteChar || !_pClient || !_pClient->isConnected()) {
        LOG_ERROR(TAG, "Scale disconnected during notifications delay (race condition prevented)");
        transitionTo(CONN_FAILED, 0);
        return;
    }

    // STABILITY FIX: Wait 200ms before enabling weight notifications
    // BLE task runs at 100Hz (10ms loop), so 200ms = ~20 iterations
    // Gives scale time to process previous commands (identify, battery)
    if (millis() - _connStateStart < 200) {
        return;  // Check again in 10ms (next BLE task loop iteration)
    }

    LOG_DEBUG(TAG, "Enabling weight notifications ...");

    // DIAGNOSTIC: Log the NOTIFICATION_REQUEST packet being sent
    LOG_INFO(TAG, "Sending NOTIFICATION_REQUEST: [ef dd 0c 09 00 01 01 02 02 05 03 04 15 06]");
    LOG_DEBUG(TAG, "  Scale type: %s", _type == NEW ? "NEW" : (_type == OLD ? "OLD" : "GENERIC"));
    LOG_DEBUG(TAG, "  Expecting weight packets: [2]=0x0C [4]=0x05 for NEW scales");

    if (!_pWriteChar->writeValue(NOTIFICATION_REQUEST, 14, true)) {
        LOG_ERROR(TAG, "Notification request write failed");
        transitionTo(CONN_FAILED, 0);
        return;
    }

    LOG_INFO(TAG, "Weight notifications enabled (waiting for 17-byte weight packets)");
    LOG_DEBUG(TAG, "  If no weight data appears, check notification handler logs");
    _connected = true;
    _lastHeartBeat = millis();
    _lastPacket = 0;  // Reset packet timer
    transitionTo(CONN_CONNECTED, 0);
}

// Main state machine update - call from loop()
bool AcaiaArduinoBLE::update()
{
    // Always reset watchdog
    esp_task_wdt_reset();

    // LVGL updates removed - handled by main loop on Core 1
    // With FreeRTOS task architecture, BLE runs on Core 0, UI on Core 1
    // LVGL is NOT thread-safe - only main loop should call lv_timer_handler()
    // unsigned long currentMillis = millis();
    // if (currentMillis - lastLvUpdate >= lvUpdateInterval)
    // {
    //     lastLvUpdate = currentMillis;
    //     LVGLTimerHandlerRoutine();
    // }

    // Check for state timeout
    // CRITICAL: Exclude CONN_RECONNECT_DELAY from timeout handling
    // That state is SUPPOSED to timeout after 500ms (it's a non-blocking delay)
    if (_connState != CONN_IDLE && _connState != CONN_CONNECTED &&
        _connState != CONN_FAILED && _connState != CONN_RECONNECT_DELAY)
    {
        if (millis() - _connStateStart > _connTimeout)
        {
            LOG_WARN(TAG, "State timeout: %s", getStateString());

            if (_pScan && _pScan->isScanning()) {
                _pScan->stop();
            }
            if (_pClient && _pClient->isConnected()) {
                _pClient->disconnect();
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
            LOG_INFO(TAG, "Connection failed - cleaning up and restarting scan");

            // Stop any ongoing scan
            if (_pScan && _pScan->isScanning()) {
                _pScan->stop();
            }

            // Disconnect client
            if (_pClient) {
                if (_pClient->isConnected()) {
                    LOG_DEBUG(TAG, "Disconnecting client");
                    _pClient->disconnect();
                }
                NimBLEDevice::deleteClient(_pClient);
                _pClient = nullptr;
            }

            // Clear connection state
            _pService = nullptr;
            _pWriteChar = nullptr;
            _pReadChar = nullptr;
            _deviceFound = false;  // Reset device found flag
            _connected = false;
            _lastPacket = 0;
            _packetPeriod = 0;
            _lastHeartBeat = 0;

            // CRITICAL FIX: Non-blocking delay before retry
            // Replaced blocking delay(500) with state machine delay
            // Gives scale adequate time to reset after disconnect
            // Especially important after identify/subscription failures
            transitionTo(CONN_RECONNECT_DELAY, 500);  // 500ms non-blocking delay
            break;

        case CONN_RECONNECT_DELAY:
            // Non-blocking delay state - wait for timeout before restarting scan
            // This prevents watchdog timeout from blocking delay() calls
            if (millis() - _connStateStart < _connTimeout) {
                break;  // Still waiting for 500ms delay to expire
            }

            // Delay expired - restart scan
            LOG_INFO(TAG, "Scan restarted (non-blocking reconnect)");
            _pScan = NimBLEDevice::getScan();
            if (_pScan) {
                _pScan->start(0, nullptr, false);
                transitionTo(CONN_SCANNING, 10000);  // 10s scan timeout
            } else {
                LOG_ERROR(TAG, "Failed to restart scan");
                transitionTo(CONN_IDLE, 0);
            }
            break;
    }

    return _connState == CONN_CONNECTED;
}

// Battery request helper (optional - can be enhanced later)
bool AcaiaArduinoBLE::requestBatterySync()
{
    // Battery request protocol can be implemented here
    _currentBattery = 0;  // Default unknown
    return true;
}

