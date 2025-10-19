/**
 * @file GravimetricShots.ino
 * @brief Gravimetric Shots - BLE-Connected Espresso Scale Controller
 *
 * @description
 * A gravimetric espresso shot controller that connects to Acaia scales via BLE,
 * monitors shot weight in real-time, and controls relay output for automated
 * shot stopping. Features LVGL touch UI on 180x640 display.
 *
 * @hardware LilyGO T-Display-S3-Long (ESP32-S3, 16MB Flash, 8MB PSRAM)
 * @author SongKeat
 * @date 2024-2025
 * @license MIT
 *
 * Features:
 * - BLE connection to Acaia scales (Lunar, Pearl, Pyxis)
 * - Real-time weight monitoring and shot timer
 * - Predictive shot ending with linear regression
 * - Relay control (GPIO 48) for solenoid valve
 * - Touch-based UI with weight target setting
 * - Shot history tracking and drip delay compensation
 * - Flush mode support
 * - Wireless debug monitoring via WebSerial (optional, debug builds only)
 */

#include "debug_config.h"  // Wireless debug configuration (must be first for macros)
#include "lvgl.h" /* https://github.com/lvgl/lvgl.git */
#include "AXS15231B.h"
#include <Arduino.h>
#include <Wire.h>
#include <ui.h>
#include <AcaiaArduinoBLE.h>  // ArduinoBLE-based scale connection
#include "esp_task_wdt.h"      // Task watchdog for auto-recovery from hangs
#include "nvs_flash.h"         // NVS initialization (needed for BLE storage)
#include <Preferences.h>
#include <cstring>

// Power Management for LED control
#define XPOWERS_CHIP_SY6970
#include <XPowersLib.h>
XPowersPPM PMU;

// Logging tags for different subsystems
static const char* TAG_SYS   = "System";  // System messages (setup, init, memory)
static const char* TAG_SCALE = "Scale";   // Scale connection and weight updates
static const char* TAG_SHOT  = "Shot";    // Shot brewing process
static const char* TAG_UI    = "UI";      // UI events (touch, display)
static const char* TAG_RELAY = "Relay";   // Relay control
static const char* TAG_TASK  = "Task";    // FreeRTOS task messages

// -----------------------------------------------------------------------------
// Display and Touch Configuration
// -----------------------------------------------------------------------------

static lv_disp_draw_buf_t draw_buf;
static lv_color_t *buf;
static lv_color_t *buf1;

// DIAGNOSTIC: Store initial buffer addresses to detect corruption
static lv_color_t *buf_initial = NULL;
static lv_color_t *buf1_initial = NULL;

constexpr int TOUCH_IICSCL = 10;
constexpr int TOUCH_IICSDA = 15;
constexpr int TOUCH_RES    = 16;

constexpr int AXS_TOUCH_ONE_POINT_LEN = 6;
constexpr int AXS_TOUCH_BUF_HEAD_LEN  = 2;

constexpr int AXS_TOUCH_GESTURE_POS = 0;
constexpr int AXS_TOUCH_POINT_NUM   = 1;
constexpr int AXS_TOUCH_EVENT_POS   = 2;
constexpr int AXS_TOUCH_X_H_POS     = 2;
constexpr int AXS_TOUCH_X_L_POS     = 3;
constexpr int AXS_TOUCH_ID_POS      = 4;
constexpr int AXS_TOUCH_Y_H_POS     = 4;
constexpr int AXS_TOUCH_Y_L_POS     = 5;
constexpr int AXS_TOUCH_WEIGHT_POS  = 6;
constexpr int AXS_TOUCH_AREA_POS    = 7;

#define AXS_GET_POINT_NUM(buf) buf[AXS_TOUCH_POINT_NUM]
#define AXS_GET_GESTURE_TYPE(buf) buf[AXS_TOUCH_GESTURE_POS]
#define AXS_GET_POINT_X(buf, point_index) (((uint16_t)(buf[AXS_TOUCH_ONE_POINT_LEN * point_index + AXS_TOUCH_X_H_POS] & 0x0F) << 8) + (uint16_t)buf[AXS_TOUCH_ONE_POINT_LEN * point_index + AXS_TOUCH_X_L_POS])
#define AXS_GET_POINT_Y(buf, point_index) (((uint16_t)(buf[AXS_TOUCH_ONE_POINT_LEN * point_index + AXS_TOUCH_Y_H_POS] & 0x0F) << 8) + (uint16_t)buf[AXS_TOUCH_ONE_POINT_LEN * point_index + AXS_TOUCH_Y_L_POS])
#define AXS_GET_POINT_EVENT(buf, point_index) (buf[AXS_TOUCH_ONE_POINT_LEN * point_index + AXS_TOUCH_EVENT_POS] >> 6)

// -----------------------------------------------------------------------------
// Brew & Application Configuration
// -----------------------------------------------------------------------------

constexpr int MAX_OFFSET          = 5;
constexpr int MIN_SHOT_DURATION_S = 5;
constexpr int MAX_SHOT_DURATION_S = 50;
constexpr int DRIP_DELAY_S        = 3;
constexpr int N                   = 10; // Samples used for trend line

constexpr int RELAY1           = 48;
constexpr int SHOT_HISTORY_CAP = 2000;  // Increased from 1000 to reduce buffer wrapping frequency

// -----------------------------------------------------------------------------
// Global State
// -----------------------------------------------------------------------------

AcaiaArduinoBLE scale;
float currentWeight       = 0.0f;
uint8_t goalWeight        = 0;
float weightOffset        = 0.0f;
bool firstBoot            = true;
int brightness            = 0;
float previousTimerValue  = 0.0f;

// Timer update tracking for independent 0.1s updates
unsigned long lastTimerUpdate = 0;
const unsigned long TIMER_UPDATE_INTERVAL_MS = 100;  // 100ms = 0.1 second

// Display sleep tracking and auto-wake
bool displayAsleep = false;
unsigned long lastTouchTime = 0;
unsigned long lastWakeTime = 0;  // Timestamp when display woke up (to prevent phantom touches)
const unsigned long DISPLAY_SLEEP_TIMEOUT_MS = 300000;  // 5 minutes idle = sleep
const unsigned long WAKE_GUARD_MS = 1000;  // Ignore touches for 1 second after wake
const unsigned long TOUCH_CONTROLLER_RECOVERY_MS = 500;  // Wait 500ms for touch controller to power up after wake

// Watchdog debugging - track reset counts and timing
static unsigned long mainLoopWDTResets = 0;
static unsigned long bleTaskWDTResets = 0;
static unsigned long lastMainWDTLog = 0;
static unsigned long lastBLEWDTLog = 0;
const unsigned long WDT_LOG_INTERVAL_MS = 5000;  // Log watchdog stats every 5 seconds

// LVGL initialization tracking (prevent crashes from calling lv_timer_handler before init)
static bool lvglInitialized = false;

// Thermal noise detection for touch controller
static uint32_t edgeTouchCount = 0;
static uint32_t lastEdgeTouchTime = 0;
static bool thermalSuppressionActive = false;
static uint32_t thermalSuppressionStart = 0;
static const uint32_t EDGE_TOUCH_WINDOW_MS = 5000;  // 5-second window for counting edge touches
static const uint32_t EDGE_TOUCH_THRESHOLD = 10;     // >10 edge touches = thermal noise
static const uint32_t THERMAL_SUPPRESSION_DURATION_MS = 10000;  // Suppress for 10 seconds

// -----------------------------------------------------------------------------
// FreeRTOS Task Architecture - Professional Separation of BLE and UI
// -----------------------------------------------------------------------------

// BLE Task Handle
TaskHandle_t bleTaskHandle = NULL;

// Shared Data Structure - Protected by Mutex
struct BLESharedData {
    // Connection status
    bool isConnected;
    bool isConnecting;
    char statusMessage[64];

    // Weight data
    float currentWeight;
    float lastStableWeight;
    unsigned long lastWeightUpdate;

    // Scale info
    int batteryLevel;
    char scaleModel[32];

    // Shot state
    bool isBrewing;
    unsigned long brewStartTime;

    // Statistics
    unsigned long connectionUptime;
    unsigned long totalPacketsReceived;
    unsigned long lastPacketTime;
};

BLESharedData bleData = {
    false,  // isConnected
    false,  // isConnecting
    "Not Connected",  // statusMessage
    0.0f,   // currentWeight
    0.0f,   // lastStableWeight
    0,      // lastWeightUpdate
    0,      // batteryLevel
    "",     // scaleModel
    false,  // isBrewing
    0,      // brewStartTime
    0,      // connectionUptime
    0,      // totalPacketsReceived
    0       // lastPacketTime
};

SemaphoreHandle_t bleDataMutex = NULL;

// Serial Print Mutex - Protect Serial.print() from thread collisions
SemaphoreHandle_t serialMutex = NULL;

// Command Queue - Main Loop to BLE Task
enum BLECommand {
    BLE_CMD_TARE,
    BLE_CMD_START_TIMER,
    BLE_CMD_STOP_TIMER,
    BLE_CMD_RESET_TIMER,
    BLE_CMD_DISCONNECT,
    BLE_CMD_FORCE_RECONNECT
};

struct BLECommandMessage {
    BLECommand command;
    uint32_t param;  // Optional parameter
};

QueueHandle_t bleCommandQueue = NULL;

// UI Update Queue - BLE Task to Main Loop (Professional Thread-Safe Pattern)
// Follows same pattern as bleCommandQueue for consistency
enum UIUpdateType {
    UI_UPDATE_WEIGHT,
    UI_UPDATE_TIMER,
    UI_UPDATE_STATUS,
    UI_UPDATE_CONNECTION
};

struct UIUpdateMessage {
    UIUpdateType type;
    char text[48];      // Large enough for any label text
    bool boolValue;     // For connection status updates
};

QueueHandle_t uiUpdateQueue = NULL;

// BLE command sequencer state machine (non-blocking)
enum BLESequenceState {
  BLE_IDLE,
  BLE_SEND_RESET,
  BLE_WAIT_AFTER_RESET,
  BLE_SEND_TARE,
  BLE_WAIT_AFTER_TARE,
  BLE_SEND_START,
  BLE_START_SHOT
};

BLESequenceState bleSequenceState = BLE_IDLE;
unsigned long bleSequenceTimestamp = 0;
const unsigned long BLE_COMMAND_DELAY_MS = 100;
bool bleSequenceInProgress = false;

// Battery request now handled during scale init (in AcaiaArduinoBLE library)

// Shot end reason tracking for debugging and user feedback
enum ShotEndReason {
  WEIGHT_ACHIEVED,    // Target weight reached
  TIME_EXCEEDED,      // Maximum shot duration exceeded
  BUTTON_PRESSED,     // User manually stopped shot
  SCALE_DISCONNECTED, // Lost connection to scale
  USER_STOPPED,       // Generic user stop
  UNDEFINED           // Not yet determined
};

struct Shot
{
  float start_timestamp_s = 0.0f;
  float shotTimer         = 0.0f;
  float end_s             = 0.0f;
  float expected_end_s    = 0.0f;
  float weight[SHOT_HISTORY_CAP] = {};
  float time_s[SHOT_HISTORY_CAP] = {};
  int   datapoints = 0;
  bool  brewing    = false;
  ShotEndReason endReason = UNDEFINED;
};

Shot shot;

// NimBLE Server objects (for advertising weight to external devices - optional)
// DISABLED: Removed NimBLE server functionality (reverted to ArduinoBLE for stability)
// NimBLEServer* pServer = nullptr;
// NimBLEService* pWeightService = nullptr;
// NimBLECharacteristic* pWeightCharacteristic = nullptr;

bool firstConnectionNotificationPending = true;
bool BatteryLow                         = false;

// Flushing sequence state
unsigned long startTimeFlushing     = 0;
unsigned long lastPrintTimeFlushing = 0;
const unsigned long flushDuration   = 5000; // ms
bool isFlushing                     = false;

constexpr uint32_t HUMAN_TOUCH_MIN_MS = 100; // Debounce time - prevents accidental double-clicks
constexpr uint32_t FLUSH_BUTTON_MIN_PRESS_MS = 300; // Flush requires longer press to prevent thermal phantom triggers

Preferences preferences;
const char *WEIGHT_KEY     = "weight";
const char *OFFSET_KEY     = "offset";
const char *BRIGHTNESS_KEY = "brightness";

// DEBUG_PRINT and DEBUG_PRINTLN are now defined in debug_config.h
// - Production builds: Output to Serial only
// - Debug builds (-DWIRELESS_DEBUG): Output to BOTH Serial AND WebSerial

bool lastScaleConnected      = false;
bool hasEverConnectedToScale = false;
bool hasShownNoScaleMessage  = false;
// CRITICAL: Must be >= BLE scan timeout (1s ArduinoBLE) + margin to prevent overlapping scans
constexpr uint32_t SCALE_INIT_RETRY_MS = 2000;  // 1s scan timeout + 1s cleanup margin
constexpr uint32_t FLUSH_STATUS_HOLD_MS = 2000;
uint32_t lastScaleInitAttempt          = 0;
char pendingScaleStatus[64] = {0};  // Fixed buffer - eliminates heap fragmentation
bool hasPendingScaleStatus = false;
char currentStatusText[64] = {0};   // Fixed buffer - eliminates heap fragmentation
bool flushMessageActive    = false;
uint32_t flushMessageHoldUntil = 0;
bool relayState = false;

static inline void setRelayState(bool high)
{
  int desired = high ? HIGH : LOW;
  if (relayState == high && digitalRead(RELAY1) == desired)
    return;
  relayState = high;
  digitalWrite(RELAY1, desired);
  LOG_DEBUG(TAG_RELAY, "Relay1 -> %s", high ? "HIGH" : "LOW");
}

