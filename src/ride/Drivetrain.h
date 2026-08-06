#pragma once
#include <Arduino.h>
#include "config.h"

// Which gear you are in, worked out from speed and cadence.
//
// Nothing here reads the drivetrain. There is no electronic shifting on this
// bike computer to ask, so the ratio is measured - wheel revolutions per crank
// revolution - and then matched against the chainrings and sprockets you told
// it about. That works well for the sprocket and less well for the chainring,
// because two different combinations can produce the same ratio.

struct DrivetrainSpec {
  const char* name;
  uint8_t rings[3];
  uint8_t ringCount;
  uint8_t sprockets[12];
  uint8_t sprocketCount;
};

class Drivetrain {
 public:
  static uint8_t presetCount();
  static const DrivetrainSpec& preset(uint8_t i);

  void setPreset(uint8_t i);
  uint8_t presetIndex() const { return _preset; }
  const DrivetrainSpec& spec() const { return preset(_preset); }

  // Call at the ride update rate. speedMps should come from a wheel sensor
  // when there is one - GPS speed lags and wanders, and a ratio is a division
  // by cadence, which magnifies both.
  void update(float speedMps, uint8_t cadence, uint16_t wheelMm, uint32_t dtMs);
  void reset();

  bool  valid() const { return _valid; }
  float ratio() const { return _ratio; }            // wheel revs per crank rev
  float development() const { return _development; }// metres per crank rev
  float gearInches() const { return _gearInches; }

  int8_t  ringIndex() const { return _ring; }       // -1 when unknown
  int8_t  sprocketIndex() const { return _sprocket; }
  uint8_t ringTeeth() const;
  uint8_t sprocketTeeth() const;
  // Big ring on the big cogs, or small ring on the small ones.
  bool crossChained() const;
  // True when another combination sits within a whisker of the measured ratio,
  // so the chainring shown is a guess between two equally good answers.
  bool ambiguous() const { return _ambiguous; }

  static float ratioOf(uint8_t ring, uint8_t sprocket) {
    return sprocket ? (float)ring / (float)sprocket : 0.0f;
  }
  static float gearInchesOf(uint8_t ring, uint8_t sprocket, uint16_t wheelMm);
  static float developmentOf(uint8_t ring, uint8_t sprocket, uint16_t wheelMm) {
    return ratioOf(ring, sprocket) * (wheelMm / 1000.0f);
  }

 private:
  uint8_t _preset = 0;
  bool    _valid = false, _ambiguous = false;
  float   _ratio = NAN, _development = NAN, _gearInches = NAN;
  int8_t  _ring = -1, _sprocket = -1;
  // Hysteresis: a candidate has to hold before it is shown, or the display
  // flickers between neighbouring sprockets on every pedal stroke.
  int8_t  _candRing = -1, _candSprocket = -1;
  uint32_t _candMs = 0;
};

extern Drivetrain g_drive;
