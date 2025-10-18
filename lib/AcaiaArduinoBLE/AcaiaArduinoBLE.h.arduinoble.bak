/*
  AcaiaArduinoBLE.h - Library for connecting to 
  an Acaia Scale using the ArduinoBLE library.
  Created by Tate Mazer, December 13, 2023.
  Released into the public domain.

  Pio Baettig: Adding Felicita Arc support 

  Known Bugs:
    * Only supports Grams
*/
#ifndef AcaiaArduinoBLE_h
#define AcaiaArduinoBLE_h

#define LIBRARY_VERSION        "2.1.2+custom"
#define WRITE_CHAR_OLD_VERSION "2a80"
#define READ_CHAR_OLD_VERSION  "2a80"
#define WRITE_CHAR_NEW_VERSION "49535343-8841-43f4-a8d4-ecbe34729bb3"
#define READ_CHAR_NEW_VERSION  "49535343-1e4d-4bd9-ba61-23c647249616"
#define WRITE_CHAR_GENERIC     "ffe1"
#define READ_CHAR_GENERIC      "ffe1"
#define HEARTBEAT_PERIOD_MS     2750
#define MAX_PACKET_PERIOD_MS    1500  // Reduced from 5000 for faster disconnect detection

#include "Arduino.h"
#include <ArduinoBLE.h>
#include <esp_task_wdt.h>  // ESP32 task watchdog timer

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


    private:
        bool isScaleName(String);
        bool requestBatterySync();  // Request battery during init (before weight notifications)

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
        BLECharacteristic   _write;
        BLECharacteristic   _read;
        long                _lastHeartBeat;
        bool                _connected;
        scale_type          _type;
        int                 _currentBattery;
        long                _packetPeriod;
        long                _lastPacket;

        // State machine variables
        ConnectionState     _connState;
        unsigned long       _connStateStart;
        unsigned long       _connTimeout;
        BLEDevice           _pendingPeripheral;
        String              _mac;
        unsigned long       _lastDisconnect;  // Track last disconnection for cooldown
};

#endif
