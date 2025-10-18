/*
  AcaiaArduinoBLE.h - Library for connecting to
  an Acaia Scale using NimBLE library (ESP32 native).

  Refactored from ArduinoBLE to NimBLE for better WiFi coexistence.
  Based on original by Tate Mazer, December 13, 2023.
  NimBLE port: October 2025.

  Features:
  - Non-blocking state machine for connection management
  - Connection watchdog (detects lost connections)
  - Support for multiple Acaia scale types (Lunar old/new, Pyxis, Pearl S)
  - Felicita Arc generic support

  Known Limitations:
  - Only supports Grams (not Ounces)
*/
#ifndef AcaiaArduinoBLE_h
#define AcaiaArduinoBLE_h

#define LIBRARY_VERSION        "3.0.0+nimble"
#define WRITE_CHAR_OLD_VERSION "2a80"
#define READ_CHAR_OLD_VERSION  "2a80"
#define WRITE_CHAR_NEW_VERSION "49535343-8841-43f4-a8d4-ecbe34729bb3"
#define READ_CHAR_NEW_VERSION  "49535343-1e4d-4bd9-ba61-23c647249616"
#define WRITE_CHAR_GENERIC     "ffe1"
#define READ_CHAR_GENERIC      "ffe1"
#define HEARTBEAT_PERIOD_MS     2750
#define MAX_PACKET_PERIOD_MS    8000  // Allow time for heartbeat (2750ms) + command response pauses

#include "Arduino.h"
#include "NimBLEDevice.h"
#include <esp_task_wdt.h>  // ESP32 task watchdog timer
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Thread-safe serial printing (uses external serialMutex from main app)
extern SemaphoreHandle_t serialMutex;

// Thread-safe Serial.print() wrappers
template<typename T>
inline void safePrint(const T& value) {
    if (serialMutex && xSemaphoreTake(serialMutex, pdMS_TO_TICKS(2000))) {
        Serial.print(value);
        xSemaphoreGive(serialMutex);
    }
}

template<typename T>
inline void safePrintln(const T& value) {
    if (serialMutex && xSemaphoreTake(serialMutex, pdMS_TO_TICKS(2000))) {
        Serial.println(value);
        xSemaphoreGive(serialMutex);
    }
}

inline void safePrintln() {
    if (serialMutex && xSemaphoreTake(serialMutex, pdMS_TO_TICKS(2000))) {
        Serial.println();
        xSemaphoreGive(serialMutex);
    }
}

enum scale_type{
    OLD,    // Lunar (pre-2021)
    NEW,    // Lunar (2021), Pyxis
    GENERIC // Felicita Arc, etc
};

enum ConnectionState {
    CONN_IDLE,           // Not connected, not scanning
    CONN_SCANNING,       // BLE scan in progress
    CONN_CONNECTING,     // Found scale, connecting
    CONN_DISCOVERING,    // Discovering BLE attributes
    CONN_SUBSCRIBING,    // Subscribing to notifications
    CONN_IDENTIFYING,    // Sending identify command
    CONN_BATTERY,        // Requesting battery level
    CONN_NOTIFICATIONS,  // Enabling weight notifications
    CONN_CONNECTED,      // Fully connected
    CONN_FAILED          // Connection failed
};

// Forward declarations for friend classes
class AcaiaClientCallbacks;
class AcaiaScanCallbacks;

class AcaiaArduinoBLE{
    public:
        AcaiaArduinoBLE();
        bool init(String = "");
        bool tare();
        bool startTimer();
        bool stopTimer();
        bool resetTimer();
        bool heartbeat();
        float getWeight();
        bool heartbeatRequired();
        bool isConnected();
        bool newWeightAvailable();
        int batteryValue();  // Returns cached battery level (set during init)

        // Non-blocking state machine methods
        bool update();                      // State machine update - call from loop()
        bool isConnecting();                // Returns true if connection in progress
        ConnectionState getConnectionState();  // Get current state for UI
        const char* getStateString();       // Get human-readable state name

        // Brewing state tracking for conditional logging
        void setIsBrewing(bool brewing);    // Set brewing state (controls weight log verbosity)

        // Friend classes for callback access
        friend class AcaiaClientCallbacks;
        friend class AcaiaScanCallbacks;

    private:
        bool isScaleName(String);
        bool requestBatterySync();  // Request battery during init (before weight notifications)

        // NimBLE callbacks
        static void notifyCallback(NimBLERemoteCharacteristic* pRemoteCharacteristic,
                                   uint8_t* pData, size_t length, bool isNotify);
        void handleNotification(uint8_t* pData, size_t length);

        // State machine handlers
        void stateScanning();
        void stateConnecting();
        void stateDiscovering();
        void stateSubscribing();
        void stateIdentifying();
        void stateBattery();
        void stateNotifications();
        void transitionTo(ConnectionState newState, unsigned long timeout);

        // Existing member variables
        float               _currentWeight;
        long                _lastHeartBeat;
        bool                _connected;
        scale_type          _type;
        int                 _currentBattery;
        long                _packetPeriod;
        long                _lastPacket;
        bool                _isBrewing;      // Track brewing state for conditional logging

        // State machine variables
        ConnectionState     _connState;
        unsigned long       _connStateStart;
        unsigned long       _connTimeout;
        String              _mac;
        unsigned long       _lastDisconnect;  // Track last disconnection for cooldown

        // NimBLE objects (replacing ArduinoBLE)
        NimBLEClient*                  _pClient;
        NimBLERemoteService*           _pService;
        NimBLERemoteCharacteristic*    _pWriteChar;
        NimBLERemoteCharacteristic*    _pReadChar;
        NimBLEAdvertisedDevice*        _pAdvDevice;
        NimBLEScan*                    _pScan;

        // Static instance pointer for callbacks
        static AcaiaArduinoBLE* _instance;
};

#endif