lv_obj_t *ui_cartext = nullptr;

// -----------------------------------------------------------------------------
// Helper Utilities
// -----------------------------------------------------------------------------

// UI Update Queue Functions (Professional Thread-Safe Pattern)
// BLE task (Core 0) enqueues UI updates, Main loop (Core 1) dequeues and updates LVGL

/**
 * @brief Enqueue a UI update message (thread-safe, non-blocking)
 * @param type Type of UI update (weight, timer, status, connection)
 * @param text Text to display (for weight, timer, status updates)
 * @note Called from BLE task (Core 0) - never touches LVGL directly!
 */
static inline void queueUIUpdate(UIUpdateType type, const char* text) {
    UIUpdateMessage msg;
    msg.type = type;
    msg.boolValue = false;

    if (text) {
        strncpy(msg.text, text, sizeof(msg.text) - 1);
        msg.text[sizeof(msg.text) - 1] = '\0';  // Ensure null termination
    } else {
        msg.text[0] = '\0';
    }

    // Non-blocking send - if queue is full, drop the message (UI updates are non-critical)
    xQueueSend(uiUpdateQueue, &msg, 0);
}

/**
 * @brief Enqueue a connection status update (thread-safe, non-blocking)
 * @param connected true if scale is connected, false otherwise
 * @note Called from BLE task (Core 0) - never touches LVGL directly!
 */
static inline void queueConnectionUpdate(bool connected) {
    UIUpdateMessage msg;
    msg.type = UI_UPDATE_CONNECTION;
    msg.boolValue = connected;
    msg.text[0] = '\0';

    xQueueSend(uiUpdateQueue, &msg, 0);
}

/**
 * @brief Process pending UI updates from the queue (LVGL-safe, Core 1 only)
 * @note Called from main loop (Core 1) - ONLY place that updates LVGL!
 * @note CRITICAL: Limits processing to prevent watchdog timeout
 */
static void processUIUpdates() {
    UIUpdateMessage msg;

    // CRITICAL FIX: Limit messages processed per loop iteration to prevent watchdog timeout
    // At 10Hz weight updates, we get ~10 messages/sec. Main loop runs at ~100Hz (10ms).
    // Process max 5 messages per iteration = 50ms worst case (well under 10s watchdog)
    const int MAX_MESSAGES_PER_ITERATION = 5;
    int processedCount = 0;

    // Process pending messages (non-blocking, with limit)
    while (processedCount < MAX_MESSAGES_PER_ITERATION &&
           xQueueReceive(uiUpdateQueue, &msg, 0) == pdTRUE) {
        switch (msg.type) {
            case UI_UPDATE_WEIGHT:
                if (ui_ScaleLabel) {
                    lv_label_set_text(ui_ScaleLabel, msg.text);
                }
                break;

            case UI_UPDATE_TIMER:
                if (ui_TimerLabel) {
                    lv_label_set_text(ui_TimerLabel, msg.text);
                }
                break;

            case UI_UPDATE_STATUS:
                if (ui_SerialLabel && ui_SerialLabel1) {
                    lv_label_set_text(ui_SerialLabel, msg.text);
                    lv_label_set_text(ui_SerialLabel1, msg.text);
                }
                // Also update local status text for consistency
                strncpy(currentStatusText, msg.text, sizeof(currentStatusText) - 1);
                currentStatusText[sizeof(currentStatusText) - 1] = '\0';
                break;

            case UI_UPDATE_CONNECTION:
                if (ui_BluetoothImage1 && ui_BluetoothImage2) {
                    if (msg.boolValue) {  // Connected
                        _ui_state_modify(ui_BluetoothImage1, LV_STATE_DISABLED, _UI_MODIFY_STATE_REMOVE);
                        _ui_state_modify(ui_BluetoothImage2, LV_STATE_DISABLED, _UI_MODIFY_STATE_REMOVE);
                    } else {  // Disconnected
                        _ui_state_modify(ui_BluetoothImage1, LV_STATE_DISABLED, _UI_MODIFY_STATE_ADD);
                        _ui_state_modify(ui_BluetoothImage2, LV_STATE_DISABLED, _UI_MODIFY_STATE_ADD);
                    }
                }
                break;
        }
        processedCount++;
    }
}

static inline void setStatusLabels(const char *text)
{
  if (strcmp(currentStatusText, text) == 0)  // Fixed: proper string comparison
    return;

  // PROFESSIONAL FIX: Use message queue for thread-safe UI updates
  // BLE task (Core 0) enqueues message, main loop (Core 1) dequeues and updates LVGL
  // This is the industry-standard pattern for LVGL multi-threading
  queueUIUpdate(UI_UPDATE_STATUS, text);

  // Keep local copy for comparison (prevents duplicate queue messages)
  strncpy(currentStatusText, text, sizeof(currentStatusText) - 1);
  currentStatusText[sizeof(currentStatusText) - 1] = '\0';
}

static inline void queueScaleStatus(const char *text)
{
  if (isFlushing)
  {
    strncpy(pendingScaleStatus, text, sizeof(pendingScaleStatus) - 1);  // Fixed: safe string copy
    pendingScaleStatus[sizeof(pendingScaleStatus) - 1] = '\0';  // Ensure null termination
    hasPendingScaleStatus = true;
  }
  else
  {
    setStatusLabels(text);
    hasPendingScaleStatus = false;
  }
}

static float seconds_f()
{
  return millis() / 1000.0f;
}

/**
 * @brief Update display refresh rate based on activity level
 * @param active_shot true during espresso shots, false when idle
 *
 * Dynamically adjusts LVGL refresh timer to optimize performance and power:
 * - Fast mode (60Hz = 16ms): During shots for smooth weight updates
 * - Slow mode (30Hz = 33ms): When idle for power savings
 */
static void updateDisplayRefreshRate(bool active_shot)
{
  lv_disp_t *display = lv_disp_get_default();
  lv_timer_t *refresh_timer = _lv_disp_get_refr_timer(display);

  if (active_shot)
  {
    // Fast refresh during shot (16ms = 60Hz) for smooth weight updates
    lv_timer_set_period(refresh_timer, 16);
    LOG_DEBUG(TAG_UI, "Display: 60Hz (fast mode)");
  }
  else
  {
    // Normal refresh when idle (33ms = 30Hz) to save CPU/power
    lv_timer_set_period(refresh_timer, 33);
    LOG_DEBUG(TAG_UI, "Display: 30Hz (normal mode)");
  }
}

/**
 * @brief Update shot timer display independently every 0.1 second
 *
 * This function runs on its own millis-based schedule, independent of scale
 * BLE notifications. Ensures smooth timer progression even when scale sends
 * infrequent weight updates.
 */
static void updateShotTimer()
{
  if (!shot.brewing)
    return;

  unsigned long now = millis();

  // Update timer display every 100ms (0.1 second)
  if (now - lastTimerUpdate >= TIMER_UPDATE_INTERVAL_MS)
  {
    shot.shotTimer = seconds_f() - shot.start_timestamp_s;

    // PROFESSIONAL FIX: Use message queue for thread-safe UI updates
    // BLE task (Core 0) enqueues timer text, main loop (Core 1) updates LVGL
    char buffer[10];
    dtostrf(shot.shotTimer, 5, 1, buffer);
    queueUIUpdate(UI_UPDATE_TIMER, buffer);

    lastTimerUpdate = now;
  }
}

/**
 * @brief Non-blocking BLE command sequencer for shot start
 *
 * Sequence: resetTimer → [100ms] → tare → [100ms] → startTimer → pump ON
 * Runs in loop() without blocking. Uses millis-based timing like handleFlushingCycle().
 */
static void handleBLESequence()
{
  unsigned long now = millis();

  switch (bleSequenceState)
  {
    case BLE_IDLE:
      // Nothing to do - waiting for shot start trigger
      break;

    case BLE_SEND_RESET:
      LOG_DEBUG(TAG_SHOT, "BLE: Sending RESET");
      if (!scale.resetTimer())
      {
        queueScaleStatus("Scale reset failed");
        bleSequenceState = BLE_IDLE;
        bleSequenceInProgress = false;
        shot.brewing = false;
        // scale.setIsBrewing(false);  // ArduinoBLE doesn't have this method
        isFlushing = false;
        return;
      }
      bleSequenceTimestamp = now;
      bleSequenceState = BLE_WAIT_AFTER_RESET;
      break;

    case BLE_WAIT_AFTER_RESET:
      // Non-blocking wait for 100ms
      if (now - bleSequenceTimestamp >= BLE_COMMAND_DELAY_MS)
      {
        bleSequenceState = BLE_SEND_TARE;
      }
      break;

    case BLE_SEND_TARE:
      LOG_DEBUG(TAG_SHOT, "BLE: Sending TARE");
      if (!scale.tare())
      {
        queueScaleStatus("Scale tare failed");
        if (scale.isConnected())
        {
          scale.stopTimer();
        }
        setRelayState(false);
        bleSequenceState = BLE_IDLE;
        bleSequenceInProgress = false;
        shot.brewing = false;
        // scale.setIsBrewing(false);  // ArduinoBLE doesn't have this method
        isFlushing = false;
        return;
      }
      bleSequenceTimestamp = now;
      bleSequenceState = BLE_WAIT_AFTER_TARE;
      break;

    case BLE_WAIT_AFTER_TARE:
      // Non-blocking wait for 100ms
      if (now - bleSequenceTimestamp >= BLE_COMMAND_DELAY_MS)
      {
        bleSequenceState = BLE_SEND_START;
      }
      break;

    case BLE_SEND_START:
      LOG_DEBUG(TAG_SHOT, "BLE: Sending START");
      if (!scale.startTimer())
      {
        queueScaleStatus("Scale timer failed");
        bleSequenceState = BLE_IDLE;
        bleSequenceInProgress = false;
        shot.brewing = false;
        // scale.setIsBrewing(false);  // ArduinoBLE doesn't have this method
        isFlushing = false;
        return;
      }
      bleSequenceState = BLE_START_SHOT;
      break;

    case BLE_START_SHOT:
      // Timer is now running - capture timestamp and turn on pump
      shot.start_timestamp_s = seconds_f();
      shot.shotTimer = 0.0f;
      shot.datapoints = 0;
      lastTimerUpdate = millis();

      // NOW set shot.brewing to true after all BLE commands succeeded
      shot.brewing = true;
      bleSequenceInProgress = false;

      LOG_INFO(TAG_SHOT, "BLE: Starting shot - turning ON pump");
      setRelayState(true);              // Turn ON pump (relay)
      updateDisplayRefreshRate(true);   // 60Hz display mode

      // Enable weight logging during shots
      // scale.setIsBrewing(true);  // ArduinoBLE doesn't have this method

      bleSequenceState = BLE_IDLE;
      LOG_INFO(TAG_SHOT, "Shot started successfully!");
      break;
  }
}

static void setBrewingState(bool brewing)
{
  if (brewing)
  {
    if (isFlushing)
    {
      LOG_INFO(TAG_SHOT, "Flushing cancelled due to brew start");
      setRelayState(false);
      isFlushing = false;
    }

    if (!scale.isConnected())
    {
      queueScaleStatus("Scale not connected");
      shot.brewing = false;
      // scale.setIsBrewing(false);  // ArduinoBLE doesn't have this method
      isFlushing   = false;
      return;
    }

    LOG_INFO(TAG_SHOT, "Shot start requested - triggering BLE sequence");

    // Trigger non-blocking BLE command sequence
    // Sequence: reset → tare → start → pump ON (handled by handleBLESequence)
    bleSequenceInProgress = true;
    bleSequenceState = BLE_SEND_RESET;
    // Note: shot.brewing will be set to true in BLE_START_SHOT state after commands complete
  }
  else
  {
    LOG_INFO(TAG_SHOT, "ShotEnded");
    shot.end_s = seconds_f() - shot.start_timestamp_s;
    if (scale.isConnected())
    {
      scale.stopTimer();
    }
    setRelayState(false);
    updateDisplayRefreshRate(false);  // Switch back to normal refresh (30Hz) when idle
  }
}

static void stopBrew(bool setDefaultStatus, ShotEndReason reason = USER_STOPPED)
{
  bool wasBrewing = shot.brewing;

  shot.brewing = false;
  isFlushing   = false;
  shot.endReason = reason;
  lastTimerUpdate = 0;  // Reset timer update tracking

  setBrewingState(false);
  updateDisplayRefreshRate(false);  // Switch back to normal refresh (30Hz) when stopped

  // Disable weight logging (return to silent idle mode)
  // scale.setIsBrewing(false);  // ArduinoBLE doesn't have this method

  if (setDefaultStatus && wasBrewing)
  {
    // Display appropriate message based on end reason
    switch(reason) {
      case WEIGHT_ACHIEVED:
        setStatusLabels("Shot ended - Weight achieved");
        break;
      case TIME_EXCEEDED:
        setStatusLabels("Shot ended - Max time");
        break;
      case BUTTON_PRESSED:
        setStatusLabels("Shot ended - Button pressed");
        break;
      case SCALE_DISCONNECTED:
        setStatusLabels("Shot ended - Scale disconnected");
        break;
      case USER_STOPPED:
      case UNDEFINED:
      default:
        setStatusLabels("Shot ended");
        break;
    }
  }
}

static void startBrew()
{
  if (shot.brewing)
  {
    return;
  }

  isFlushing = false;

  // CRITICAL FIX: Reset shotTimer BEFORE setting brewing=true
  // Otherwise old timer value from previous shot triggers "Max brew duration" immediately
  // during the BLE command sequence (reset→tare→start takes ~300ms)
  shot.shotTimer = 0.0f;
  shot.datapoints = 0;

  shot.brewing = true;
  setBrewingState(true);
}

