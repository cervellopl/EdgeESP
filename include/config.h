#pragma once
#include <Arduino.h>

// ---------------------------------------------------------------------------
// EdgeESP pin map - ESP32-S3-WROOM-1 (quad SPI flash/PSRAM so GPIO33..37 are free)
//
// The MCUFRIEND 3.6" shield is a hardwired 16-bit 8080 panel, so the data bus
// eats 16 GPIOs before anything else. Everything below is chosen around that.
// ---------------------------------------------------------------------------

// --- LCD 16-bit parallel data bus -----------------------------------------
#define LCD_D0   9
#define LCD_D1  10
#define LCD_D2  11
#define LCD_D3  12
#define LCD_D4  13
#define LCD_D5  14
#define LCD_D6  15
#define LCD_D7  16
#define LCD_D8  17
#define LCD_D9  18
#define LCD_D10 21
#define LCD_D11 33
#define LCD_D12 34
#define LCD_D13 35
#define LCD_D14 36
#define LCD_D15 37

// --- LCD control ----------------------------------------------------------
#define LCD_WR   8
#define LCD_RD  47   // needed only for the panel-ID read; tie to 3V3 via 10k if unused
#define LCD_RS   7   // a.k.a. D/C
#define LCD_CS   6
#define LCD_RST  5
#define LCD_BL  48   // backlight, PWM via LEDC

#define LCD_BUS_FREQ_HZ 20000000   // 20 MHz write clock; drop to 10 MHz on long wiring

// --- u-blox GPS (UART1) ---------------------------------------------------
#define GPS_RX_PIN   1   // ESP32 input  <- GPS TX
#define GPS_TX_PIN   2   // ESP32 output -> GPS RX
#define GPS_PPS_PIN 42   // optional 1PPS, -1 to disable
#define GPS_BAUD_TARGET 115200

// --- microSD (the shield's own slot, on JP4) ------------------------------
#define SD_SCK_PIN  39
#define SD_MISO_PIN 40
#define SD_MOSI_PIN 41
#define SD_CS_PIN   38

// --- I2C (BME280 barometer/thermometer, optional fuel gauge) --------------
#define I2C_SDA_PIN 43
#define I2C_SCL_PIN 44
#define BME280_ADDR 0x76

// --- Analog ---------------------------------------------------------------
#define BTN_ADC_PIN   4   // 5-button resistor ladder
#define VBAT_ADC_PIN  3   // 100k/100k divider off the battery
#define VBAT_DIVIDER  2.0f

// --- Optional magnetometer (QMC5883L @0x0D or HMC5883L @0x1E) -------------
// Shares the I2C bus with the barometer. Without it the compass page still
// works, but only from GPS course - which means only while actually moving.
#define COMPASS_ENABLE          1
// Rotate the sensor's zero to match how the board sits in the case. If the
// compass reads 90 degrees off, put 90 here.
#define COMPASS_MOUNT_OFFSET_DEG  0.0f
// Magnetic declination, degrees east. GPS bearings are true north, so without
// this the needle and the bearing-to-start disagree by exactly this much.
// Central Poland is about +6.2 in 2026; look yours up at magnetic-declination.com
#define MAG_DECLINATION_DEG     6.2f

// --- Misc -----------------------------------------------------------------
#define BUZZER_PIN    0   // shares the BOOT button pad; set -1 if unused

// ---------------------------------------------------------------------------
// Behaviour tunables
// ---------------------------------------------------------------------------
#define GPS_NAV_RATE_HZ       5
#define REC_INTERVAL_MS    1000     // one FIT record per second, like Garmin "1s" mode

#define AUTOPAUSE_ON            true
#define AUTOPAUSE_SPEED_MPS     0.8f    // ~2.9 km/h
#define AUTOPAUSE_RESUME_MPS    1.4f
#define MOVING_SPEED_MPS        0.8f

#define AUTOLAP_ON              true
#define AUTOLAP_DISTANCE_M      5000.0f

