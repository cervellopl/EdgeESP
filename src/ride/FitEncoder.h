#pragma once
#include <Arduino.h>
#include <FS.h>

// Writes Garmin FIT activity files straight to SD as the ride happens.
// Scope: file_id, event, record, lap, session, activity - which is exactly the
// set Strava / Garmin Connect / komoot need to accept an upload.

struct FitRecord {
  uint32_t timestamp;      // FIT epoch seconds
  int32_t  lat_semi;       // semicircles, INT32_MAX = absent
  int32_t  lon_semi;
  uint32_t distance_cm;    // 0.01 m
  uint16_t altitude_raw;   // (m + 500) * 5
  uint16_t speed_mms;      // 0.001 m/s
  uint8_t  heart_rate;     // 0xFF = absent
  uint8_t  cadence;        // 0xFF = absent
  uint16_t power;          // 0xFFFF = absent
  int8_t   temperature;    // 0x7F = absent
  int16_t  grade_x100;     // 0x7FFF = absent
};

struct FitSummary {
  uint32_t start_time;
  uint32_t timestamp;
  uint32_t elapsed_ms;
  uint32_t timer_ms;
  uint32_t distance_cm;
  uint16_t ascent_m;
  uint16_t descent_m;
  uint16_t avg_speed_mms;
  uint16_t max_speed_mms;
  uint8_t  avg_hr;
  uint8_t  max_hr;
  uint8_t  avg_cad;
  uint8_t  max_cad;
  uint16_t avg_power;
  uint16_t max_power;
  uint16_t calories;
  int32_t  start_lat_semi;
  int32_t  start_lon_semi;
};

class FitEncoder {
 public:
  // Unix epoch -> FIT epoch (1989-12-31T00:00:00Z).
  static uint32_t toFitTime(uint32_t unix) {
    return unix > 631065600UL ? unix - 631065600UL : 0;
  }
  static int32_t toSemicircles(double deg) {
    return (int32_t)llround(deg * (2147483648.0 / 180.0));
  }
  static uint16_t toAltitudeRaw(float m) {
    float v = (m + 500.0f) * 5.0f;
    return (v < 0 || v > 65534) ? 0xFFFF : (uint16_t)lroundf(v);
  }

  bool begin(File& f, uint32_t startUnix);
  // Pick up a file that was already being written. The definitions are
  // already in it, so they must not be emitted a second time.
  bool resume(File& f, uint32_t dataSize);
  void writeEvent(uint32_t fitTime, uint8_t event, uint8_t eventType);
  void writeRecord(const FitRecord& r);
  void writeLap(const FitSummary& s, uint16_t index);
  void writeSession(const FitSummary& s, uint16_t numLaps);
  // Patches the header, then streams the file back through the CRC and appends it.
  bool finalize(const FitSummary& s, uint16_t numLaps);

  uint32_t dataBytes() const { return _dataSize; }

 private:
  File*    _f = nullptr;
  uint32_t _dataSize = 0;
  bool     _defsWritten = false;

  void put(const void* p, size_t n);
  void put8(uint8_t v)   { put(&v, 1); }
  void put16(uint16_t v) { put(&v, 2); }
  void put32(uint32_t v) { put(&v, 4); }
  void writeDefinitions();
  void writeHeader(uint32_t dataSize);
};

uint16_t fitCrc16(uint16_t crc, uint8_t b);
