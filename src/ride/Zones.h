#pragma once
#include <Arduino.h>

// Training zones. Power is Coggan's seven, as percentages of FTP; heart rate is
// the usual five, as percentages of threshold HR.
//
// Kept free of any display concerns so the boundaries can be checked on the
// host - an off-by-one here silently misfiles every second of a ride.

struct ZoneDef {
  const char* name;
  const char* code;     // "Z1" ...
  // Percent of threshold for power and heart rate; absolute rpm for cadence,
  // which has no threshold to be a percentage of.
  uint8_t     lo;       // inclusive
  uint8_t     hi;       // inclusive; 255 means open-ended
};

class Zones {
 public:
  static const uint8_t POWER_COUNT = 7;
  static const uint8_t HR_COUNT    = 5;
  static const uint8_t CAD_COUNT   = 5;

  static const ZoneDef& power(uint8_t i);
  static const ZoneDef& hr(uint8_t i);
  static const ZoneDef& cadence(uint8_t i);

  // Which zone a value falls in. Clamped, so it always returns a valid index.
  static uint8_t powerZoneFor(uint16_t watts, uint16_t ftp);
  static uint8_t hrZoneFor(uint8_t bpm, uint8_t lthr);
  // 0 rpm is coasting, not a zone. Callers must check for it separately;
  // this returns the bottom zone so it can never index out of range.
  static uint8_t cadenceZoneFor(uint8_t rpm);

  // Absolute bounds for display. hi is 0 for the open-ended top zone.
  static uint16_t powerLo(uint8_t i, uint16_t ftp);
  static uint16_t powerHi(uint8_t i, uint16_t ftp);
  static uint16_t hrLo(uint8_t i, uint8_t lthr);
  static uint16_t hrHi(uint8_t i, uint8_t lthr);
  static uint16_t cadenceLo(uint8_t i);
  static uint16_t cadenceHi(uint8_t i);
};
