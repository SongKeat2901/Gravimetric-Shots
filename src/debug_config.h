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
//
// Thread Safety:
//   All DEBUG_PRINT macros use serialMutex to prevent fragmented output
//   when multiple FreeRTOS tasks print simultaneously (Core 0 + Core 1)
// =============================================================================

#include <Arduino.h>          // For millis(), Serial
#include <string.h>           // For strlen()
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// WebSerial includes (needed for log_write function when WIRELESS_DEBUG enabled)
#ifdef WIRELESS_DEBUG
  #include <WiFi.h>
  #include <WebSerial.h>
  #include <ESPAsyncWebServer.h>
#endif

// External serial mutex (defined in main .ino file)
extern SemaphoreHandle_t serialMutex;

#ifdef WIRELESS_DEBUG
// WebSerial ready flag (set to true after WebSerial.begin() is called)
extern bool webSerialReady;
#else
// Stub for non-debug builds (not used, but prevents compilation errors)
static constexpr bool webSerialReady = false;
#endif

// =============================================================================
// Professional Logging System (ESP-IDF Style with ANSI Colors)
// =============================================================================
// Industry-standard tag-based logging inspired by ESP-IDF ESP_LOGx API
//
// Features:
//   - Hierarchical log levels (ERROR/WARN/INFO/DEBUG/VERBOSE)
//   - ANSI color coding (red errors, yellow warnings, etc.)
//   - Tag-based filtering (like Log4j/spdlog loggers)
//   - Millisecond timestamps
//   - Thread-safe (FreeRTOS mutex)
//   - Printf-style formatting
//
// Usage:
//   static const char* TAG = "BLE";
//   LOG_ERROR(TAG, "Connection failed: %s", error);   // Red
//   LOG_WARN(TAG, "Timeout after %d ms", timeout);    // Yellow
//   LOG_INFO(TAG, "Connected to %s", device);         // Green
//   LOG_DEBUG(TAG, "Packet size: %d bytes", size);    // Cyan
//   LOG_VERBOSE(TAG, "Heartbeat sent");               // Gray
//
// Configuration:
//   #define LOG_LOCAL_LEVEL LOG_INFO   // Set global verbosity
// =============================================================================

// Log levels (hierarchical)
typedef enum {
    LOG_NONE    = 0,  // No logging
    LOG_ERROR   = 1,  // Critical errors only
    LOG_WARN    = 2,  // Errors + warnings
    LOG_INFO    = 3,  // Errors + warnings + info
    LOG_DEBUG   = 4,  // Everything except verbose
    LOG_VERBOSE = 5   // Everything including trace
} log_level_t;

// =============================================================================
// Per-Tag Log Level Configuration (Fine-Grained Control)
// =============================================================================
// Set individual log levels for each subsystem tag
// This allows you to silence noisy subsystems while keeping others verbose
//
// Example: To debug LVGL issues, set LVGL to VERBOSE and BLE to ERROR
//
// Available tags: System, Task, BLE, Scale, UI, Relay, Weight
// =============================================================================

// Default global log level (fallback for tags not explicitly configured)
#ifndef LOG_LOCAL_LEVEL
#define LOG_LOCAL_LEVEL LOG_DEBUG  // DEBUG level only
#endif

// Per-tag overrides (comment out to use LOG_LOCAL_LEVEL default)
#define LOG_LEVEL_SYSTEM  LOG_DEBUG   // System startup/shutdown
#define LOG_LEVEL_TASK    LOG_DEBUG   // Task heartbeats
#define LOG_LEVEL_BLE     LOG_DEBUG   // BLE operations
#define LOG_LEVEL_SCALE   LOG_DEBUG   // Scale communication
#define LOG_LEVEL_UI      LOG_INFO    // UI/LVGL updates (INFO = hide touch I2C debug data)
#define LOG_LEVEL_RELAY   LOG_DEBUG   // Relay control
#define LOG_LEVEL_WEIGHT  LOG_DEBUG   // Weight updates
#define LOG_LEVEL_LCD_DMA LOG_INFO    // Display DMA operations (lcd_PushColors calls)
#define LOG_LEVEL_SHOT    LOG_VERBOSE // Shot weight data (enables real-time weight logging)

