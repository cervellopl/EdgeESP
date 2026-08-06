#pragma once
#include <Arduino.h>
#include "config.h"

// Runtime settings, edited on the device and kept in NVS. The values in
// config.h remain the defaults; this is what the firmware actually reads.
//
// Unit conversion lives here rather than at each call site so there is exactly
// one place that knows what a mile is.

enum class Units : uint8_t { Metric = 0, Imperial = 1 };

class Settings {
 public:
  void load();
  void save();
  void resetToDefaults();

  // --- values ---
  Units    units    = Units::Metric;
  float    riderKg  = RIDER_WEIGHT_KG;
  float    bikeKg   = BIKE_WEIGHT_KG;
  uint16_t wheelMm  = WHEEL_CIRCUMFERENCE_MM;
  uint16_t ftpWatts = RIDER_FTP_W;   // built-in workouts are written as %FTP
  uint8_t  lthrBpm  = RIDER_LTHR_BPM;// threshold heart rate, for hrTSS
  uint8_t  drivetrain = DRIVETRAIN_DEFAULT;  // index into the gearing table
  uint8_t  mapZoom  = MAP_ZOOM_DEFAULT;      // 0 = auto, else a fixed level
  uint8_t  backlight = BACKLIGHT_DEFAULT;
  bool     autoPause = AUTOPAUSE_ON;
  uint16_t autoLapM  = AUTOLAP_DISTANCE_M;   // 0 = off

  bool imperial() const { return units == Units::Imperial; }
  float totalMassKg() const { return riderKg + bikeKg; }

  // --- conversions out of SI, for display ---
  float speed(float mps) const { return imperial() ? mps * 2.236936f : mps * 3.6f; }
  float distLong(float m) const { return imperial() ? m / 1609.344f : m / 1000.0f; }
  float distShort(float m) const { return imperial() ? m * 3.280840f : m; }
  float elev(float m) const { return imperial() ? m * 3.280840f : m; }
  float temp(float c) const { return imperial() ? c * 1.8f + 32.0f : c; }
  float mass(float kg) const { return imperial() ? kg * 2.204623f : kg; }

  const char* speedUnit()     const { return imperial() ? "mph"  : "km/h"; }
  const char* distLongUnit()  const { return imperial() ? "mi"   : "km"; }
  const char* distShortUnit() const { return imperial() ? "ft"   : "m"; }
  const char* elevUnit()      const { return imperial() ? "ft"   : "m"; }
  const char* tempUnit()      const { return imperial() ? "F"    : "C"; }
  const char* massUnit()      const { return imperial() ? "lb"   : "kg"; }
  // Below this many metres a distance reads better in the short unit.
  float shortCutoffM()        const { return imperial() ? 304.8f : 1000.0f; }

  // --- adjustment, stepping in whatever unit is on screen ---
  void stepUnits(int dir);
  void stepRider(int dir);
  void stepBike(int dir);
  void stepWheel(int dir);
  void stepFtp(int dir);
  void stepLthr(int dir);
  void stepDrivetrain(int dir);
  void stepMapZoom(int dir);

  // 0 = auto (the map sizes itself from speed), otherwise the span across
  // the screen in metres. Levels are round numbers in the rider's own
  // units, so imperial gets quarter-miles rather than 402 m.
  static const uint8_t MAP_ZOOM_COUNT = 9;
  float mapZoomLevelM(uint8_t level) const;
  float mapZoomSpanM() const { return mapZoomLevelM(mapZoom); }
  bool  mapZoomAuto() const { return mapZoom == 0; }
  void stepBacklight(int dir);
  void stepAutoPause(int dir);
  void stepAutoLap(int dir);
};

extern Settings g_settings;
