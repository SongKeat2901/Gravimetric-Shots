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
#include <AcaiaArduinoBLE.h>
#include "esp_task_wdt.h" // Task watchdog for auto-recovery from hangs
#include "nvs_flash.h"    // NVS initialization for WiFi+BLE coexistence
#include <Preferences.h>
#include <cstring>

// Power Management for LED control
#define XPOWERS_CHIP_SY6970
#include <XPowersLib.h>
XPowersPPM PMU;

// -----------------------------------------------------------------------------
// Display and Touch Configuration
// -----------------------------------------------------------------------------

static lv_disp_draw_buf_t draw_buf;
static lv_color_t *buf;
static lv_color_t *buf1;

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
constexpr int SHOT_HISTORY_CAP = 1000;

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
const unsigned long DISPLAY_SLEEP_TIMEOUT_MS = 300000;  // 5 minutes idle = sleep

// Thermal noise detection for touch controller
static uint32_t edgeTouchCount = 0;
static uint32_t lastEdgeTouchTime = 0;
static bool thermalSuppressionActive = false;
static uint32_t thermalSuppressionStart = 0;
static const uint32_t EDGE_TOUCH_WINDOW_MS = 5000;  // 5-second window for counting edge touches
static const uint32_t EDGE_TOUCH_THRESHOLD = 10;     // >10 edge touches = thermal noise
static const uint32_t THERMAL_SUPPRESSION_DURATION_MS = 10000;  // Suppress for 10 seconds

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

BLEService weightService("00002a98-0000-1000-8000-00805f9b34fb");
BLEByteCharacteristic weightCharacteristic("0x2A98", BLEWrite | BLERead);

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
constexpr uint32_t SCALE_INIT_RETRY_MS = 2000;
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
  DEBUG_PRINT("Relay1 -> ");
  DEBUG_PRINTLN(high ? "HIGH" : "LOW");
}

lv_obj_t *ui_cartext = nullptr;

// -----------------------------------------------------------------------------
// Helper Utilities
// -----------------------------------------------------------------------------