// ANSI color codes (prefixed with GS_ to avoid ESP-IDF conflicts)
#define GS_COLOR_BLACK   "30"
#define GS_COLOR_RED     "31"
#define GS_COLOR_GREEN   "32"
#define GS_COLOR_YELLOW  "33"
#define GS_COLOR_BLUE    "34"
#define GS_COLOR_MAGENTA "35"
#define GS_COLOR_CYAN    "36"
#define GS_COLOR_WHITE   "37"
#define GS_COLOR_GRAY    "90"

#define GS_LOG_COLOR(COLOR)  "\033[0;" COLOR "m"
#define GS_LOG_BOLD(COLOR)   "\033[1;" COLOR "m"
#define GS_LOG_RESET_COLOR   "\033[0m"

// Helper to get per-tag log level
inline log_level_t get_tag_log_level(const char* tag) {
    // Check tag-specific overrides (case-insensitive comparison)
#ifdef LOG_LEVEL_SYSTEM
    if (strcasecmp(tag, "System") == 0 || strcasecmp(tag, "SYS") == 0) return LOG_LEVEL_SYSTEM;
#endif
#ifdef LOG_LEVEL_TASK
    if (strcasecmp(tag, "Task") == 0) return LOG_LEVEL_TASK;
#endif
#ifdef LOG_LEVEL_BLE
    if (strcasecmp(tag, "BLE") == 0) return LOG_LEVEL_BLE;
#endif
#ifdef LOG_LEVEL_SCALE
    if (strcasecmp(tag, "Scale") == 0) return LOG_LEVEL_SCALE;
#endif
#ifdef LOG_LEVEL_UI
    if (strcasecmp(tag, "UI") == 0) return LOG_LEVEL_UI;
#endif
#ifdef LOG_LEVEL_RELAY
    if (strcasecmp(tag, "Relay") == 0) return LOG_LEVEL_RELAY;
#endif
#ifdef LOG_LEVEL_WEIGHT
    if (strcasecmp(tag, "Weight") == 0) return LOG_LEVEL_WEIGHT;
#endif
#ifdef LOG_LEVEL_LCD_DMA
    if (strcasecmp(tag, "LCD_DMA") == 0) return LOG_LEVEL_LCD_DMA;
#endif
#ifdef LOG_LEVEL_SHOT
    if (strcasecmp(tag, "Shot") == 0) return LOG_LEVEL_SHOT;
#endif

    // Default to global level for unknown tags
    return static_cast<log_level_t>(LOG_LOCAL_LEVEL);
}

// Core logging function
inline void log_write(log_level_t level, const char* tag, const char* format, ...)
    __attribute__((format(printf, 3, 4)));

inline void log_write(log_level_t level, const char* tag, const char* format, ...) {
    // Check if level is enabled for this specific tag
    log_level_t tag_level = get_tag_log_level(tag);
    if (level > tag_level) {
        return;  // Level too verbose for this tag, skip completely
    }

    // Try to take mutex (or continue without if not created yet for early boot messages)
    bool hasMutex = (serialMutex && xSemaphoreTake(serialMutex, pdMS_TO_TICKS(2000)));

    // ALWAYS print to Serial, even if mutex isn't available (for early boot debugging)
    {
        // Get timestamp
        uint32_t timestamp = millis();

        // Determine level letter and color
        const char* level_letter;
        const char* level_color;
        switch(level) {
            case LOG_ERROR:
                level_letter = "E";
                level_color = GS_LOG_COLOR(GS_COLOR_RED);
                break;
            case LOG_WARN:
                level_letter = "W";
                level_color = GS_LOG_COLOR(GS_COLOR_YELLOW);
                break;
            case LOG_INFO:
                level_letter = "I";
                level_color = GS_LOG_COLOR(GS_COLOR_GREEN);
                break;
            case LOG_DEBUG:
                level_letter = "D";
                level_color = GS_LOG_COLOR(GS_COLOR_CYAN);
                break;
            case LOG_VERBOSE:
                level_letter = "V";
                level_color = GS_LOG_COLOR(GS_COLOR_GRAY);
                break;
            default:
                level_letter = "?";
                level_color = "";
                break;
        }

        // Format the message first (before building complete line)
        va_list args;
        va_start(args, format);
        char msg_buffer[256];
        vsnprintf(msg_buffer, sizeof(msg_buffer), format, args);
        va_end(args);

        // Build COMPLETE log line in single buffer for atomic output
        // Format: COLOR LEVEL (timestamp) [tag]: RESET message\n
        // This prevents fragmentation when multiple tasks print simultaneously
        char line_buffer[384];
        snprintf(line_buffer, sizeof(line_buffer), "%s%s (%lu) [%s]:%s %s\n",
                 level_color,           // ANSI color code
                 level_letter,          // E/W/I/D/V
                 timestamp,             // Milliseconds since boot
                 tag,                   // Subsystem tag
                 GS_LOG_RESET_COLOR,    // ANSI reset
                 msg_buffer);           // Formatted message

        // SINGLE atomic write - prevents interleaving from other tasks
        // Mutex ensures only one task can write at a time
        Serial.print(line_buffer);
        // No flush - let USB CDC buffer naturally (prevents overflow)

#ifdef WIRELESS_DEBUG
        // Also send to WebSerial (without ANSI colors - browsers don't support them)
        // Only if WebSerial has been initialized
        if (webSerialReady) {
            char web_line[384];
            snprintf(web_line, sizeof(web_line), "%s (%lu) [%s]: %s\n",
                     level_letter, timestamp, tag, msg_buffer);
            WebSerial.print(web_line);
        }
#endif

        // Release mutex if we acquired it
        if (hasMutex) {
            xSemaphoreGive(serialMutex);
        }
    }
}

