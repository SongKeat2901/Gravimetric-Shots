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
  Serial.begin(115200);
  delay(100);  // Brief delay for Serial to stabilize

  Serial.println("\n");
  Serial.println("=============================================================================");
  Serial.println("  Gravimetric Shots - WIRELESS DEBUG MODE");
  Serial.println("=============================================================================");
  Serial.println();

  // Enable BLE/WiFi coexistence BEFORE initializing WiFi
  // This is critical on ESP32-S3 where BLE and WiFi share the same radio
  esp_coex_preference_set(ESP_COEX_PREFER_BALANCE);  // Balanced priority for BLE and WiFi

  // Connect to WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  Serial.print("[WiFi] Connecting to: ");
  Serial.println(WIFI_SSID);
  Serial.print("[WiFi] Status: ");

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(" Connected!");
    Serial.println();
    Serial.println("[WiFi] Connection successful:");
    Serial.print("  IP Address:  ");
    Serial.println(WiFi.localIP());
    Serial.print("  MAC Address: ");
    Serial.println(WiFi.macAddress());
    Serial.print("  RSSI:        ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
    Serial.println();
    Serial.println("[WebSerial] Access URL:");
    Serial.print("  http://");
    Serial.print(WiFi.localIP());
    Serial.println("/webserial");
    Serial.println();

    // Optimize WiFi for BLE coexistence
    // - Disable sleep for lower latency (BLE heartbeat timing is critical)
    // - Trade: Higher power consumption for better responsiveness
    WiFi.setSleep(false);
    Serial.println("[WiFi] Low-latency mode enabled (sleep disabled)");

    // Initialize WebSerial
    WebSerial.begin(&debugServer);
    WebSerial.onMessage(webSerialCallback);

    // Start HTTP server
    debugServer.begin();

    Serial.println("[WebSerial] Server started on port 80");
    Serial.println();
    Serial.println("=============================================================================");
    Serial.println("  Ready for wireless monitoring!");
    Serial.println("  Open the URL above in any browser (phone, tablet, laptop)");
    Serial.println("=============================================================================");
    Serial.println();
  }
  else {
    Serial.println(" FAILED");
    Serial.println();
    Serial.println("[WiFi] Connection failed after 10 seconds");
    Serial.println("[WiFi] Continuing without wireless debug");
    Serial.println("[WiFi] Check credentials in wifi_credentials.h:");
    Serial.print("  SSID: ");
    Serial.println(WIFI_SSID);
    Serial.println("  PASS: ********");
    Serial.println();
    Serial.println("=============================================================================");
    Serial.println("  USB Serial monitoring only (WiFi unavailable)");
    Serial.println("=============================================================================");
    Serial.println();
  }
}

#endif // WIRELESS_DEBUG