static void calculateEndTime(Shot *s)
{
  if ((s->datapoints < N) || (s->weight[s->datapoints - 1] < 10))
  {
    s->expected_end_s = MAX_SHOT_DURATION_S;
    return;
  }

  float sumXY = 0.0f;
  float sumX  = 0.0f;
  float sumY  = 0.0f;
  float sumSquaredX = 0.0f;

  for (int i = s->datapoints - N; i < s->datapoints; i++)
  {
    sumXY += s->time_s[i] * s->weight[i];
    sumX  += s->time_s[i];
    sumY  += s->weight[i];
    sumSquaredX += s->time_s[i] * s->time_s[i];
  }

  float m = (N * sumXY - sumX * sumY) / (N * sumSquaredX - (sumX * sumX));
  float meanX = sumX / N;
  float meanY = sumY / N;
  float b = meanY - m * meanX;

  // Handle negative or near-zero slope - prevents infinite/absurd predictions
  if (m < 0.001f)
  {
    s->expected_end_s = MAX_SHOT_DURATION_S;
    return;
  }

  // Calculate expected end time
  float expected = (goalWeight - weightOffset - b) / m;

  // Clamp to reasonable bounds (prevents UI showing nonsensical predictions)
  if (expected < MIN_SHOT_DURATION_S) expected = MIN_SHOT_DURATION_S;
  if (expected > MAX_SHOT_DURATION_S) expected = MAX_SHOT_DURATION_S;

  s->expected_end_s = expected;
}

static void enforceRelayState()
{
  bool shouldBeHigh = shot.brewing || isFlushing;
  int  actualState   = digitalRead(RELAY1);

  if (shouldBeHigh && actualState == LOW)
  {
    LOG_WARN(TAG_RELAY, "Relay corrected to HIGH");
    setRelayState(true);
  }
  else if (!shouldBeHigh && actualState == HIGH)
  {
    LOG_WARN(TAG_RELAY, "Relay was HIGH unexpectedly, forcing LOW");
    setRelayState(false);
  }
}

// -----------------------------------------------------------------------------
// FreeRTOS Helper Functions - Safe Data Access
// -----------------------------------------------------------------------------

// Safe read from shared BLE data
float readCurrentWeight() {
    float weight = 0;
    if (xSemaphoreTake(bleDataMutex, pdMS_TO_TICKS(10))) {
        weight = bleData.currentWeight;
        xSemaphoreGive(bleDataMutex);
    }
    return weight;
}

bool readConnectionStatus() {
    bool connected = false;
    if (xSemaphoreTake(bleDataMutex, pdMS_TO_TICKS(10))) {
        connected = bleData.isConnected;
        xSemaphoreGive(bleDataMutex);
    }
    return connected;
}

bool readConnectingStatus() {
    bool connecting = false;
    if (xSemaphoreTake(bleDataMutex, pdMS_TO_TICKS(10))) {
        connecting = bleData.isConnecting;
        xSemaphoreGive(bleDataMutex);
    }
    return connecting;
}

// Safe write to shared BLE data
void updateSharedWeight(float weight) {
    if (xSemaphoreTake(bleDataMutex, pdMS_TO_TICKS(10))) {
        bleData.currentWeight = weight;
        bleData.lastWeightUpdate = millis();
        xSemaphoreGive(bleDataMutex);
    }
}

void updateSharedConnectionStatus(bool connected, bool connecting) {
    if (xSemaphoreTake(bleDataMutex, pdMS_TO_TICKS(10))) {
        bleData.isConnected = connected;
        bleData.isConnecting = connecting;
        xSemaphoreGive(bleDataMutex);
    }
}

void updateSharedStatusMessage(const char* message) {
    if (xSemaphoreTake(bleDataMutex, pdMS_TO_TICKS(10))) {
        strncpy(bleData.statusMessage, message, sizeof(bleData.statusMessage) - 1);
        bleData.statusMessage[sizeof(bleData.statusMessage) - 1] = '\0';
        xSemaphoreGive(bleDataMutex);
    }
}

// Send command to BLE task
bool sendBLECommand(BLECommand cmd, uint32_t param = 0) {
    BLECommandMessage msg = {cmd, param};
    return xQueueSend(bleCommandQueue, &msg, pdMS_TO_TICKS(100)) == pdTRUE;
}

// -----------------------------------------------------------------------------
// Display & Touch Callbacks
// -----------------------------------------------------------------------------

// Display flush callback monitoring - tracks flush frequency for diagnostics
// NOTE: Flush only happens when UI changes (not continuous during idle - this is normal!)
// Real freeze detection is done via lv_timer_handler() monitoring (see LVGLTimerHandlerRoutine)
static unsigned long flushCallCount = 0;
static unsigned long lastFlushTimestamp = 0;  // Track last flush time for UI health monitoring
unsigned long lastTouchEvent = 0;  // Track last touch event for UI health monitoring (non-static for extern access)

void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
{
  unsigned long now = millis();

  flushCallCount++;

  // Track last flush time for UI health monitoring
  lastFlushTimestamp = now;

  // ===== DIAGNOSTIC: PSRAM Buffer Integrity Check =====
  // Check every 100 flushes to avoid performance impact
  static uint32_t bufferIntegrityCheckCount = 0;
  if (flushCallCount % 100 == 0) {
    bufferIntegrityCheckCount++;

    // Verify LVGL draw buffer pointers haven't been corrupted
    if (disp != NULL && disp->draw_buf != NULL) {
      lv_color_t* current_buf1 = (lv_color_t*)disp->draw_buf->buf1;
      lv_color_t* current_buf2 = (lv_color_t*)disp->draw_buf->buf2;

      // Check if buffers moved (indicates memory corruption)
      if (buf_initial != NULL && current_buf1 != buf_initial) {
        LOG_ERROR(TAG_UI, "🚨 BUFFER CORRUPTION! buf1 moved: %p → %p",
                 buf_initial, current_buf1);
      }
      if (buf1_initial != NULL && current_buf2 != buf1_initial) {
        LOG_ERROR(TAG_UI, "🚨 BUFFER CORRUPTION! buf2 moved: %p → %p",
                 buf1_initial, current_buf2);
      }

      // Log buffer status periodically
      if (bufferIntegrityCheckCount % 10 == 0) {  // Every 1000 flushes
        LOG_DEBUG(TAG_UI, "💾 Buffer Check #%lu: buf1=%p buf2=%p (OK)",
                 bufferIntegrityCheckCount, current_buf1, current_buf2);
      }
    } else {
      LOG_ERROR(TAG_UI, "🚨 Display driver structure is NULL!");
    }

    // Verify color_p parameter isn't NULL
    if (color_p == NULL) {
      LOG_ERROR(TAG_UI, "🚨 color_p parameter is NULL in my_disp_flush!");
      lv_disp_flush_ready(disp);
      return;
    }
  }
  // ===== END BUFFER INTEGRITY CHECK =====

  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);

  // ===== DIAGNOSTIC: Pixel Data Validation =====
  // Log detailed flush information for first 5 flushes to diagnose blank display
  static uint8_t flushDetailCount = 0;
  if (flushDetailCount < 5) {
    LOG_INFO(TAG_UI, "🔬 FLUSH #%d: area=(%ld,%ld)-(%ld,%ld) size=%lux%lu pixels=%lu",
             flushDetailCount + 1,
             area->x1, area->y1, area->x2, area->y2,
             w, h, w * h);

    // Sample first few pixels to verify data isn't all zeros
    uint16_t* pixels = (uint16_t *)&color_p->full;
    LOG_INFO(TAG_UI, "   First pixels: [0]=%04X [1]=%04X [2]=%04X [3]=%04X",
             pixels[0], pixels[1], pixels[2], pixels[3]);

    // Check if buffer contains any non-zero pixels
    uint32_t totalPixels = w * h;
    uint32_t nonZeroCount = 0;
    for (uint32_t i = 0; i < totalPixels; i++) {
      if (pixels[i] != 0) nonZeroCount++;
    }
    LOG_INFO(TAG_UI, "   Non-zero pixels: %lu of %lu (%.1f%%)",
             nonZeroCount, totalPixels, (nonZeroCount * 100.0) / totalPixels);

    flushDetailCount++;
  }
  // ===== END DIAGNOSTIC =====

  // Log flush activity every 10 seconds for diagnostic purposes
  // NOTE: Flush rate varies with UI activity (0 Hz when idle is normal)
  static unsigned long lastFlushLog = 0;
  if (now - lastFlushLog > 10000) {
    LOG_DEBUG(TAG_UI, "🖼️  Display Flush: %lu calls in last 10s (~%lu Hz)",
              flushCallCount, flushCallCount / 10);
    flushCallCount = 0;
    lastFlushLog = now;
  }

#ifdef LCD_SPI_DMA
  char i = 0;
  while (get_lcd_spi_dma_write())
  {
    i = i >> 1;
    lcd_PushColors(0, 0, 0, 0, NULL);
  }
#endif
  lcd_PushColors(area->x1, area->y1, w, h, (uint16_t *)&color_p->full);

#ifdef LCD_SPI_DMA

#else
  lv_disp_flush_ready(disp);
#endif
}