// Convenience macros (ESP-IDF style)
#define LOG_ERROR(tag, format, ...)   log_write(LOG_ERROR,   tag, format, ##__VA_ARGS__)
#define LOG_WARN(tag, format, ...)    log_write(LOG_WARN,    tag, format, ##__VA_ARGS__)
#define LOG_INFO(tag, format, ...)    log_write(LOG_INFO,    tag, format, ##__VA_ARGS__)
#define LOG_DEBUG(tag, format, ...)   log_write(LOG_DEBUG,   tag, format, ##__VA_ARGS__)
#define LOG_VERBOSE(tag, format, ...) log_write(LOG_VERBOSE, tag, format, ##__VA_ARGS__)

// Backward compatibility with old debugPrint() function
inline void debugPrint(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
inline void debugPrint(const char* fmt, ...) {
    // Route through LOG_INFO with generic "APP" tag
    va_list args;
    va_start(args, fmt);
    char buffer[256];
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    // Remove trailing newline if present (LOG_INFO adds it)
    size_t len = strlen(buffer);
    if (len > 0 && buffer[len-1] == '\n') {
        buffer[len-1] = '\0';
    }

    LOG_INFO("APP", "%s", buffer);
}

#ifdef WIRELESS_DEBUG
  #include "wifi_credentials.h"  // Contains WIFI_SSID and WIFI_PASS

  extern AsyncWebServer debugServer;

  // Initialize wireless debugging (call in setup())
  void setupWirelessDebug();

  // Legacy DEBUG_ macros (backward compatibility)
  // Route through LOG_INFO with generic "APP" tag
  #define DEBUG_INIT() setupWirelessDebug()

  #define DEBUG_PRINT(x) do { \
    char _tmp[256]; \
    snprintf(_tmp, sizeof(_tmp), "%s", String(x).c_str()); \
    LOG_INFO("APP", "%s", _tmp); \
  } while(0)

  #define DEBUG_PRINTLN(x) do { \
    char _tmp[256]; \
    snprintf(_tmp, sizeof(_tmp), "%s", String(x).c_str()); \
    LOG_INFO("APP", "%s", _tmp); \
  } while(0)

  #define DEBUG_PRINTF(...) LOG_INFO("APP", __VA_ARGS__)

#else
  // Production build - USB Serial only
  // Legacy DEBUG_ macros (backward compatibility)
  // Route through LOG_INFO with generic "APP" tag
  #define DEBUG_INIT() Serial.begin(115200)

  #define DEBUG_PRINT(x) do { \
    char _tmp[256]; \
    snprintf(_tmp, sizeof(_tmp), "%s", String(x).c_str()); \
    LOG_INFO("APP", "%s", _tmp); \
  } while(0)

  #define DEBUG_PRINTLN(x) do { \
    char _tmp[256]; \
    snprintf(_tmp, sizeof(_tmp), "%s", String(x).c_str()); \
    LOG_INFO("APP", "%s", _tmp); \
  } while(0)

  #define DEBUG_PRINTF(...) LOG_INFO("APP", __VA_ARGS__)
#endif

#endif // DEBUG_CONFIG_H