static inline void setStatusLabels(const char *text)
{
  if (strcmp(currentStatusText, text) == 0)  // Fixed: proper string comparison
    return;
  lv_label_set_text(ui_SerialLabel, text);
  lv_label_set_text(ui_SerialLabel1, text);
  strncpy(currentStatusText, text, sizeof(currentStatusText) - 1);  // Fixed: safe string copy
  currentStatusText[sizeof(currentStatusText) - 1] = '\0';  // Ensure null termination
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
    DEBUG_PRINTLN("Display: 60Hz (fast mode)");
  }
  else
  {
    // Normal refresh when idle (33ms = 30Hz) to save CPU/power
    lv_timer_set_period(refresh_timer, 33);
    DEBUG_PRINTLN("Display: 30Hz (normal mode)");
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

    char buffer[10];
    dtostrf(shot.shotTimer, 5, 1, buffer);
    lv_label_set_text(ui_TimerLabel, buffer);

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
      DEBUG_PRINTLN("BLE: Sending RESET");
      if (!scale.resetTimer())
      {
        queueScaleStatus("Scale reset failed");
        bleSequenceState = BLE_IDLE;
        bleSequenceInProgress = false;
        shot.brewing = false;
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
      DEBUG_PRINTLN("BLE: Sending TARE");
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
      DEBUG_PRINTLN("BLE: Sending START");
      if (!scale.startTimer())
      {
        queueScaleStatus("Scale timer failed");
        bleSequenceState = BLE_IDLE;
        bleSequenceInProgress = false;
        shot.brewing = false;
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

      DEBUG_PRINTLN("BLE: Starting shot - turning ON pump");
      setRelayState(true);              // Turn ON pump (relay)
      updateDisplayRefreshRate(true);   // 60Hz display mode

      bleSequenceState = BLE_IDLE;
      DEBUG_PRINTLN("Shot started successfully!");
      break;
  }
}

static void setBrewingState(bool brewing)
{
  if (brewing)
  {
    if (isFlushing)
    {
      DEBUG_PRINTLN("Flushing cancelled due to brew start");
      setRelayState(false);
      isFlushing = false;
    }

    if (!scale.isConnected())
    {
      queueScaleStatus("Scale not connected");
      shot.brewing = false;
      isFlushing   = false;
      return;
    }

    DEBUG_PRINTLN("Shot start requested - triggering BLE sequence");

    // Trigger non-blocking BLE command sequence
    // Sequence: reset → tare → start → pump ON (handled by handleBLESequence)
    bleSequenceInProgress = true;
    bleSequenceState = BLE_SEND_RESET;
    // Note: shot.brewing will be set to true in BLE_START_SHOT state after commands complete
  }
  else
  {
    DEBUG_PRINTLN("ShotEnded");
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
    DEBUG_PRINTLN("Relay corrected to HIGH");
    setRelayState(true);
  }
  else if (!shouldBeHigh && actualState == HIGH)
  {
    DEBUG_PRINTLN("Relay was HIGH unexpectedly, forcing LOW");
    setRelayState(false);
  }
}

// -----------------------------------------------------------------------------
// Display & Touch Callbacks
// -----------------------------------------------------------------------------

void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
{
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);

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
  uint8_t buff[20] = {0};
  static bool haveLastPoint        = false;
  static lv_coord_t lastPointX     = 0;
  static lv_coord_t lastPointY     = 0;
  static constexpr uint8_t filterDepth = 2;
  static lv_coord_t historyX[filterDepth] = {0};
  static lv_coord_t historyY[filterDepth] = {0};
  static uint8_t historyCount      = 0;
  static uint8_t historyWriteIndex = 0;

  Wire.beginTransmission(0x3B);
  Wire.write(read_touchpad_cmd, 8);
  Wire.endTransmission();
  Wire.requestFrom(0x3B, 8);
  while (!Wire.available())
    ;
  Wire.readBytes(buff, 8);

  uint16_t rawX = AXS_GET_POINT_X(buff, 0);
  uint16_t rawY = AXS_GET_POINT_Y(buff, 0);
  uint16_t type = AXS_GET_GESTURE_TYPE(buff);

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
          DEBUG_PRINTLN("⚠️ THERMAL NOISE DETECTED - Suppressing left edge touches for 10s");
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
        DEBUG_PRINTLN("✓ Thermal suppression ended");
      }
      // Suppress left edge touches during thermal event
      else if (rawY < 50)
      {
        DEBUG_PRINTLN("Left edge touch suppressed (thermal mode)");
        data->state = LV_INDEV_STATE_REL;
        return;
      }
    }
    // ===== END THERMAL PROTECTION =====

    // Wake display if asleep (touch-to-wake functionality)
    if (displayAsleep)
    {
      DEBUG_PRINTLN("Touch detected - waking display");
      lcd_wake();

      // Restore backlight to saved brightness level
      int LCDBrightness = map(brightness, 0, 100, 70, 256);
      analogWrite(TFT_BL, LCDBrightness);
      DEBUG_PRINT("Backlight restored to ");
      DEBUG_PRINTLN(brightness);

      displayAsleep = false;
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

    char buf[20] = {0};
    sprintf(buf, "(%d, %d)", data->point.x, data->point.y);
    if (ui_cartext != nullptr)
      lv_label_set_text(ui_cartext, buf);
  }
  else
  {
    data->state = LV_INDEV_STATE_REL;
    haveLastPoint     = false;
    historyCount      = 0;
    historyWriteIndex = 0;
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
        DEBUG_PRINTLN("Flush: Rejected edge touch (X < 5px)");
        pressedAt = 0;  // Invalidate press
        return;
      }
    }

    DEBUG_PRINTLN("Flush button PRESSED");
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

      DEBUG_PRINT("Flush activated (");
      DEBUG_PRINT(millis() - pressedAt);
      DEBUG_PRINTLN("ms press)");

      flushingFeature();
    }
    pressedAt = 0;  // Reset
  }
  else if (event_code == LV_EVENT_CLICKED && pressedAt > 0)
  {
    DEBUG_PRINT("Flush REJECTED - press too short (");
    DEBUG_PRINT(millis() - pressedAt);
    DEBUG_PRINTLN("ms)");
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
    DEBUG_PRINT("Brightness value saved @ ");
    DEBUG_PRINTLN(brightnessValue);
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
  DEBUG_PRINTLN("Flushing started");
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

  if (!ui_BluetoothImage1 || !ui_BluetoothImage2 || !ui_SerialLabel)
  {
    lastScaleConnected = connected;
    return;
  }

  if (!connected)
  {
    _ui_state_modify(ui_BluetoothImage1, LV_STATE_DISABLED, _UI_MODIFY_STATE_ADD);
    _ui_state_modify(ui_BluetoothImage2, LV_STATE_DISABLED, _UI_MODIFY_STATE_ADD);

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

    // Non-blocking connection state machine
    if (!scale.isConnecting())
    {
      // Not currently connecting, can start new attempt
      if (!isFlushing && ((now - lastScaleInitAttempt) >= SCALE_INIT_RETRY_MS || lastScaleInitAttempt == 0))
      {
        scale.init();  // Starts state machine, returns immediately
        lastScaleInitAttempt = now;
      }
    }
    else
    {
      // Connection in progress, update state machine
      if (scale.update())
      {
        // Connection successful!
        Serial.println("checkScaleStatus: Scale fully connected!");
        queueScaleStatus("Scale connected");
        firstConnectionNotificationPending = true;
      }
      // If update() returns false and state is FAILED, will retry on next loop
    }

    currentWeight = 0;
    firstConnectionNotificationPending = true;

    if (shot.brewing)
    {
      stopBrew(false, SCALE_DISCONNECTED);
    }
  }
  else
  {
    _ui_state_modify(ui_BluetoothImage1, LV_STATE_DISABLED, _UI_MODIFY_STATE_REMOVE);
    _ui_state_modify(ui_BluetoothImage2, LV_STATE_DISABLED, _UI_MODIFY_STATE_REMOVE);

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

    DEBUG_PRINT("Flushing... ");
    DEBUG_PRINT(remaining / 1000);
    DEBUG_PRINTLN(" seconds remaining");

    char labelBuf[48];
    snprintf(labelBuf, sizeof(labelBuf), "Flushing... %lu seconds remaining", remaining / 1000);
    setStatusLabels(labelBuf);
  }

  if (elapsed >= flushDuration)
  {
    setRelayState(false);
    isFlushing = false;
    DEBUG_PRINTLN("Flushing ended");
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
  // Check newWeightAvailable FIRST - it detects timeouts and updates connection state
  // Then check isConnected to see if we should process the weight
  if (!scale.newWeightAvailable() || !scale.isConnected())
    return;

  currentWeight = scale.getWeight();

  char buffer[10];
  dtostrf(currentWeight, 5, 1, buffer);
  lv_label_set_text(ui_ScaleLabel, buffer);

  DEBUG_PRINT(currentWeight);

  if (!shot.brewing)
  {
    DEBUG_PRINTLN("");
    return;
  }

  if (shot.datapoints >= SHOT_HISTORY_CAP)
  {
    // Clamp FIRST to prevent out-of-bounds access if datapoints > SHOT_HISTORY_CAP
    shot.datapoints = SHOT_HISTORY_CAP - 1;

    // Now safe to shift array (discard oldest datapoint)
    std::memmove(shot.time_s,   shot.time_s + 1,   shot.datapoints * sizeof(float));
    std::memmove(shot.weight,   shot.weight + 1,   shot.datapoints * sizeof(float));

    DEBUG_PRINTLN("Shot history buffer full, oldest datapoint discarded");
  }

  const float nowSeconds = seconds_f() - shot.start_timestamp_s;
  shot.time_s[shot.datapoints] = nowSeconds;
  shot.weight[shot.datapoints] = currentWeight;
  shot.shotTimer                = nowSeconds;
  shot.datapoints++;

  DEBUG_PRINT(" ");
  DEBUG_PRINT(shot.shotTimer);

  // Timer display now updated independently by updateShotTimer() function

  calculateEndTime(&shot);
  DEBUG_PRINT(" ");
  DEBUG_PRINT(shot.expected_end_s);

  char labelBuf[40];
  snprintf(labelBuf, sizeof(labelBuf), "Expected end time @ %.1f s", shot.expected_end_s);
  setStatusLabels(labelBuf);

  DEBUG_PRINTLN("");
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
    DEBUG_PRINTLN("Max brew duration reached");
    setStatusLabels("Max brew duration reached");
    stopBrew(false, TIME_EXCEEDED);
  }

  if (shot.brewing && shot.shotTimer >= shot.expected_end_s && shot.shotTimer > MIN_SHOT_DURATION_S)
  {
    DEBUG_PRINTLN("weight achieved");
    setStatusLabels("Weight achieved");
    stopBrew(false, WEIGHT_ACHIEVED);
  }

  if (shot.start_timestamp_s && shot.end_s && currentWeight >= (goalWeight - weightOffset) &&
      seconds_f() > shot.start_timestamp_s + shot.end_s + DRIP_DELAY_S)
  {
    shot.start_timestamp_s = 0;
    shot.end_s             = 0;

    DEBUG_PRINT("I detected a final weight of ");
    DEBUG_PRINT(currentWeight);
    DEBUG_PRINT("g. The goal was ");
    DEBUG_PRINT(goalWeight);
    DEBUG_PRINT("g with a negative offset of ");
    DEBUG_PRINT(weightOffset);

    if (abs(currentWeight - goalWeight + weightOffset) > MAX_OFFSET)
    {
      DEBUG_PRINT("g. Error assumed. Offset unchanged. ");
      setStatusLabels("Error assumed. Offset unchanged.");
    }
    else
    {
      DEBUG_PRINT("g. Next time I'll create an offset of ");
      weightOffset += currentWeight - goalWeight;
      DEBUG_PRINT(weightOffset);
      saveOffset(static_cast<int>(weightOffset * 10.0f));
    }

    DEBUG_PRINTLN("");
  }
}