uint8_t read_touchpad_cmd[11] = {0xb5, 0xab, 0xa5, 0x5a, 0x0, 0x0, 0x0, 0x8};
void my_touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data)
{
  // CRITICAL: Special handling when display is asleep
  // Touch controller (CUSTOM PROTOCOL @ 0x3B, NOT standard CST816) has hardware interrupt pin
  // that wakes us up. We need to detect the wake event and restore display, but NOT access I2C yet
  if (displayAsleep)
  {
    // Check if touch interrupt pin is active (indicates user touched screen)
    // Touch controller INT pin pulls low on touch event
    // For now, we'll wake on ANY call to this function during sleep
    // (LVGL still polls even during sleep)
    LOG_INFO(TAG_UI, "=== WAKE EVENT: Touch detected during sleep ===");
    LOG_INFO(TAG_UI, "Waking display and restoring backlight...");
    lcd_wake();

    // Note: Display controller and touch controller need time to stabilize
    // We don't use delay() here (blocks main loop → watchdog timeout)
    // Instead, the TOUCH_CONTROLLER_RECOVERY_MS check below handles this

    // Restore backlight to saved brightness level
    int LCDBrightness = map(brightness, 0, 100, 70, 256);
    analogWrite(TFT_BL, LCDBrightness);
    LOG_INFO(TAG_UI, "Display awake! Backlight=%d (brightness=%d%%)", LCDBrightness, brightness);

    displayAsleep = false;
    lastWakeTime = millis();  // Record wake time to guard against phantom touches
    lastTouchTime = millis(); // Update touch time to prevent immediate re-sleep

    // Return no-touch for this cycle (prevent phantom touch during wake)
    data->point.x = 0;
    data->point.y = 0;
    data->state = LV_INDEV_STATE_REL;  // Released (no touch)
    return;  // Skip I2C access this cycle - let controller stabilize
  }

  // CRITICAL: Skip I2C access during touch controller recovery period after wake
  // CST816 touch controller powers down with display and needs 500ms to recover
  if (lastWakeTime > 0 && (millis() - lastWakeTime) < TOUCH_CONTROLLER_RECOVERY_MS)
  {
    // Still in recovery period - don't access I2C yet
    data->point.x = 0;
    data->point.y = 0;
    data->state = LV_INDEV_STATE_REL;  // Released (no touch)
    return;  // Skip I2C access - prevents Error -1 spam during recovery
  }

  uint8_t buff[20] = {0};
  static bool haveLastPoint        = false;
  static lv_coord_t lastPointX     = 0;
  static lv_coord_t lastPointY     = 0;
  static constexpr uint8_t filterDepth = 2;
  static lv_coord_t historyX[filterDepth] = {0};
  static lv_coord_t historyY[filterDepth] = {0};
  static uint8_t historyCount      = 0;
  static uint8_t historyWriteIndex = 0;

  // DIAGNOSTIC: Log I2C touch transaction
  static uint32_t touchReadCount = 0;
  touchReadCount++;

  // CRITICAL: Match LilyGO lvgl_demo.ino I2C sequence (no error checking on Wire calls)
  // Touch controller I2C state machine gets confused by timing delays from error checks
  // BUT: Add timeout to prevent infinite blocking when touch returns [00 00 00...] data
  // See: https://github.com/Xinyuan-LilyGO/T-Display-S3-Long/blob/main/examples/lvgl_demo/lvgl_demo.ino#L70-L75
  Wire.beginTransmission(0x3B);
  Wire.write(read_touchpad_cmd, 8);
  Wire.endTransmission();
  Wire.requestFrom(0x3B, 8);

  // CRITICAL: Prevent infinite blocking when touch controller returns [00 00 00...]
  // Simple 50ms timeout - short enough to not confuse controller timing
  unsigned long timeout_start = millis();
  while (!Wire.available()) {
    if (millis() - timeout_start > 50) {
      break;  // Timeout - read whatever's available (might be zeros)
    }
  }

  Wire.readBytes(buff, 8);

  // ===== DIAGNOSTIC: Touch I2C Data Logging =====
  // Log raw I2C data from touch controller every 100 reads (to avoid spam)
  if (touchReadCount % 100 == 0) {
    LOG_DEBUG(TAG_UI, "Touch I2C #%lu: [%02X %02X %02X %02X %02X %02X %02X %02X]",
             touchReadCount, buff[0], buff[1], buff[2], buff[3], buff[4], buff[5], buff[6], buff[7]);
  }
  // NOTE: Periodic ChipID polling was REMOVED - it's not supported by this touch controller
  // and caused crashes (see LilyGO lvgl_demo.ino for reference implementation)
  // ===== END TOUCH I2C DATA LOGGING =====

  uint16_t rawX = AXS_GET_POINT_X(buff, 0);
  uint16_t rawY = AXS_GET_POINT_Y(buff, 0);
  uint16_t type = AXS_GET_GESTURE_TYPE(buff);

  // CRITICAL: Handle [00 00 00...] and [AF AF AF...] corrupted patterns
  // These patterns indicate touch controller errors and should be treated as "no touch"
  // [00 00 00...]: Clean but invalid state (may cause Wire.available() timeout)
  // [AF AF AF...]: Error state after touch interaction (0xAF = 0xA2 | 0x0D)
  bool isCorruptedPattern = (buff[0] == 0x00 && buff[1] == 0x00) ||
                            (buff[0] == 0xAF && buff[1] == 0xAF);

  if (isCorruptedPattern) {
    // Force touch release to prevent LVGL from getting stuck in pressed state
    data->point.x = 0;
    data->point.y = 0;
    data->state = LV_INDEV_STATE_REL;
    return;
  }

  // DIAGNOSTIC: Log parsed touch data when touch detected
  if (!type && (rawX || rawY)) {
    LOG_INFO(TAG_UI, "Touch RAW data #%lu: rawX=%d, rawY=%d, type=%d, buff0=0x%02X",
             touchReadCount, rawX, rawY, type, buff[0]);
  }

  if (!type && (rawX || rawY))
  {
    // ===== THERMAL NOISE PROTECTION =====
    // Track edge touches to detect thermal drift patterns
    uint32_t now = millis();

    // Check if touch is near left edge (flush button area - thermal sensitive zone)
    if (rawY < 50)  // Left edge in portrait mode (Y=0 is left edge)
    {
      // Reset counter if window expired
      if (now - lastEdgeTouchTime > EDGE_TOUCH_WINDOW_MS) {
        edgeTouchCount = 0;
      }

      edgeTouchCount++;
      lastEdgeTouchTime = now;

      // If too many edge touches in short time, activate thermal suppression
      if (edgeTouchCount > EDGE_TOUCH_THRESHOLD) {
        if (!thermalSuppressionActive) {
          thermalSuppressionActive = true;
          thermalSuppressionStart = now;
          LOG_WARN(TAG_UI, "THERMAL NOISE DETECTED - Suppressing left edge touches for 10s");
        }
        edgeTouchCount = 0;  // Reset counter
      }
    }

    // Check if thermal suppression is active
    if (thermalSuppressionActive)
    {
      // End suppression after timeout
      if (now - thermalSuppressionStart > THERMAL_SUPPRESSION_DURATION_MS) {
        thermalSuppressionActive = false;
        LOG_INFO(TAG_UI, "Thermal suppression ended");
      }
      // Suppress left edge touches during thermal event
      else if (rawY < 50)
      {
        LOG_VERBOSE(TAG_UI, "Left edge touch suppressed (thermal mode)");
        data->state = LV_INDEV_STATE_REL;
        return;
      }
    }
    // ===== END THERMAL PROTECTION =====

    // GUARD: Ignore touch events within 1 second after wake (prevents phantom touches from I2C glitches)
    // Wake logic now happens at the top of this function (line 860) before I2C access
    if (lastWakeTime > 0 && (millis() - lastWakeTime) < WAKE_GUARD_MS)
    {
      // Still in wake guard period - reject this touch
      data->point.x = 0;
      data->point.y = 0;
      data->state = LV_INDEV_STATE_REL;
      return;
    }

    // Update last touch time for sleep timeout tracking
    lastTouchTime = millis();


    int32_t rotatedX = static_cast<int32_t>(EXAMPLE_LCD_V_RES - 1) - static_cast<int32_t>(rawX);
    if (rotatedX < 0)
      rotatedX = 0;
    if (rotatedX >= EXAMPLE_LCD_V_RES)
      rotatedX = EXAMPLE_LCD_V_RES - 1;

    if (rawY >= EXAMPLE_LCD_H_RES)
      rawY = EXAMPLE_LCD_H_RES - 1;

    lv_coord_t finalX = static_cast<lv_coord_t>(rawY);
    lv_coord_t finalY = static_cast<lv_coord_t>(rotatedX);
    const lv_coord_t maxX = EXAMPLE_LCD_H_RES - 1;
    const lv_coord_t maxY = EXAMPLE_LCD_V_RES - 1;

    if (haveLastPoint)
    {
      lv_coord_t deltaX = finalX - lastPointX;
      if (deltaX < 0)
        deltaX = -deltaX;
      if ((finalX <= 1 || finalX >= maxX) && deltaX > 6)
      {
        finalX = lastPointX;
      }

      lv_coord_t deltaY = finalY - lastPointY;
      if (deltaY < 0)
        deltaY = -deltaY;
      if ((finalY <= 5 || finalY >= maxY - 5) && deltaY > 20)
      {
        finalY = lastPointY;
      }

      if (deltaX > 50)  // Increased from 24 for slider responsiveness
      {
        finalX = lastPointX;
      }

      if (deltaY > 60)  // Increased from 32 for slider responsiveness
      {
        finalY = lastPointY;
      }
    }

    // 2-sample averaging filter for touch stability
    historyX[historyWriteIndex] = finalX;
    historyY[historyWriteIndex] = finalY;
    historyWriteIndex           = (historyWriteIndex + 1) % filterDepth;
    if (historyCount < filterDepth)
      historyCount++;

    lv_coord_t accumX = 0;
    lv_coord_t accumY = 0;
    for (uint8_t i = 0; i < historyCount; i++)
    {
      accumX += historyX[i];
      accumY += historyY[i];
    }

    lv_coord_t filteredX = historyCount ? (accumX / historyCount) : finalX;
    lv_coord_t filteredY = historyCount ? (accumY / historyCount) : finalY;

    lastPointX    = filteredX;
    lastPointY    = filteredY;
    haveLastPoint = true;

    data->state   = LV_INDEV_STATE_PR;
    data->point.x = lastPointX;
    data->point.y = lastPointY;

    // Track last touch event time for UI health monitoring
    extern unsigned long lastTouchEvent;
    lastTouchEvent = millis();

    // Log valid touches (throttled to avoid spam)
    static unsigned long lastTouchLog = 0;
    if (millis() - lastTouchLog > 2000) {  // Log every 2 seconds max
      LOG_DEBUG(TAG_UI, "Touch detected: (%d, %d)", data->point.x, data->point.y);
      lastTouchLog = millis();
    }

    char buf[20] = {0};
    sprintf(buf, "(%d, %d)", data->point.x, data->point.y);
    if (ui_cartext != nullptr)
      lv_label_set_text(ui_cartext, buf);
  }
  else
  {
    // DIAGNOSTIC: Log when touch is released (not every poll, only on state change)
    static bool wasPressed = false;
    if (wasPressed) {
      LOG_INFO(TAG_UI, "Touch RELEASED (no touch detected, type=%d, rawX=%d, rawY=%d)", type, rawX, rawY);
      wasPressed = false;
    }

    data->state = LV_INDEV_STATE_REL;
    haveLastPoint     = false;
    historyCount      = 0;
    historyWriteIndex = 0;

    // Track if we were just pressed (for next cycle logging)
    if (data->state == LV_INDEV_STATE_PR) {
      wasPressed = true;
    }
  }
}

// -----------------------------------------------------------------------------
// LVGL Event Handlers
// -----------------------------------------------------------------------------

void ui_event_FlushButton(lv_event_t *e)
{
  static uint32_t pressedAt = 0;
  static lv_coord_t pressX = 0;
  static lv_coord_t pressY = 0;

  lv_event_code_t event_code = lv_event_get_code(e);

  if (event_code == LV_EVENT_PRESSED)
  {
    pressedAt = millis();

    // Record press coordinates for edge rejection (thermal protection)
    lv_indev_t* indev = lv_indev_get_act();
    if (indev) {
      lv_point_t point;
      lv_indev_get_point(indev, &point);
      pressX = point.x;
      pressY = point.y;

      // Reject touches too close to left edge (thermal drift zone)
      if (pressX < 5) {
        LOG_DEBUG(TAG_UI, "Flush: Rejected edge touch (X < 5px)");
        pressedAt = 0;  // Invalidate press
        return;
      }
    }

    LOG_DEBUG(TAG_UI, "Flush button PRESSED");
    return;
  }

  if (event_code == LV_EVENT_CLICKED && pressedAt > 0 &&
      (millis() - pressedAt) >= FLUSH_BUTTON_MIN_PRESS_MS)
  {
    if (!isFlushing)
    {
      if (shot.brewing)
      {
        queueScaleStatus("Cannot flush while shot running");
        return;
      }

      LOG_INFO(TAG_UI, "Flush activated (%lums press)", millis() - pressedAt);

      flushingFeature();
    }
    pressedAt = 0;  // Reset
  }
  else if (event_code == LV_EVENT_CLICKED && pressedAt > 0)
  {
    LOG_DEBUG(TAG_UI, "Flush REJECTED - press too short (%lums)", millis() - pressedAt);
    pressedAt = 0;
  }
}

void ui_event_StartButton(lv_event_t *e)
{
  static uint32_t pressedAt = 0;
  lv_event_code_t event_code = lv_event_get_code(e);

  if (event_code == LV_EVENT_PRESSED)
  {
    pressedAt = millis();
    return;
  }

  if (event_code == LV_EVENT_CLICKED && (millis() - pressedAt) >= HUMAN_TOUCH_MIN_MS)
  {
    setStatusLabels("Start Button Pressed");
    startBrew();
  }
}

void ui_event_StopButton(lv_event_t *e)
{
  static uint32_t pressedAt = 0;
  lv_event_code_t event_code = lv_event_get_code(e);

  if (event_code == LV_EVENT_PRESSED)
  {
    pressedAt = millis();
    return;
  }

  if (event_code == LV_EVENT_CLICKED && (millis() - pressedAt) >= HUMAN_TOUCH_MIN_MS)
  {
    setStatusLabels("Stop Button Pressed");
    stopBrew(false, BUTTON_PRESSED);
  }
}

void ui_event_ScaleResetButton(lv_event_t *e)
{
  static uint32_t pressedAt = 0;
  lv_event_code_t event_code = lv_event_get_code(e);

  if (event_code == LV_EVENT_PRESSED)
  {
    pressedAt = millis();
    return;
  }

  if (event_code == LV_EVENT_CLICKED && (millis() - pressedAt) >= HUMAN_TOUCH_MIN_MS)
  {
    if (!scale.isConnected())
    {
      queueScaleStatus("Scale not connected");
      return;
    }

    if (!scale.tare())
    {
      setStatusLabels("Scale tare failed");
      return;
    }

    setStatusLabels("Scale Tared.");
  }
}

void ui_event_PresetWeightSlight(lv_event_t *e)
{
  lv_event_code_t event_code = lv_event_get_code(e);
  lv_obj_t *target = lv_event_get_target(e);
  if (event_code == LV_EVENT_VALUE_CHANGED)
  {
    _ui_slider_set_text_value(ui_PresetWeightLabel, target, "", " g");
    int PresetWeightValue = lv_slider_get_value(target);
    goalWeight = PresetWeightValue;  // Update RAM variable for immediate effect
    saveWeight(PresetWeightValue);   // Save to Flash for persistence across reboots
    _ui_slider_set_text_value(ui_SerialLabel1, target, "Preset Weight Value Set @ ", " g");
  }
}

void ui_event_BacklightSlider(lv_event_t *e)
{
  lv_event_code_t event_code = lv_event_get_code(e);
  lv_obj_t *target = lv_event_get_target(e);
  if (event_code == LV_EVENT_VALUE_CHANGED)
  {
    _ui_slider_set_text_value(ui_BacklightLabel, target, "", " %");
    _ui_slider_set_text_value(ui_SerialLabel1, target, "Backlight Value Set @ ", " %");
    int brightnessValue = lv_slider_get_value(target);
    brightness = map(brightnessValue, 0, 100, 70, 255);
    saveBrightness(brightnessValue); // Save 0-100% brightness value
    LOG_DEBUG(TAG_UI, "Brightness value saved @ %d", brightnessValue);
    analogWrite(TFT_BL, brightness); // PWM based on 0-255
  }
}

void ui_event_TimerResetButton(lv_event_t *e)
{
  static uint32_t pressedAt = 0;
  lv_event_code_t event_code = lv_event_get_code(e);

  if (event_code == LV_EVENT_PRESSED)
  {
    pressedAt = millis();
    return;
  }

  if (event_code == LV_EVENT_CLICKED && (millis() - pressedAt) >= HUMAN_TOUCH_MIN_MS)
  {
    char buffer[5];
    shot.shotTimer = 0;
    dtostrf(shot.shotTimer, 5, 1, buffer);
    lv_label_set_text(ui_TimerLabel, buffer);
  }
}

// -----------------------------------------------------------------------------
// Persistence Helpers
// -----------------------------------------------------------------------------

void flushingFeature()
{
  if (shot.brewing)
  {
    queueScaleStatus("Cannot flush during shot");
    return;
  }

  flushMessageActive = false;
  setRelayState(true);          // Turn on the output pin
  startTimeFlushing = millis(); // Record the current time
  isFlushing = true;            // Set the flushing flag
  LOG_INFO(TAG_UI, "Flushing started");
  enforceRelayState();
}

void saveBrightness(int brightness)
{
  preferences.begin("myApp", false);              // Open the preferences with a namespace and read-only flag
  preferences.putInt(BRIGHTNESS_KEY, brightness); // Write the brightness value to preferences
  preferences.end();                              // Close the preferences
}

