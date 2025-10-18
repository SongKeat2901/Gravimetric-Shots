// =============================================================================
// Wireless Debug Implementation for Gravimetric Shots
// =============================================================================
// Provides WiFi + WebSerial functionality for wireless debug monitoring.
// Only compiled when -DWIRELESS_DEBUG build flag is set.
// =============================================================================

#include "debug_config.h"

#ifdef WIRELESS_DEBUG

#include "esp_coexist.h"  // For BLE/WiFi coexistence API

// Web server for WebSerial (port 80)
AsyncWebServer debugServer(80);

// WebSerial ready flag (false until WebSerial.begin() is called)
bool webSerialReady = false;

// Logging tag for WiFi subsystem
static const char* TAG = "WiFi";

// =============================================================================
// WebSerial Message Callback
// =============================================================================
// Handles commands sent from the web browser interface.
// Optional feature - can send commands to ESP32 during runtime.
// =============================================================================
void webSerialCallback(uint8_t *data, size_t len) {
  String cmd = "";
  for(size_t i = 0; i < len; i++) {
    cmd += char(data[i]);
  }

  WebSerial.println("Received: " + cmd);
  Serial.println("[WebSerial] Received: " + cmd);

  // Example commands
  if(cmd == "restart" || cmd == "reboot") {
    WebSerial.println("Restarting ESP32...");
    Serial.println("[WebSerial] Restart requested");
    delay(100);
    ESP.restart();
  }
  else if(cmd == "heap") {
    WebSerial.printf("Free heap: %d bytes, Min free: %d bytes\n",
                     ESP.getFreeHeap(), ESP.getMinFreeHeap());
  }
  else if(cmd == "wifi") {
    int rssi = WiFi.RSSI();
    WebSerial.printf("WiFi RSSI: %d dBm (%s)\n", rssi,
                     rssi > -50 ? "Excellent" :
                     rssi > -60 ? "Good" :
                     rssi > -70 ? "Fair" : "Weak");
  }
  else if(cmd == "help") {
    WebSerial.println("Available commands:");
    WebSerial.println("  restart - Reboot ESP32");
    WebSerial.println("  heap    - Show memory usage");
    WebSerial.println("  wifi    - Show WiFi signal strength");
    WebSerial.println("  help    - Show this message");
  }
}

// =============================================================================
// Setup Wireless Debugging
// =============================================================================
// Initializes WiFi connection and WebSerial server.
// Called from setup() when WIRELESS_DEBUG is defined.
//
// Behavior:
//   - Connects to WiFi (10-second timeout)
//   - Prints IP address to USB Serial
//   - Starts WebSerial server on port 80
//   - Configures WiFi for BLE coexistence (low-latency mode)
//   - Continues without WiFi if connection fails (graceful degradation)
// =============================================================================
void setupWirelessDebug() {
  // DON'T call Serial.begin() here - already initialized in main setup()!
  // Calling Serial.begin() again after NimBLE init corrupts USB CDC on ESP32-S3
  // Serial.begin(115200);  ← REMOVED - causes "Device not configured" error
  // delay(100);

  // WiFi setup banner (using LOG_INFO for important startup messages)
  LOG_INFO(TAG, "");
  LOG_INFO(TAG, "=============================================================================");
  LOG_INFO(TAG, "  Gravimetric Shots - WIRELESS DEBUG MODE");
  LOG_INFO(TAG, "=============================================================================");
  LOG_INFO(TAG, "");

  // Enable BLE/WiFi coexistence BEFORE initializing WiFi
  // This is critical on ESP32-S3 where BLE and WiFi share the same radio
  LOG_DEBUG(TAG, "Enabling BLE/WiFi coexistence (balanced mode)");
  esp_coex_preference_set(ESP_COEX_PREFER_BALANCE);

  // Connect to WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  // WiFi connection attempt
  LOG_INFO(TAG, "Connecting to: %s", WIFI_SSID);
  LOG_DEBUG(TAG, "Status: connecting...");

  // Print dots without newline (special case - keep mutex-protected)
  if (serialMutex && xSemaphoreTake(serialMutex, pdMS_TO_TICKS(2000))) {
    Serial.print("         ");  // Indent for visual alignment
    xSemaphoreGive(serialMutex);
  }

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    if (serialMutex && xSemaphoreTake(serialMutex, pdMS_TO_TICKS(2000))) {
      Serial.print(".");
      xSemaphoreGive(serialMutex);
    }
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    // WiFi connection successful
    LOG_INFO(TAG, "Connected!");
    LOG_INFO(TAG, "");
    LOG_INFO(TAG, "Connection successful:");
    LOG_INFO(TAG, "  IP Address:  %s", WiFi.localIP().toString().c_str());
    LOG_DEBUG(TAG, "  MAC Address: %s", WiFi.macAddress().c_str());
    LOG_DEBUG(TAG, "  RSSI:        %d dBm", WiFi.RSSI());
    LOG_INFO(TAG, "");
    LOG_INFO(TAG, "WebSerial Access URL:");
    LOG_INFO(TAG, "  http://%s/webserial", WiFi.localIP().toString().c_str());
    LOG_INFO(TAG, "");

    // CRITICAL: Enable WiFi modem sleep for BLE coexistence
    // ESP32-S3 REQUIRES modem sleep when both WiFi and BLE are active
    // This allows time-division multiplexing of the shared radio
    WiFi.setSleep(WIFI_PS_MIN_MODEM);
    LOG_DEBUG(TAG, "Modem sleep enabled (required for BLE coexistence)");

    // Initialize WebSerial
    WebSerial.begin(&debugServer);
    WebSerial.onMessage(webSerialCallback);

    // Mark WebSerial as ready for logging
    webSerialReady = true;

    // Start HTTP server
    debugServer.begin();

    // Final banner
    LOG_INFO(TAG, "WebSerial server started on port 80");
    LOG_INFO(TAG, "");
    LOG_INFO(TAG, "=============================================================================");
    LOG_INFO(TAG, "  Ready for wireless monitoring!");
    LOG_INFO(TAG, "  Open the URL above in any browser (phone, tablet, laptop)");
    LOG_INFO(TAG, "=============================================================================");
    LOG_INFO(TAG, "");
  }
  else {
    // WiFi connection failed
    LOG_ERROR(TAG, "FAILED");
    LOG_ERROR(TAG, "");
    LOG_ERROR(TAG, "Connection failed after 10 seconds");
    LOG_WARN(TAG, "Continuing without wireless debug");
    LOG_INFO(TAG, "Check credentials in wifi_credentials.h:");
    LOG_INFO(TAG, "  SSID: %s", WIFI_SSID);
    LOG_INFO(TAG, "  PASS: ********");
    LOG_INFO(TAG, "");
    LOG_INFO(TAG, "=============================================================================");
    LOG_INFO(TAG, "  USB Serial monitoring only (WiFi unavailable)");
    LOG_INFO(TAG, "=============================================================================");
    LOG_INFO(TAG, "");
  }
}

#endif // WIRELESS_DEBUG
