#pragma once
#include <Arduino.h>

// A long, coarse record of what the on-board sensors measured: one sample a
// minute for eight hours. The ride pages keep a ten-minute history at 1 Hz,
// which is the wrong shape entirely for weather - pressure moves over hours.
//
// 480 samples map one-to-one onto the 480 px screen, so the graph needs no
// resampling.

class EnvHistory {
 public:
  static const uint16_t SAMPLES = 480;
  static const uint32_t INTERVAL_MS = 60000UL;

  void clear();
  // Call often; stores a sample when the interval has elapsed. Returns true
  // when one was actually taken.
  bool sample(float tempC, float pressureHpa, float humidity, uint32_t nowMs);

  uint16_t count() const { return _count; }
  uint16_t spanMinutes() const { return _count ? _count - 1 : 0; }
  // i = 0 is the oldest kept sample. Missing fields come back NAN.
  bool get(uint16_t i, float& tempC, float& pressureHpa, float& humidity) const;

  float minTemp() const { return _count ? _minT : NAN; }
  float maxTemp() const { return _count ? _maxT : NAN; }

  // Pressure change across the window, and how many minutes that covers.
  // False until there is enough history to say anything at all.
  bool pressureTrend(float& changeHpa, uint16_t& spanMin) const;
  // The usual barometric wording, normalised to a three-hour rate. Empty until
  // there is at least an hour behind it - a five-minute slope is noise.
  const char* trendWord() const;

 private:
  struct Sample {
    int16_t  t10;    // degC x10, INT16_MIN = absent
    uint16_t p10;    // hPa x10, 0 = absent
    uint8_t  rh;     // percent, 255 = absent
  };
  Sample   _buf[SAMPLES];
  uint16_t _count = 0;
  uint32_t _lastMs = 0;
  bool     _started = false;
  float    _minT = NAN, _maxT = NAN;

  float changePerThreeHours() const;
};

extern EnvHistory g_env;