void saveOffset(int offset)
{
  preferences.begin("myApp", false);      // Open the preferences with a namespace and read-only flag
  preferences.putInt(OFFSET_KEY, offset); // Write the brightness value to preferences
  preferences.end();                      // Close the preferences
}

void saveWeight(int weight)
{
  preferences.begin("myApp", false);      // Open the preferences with a namespace and read-only flag
  preferences.putInt(WEIGHT_KEY, weight); // Write the brightness value to preferences
  preferences.end();                      // Close the preferences
}

// -----------------------------------------------------------------------------
// Scale & Connectivity Utilities
// -----------------------------------------------------------------------------

void checkHeartBreat()
{
  if (!scale.isConnected())
  {
    return;
  }
  if (scale.heartbeatRequired())
  {
    scale.heartbeat();
  }
}

// Battery request now handled during scale init in AcaiaArduinoBLE library

void checkScaleStatus()
{
  bool connected = scale.isConnected();
  uint32_t now   = millis();

  // CRITICAL: This function runs in BLE task (Core 0)
  // CANNOT access LVGL UI elements - they're not thread-safe!
  // UI updates are handled by main loop reading shared data

  if (!connected)
  {
    // UI updates removed - handled by updateUIWithBLEData() on Core 1

    if (!hasEverConnectedToScale)
    {
      if (!hasShownNoScaleMessage)
      {
        queueScaleStatus("Scale Not Connected");
        hasShownNoScaleMessage = true;
      }
    }
    else if (lastScaleConnected)
    {
      queueScaleStatus("Scale disconnected");
    }

    lastScaleConnected = false;

    // ArduinoBLE: init() is blocking (with LVGL keepalive inside), safe on Core 0
    // Retry logic ensures we don't spam connection attempts
    if (!scale.isConnected())
    {
      // Not currently connected, can start new attempt
      if (!isFlushing && ((now - lastScaleInitAttempt) >= SCALE_INIT_RETRY_MS || lastScaleInitAttempt == 0))
      {
        scale.init();  // Blocking call (1s timeout), but runs on Core 0 so UI on Core 1 is unaffected
        lastScaleInitAttempt = now;
      }
    }
    // NOTE: Dead code removed (was checking scale.isConnected() inside !connected block)
    // Connection success is now only handled in the else block below

    currentWeight = 0;
    firstConnectionNotificationPending = true;

    if (shot.brewing)
    {
      stopBrew(false, SCALE_DISCONNECTED);
    }
  }
  else
  {
    // UI updates removed - handled by updateUIWithBLEData() on Core 1

    // ArduinoBLE: isConnected() already means fully connected (init() succeeded)
    // No need to check connection state - if init() returned true, we're fully connected
    if (!lastScaleConnected)
    {
      queueScaleStatus("Scale Connected");
    }

    firstConnectionNotificationPending = false;
    lastScaleConnected                 = true;
    hasEverConnectedToScale            = true;
    hasShownNoScaleMessage             = false;
    lastScaleInitAttempt               = now;
  }

  if (!isFlushing && hasPendingScaleStatus)
  {
    if (!flushMessageActive || millis() >= flushMessageHoldUntil)
    {
      setStatusLabels(pendingScaleStatus);  // Fixed: already char array, no .c_str() needed
      hasPendingScaleStatus = false;
      flushMessageActive    = false;
    }
  }
}

// ============================================================================
//  Runtime helpers
// ============================================================================
static void handleFlushingCycle()
{
  if (!isFlushing)
    return;

  const unsigned long now       = millis();
  const unsigned long elapsed   = now - startTimeFlushing;
  const unsigned long remaining = (flushDuration > elapsed) ? (flushDuration - elapsed) : 0;

  if (now - lastPrintTimeFlushing >= 1000)
  {
    lastPrintTimeFlushing = now;

    if (!hasPendingScaleStatus && strlen(currentStatusText) > 0)  // Fixed: use strlen() for char array
    {
      strncpy(pendingScaleStatus, currentStatusText, sizeof(pendingScaleStatus) - 1);  // Fixed: safe string copy
      pendingScaleStatus[sizeof(pendingScaleStatus) - 1] = '\0';  // Ensure null termination
      hasPendingScaleStatus = true;
    }

    LOG_DEBUG(TAG_UI, "Flushing... %lu seconds remaining", remaining / 1000);

    char labelBuf[48];
    snprintf(labelBuf, sizeof(labelBuf), "Flushing... %lu seconds remaining", remaining / 1000);
    setStatusLabels(labelBuf);
  }

  if (elapsed >= flushDuration)
  {
    setRelayState(false);
    isFlushing = false;
    LOG_INFO(TAG_UI, "Flushing ended");
    setStatusLabels("Flushing ended");

    flushMessageActive    = true;
    flushMessageHoldUntil = now + FLUSH_STATUS_HOLD_MS;
  }
}

static void processPendingStatusQueue()
{
  if (isFlushing || !hasPendingScaleStatus)
    return;

  if (!flushMessageActive || millis() >= flushMessageHoldUntil)
  {
    setStatusLabels(pendingScaleStatus);  // Fixed: already char array, no .c_str() needed
    hasPendingScaleStatus = false;
    flushMessageActive    = false;
  }
}

static void updateScaleReadings()
{
  // CRITICAL: Check isConnected FIRST to prevent reading uninitialized BLE characteristics
  // C++ short-circuit evaluation: if isConnected() is false, newWeightAvailable() won't be called
  // Bug fix: Calling newWeightAvailable() when disconnected causes LoadProhibited crash
  if (!scale.isConnected() || !scale.newWeightAvailable())
    return;

  currentWeight = scale.getWeight();

  // CRITICAL FIX: Throttle UI updates to prevent watchdog timeout
  // Rate limit weight updates to 10Hz (100ms) to prevent UI queue overflow
  // Without throttling, 3-4 updates/sec fills queue → processUIUpdates() blocks → watchdog timeout
  static unsigned long lastWeightUIUpdate = 0;
  static float lastUIWeight = 0;
  const unsigned long WEIGHT_UI_UPDATE_INTERVAL = 100;  // 100ms = 10Hz max UI updates

  unsigned long now = millis();
  bool weightChanged = fabs(currentWeight - lastUIWeight) > 0.1;  // >0.1g change
  bool intervalElapsed = (now - lastWeightUIUpdate) >= WEIGHT_UI_UPDATE_INTERVAL;

  if (weightChanged || intervalElapsed) {
    // PROFESSIONAL FIX: Use message queue for thread-safe UI updates
    // BLE task (Core 0) enqueues weight text, main loop (Core 1) updates LVGL
    char buffer[10];
    dtostrf(currentWeight, 5, 1, buffer);
    queueUIUpdate(UI_UPDATE_WEIGHT, buffer);
    lastWeightUIUpdate = now;
    lastUIWeight = currentWeight;
  }

  // Rate limit weight printing to prevent WebSocket overflow
  // Only print every 100ms OR when weight changes by >0.1g
  static unsigned long lastWeightPrint = 0;
  static float lastPrintedWeight = 0;
  const unsigned long WEIGHT_PRINT_INTERVAL = 100;  // 100ms = 10Hz (was 10ms = 100Hz)

  bool shouldPrint = false;

  if (now - lastWeightPrint >= WEIGHT_PRINT_INTERVAL) {
    shouldPrint = true;  // Time-based: print every 100ms
  } else if (abs(currentWeight - lastPrintedWeight) > 0.1) {
    shouldPrint = true;  // Change-based: significant weight change (>0.1g)
  }

  if (shouldPrint) {
    LOG_VERBOSE(TAG_SHOT, "%.2fg", currentWeight);
    lastWeightPrint = now;
    lastPrintedWeight = currentWeight;
  }

  if (!shot.brewing)
  {
    return;
  }

  if (shot.datapoints >= SHOT_HISTORY_CAP)
  {
    // Clamp FIRST to prevent out-of-bounds access if datapoints > SHOT_HISTORY_CAP
    shot.datapoints = SHOT_HISTORY_CAP - 1;

    // Now safe to shift array (discard oldest datapoint)
    std::memmove(shot.time_s,   shot.time_s + 1,   shot.datapoints * sizeof(float));
    std::memmove(shot.weight,   shot.weight + 1,   shot.datapoints * sizeof(float));

    LOG_DEBUG(TAG_SHOT, "Shot history buffer full, oldest datapoint discarded");  // Changed to DEBUG - this is normal operation
  }

  const float nowSeconds = seconds_f() - shot.start_timestamp_s;
  shot.time_s[shot.datapoints] = nowSeconds;
  shot.weight[shot.datapoints] = currentWeight;
  shot.shotTimer                = nowSeconds;
  shot.datapoints++;

  // Timer display now updated independently by updateShotTimer() function

  calculateEndTime(&shot);

  if (shouldPrint) {
    LOG_VERBOSE(TAG_SHOT, "Timer: %.1fs, Expected end: %.1fs", shot.shotTimer, shot.expected_end_s);
  }

  char labelBuf[40];
  snprintf(labelBuf, sizeof(labelBuf), "Expected end time @ %.1f s", shot.expected_end_s);
  setStatusLabels(labelBuf);
}

static void handleShotWatchdogs()
{
  if (shot.brewing || isFlushing)
    setRelayState(true);
  else
  {
    setRelayState(false);
    previousTimerValue = 0.0f;
    lastTimerUpdate = 0;
  }

  enforceRelayState();

  if (shot.brewing && shot.shotTimer > MAX_SHOT_DURATION_S)
  {
    LOG_WARN(TAG_SHOT, "Max brew duration reached");
    setStatusLabels("Max brew duration reached");
    stopBrew(false, TIME_EXCEEDED);
  }

  if (shot.brewing && shot.shotTimer >= shot.expected_end_s && shot.shotTimer > MIN_SHOT_DURATION_S)
  {
    LOG_INFO(TAG_SHOT, "Weight achieved");
    setStatusLabels("Weight achieved");
    stopBrew(false, WEIGHT_ACHIEVED);
  }

  if (shot.start_timestamp_s && shot.end_s && currentWeight >= (goalWeight - weightOffset) &&
      seconds_f() > shot.start_timestamp_s + shot.end_s + DRIP_DELAY_S)
  {
    shot.start_timestamp_s = 0;
    shot.end_s             = 0;

    if (abs(currentWeight - goalWeight + weightOffset) > MAX_OFFSET)
    {
      LOG_INFO(TAG_SHOT, "Final weight: %.2fg, Goal: %dg, Offset: %.2fg - Error assumed. Offset unchanged.",
               currentWeight, goalWeight, weightOffset);
      setStatusLabels("Error assumed. Offset unchanged.");
    }
    else
    {
      weightOffset += currentWeight - goalWeight;
      LOG_INFO(TAG_SHOT, "Final weight: %.2fg, Goal: %dg, New offset: %.2fg",
               currentWeight, goalWeight, weightOffset);
      saveOffset(static_cast<int>(weightOffset * 10.0f));
    }
  }
}

extern uint32_t transfer_num;
extern size_t lcd_PushColors_len;

// LVGL Freeze Detection - track when lv_timer_handler() was last called successfully
static unsigned long lastLVGLTimerCall = 0;
static unsigned long lvglTimerCallCount = 0;
static bool lvglFreezeWarned = false;

void LVGLTimerHandlerRoutine()
{
  // CRITICAL: Don't call lv_timer_handler() before LVGL is initialized
  // This prevents crashes when BLE library calls this during early boot
  if (!lvglInitialized)
    return;

  unsigned long now = millis();

  // Detect LVGL freeze (no successful timer calls for 3+ seconds)
  if (lastLVGLTimerCall > 0 && (now - lastLVGLTimerCall) > 3000) {
    if (!lvglFreezeWarned) {
      LOG_ERROR(TAG_UI, "🚨 LVGL FREEZE DETECTED!");
      LOG_ERROR(TAG_UI, "   Last successful timer call: %lums ago", now - lastLVGLTimerCall);
      LOG_ERROR(TAG_UI, "   Total calls: %lu", lvglTimerCallCount);
      LOG_ERROR(TAG_UI, "   transfer_num=%d, lcd_PushColors_len=%d", transfer_num, lcd_PushColors_len);
      lvglFreezeWarned = true;
    }
  } else if (lvglFreezeWarned && (now - lastLVGLTimerCall) <= 3000) {
    LOG_ERROR(TAG_UI, "✅ LVGL RECOVERED from freeze");
    lvglFreezeWarned = false;
  }

  if (transfer_num <= 0 && lcd_PushColors_len <= 0)
  {
    lv_timer_handler();
    lastLVGLTimerCall = now;
    lvglTimerCallCount++;

    // Log LVGL activity every 10 seconds for diagnostic purposes
    // NOTE: This should always run at ~500-700 Hz - if it drops, there's a real freeze
    static unsigned long lastActivityLog = 0;
    if (now - lastActivityLog > 10000) {
      LOG_DEBUG(TAG_UI, "📊 LVGL Activity: %lu timer calls in last 10s (~%lu Hz)",
                lvglTimerCallCount, lvglTimerCallCount / 10);
      lvglTimerCallCount = 0;
      lastActivityLog = now;
    }
  }

  if (transfer_num <= 1 && lcd_PushColors_len > 0)
  {
    lcd_PushColors(0, 0, 0, 0, NULL);
  }
}

// -----------------------------------------------------------------------------
// Application Entry Points
// -----------------------------------------------------------------------------

