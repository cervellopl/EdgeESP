#pragma once
#include <Arduino.h>
#include "config.h"

// Optional 3-axis magnetometer on the existing I2C bus. Auto-detects the two
// chips that turn up on every "GY-271" module: QMC5883L (0x0D, what you almost
// always actually get) and HMC5883L (0x1E, what the silkscreen usually claims).
//
// Everything degrades to NAN when the chip is absent, exactly like the
// barometer, so the board works without one.
//
// No tilt compensation: that needs an accelerometer this design does not have.
// Readings are valid while the unit is roughly level, which a bar mount is.

enum class MagChip : uint8_t { None, QMC5883L, HMC5883L };

class Compass {
 public:
  bool begin();
  bool present() const { return _chip != MagChip::None; }
  MagChip chip() const { return _chip; }
  const char* chipName() const;
  void update();

  // Heading after hard-iron correction and mount offset. NAN when there is no
  // chip, or when it has never been calibrated - an uncalibrated magnetometer
  // sitting next to a battery and a steel handlebar is not a compass, and
  // showing its output would be worse than showing nothing.
  float magneticHeading() const;
  float trueHeading() const;          // magnetic heading + declination

  bool  calibrated() const { return _calibrated; }

  // --- calibration: spin the device through a full turn ---
  void    startCalibration();
  void    cancelCalibration();
  bool    saveCalibration();          // false if coverage is too poor to trust
  bool    calibrating() const { return _calibrating; }
  uint8_t coverage() const;           // 0..100, share of the circle seen
  uint16_t sectorMask() const { return _sectors; }

  int16_t rawX() const { return _x; }
  int16_t rawY() const { return _y; }
  int16_t rawZ() const { return _z; }

 private:
  MagChip _chip = MagChip::None;
  uint8_t _addr = 0;
  int16_t _x = 0, _y = 0, _z = 0;
  float   _smoothSin = 0, _smoothCos = 0;
  bool    _haveSmooth = false;

  int16_t _minX = 32767, _maxX = -32768;
  int16_t _minY = 32767, _maxY = -32768;
  int16_t _minZ = 32767, _maxZ = -32768;
  bool    _calibrated = false, _calibrating = false;
  uint16_t _sectors = 0;              // 12 bits, one per 30 degree sector

  bool readRaw(int16_t& x, int16_t& y, int16_t& z);
  void loadCalibration();
};

extern Compass g_compass;
