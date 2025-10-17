#ifndef DEBUG_CONFIG_H
#define DEBUG_CONFIG_H

// =============================================================================
// Wireless Debug Configuration for Gravimetric Shots
// =============================================================================
// This file provides conditional wireless debugging via WebSerial over WiFi.
//
// Usage:
//   - Production build: DEBUG_PRINT routes to USB Serial
//   - Debug build (-DWIRELESS_DEBUG): Routes to BOTH WebSerial AND USB Serial
//
// Build commands:
//   Production: pio run -e gravimetric_shots
//   Debug:      pio run -e gravimetric_shots_debug
//
// Access WebSerial:
//   http://<ESP32-IP>/webserial (IP shown in USB Serial at boot)
// =============================================================================

#ifdef WIRELESS_DEBUG
  #include <WiFi.h>
  #include <WebSerial.h>
  #include <ESPAsyncWebServer.h>
  #include "wifi_credentials.h"  // Contains WIFI_SSID and WIFI_PASS

  extern AsyncWebServer debugServer;

  // Initialize wireless debugging (call in setup())
  void setupWirelessDebug();

  // Debug macros - output to BOTH WebSerial AND USB Serial
  #define DEBUG_INIT() setupWirelessDebug()

  #define DEBUG_PRINT(x) do { \
    WebSerial.print(x); \
    Serial.print(x); \
  } while(0)

  #define DEBUG_PRINTLN(x) do { \
    WebSerial.println(x); \
    Serial.println(x); \
  } while(0)

  #define DEBUG_PRINTF(fmt, ...) do { \
    WebSerial.printf(fmt, ##__VA_ARGS__); \
    Serial.printf(fmt, ##__VA_ARGS__); \
  } while(0)

#else
  // Production build - USB Serial only
  #define DEBUG_INIT() Serial.begin(115200)
  #define DEBUG_PRINT(x) Serial.print(x)
  #define DEBUG_PRINTLN(x) Serial.println(x)
  #define DEBUG_PRINTF(fmt, ...) Serial.printf(fmt, ##__VA_ARGS__)
#endif

#endif // DEBUG_CONFIG_H