void setup()
{
  // =============================================================================
  // Boot Sequence (order is CRITICAL for WiFi+BLE coexistence)
  // =============================================================================
  // 0. NVS init - MUST be first (prevents double-init crash)
  // 1. Serial.begin() - For early boot messages
  // 2. Create serialMutex - Required before any LOG_*() calls
  // 3. NimBLE init - Must claim radio BEFORE WiFi
  // 4. DEBUG_INIT() - WiFi setup (debug builds only), re-calls Serial.begin() safely
  // =============================================================================

  // Step 0: Initialize NVS (Non-Volatile Storage) FIRST - before WiFi or BLE
  // This prevents both radios from trying to initialize NVS independently
  // Critical for WiFi+BLE coexistence on ESP32-S3
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();
    nvs_flash_init();
  }

  // Step 1: Initialize Serial (production builds need this, debug builds will re-init)
  Serial.begin(115200);
  delay(500);  // Delay for Serial to stabilize (increased for reliability)

  // Step 2: Create serial mutex BEFORE any LOG_*() calls
  // This prevents output fragmentation when multiple tasks print simultaneously
  serialMutex = xSemaphoreCreateMutex();
  if (serialMutex == NULL) {
    // CRITICAL: Can't use LOG_*() here - mutex creation failed!
    // This is the ONLY place where raw Serial.println() is acceptable
    Serial.println("\n\n=== BOOT FAILURE ===");
    Serial.println("[FATAL] Failed to create serialMutex!");
    Serial.println("System halted - restart required");
    while(1) delay(1000);  // Halt - critical failure
  }

  // Mutex created successfully - can now use LOG_*() macros safely
  LOG_INFO(TAG_SYS, "");
  LOG_INFO(TAG_SYS, "=== BOOT START ===");
  LOG_INFO(TAG_SYS, "Serial initialized @ 115200 baud");
  LOG_INFO(TAG_SYS, "FreeRTOS mutex created successfully");

  // Log reset reason - WHY did we boot? (FORCE with raw Serial to bypass log filtering)
  esp_reset_reason_t reset_reason = esp_reset_reason();
  const char* reason_str;
  switch(reset_reason) {
    case ESP_RST_POWERON:   reason_str = "Power-on reset"; break;
    case ESP_RST_SW:        reason_str = "Software reset via esp_restart()"; break;
    case ESP_RST_PANIC:     reason_str = "Exception/panic (CRASH!)"; break;
    case ESP_RST_INT_WDT:   reason_str = "Interrupt watchdog timeout"; break;
    case ESP_RST_TASK_WDT:  reason_str = "Task watchdog timeout"; break;
    case ESP_RST_WDT:       reason_str = "Other watchdog timeout"; break;
    case ESP_RST_DEEPSLEEP: reason_str = "Deep sleep wake"; break;
    case ESP_RST_BROWNOUT:  reason_str = "Brownout detector (power glitch)"; break;
    case ESP_RST_EXT:       reason_str = "External reset pin"; break;
    case ESP_RST_SDIO:      reason_str = "SDIO reset"; break;
    default:                reason_str = "Unknown reset"; break;
  }

  // CRITICAL: Wait for USB CDC to reconnect BEFORE printing
  // Serial monitor drops connection during reset, needs time to reconnect
  delay(500);

  // FORCE output with raw Serial (bypasses all log filtering - GUARANTEED to show)
  Serial.println("\n\n╔═══════════════════════════════════════════╗");
  Serial.print("║  RESET REASON: ");
  Serial.print(reason_str);
  for (int i = strlen(reason_str); i < 26; i++) Serial.print(" ");
  Serial.println(" ║");
  Serial.println("╚═══════════════════════════════════════════╝");

  // Special diagnostics for interrupt watchdog timeout (most common crash type)
  if (reset_reason == ESP_RST_INT_WDT) {
    Serial.println("║                                           ║");
    Serial.println("║  🚨 INTERRUPT WATCHDOG TIMEOUT!          ║");
    Serial.println("║                                           ║");
    Serial.println("║  An ISR (interrupt) ran too long         ║");
    Serial.println("║  Timeout: 1000ms (increased from 300ms)  ║");
    Serial.println("║                                           ║");
    Serial.println("║  Likely causes:                          ║");
    Serial.println("║  • BLE notification handler blocking     ║");
    Serial.println("║  • Display SPI DMA stall                 ║");
    Serial.println("║  • Touch I2C interrupt conflict          ║");
    Serial.println("║  • LVGL rendering during critical section║");
    Serial.println("╚═══════════════════════════════════════════╝");
  } else if (reset_reason == ESP_RST_PANIC) {
    Serial.println("║                                           ║");
    Serial.println("║  💀 EXCEPTION/PANIC - Check above logs!  ║");
    Serial.println("║  (Should see backtrace with addresses)   ║");
    Serial.println("╚═══════════════════════════════════════════╝");
  } else {
    Serial.println("╚═══════════════════════════════════════════╝");
  }
  Serial.println();
  Serial.flush();  // Force USB CDC to send immediately

  // Step 3: Initialize ArduinoBLE (creates HCI stream buffers for BLE communication)
  LOG_INFO(TAG_SYS, "");
  LOG_INFO(TAG_SYS, "=== Initializing ArduinoBLE ===");
  if (!BLE.begin()) {
    LOG_ERROR(TAG_SYS, "❌ ArduinoBLE initialization failed!");
    LOG_ERROR(TAG_SYS, "System halted - BLE init is critical for scale connection");
    while(1) delay(1000);  // Halt - BLE init failure is unrecoverable
  }
  LOG_INFO(TAG_SYS, "✅ ArduinoBLE initialized - HCI stream buffers ready");

  // BACKUP: Log reset reason again via LOG_ERROR (in case raw Serial was missed)
  // This provides redundancy if USB CDC wasn't connected during early boot
  LOG_ERROR(TAG_SYS, "🔄 RESET REASON: %s", reason_str);

  // Step 4: Initialize WiFi (debug builds only) via DEBUG_INIT()
  // NimBLE is already running, WiFi will coexist properly
  // Note: DEBUG_INIT() calls Serial.begin() again - this is safe (no-op on already-initialized serial)
  DEBUG_INIT();  // Production: no-op, Debug builds: WiFi+WebSerial setup
  delay(500);    // Brief delay for WiFi connection (if enabled)

  LOG_INFO(TAG_SYS, "=== Gravimetric Shots Initializing ===");

  // Initialize PMU (Power Management Unit) for LED control
  // T-Display-S3-Long uses SY6970 on I2C pins: SDA=15, SCL=10 (same as touch)
  Wire.begin(TOUCH_IICSDA, TOUCH_IICSCL);  // Touch controller pins (also used for PMU)
  Wire.setClock(100000);  // 100kHz for reliability under thermal stress
  if (PMU.init(Wire, TOUCH_IICSDA, TOUCH_IICSCL, SY6970_SLAVE_ADDRESS))
  {
    PMU.disableStatLed();  // Turn off green charging indicator LED
    LOG_INFO(TAG_SYS, "PMU initialized, status LED disabled");
  }
  else
  {
    LOG_WARN(TAG_SYS, "PMU init failed (LED control unavailable)");
  }

  // Initialize task watchdog (20 second timeout) - auto-reboot on hang
  // CRITICAL: Increased from 10s to 20s to accommodate BLE discovery delays
  // peripheral.discoverAttributes() can take 1-10+ seconds during reconnection
  // 20s provides safety margin while still catching real freezes
  esp_task_wdt_init(20, true);  // 20s timeout, panic & reboot on trigger
  esp_task_wdt_add(NULL);       // Add current task to watchdog
  LOG_INFO(TAG_SYS, "Task watchdog enabled (20s timeout)");

  preferences.begin("myApp", false);                       // Open the preferences with a namespace and read-only flag
  brightness = preferences.getInt(BRIGHTNESS_KEY, 0);      // Read the brightness value from preferences
  goalWeight = preferences.getInt(WEIGHT_KEY, 0);          // Read the target weight value from preferences
  weightOffset = preferences.getInt(OFFSET_KEY, 0) / 10.0; // Read the offset value from preferences
  preferences.end();                                       // Close the preferences

  LOG_DEBUG(TAG_SYS, "Brightness read from preferences: %d", brightness);
  LOG_DEBUG(TAG_SYS, "Goal Weight retrieved: %d", goalWeight);
  LOG_DEBUG(TAG_SYS, "Offset retrieved: %.1f", weightOffset);

  if ((goalWeight < 10) || (goalWeight > 200)) // If preferences isn't initialized and has an unreasonable weight/offset, default to 36g/1.5g
  {
    goalWeight = 36;
    LOG_INFO(TAG_SYS, "Goal Weight set to: %d g", goalWeight);
  }

  if (weightOffset > MAX_OFFSET)
  {
    weightOffset = 1.5;
    LOG_INFO(TAG_SYS, "Offset set to: %.1f g", weightOffset);
  }

  if ((brightness < 0) || (brightness > 100)) // If preferences isn't initialized set brightness to 50%
  {
    brightness = 50;
    LOG_INFO(TAG_SYS, "Backlight set to: Default @ %d %%", brightness);
  }

  // initialize the GPIO hardware
  // To add in progress
  pinMode(RELAY1, OUTPUT); // RELAY 1 Output
  relayState = true; // force update on first set
  setRelayState(false);

  // NimBLE server functionality DISABLED (reverted to ArduinoBLE for stability)
  // Weight advertising to external devices is optional - not essential for core functionality
  LOG_DEBUG(TAG_SYS, "NimBLE server disabled (ArduinoBLE mode)");
  // COMMENTED OUT: NimBLE server code removed for ArduinoBLE compatibility
  // pServer = NimBLEDevice::createServer();
  // if (pServer) {
  //   pWeightService = pServer->createService("00002a98-0000-1000-8000-00805f9b34fb");
  //   if (pWeightService) {
  //     pWeightCharacteristic = pWeightService->createCharacteristic(
  //       "0x2A98",
  //       NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE
  //     );
  //     if (pWeightCharacteristic) {
  //       uint8_t initialValue = 36;
  //       pWeightCharacteristic->setValue(&initialValue, 1);
  //     }
  //     pWeightService->start();
  //   }
  //   NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
  //   pAdvertising->addServiceUUID("00002a98-0000-1000-8000-00805f9b34fb");
  //   pAdvertising->start();
  //   LOG_INFO(TAG_SYS, "NimBLE server ready");
  // }

  pinMode(TOUCH_RES, OUTPUT);
  digitalWrite(TOUCH_RES, HIGH);
  delay(2);
  digitalWrite(TOUCH_RES, LOW);
  delay(10);
  digitalWrite(TOUCH_RES, HIGH);
  delay(2);

  Wire.begin(TOUCH_IICSDA, TOUCH_IICSCL);
  Wire.setClock(100000);  // 100kHz for reliability under thermal stress

  // DIAGNOSTIC: Test touch controller I2C communication after reset
  LOG_INFO(TAG_UI, "═══════════════════════════════════════════════");
  LOG_INFO(TAG_UI, "  🔬 TOUCH CONTROLLER (CST816) INITIALIZATION TEST");
  LOG_INFO(TAG_UI, "═══════════════════════════════════════════════");
  LOG_INFO(TAG_UI, "Touch reset: GPIO %d (toggled LOW 10ms, now HIGH)", TOUCH_RES);
  LOG_INFO(TAG_UI, "I2C pins: SDA=GPIO %d, SCL=GPIO %d, freq=100kHz", TOUCH_IICSDA, TOUCH_IICSCL);

  delay(50);  // Give touch controller time to boot after reset

  // Test I2C communication with touch controller
  Wire.beginTransmission(0x3B);
  uint8_t i2c_test_error = Wire.endTransmission();
  if (i2c_test_error == 0) {
    LOG_INFO(TAG_UI, "✅ Touch controller ACK at address 0x3B");
  } else {
    LOG_ERROR(TAG_UI, "❌ Touch controller NO ACK at 0x3B (error=%d)", i2c_test_error);
    LOG_ERROR(TAG_UI, "   Error codes: 1=too long, 2=NACK addr, 3=NACK data, 4=other");
  }

  // NOTE: Touch controller uses CUSTOM protocol (not standard CST816)
  // ChipID register 0xA7 is NOT supported - reading it causes controller confusion and crashes
  // LilyGO lvgl_demo.ino does NOT send ANY I2C commands to touch controller during setup()!
  // First touch I2C transaction happens naturally in my_touchpad_read() during loop()
  // Sending test commands (like 0xD0) can put controller into undefined state
  LOG_INFO(TAG_UI, "═══════════════════════════════════════════════");

  pinMode(TFT_BL, OUTPUT);    // initialized TFT Backlight Pin as output
  digitalWrite(TFT_BL, LOW);  // Keep backlight OFF during initialization to prevent noise/garbage display

  axs15231_init(); // initialized Screen

  lv_init(); // initialized LVGL

  // ===== DIAGNOSTIC: Register LVGL log callback =====
  // Route LVGL internal logs through our logging system
  #if LV_USE_LOG
    lv_log_register_print_cb([](const char* msg) {
      // LVGL messages come with newlines, remove them
      char clean_msg[256];
      strncpy(clean_msg, msg, sizeof(clean_msg) - 1);
      clean_msg[sizeof(clean_msg) - 1] = '\0';
      size_t len = strlen(clean_msg);
      if (len > 0 && clean_msg[len - 1] == '\n') {
        clean_msg[len - 1] = '\0';
      }
      LOG_WARN("LVGL", "%s", clean_msg);
    });
    LOG_INFO(TAG_SYS, "✅ LVGL logging enabled (callback registered)");
  #endif
  // ===== END LVGL LOG CALLBACK =====

  // Display init code
  {
    size_t buffer_pixels = EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES;
    size_t buffer_size = sizeof(lv_color_t) * buffer_pixels;
    buf = (lv_color_t *)ps_malloc(buffer_size);
    if (buf == NULL)
    {
      while (1)
      {
        LOG_ERROR(TAG_SYS, "buf NULL - PSRAM allocation failed!");
        delay(500);
      }
    }
    // DIAGNOSTIC: Save initial buffer address for integrity checks
    buf_initial = buf;
    LOG_INFO(TAG_SYS, "💾 PSRAM buf allocated at %p (%zu bytes)", buf, buffer_size);

    buf1 = (lv_color_t *)ps_malloc(buffer_size);
    if (buf1 == NULL)
    {
      while (1)
      {
        LOG_ERROR(TAG_SYS, "buf1 NULL - PSRAM allocation failed!");
        delay(500);
      }
    }
    // DIAGNOSTIC: Save initial buffer address for integrity checks
    buf1_initial = buf1;
    LOG_INFO(TAG_SYS, "💾 PSRAM buf1 allocated at %p (%zu bytes)", buf1, buffer_size);

    lv_disp_draw_buf_init(&draw_buf, buf, buf1, buffer_pixels);
    /*Initialize the display*/
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    /*Change the following line to your display resolution*/
    disp_drv.hor_res = EXAMPLE_LCD_H_RES;
    disp_drv.ver_res = EXAMPLE_LCD_V_RES;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    disp_drv.sw_rotate = 1; // If you turn on software rotation, Do not update or replace LVGL
    disp_drv.rotated = LV_DISP_ROT_270;
    disp_drv.full_refresh = 1; // full_refresh must be 1 (partial refresh causes smearing)
    lv_disp_drv_register(&disp_drv);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = my_touchpad_read;
    lv_indev_drv_register(&indev_drv);
  }

  // ===== CRITICAL: Turn on backlight BEFORE hardware test =====
  // The test pattern must be visible when backlight is ON
  // Previously, backlight was turned on AFTER UI rendering, making test pattern invisible
  int LCDBrightness = map(brightness, 0, 100, 70, 256);
  analogWrite(TFT_BL, LCDBrightness);
  LOG_INFO(TAG_UI, "🔆 Backlight ON EARLY for hardware test: PWM=%d (brightness=%d%%)", LCDBrightness, brightness);
  delay(100);  // Allow backlight to stabilize

  // ===== DIAGNOSTIC: Display Hardware Test =====
  // Test if display hardware is working BEFORE initializing UI
  // This helps determine if the problem is display hardware or LVGL rendering
  LOG_INFO(TAG_UI, "═══════════════════════════════════════════════");
  LOG_INFO(TAG_UI, "  🔬 DISPLAY HARDWARE TEST");
  LOG_INFO(TAG_UI, "  Drawing test pattern to verify SPI/display");
  LOG_INFO(TAG_UI, "  🔆 BACKLIGHT IS NOW ON - SQUARES SHOULD BE VISIBLE");
  LOG_INFO(TAG_UI, "═══════════════════════════════════════════════");

  // Allocate buffer on HEAP (not stack!) to avoid heap corruption
  #define TEST_SIZE 40
  uint16_t* testPixels = (uint16_t*)malloc(TEST_SIZE * TEST_SIZE * sizeof(uint16_t));
  if (testPixels == NULL) {
    LOG_ERROR(TAG_UI, "❌ Failed to allocate test buffer - skipping hardware test");
  } else {
    // Test RED
    for (int i = 0; i < TEST_SIZE * TEST_SIZE; i++) {
      testPixels[i] = 0xF800;  // RGB565 red
    }
    LOG_INFO(TAG_UI, "Pushing %d red pixels to (10,10)...", TEST_SIZE * TEST_SIZE);
    lcd_PushColors(10, 10, TEST_SIZE, TEST_SIZE, testPixels);
    delay(50);

    // Test GREEN
    for (int i = 0; i < TEST_SIZE * TEST_SIZE; i++) {
      testPixels[i] = 0x07E0;  // RGB565 green
    }
    LOG_INFO(TAG_UI, "Pushing %d green pixels to (60,10)...", TEST_SIZE * TEST_SIZE);
    lcd_PushColors(60, 10, TEST_SIZE, TEST_SIZE, testPixels);
    delay(50);

    // Test BLUE
    for (int i = 0; i < TEST_SIZE * TEST_SIZE; i++) {
      testPixels[i] = 0x001F;  // RGB565 blue
    }
    LOG_INFO(TAG_UI, "Pushing %d blue pixels to (10,60)...", TEST_SIZE * TEST_SIZE);
    lcd_PushColors(10, 60, TEST_SIZE, TEST_SIZE, testPixels);
    delay(50);

    free(testPixels);  // Free heap memory

    LOG_INFO(TAG_UI, "✅ Hardware test complete - check for colored squares on display");
    LOG_INFO(TAG_UI, "   Expected: RED at (10,10), GREEN at (60,10), BLUE at (10,60)");
    LOG_INFO(TAG_UI, "═══════════════════════════════════════════════");

    delay(2000);  // Hold test pattern for 2 seconds before UI init
  }
  // ===== END DIAGNOSTIC =====

  ui_init(); // initialized LVGL UI intereface

  // ===== DIAGNOSTIC: LVGL Widget Tree Validation =====
  LOG_INFO(TAG_UI, "═══════════════════════════════════════════════");
  LOG_INFO(TAG_UI, "  🔬 LVGL WIDGET TREE VALIDATION");
  LOG_INFO(TAG_UI, "═══════════════════════════════════════════════");

  // Get active screen
  lv_obj_t* activeScreen = lv_scr_act();
  if (activeScreen) {
    LOG_INFO(TAG_UI, "✅ Active screen exists at %p", activeScreen);

    // Count child widgets
    uint32_t childCount = lv_obj_get_child_cnt(activeScreen);
    LOG_INFO(TAG_UI, "   Child widgets: %lu", childCount);

    if (childCount == 0) {
      LOG_ERROR(TAG_UI, "❌ WARNING: Active screen has NO child widgets!");
      LOG_ERROR(TAG_UI, "   ui_init() may have failed to create UI elements");
    }

    // Force screen invalidation to trigger render
    LOG_INFO(TAG_UI, "   Forcing screen invalidation to trigger render...");
    lv_obj_invalidate(activeScreen);

    // Check if screen is visible
    if (lv_obj_has_flag(activeScreen, LV_OBJ_FLAG_HIDDEN)) {
      LOG_ERROR(TAG_UI, "❌ WARNING: Active screen is HIDDEN!");
    } else {
      LOG_INFO(TAG_UI, "✅ Active screen is VISIBLE");
    }
  } else {
    LOG_ERROR(TAG_UI, "❌ CRITICAL: No active screen! LVGL UI not initialized!");
  }

  // Check display driver
  lv_disp_t* disp = lv_disp_get_default();
  if (disp && disp->driver) {
    LOG_INFO(TAG_UI, "✅ Display driver registered: %dx%d",
             disp->driver->hor_res, disp->driver->ver_res);
    LOG_INFO(TAG_UI, "   Draw buffer: %p (size: %lu pixels)",
             disp->driver->draw_buf, disp->driver->draw_buf->size);
  } else {
    LOG_ERROR(TAG_UI, "❌ Display driver NOT registered!");
  }

  LOG_INFO(TAG_UI, "═══════════════════════════════════════════════");
  // ===== END DIAGNOSTIC =====

  // CRITICAL: Mark LVGL as initialized - safe to call lv_timer_handler() now
  // This prevents crashes when BLE library tries to keep UI responsive during connection
  lvglInitialized = true;

  // Force LVGL to render the first frame before turning on backlight
  // This prevents showing uninitialized frame buffer (noise/garbage)
  LOG_INFO(TAG_UI, "Rendering initial UI frame before backlight ON...");

  // CRITICAL FIX: Use lv_timer_handler() for LVGL v8 (lv_task_handler() was deprecated in v7)
  for (int i = 0; i < 10; i++) {
    lv_timer_handler();  // LVGL v8.x API (correct)
    delay(5);            // Small delay to allow rendering to complete
  }

  // Verify LVGL display is properly initialized
  disp = lv_disp_get_default();  // Reuse disp variable from widget validation above
  if (disp && disp->driver) {
    LOG_INFO(TAG_UI, "✅ LVGL display still valid after rendering: %dx%d",
             disp->driver->hor_res, disp->driver->ver_res);
  } else {
    LOG_ERROR(TAG_UI, "❌ LVGL display NOT initialized - display will be blank!");
  }

  // NOTE: Backlight already turned ON earlier (before hardware test) - no need to turn on again
  LOG_INFO(TAG_UI, "UI rendering complete - backlight already ON from hardware test phase");

  // initialized the Backlight Slider and Label Value
  lv_slider_set_value(ui_BacklightSlider, brightness, LV_ANIM_OFF);
  char buffer[10]; // Make sure the buffer is large enough to hold the result
  char prefixedBuffer[10];

  dtostrf(brightness, 3, 0, buffer);
  snprintf(prefixedBuffer, sizeof(prefixedBuffer), "%s %%", buffer);
  lv_label_set_text(ui_BacklightLabel, prefixedBuffer);

  // Initialized PresetWeight Slider and Label Value.
  dtostrf(goalWeight, 3, 0, buffer);
  snprintf(prefixedBuffer, sizeof(prefixedBuffer), "%s g", buffer);
  lv_label_set_text(ui_PresetWeightLabel, prefixedBuffer);


  LOG_INFO(TAG_SYS, "Flash size: %u bytes", ESP.getFlashChipSize());
  LOG_INFO(TAG_SYS, "App partition: %u used / %u bytes total", ESP.getSketchSize(), ESP.getSketchSize() + ESP.getFreeSketchSpace());
  LOG_INFO(TAG_SYS, "Heap total: %u bytes, free: %u bytes", ESP.getHeapSize(), ESP.getFreeHeap());
  LOG_INFO(TAG_SYS, "PSRAM total: %u bytes, free: %u bytes", ESP.getPsramSize(), ESP.getFreePsram());
  LOG_INFO(TAG_SYS, "Setup Completed");

  // Initialize touch time tracking
  lastTouchTime = millis();

  // Create FreeRTOS mutex and queue
  bleDataMutex = xSemaphoreCreateMutex();
  if (bleDataMutex == NULL) {
    LOG_ERROR(TAG_TASK, "Failed to create bleDataMutex!");
    while(1) delay(1000);  // Halt - critical failure
  }

  bleCommandQueue = xQueueCreate(10, sizeof(BLECommandMessage));
  if (bleCommandQueue == NULL) {
    LOG_ERROR(TAG_TASK, "Failed to create bleCommandQueue!");
    while(1) delay(1000);  // Halt - critical failure
  }

  uiUpdateQueue = xQueueCreate(20, sizeof(UIUpdateMessage));
  if (uiUpdateQueue == NULL) {
    LOG_ERROR(TAG_TASK, "Failed to create uiUpdateQueue!");
    while(1) delay(1000);  // Halt - critical failure
  }

  // Create BLE task on Core 0 (BLE/WiFi core)
  LOG_INFO(TAG_TASK, "Creating BLE task on Core 0...");

  xTaskCreatePinnedToCore(
      bleTaskFunction,       // Task function
      "BLE_Task",            // Task name
      20480,                 // Stack size (20KB - increased from 16KB)
      NULL,                  // Parameters
      2,                     // Priority (higher than main loop = 1)
      &bleTaskHandle,        // Task handle
      0                      // Core 0 (BLE/WiFi core)
  );

  // Wait for BLE task to print its startup message (avoid serial collision)
  vTaskDelay(pdMS_TO_TICKS(500));

  LOG_INFO(TAG_TASK, "BLE task created successfully, main loop on Core %d", xPortGetCoreID());
}