#define WHEEL_CIRCUMFERENCE_MM  2105   // 700x25c; used for BLE speed sensor
// Kept here so Settings can range-check the stored index without linking the
// drivetrain table. A static_assert in Drivetrain.cpp keeps the two in step.
#define DRIVETRAIN_PRESET_COUNT    8
#define DRIVETRAIN_DEFAULT         0
#define RIDER_WEIGHT_KG         78.0f
#define BIKE_WEIGHT_KG          9.0f
#define RIDER_FTP_W              200   // functional threshold power, W
#define RIDER_LTHR_BPM           165   // lactate threshold heart rate, for hrTSS
#define RIDER_CDA               0.32f  // drag area, hoods on a road bike
#define RIDER_CRR               0.005f // rolling resistance, 25 mm tyre on tarmac

// --- weather, pushed from the phone ---------------------------------------
#define WEATHER_MAX_HOURS         12   // hourly forecast slots kept
#define WEATHER_STALE_MIN         45   // greyed out once this old
#define WEATHER_RAIN_ALERT_MM   0.3f   // an hour wetter than this counts as rain
#define WEATHER_RAIN_ALERT_MIN    75   // warn about rain arriving within this

#define BATT_FULL_MV          4180
#define BATT_EMPTY_MV         3300
#define BATT_CRITICAL_MV      3400
#define BATT_WARN_PCT           20   // first warning
#define BATT_WARN_LOW_PCT       10   // second, and the backlight comes down
#define BATT_WARN_HYSTERESIS_PCT 5   // climb this far clear before warning again

#define BACKLIGHT_DEFAULT       160    // 0..255
#define BACKLIGHT_DIM            30
#define BACKLIGHT_DIM_AFTER_MS  60000UL
#define SLEEP_AFTER_IDLE_MS   600000UL // 10 min with no ride and no buttons

#define TRACK_BUFFER_POINTS   2048     // breadcrumb ring buffer for the map page

// --- course following ------------------------------------------------------
#define COURSE_DIR              "/courses"
#define COURSE_MAX_POINTS       8000    // ~190 KB in PSRAM
#define COURSE_MAX_CUES          200
#define COURSE_MIN_SPACING_M      5.0f  // drop GPX points closer than this
#define OFF_COURSE_THRESHOLD_M   40.0f  // cross-track distance that counts as "off"
#define OFF_COURSE_HOLD_S           8   // ...sustained for this long before alerting
#define ON_COURSE_HOLD_S            5   // ...and this long back inside before clearing
#define COURSE_FINISH_RADIUS_M   50.0f
#define MAP_ZOOM_DEFAULT            0   // 0 = auto, 1..8 = a fixed span
#define NAV_ZOOM_SECONDS          120   // auto: map window = this much riding time
#define NAV_ZOOM_MIN_M           300.0f
#define NAV_ZOOM_MAX_M          1500.0f

// --- structured workouts ---------------------------------------------------
#define WORKOUT_DIR            "/workouts"
#define WORKOUT_MAX_STEPS         64   // repeats are expanded, so this is the total
#define WORKOUT_TARGET_DWELL_S     6   // seconds out of range before it complains
#define WORKOUT_TARGET_REARM_S     4   // ...and back inside before it forgives

// --- turn-by-turn guidance -------------------------------------------------
#define NAV_LOOKAHEAD_M         3000.0f // how far ahead turns are searched for
#define NAV_LOOKAHEAD_CUES          6   // how many upcoming turns are kept
#define NAV_TURN_BASELINE_M       25.0f // bearing measured over this baseline
#define NAV_TURN_MIN_DEG          28.0f // below this it is a bend, not a turn
#define NAV_CUE_TURN_MIN_DEG      12.0f // ...but a placed cue point earns an arrow sooner
#define NAV_TURN_MERGE_M          35.0f // candidates closer than this collapse
#define NAV_CUE_MATCH_M           30.0f // a GPX waypoint this close names a turn
#define NAV_ANNOUNCE_FAR_M       400.0f // "turn in 400 m"
#define NAV_ANNOUNCE_NEAR_M      150.0f // "turn ahead"
#define NAV_ANNOUNCE_NOW_M        30.0f // "turn now"

#define DEVICE_NAME        "EdgeESP"
#define FIRMWARE_VERSION   "1.0.0"

// Nordic UART Service - the companion phone link
#define NUS_SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define NUS_RX_UUID      "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"  // phone -> device
#define NUS_TX_UUID      "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"  // device -> phone
