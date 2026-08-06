#pragma once
#include <Arduino.h>
#include "config.h"

// Weather pushed from the phone over the BLE link. The head unit has no
// internet on a ride, so the companion fetches a forecast for wherever the GPS
// says we are and sends it down as text lines.
//
// Wire format, one line per write on the NUS RX characteristic:
//
//   WX  t=18.4 f=17.1 h=62 p=1013 ws=5.2 wd=270 g=9.1 c=40 pr=0.2 uv=3
//       code=61 sr=1754194800 ss=1754250000|Light rain
//   WXH 0 t=18.4 ws=5.2 wd=270 pr=0.0 pp=10 code=2
//   WXH 1 t=18.9 ws=6.1 wd=275 pr=0.4 pp=60 code=61
//
// Keys are optional and order does not matter, so the companion can send only
// what its data source actually provides.

enum class WxIcon : uint8_t { Clear, PartCloud, Cloud, Fog, Drizzle, Rain, Snow, Storm };
enum class WeatherEvent : uint8_t { None, RainSoon, RainStopping, WindWarning };

struct WeatherNow {
  float    tempC = NAN, feelsC = NAN, humidity = NAN, pressureHpa = NAN;
  float    windMps = NAN, gustMps = NAN;
  int16_t  windFromDeg = -1;      // met convention: where the wind blows FROM
  float    cloudsPct = NAN, precipMm = NAN, uv = NAN;
  int16_t  code = -1;
  char     desc[24] = {0};
  uint32_t sunriseUnix = 0, sunsetUnix = 0;
  uint32_t updatedMs = 0;
  bool     valid = false;
};

struct WeatherHour {
  float   tempC = NAN, windMps = NAN, precipMm = NAN, precipProb = NAN;
  int16_t windFromDeg = -1, code = -1;
  bool    valid = false;
};

class Weather {
 public:
  // Called from the BLE task. Returns true if the line was a weather line.
  bool feedLine(const char* line);
  void clear();

  bool valid() const { return _now.valid; }
  bool stale() const {
    return !_now.valid || (millis() - _now.updatedMs) > WEATHER_STALE_MIN * 60000UL;
  }
  uint32_t ageMinutes() const {
    return _now.valid ? (millis() - _now.updatedMs) / 60000UL : 0;
  }
  // Snapshot under the lock - safe to call from the render loop.
  WeatherNow  now() const;
  WeatherHour hour(uint8_t i) const;
  uint8_t     hourCount() const { return _hourCount; }

  // --- wind in the rider's frame -----------------------------------------
  // 0 = blowing straight into your face, +90 = coming from your right.
  float relativeWindDeg(float headingDeg) const;
  float headwind(float headingDeg) const;    // m/s, positive = headwind
  float crosswind(float headingDeg) const;   // m/s, positive = from the right

  // Air density from the local barometer, for the aero power model.
  static float airDensity(float pressureHpa, float tempC);

  // Call once a second; raises rain warnings off the hourly forecast.
  void tick();
  WeatherEvent takeEvent();
  // Minutes until the first wet hour, or -1.
  int16_t minutesToRain() const { return _minsToRain; }

  static WxIcon      iconFor(int16_t code);
  static const char* textFor(int16_t code);

 private:
  WeatherNow  _now;
  WeatherHour _hours[WEATHER_MAX_HOURS];
  uint8_t     _hourCount = 0;
  WeatherEvent _pending = WeatherEvent::None;
  int16_t     _minsToRain = -1;
  bool        _rainAnnounced = false, _wasRaining = false;
  uint32_t    _lastTickMs = 0;
};

extern Weather g_weather;