// -----------------------------------------------------------------------------
// BLE Task - Runs on Core 0 (BLE/WiFi Core)
// -----------------------------------------------------------------------------

// Process commands from main loop
void processBLECommand(BLECommandMessage& cmd)
{
    switch (cmd.command)
    {
        case BLE_CMD_TARE:
            LOG_DEBUG(TAG_TASK, "Command: TARE");
            bleSequenceState = BLE_SEND_TARE;
            bleSequenceInProgress = true;
            break;

        case BLE_CMD_START_TIMER:
            LOG_DEBUG(TAG_TASK, "Command: START_TIMER");
            bleSequenceState = BLE_SEND_START;
            bleSequenceInProgress = true;
            break;

        case BLE_CMD_STOP_TIMER:
            LOG_DEBUG(TAG_TASK, "Command: STOP_TIMER");
            // Implement stop logic if needed
            break;

        case BLE_CMD_RESET_TIMER:
            LOG_DEBUG(TAG_TASK, "Command: RESET_TIMER");
            bleSequenceState = BLE_SEND_RESET;
            bleSequenceInProgress = true;
            break;

        case BLE_CMD_DISCONNECT:
            LOG_DEBUG(TAG_TASK, "Command: DISCONNECT");
            // scale.disconnect();  // Implement if needed
            break;

        case BLE_CMD_FORCE_RECONNECT:
            LOG_DEBUG(TAG_TASK, "Command: FORCE_RECONNECT");
            // Force reconnection logic
            break;
    }
}