extern uint32_t transfer_num;
extern size_t lcd_PushColors_len;

void LVGLTimerHandlerRoutine()
{
  if (transfer_num <= 0 && lcd_PushColors_len <= 0)
    lv_timer_handler();

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
  // Initialize NVS (Non-Volatile Storage) FIRST - before WiFi or BLE
  // This prevents both radios from trying to initialize NVS independently
  // Critical for WiFi+BLE coexistence on ESP32-S3
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();
    nvs_flash_init();
  }

  DEBUG_INIT();  // Initializes Serial (production) or WiFi+WebSerial (debug builds)
  delay(500); // Reduced from 5000ms - just enough for serial init, won't cause black screen on direct power

  DEBUG_PRINTLN("=== Gravimetric Shots Initializing ===");

  // Initialize PMU (Power Management Unit) for LED control
  // T-Display-S3-Long uses SY6970 on I2C pins: SDA=15, SCL=10 (same as touch)
  Wire.begin(TOUCH_IICSDA, TOUCH_IICSCL);  // Touch controller pins (also used for PMU)
  Wire.setClock(100000);  // 100kHz for reliability under thermal stress
  if (PMU.init(Wire, TOUCH_IICSDA, TOUCH_IICSCL, SY6970_SLAVE_ADDRESS))
  {
    PMU.disableStatLed();  // Turn off green charging indicator LED
    DEBUG_PRINTLN("PMU initialized, status LED disabled");
  }
  else
  {
    DEBUG_PRINTLN("PMU init failed (LED control unavailable)");
  }

  // Initialize task watchdog (10 second timeout) - auto-reboot on hang
  esp_task_wdt_init(10, true);  // 10s timeout, panic & reboot on trigger
  esp_task_wdt_add(NULL);       // Add current task to watchdog
  DEBUG_PRINTLN("Task watchdog enabled (10s timeout)");

  preferences.begin("myApp", false);                       // Open the preferences with a namespace and read-only flag
  brightness = preferences.getInt(BRIGHTNESS_KEY, 0);      // Read the brightness value from preferences
  goalWeight = preferences.getInt(WEIGHT_KEY, 0);          // Read the target weight value from preferences
  weightOffset = preferences.getInt(OFFSET_KEY, 0) / 10.0; // Read the offset value from preferences
  preferences.end();                                       // Close the preferences

  DEBUG_PRINT("Brightness read from preferences: ");
  DEBUG_PRINTLN(brightness);
  DEBUG_PRINT("Goal Weight retrieved: ");
  DEBUG_PRINTLN(goalWeight);
  DEBUG_PRINT("Offset retrieved: ");
  DEBUG_PRINTLN(weightOffset);

  if ((goalWeight < 10) || (goalWeight > 200)) // If preferences isn't initialized and has an unreasonable weight/offset, default to 36g/1.5g
  {
    goalWeight = 36;
    char debugBuf[64];
    snprintf(debugBuf, sizeof(debugBuf), "Goal Weight set to: %d g", goalWeight);
    DEBUG_PRINTLN(debugBuf);
  }

  if (weightOffset > MAX_OFFSET)
  {
    weightOffset = 1.5;
    char debugBuf[64];
    snprintf(debugBuf, sizeof(debugBuf), "Offset set to: %.1f g", weightOffset);
    DEBUG_PRINTLN(debugBuf);
  }

  if ((brightness < 0) || (brightness > 100)) // If preferences isn't initialized set brightness to 50%
  {
    brightness = 50;
    char debugBuf[64];
    snprintf(debugBuf, sizeof(debugBuf), "Backlight set to: Default @ %d %%", brightness);
    DEBUG_PRINTLN(debugBuf);
  }

  // initialize the GPIO hardware
  // To add in progress
  pinMode(RELAY1, OUTPUT); // RELAY 1 Output
  relayState = true; // force update on first set
  setRelayState(false);

  // initialize the Bluetooth® Low Energy hardware
  BLE.begin();
  BLE.setLocalName("shotStopper");
  BLE.setAdvertisedService(weightService);
  weightService.addCharacteristic(weightCharacteristic);
  BLE.addService(weightService);
  weightCharacteristic.writeValue(36);
  BLE.advertise();
  DEBUG_PRINTLN("Bluetooth® device active, waiting for connections...");

  pinMode(TOUCH_RES, OUTPUT);
  digitalWrite(TOUCH_RES, HIGH);
  delay(2);
  digitalWrite(TOUCH_RES, LOW);
  delay(10);
  digitalWrite(TOUCH_RES, HIGH);
  delay(2);

  Wire.begin(TOUCH_IICSDA, TOUCH_IICSCL);
  Wire.setClock(100000);  // 100kHz for reliability under thermal stress

  pinMode(TFT_BL, OUTPUT);    // initialized TFT Backlight Pin as output
  digitalWrite(TFT_BL, LOW);  // Keep backlight OFF during initialization to prevent noise/garbage display

  axs15231_init(); // initialized Screen

  lv_init(); // initialized LVGL
  // Display init code
  {
    size_t buffer_pixels = EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES;
    size_t buffer_size = sizeof(lv_color_t) * buffer_pixels;
    buf = (lv_color_t *)ps_malloc(buffer_size);
    if (buf == NULL)
    {
      while (1)
      {
        DEBUG_PRINTLN("buf NULL");
        delay(500);
      }
    }

    buf1 = (lv_color_t *)ps_malloc(buffer_size);
    if (buf1 == NULL)
    {
      while (1)
      {
        DEBUG_PRINTLN("buf NULL");
        delay(500);
      }
    }

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
  ui_init(); // initialized LVGL UI intereface

  // Force LVGL to render the first frame before turning on backlight
  // This prevents showing uninitialized frame buffer (noise/garbage)
  for (int i = 0; i < 10; i++) {
    lv_task_handler();  // Process LVGL tasks to render UI
    delay(5);           // Small delay to allow rendering to complete
  }

  // NOW turn on backlight - UI is fully rendered and ready
  int LCDBrightness = map(brightness, 0, 100, 70, 256); // brightness value is 0~100% have to map it out to pwm value.
  analogWrite(TFT_BL, LCDBrightness);

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


  DEBUG_PRINT("Flash size: "); DEBUG_PRINT(ESP.getFlashChipSize()); DEBUG_PRINTLN(" bytes");
  DEBUG_PRINT("App partition: "); DEBUG_PRINT(ESP.getSketchSize()); DEBUG_PRINT(" used / "); DEBUG_PRINT(ESP.getSketchSize() + ESP.getFreeSketchSpace()); DEBUG_PRINTLN(" bytes total");
  DEBUG_PRINT("Heap total: "); DEBUG_PRINT(ESP.getHeapSize()); DEBUG_PRINT(" bytes, free: "); DEBUG_PRINT(ESP.getFreeHeap()); DEBUG_PRINTLN(" bytes");
  DEBUG_PRINT("PSRAM total: "); DEBUG_PRINT(ESP.getPsramSize()); DEBUG_PRINT(" bytes, free: "); DEBUG_PRINT(ESP.getFreePsram()); DEBUG_PRINTLN(" bytes");
  DEBUG_PRINTLN("Setup Completed");

  // Initialize touch time tracking
  lastTouchTime = millis();
}

void loop()
{
  esp_task_wdt_reset();  // Reset watchdog at start of every loop iteration

  // Update scale connection state machine if connecting
  if (scale.isConnecting())
  {
    scale.update();  // Non-blocking - returns immediately after state update
  }

  LVGLTimerHandlerRoutine();

  // Check for display sleep timeout (5 minutes of inactivity)
  if (!displayAsleep && (millis() - lastTouchTime) > DISPLAY_SLEEP_TIMEOUT_MS)
  {
    DEBUG_PRINTLN("Display sleep timeout - putting display to sleep");
    lcd_sleep();
    displayAsleep = true;
  }
  checkScaleStatus();
  checkHeartBreat();
  handleFlushingCycle();
  handleBLESequence();    // Non-blocking BLE command sequencer
  updateScaleReadings();
  updateShotTimer();      // Independent timer update every 0.1s
  handleShotWatchdogs();
  processPendingStatusQueue();
}
