#pragma once
#include <Arduino.h>

// BME280 barometric altimeter + thermometer. Everything degrades gracefully to
// NAN when the chip is absent, so the board works without it.
class Baro {
 public:
  bool begin();
  bool present() const { return _ok; }
  void update();

  float altitude()    const { return _ok ? _alt : NAN; }   // metres
  float temperature() const { return _ok ? _temp : NAN; }  // degC
  float pressure()    const { return _ok ? _press : NAN; } // hPa
  float humidity()    const { return _ok ? _hum : NAN; }   // percent

  // Slowly pulls the sea-level reference toward the value implied by GPS MSL,
  // which is how a head unit keeps baro altitude honest over hours.
  void calibrateToGps(float gpsAltMsl);
  float seaLevelHpa() const { return _seaLevel; }

 private:
  bool  _ok = false;
  float _alt = NAN, _temp = NAN, _press = NAN, _hum = NAN;
  float _seaLevel = 1013.25f;
  uint32_t _lastMs = 0;
};