// BLE Task Function - Runs continuously on Core 0
void bleTaskFunction(void* parameter)
{
    // Get core ID and verify correct assignment
    uint8_t coreID = xPortGetCoreID();

    // Wait to avoid serial collision with setup()
    vTaskDelay(pdMS_TO_TICKS(200));

    LOG_INFO(TAG_TASK, "=================================");
    LOG_INFO(TAG_TASK, "Running on Core: %d", coreID);

    if (coreID != 0) {
        LOG_WARN(TAG_TASK, "WARNING: Should be on Core 0, but running on Core 1!");
        LOG_WARN(TAG_TASK, "Task pinning may have failed!");
    } else {
        LOG_INFO(TAG_TASK, "Core assignment correct!");
    }

    // Check initial stack size
    UBaseType_t stackSize = uxTaskGetStackHighWaterMark(NULL);
    LOG_INFO(TAG_TASK, "Stack allocated: 20KB, available: %u bytes", stackSize);
    LOG_INFO(TAG_TASK, "=================================");

    // CRITICAL: Add this BLE task to the watchdog
    // This task MUST call esp_task_wdt_reset() regularly or system will reboot
    esp_task_wdt_add(NULL);  // Add current task (BLE task) to watchdog
    LOG_INFO(TAG_TASK, "BLE task added to watchdog monitor");

    // Track stack usage and heap monitoring
    unsigned long lastStackCheck = 0;
    unsigned long lastHeapLog = 0;
    const unsigned long HEAP_LOG_INTERVAL_MS = 30000;  // Log heap every 30 seconds

    while (true)
    {
        // Reset watchdog and track timing
        esp_task_wdt_reset();
        bleTaskWDTResets++;

        // Log watchdog reset statistics every 5 seconds
        unsigned long now = millis();
        if (now - lastBLEWDTLog > WDT_LOG_INTERVAL_MS) {
            LOG_DEBUG(TAG_TASK, "BLE Task WDT: %lu resets, last reset %lums ago",
                      bleTaskWDTResets, now - lastBLEWDTLog);
            lastBLEWDTLog = now;
        }

        // Log heap status every 30 seconds (detects memory leaks from BLE scanning)
        if (now - lastHeapLog > HEAP_LOG_INTERVAL_MS) {
            // Get INTERNAL DRAM heap (not PSRAM) - this is what gets exhausted during BLE scanning
            size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            size_t min_free_internal = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            size_t largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

            // Always log heap status (changed from DEBUG to WARN for visibility)
            LOG_WARN(TAG_TASK, "💾 Internal DRAM: free=%u, min_ever=%u, largest=%u bytes",
                     free_internal, min_free_internal, largest_block);

            // Progressive warning levels
            if (free_internal < 100000) {
                LOG_ERROR(TAG_TASK, "🔴 CRITICAL: Internal heap = %u bytes (< 100KB!)", free_internal);
            }
            if (free_internal < 50000) {
                LOG_ERROR(TAG_TASK, "💀 FATAL: Internal heap = %u bytes (< 50KB! CRASH IMMINENT!)", free_internal);
            }
            if (largest_block < 20000) {
                LOG_ERROR(TAG_TASK, "⚠️  Heap fragmentation severe! Largest block = %u bytes", largest_block);
            }

            lastHeapLog = now;
        }

        // Process commands from main loop (non-blocking check)
        BLECommandMessage cmd;
        if (xQueueReceive(bleCommandQueue, &cmd, 0) == pdTRUE) {
            processBLECommand(cmd);
        }

        // CRITICAL FIX: Always call update() to drive state machine
        // ArduinoBLE doesn't have update() - init() is blocking, no state machine
        // scale.update();  // Not needed for ArduinoBLE

        // Check scale status and manage connection
        checkScaleStatus();

        // Send heartbeat to keep connection alive
        checkHeartBreat();

        // Handle BLE command sequence (tare, start timer, etc.)
        handleBLESequence();

        // Update weight readings
        updateScaleReadings();

        // Update shot timer (independent 0.1s updates)
        updateShotTimer();

        // Handle shot watchdogs
        handleShotWatchdogs();

        // Update shared data for main loop
        // ArduinoBLE doesn't have isConnecting() - always false (blocking connection)
        updateSharedConnectionStatus(scale.isConnected(), false);
        updateSharedWeight(currentWeight);

        // Monitor BLE task stack usage every 10 seconds (detect stack overflow)
        if (millis() - lastStackCheck > 10000) {
            UBaseType_t stackLeft = uxTaskGetStackHighWaterMark(NULL);

            // Always log stack status (changed from VERBOSE to WARN for visibility)
            LOG_WARN(TAG_TASK, "📚 BLE Task Stack: %u bytes remaining (of 20480 total)", stackLeft);

            // Progressive stack overflow warnings
            if (stackLeft < 5000) {
                LOG_ERROR(TAG_TASK, "🔴 LOW STACK WARNING! Only %u bytes remaining!", stackLeft);
            }
            if (stackLeft < 2000) {
                LOG_ERROR(TAG_TASK, "🚨 STACK OVERFLOW IMMINENT! Only %u bytes left!", stackLeft);
            }

            lastStackCheck = millis();
        }

        // Small delay to prevent task starvation (10ms)
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// Update UI with data from BLE task (runs on Core 1)
// NOTE: Weight/timer/status updates use message queue (processUIUpdates)
// This function reads shared data for global variables (currentWeight, connection status)
void updateUIWithBLEData()
{
    static unsigned long lastUIUpdate = 0;
    static bool lastKnownConnectionState = false;

    if (millis() - lastUIUpdate < 50) return;  // Update UI every 50ms max

    // Read shared data from BLE task
    if (xSemaphoreTake(bleDataMutex, pdMS_TO_TICKS(5))) {
        // CRITICAL: Update currentWeight for shot logic and touch handlers
        currentWeight = bleData.currentWeight;

        bool connected = bleData.isConnected;
        xSemaphoreGive(bleDataMutex);

        // Only queue connection update if state changed (avoid flooding queue)
        if (connected != lastKnownConnectionState) {
            queueConnectionUpdate(connected);
            lastKnownConnectionState = connected;
        }
    }

    lastUIUpdate = millis();
}

// Main Loop - UI Operations Only (Core 1)
void loop()
{
  // Reset watchdog and track timing
  esp_task_wdt_reset();
  mainLoopWDTResets++;

  // Log watchdog reset statistics every 5 seconds
  unsigned long now = millis();
  if (now - lastMainWDTLog > WDT_LOG_INTERVAL_MS) {
    LOG_DEBUG(TAG_TASK, "Main Loop WDT: %lu resets, last reset %lums ago",
              mainLoopWDTResets, now - lastMainWDTLog);
    lastMainWDTLog = now;
  }

  // DIAGNOSTIC: Log main loop heartbeat every 10 seconds to detect freezes
  static unsigned long lastLoopLog = 0;
  if (millis() - lastLoopLog > 10000) {
    LOG_DEBUG(TAG_TASK, "Main loop heartbeat (Core 1 alive)");
    lastLoopLog = millis();
  }

  // ========================================================================
  // UI OPERATIONS ONLY - All BLE operations moved to BLE task on Core 0!
  // ========================================================================

  // LVGL UI updates (CRITICAL - must run frequently)
  LVGLTimerHandlerRoutine();

  // DIAGNOSTIC: LVGL heartbeat to detect display freezes
  static unsigned long lastLVGLLog = 0;
  if (millis() - lastLVGLLog > 5000) {  // Every 5 seconds
    LOG_DEBUG(TAG_UI, "LVGL heartbeat - display active (asleep=%d) | WDT: Main=%lu BLE=%lu",
              displayAsleep, mainLoopWDTResets, bleTaskWDTResets);
    lastLVGLLog = millis();
  }

  // Process queued UI updates from BLE task (professional thread-safe pattern)
  processUIUpdates();

  // Update connection status from BLE task (polls shared memory)
  updateUIWithBLEData();

  // Display sleep/wake management (UI operation) - Disabled for production
  // Sleep mode can be re-enabled by uncommenting the block below
  // if (!displayAsleep && (millis() - lastTouchTime) > DISPLAY_SLEEP_TIMEOUT_MS)
  // {
  //   LOG_INFO(TAG_UI, "Display sleep timeout (5 min idle) - putting display to sleep");
  //   LOG_INFO(TAG_UI, "Touch screen to wake");
  //   lcd_sleep();
  //   displayAsleep = true;
  // }

  // Handle flushing cycle (relay control - UI related)
  handleFlushingCycle();

  // Process status message queue (UI updates)
  processPendingStatusQueue();

  // All BLE operations now run in bleTaskFunction() on Core 0:
  //   - scale.update()
  //   - checkScaleStatus()
  //   - checkHeartBreat()
  //   - handleBLESequence()
  //   - updateScaleReadings()
  //   - updateShotTimer()
  //   - handleShotWatchdogs()

  // UI Health Monitor - Detects blank display and unresponsive touch issues
  // This catches the problems that standard logging misses
  static unsigned long lastUIHealthCheck = 0;
  static unsigned long lastFlushDetected = 0;
  static unsigned long lastTouchEventRecorded = 0;  // Will be updated from my_touchpad_read()
  static bool uiHealthWarningShown = false;

  if (millis() - lastUIHealthCheck > 30000) {  // Check every 30 seconds
    unsigned long now = millis();
    bool displayProblem = false;

    // Check 1: Display flush happening? (even idle should flush occasionally)
    // lastFlushTimestamp is updated in my_disp_flush() callback
    if (lastFlushTimestamp > 0 && (now - lastFlushTimestamp) > 120000) {  // No flush for 2 minutes
      LOG_ERROR(TAG_UI, "🚨 UI HEALTH: No display flush in 120s!");
      displayProblem = true;
    }

    // Check 2: LVGL timer handler running?
    if ((now - lastLVGLTimerCall) > 5000) {
      LOG_ERROR(TAG_UI, "🚨 UI HEALTH: LVGL timer handler stopped for 5s!");
      displayProblem = true;
    }

    // Check 3: Display driver still registered?
    lv_disp_t* disp = lv_disp_get_default();
    if (!disp || !disp->driver) {
      LOG_ERROR(TAG_UI, "🚨 UI HEALTH: LVGL display driver MISSING!");
      displayProblem = true;
    }

    // Check 4: Touch responsiveness (only warn if touch is expected but not working)
    // NOTE: We don't warn about no touch - user may simply not be touching screen
    // This is just for logging/debugging purposes

    // ===== DIAGNOSTIC: LVGL Object Tree Validation =====
    // Check if screen object and UI widgets still exist
    if (disp != NULL) {
      lv_obj_t* screen = lv_disp_get_scr_act(disp);
      if (screen != NULL) {
        uint32_t child_count = lv_obj_get_child_cnt(screen);
        static uint32_t last_child_count = 0;

        // Detect if widgets disappeared (object count dropped)
        if (last_child_count > 0 && child_count < last_child_count) {
          LOG_ERROR(TAG_UI, "🚨 WIDGETS DISAPPEARED! Count: %lu → %lu (lost %lu objects)",
                   last_child_count, child_count, last_child_count - child_count);
          displayProblem = true;
        }

        // Log object count every 5 checks (every 150 seconds)
        static uint8_t object_check_counter = 0;
        object_check_counter++;
        if (object_check_counter >= 5) {
          LOG_INFO(TAG_UI, "📊 LVGL Object Tree: %lu child objects on screen", child_count);
          object_check_counter = 0;
        }

        last_child_count = child_count;
      } else {
        LOG_ERROR(TAG_UI, "🚨 Screen object is NULL!");
        displayProblem = true;
      }
    }
    // ===== END LVGL OBJECT TREE VALIDATION =====

    // ===== DIAGNOSTIC: PSRAM Heap Monitoring =====
    // Monitor PSRAM free space (icons/images stored in PSRAM buffers)
    size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t psram_min = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);
    static uint8_t psram_check_counter = 0;
    psram_check_counter++;
    if (psram_check_counter >= 3) {  // Every 90 seconds
      LOG_INFO(TAG_UI, "💾 PSRAM Heap: free=%zu KB, min_ever=%zu KB",
               psram_free / 1024, psram_min / 1024);
      psram_check_counter = 0;
    }
    // ===== END PSRAM MONITORING =====

    // Periodic status logging
    LOG_DEBUG(TAG_UI, "UI Health: Flush=%lums ago, Timer=%lums ago, Touch=%lums ago",
              (lastFlushTimestamp > 0) ? (now - lastFlushTimestamp) : 999999,
              now - lastLVGLTimerCall,
              (lastTouchEvent > 0) ? (now - lastTouchEvent) : 999999);

    if (displayProblem && !uiHealthWarningShown) {
      LOG_ERROR(TAG_UI, "═════════════════════════════════════════════");
      LOG_ERROR(TAG_UI, "  🚨 UI SYSTEM FAILURE DETECTED!");
      LOG_ERROR(TAG_UI, "  Display may be blank or frozen");
      LOG_ERROR(TAG_UI, "  Possible causes:");
      LOG_ERROR(TAG_UI, "  - LVGL not initialized (check lv_timer_handler)");
      LOG_ERROR(TAG_UI, "  - Display driver failed");
      LOG_ERROR(TAG_UI, "  - SPI communication issue");
      LOG_ERROR(TAG_UI, "  Check serial logs from setup() for errors");
      LOG_ERROR(TAG_UI, "═════════════════════════════════════════════");
      uiHealthWarningShown = true;
    } else if (!displayProblem && uiHealthWarningShown) {
      LOG_INFO(TAG_UI, "✅ UI health recovered");
      uiHealthWarningShown = false;
    }

    lastUIHealthCheck = now;
  }

  // CRITICAL: Yield CPU to prevent main loop from starving BLE task
  // Without this, main loop runs at 56,000 Hz and blocks BLE task
  // This allows FreeRTOS scheduler to balance workload between cores
  delay(1);  // 1ms delay = ~1000 Hz max loop rate (plenty fast for UI)
}
