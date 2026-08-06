#include "ride/Zones.h"

// Coggan's power zones. The bands are contiguous: each zone starts one percent
// above the last, so no value can fall between two of them.
static const ZoneDef kPower[Zones::POWER_COUNT] = {
  {"Active recovery", "Z1",   0,  55},
  {"Endurance",       "Z2",  56,  75},
  {"Tempo",           "Z3",  76,  90},
  {"Threshold",       "Z4",  91, 105},
  {"VO2max",          "Z5", 106, 120},
  {"Anaerobic",       "Z6", 121, 150},
  {"Neuromuscular",   "Z7", 151, 255},
};

// Threshold-HR zones. Heart rate cannot be driven the way power can, so the
// bands are narrower near threshold and there are fewer of them.
static const ZoneDef kHr[Zones::HR_COUNT] = {
  {"Recovery",   "Z1",   0,  80},
  {"Endurance",  "Z2",  81,  89},
  {"Tempo",      "Z3",  90,  93},
  {"Threshold",  "Z4",  94,  99},
  {"VO2max",     "Z5", 100, 255},
};

// Cadence bands are absolute rpm. Unlike power and heart rate there is no
// threshold to scale from and no single accepted scheme, so these are a
// convention rather than a derivation - edit the table to suit.
//
// Z1 starts at 1, not 0: zero rpm is coasting, which is counted separately.
static const ZoneDef kCadence[Zones::CAD_COUNT] = {
  {"Grinding",  "Z1",   1,  59},
  {"Low",       "Z2",  60,  74},
  {"Endurance", "Z3",  75,  89},
  {"Fast",      "Z4",  90, 104},
  {"Spinning",  "Z5", 105, 255},
};

const ZoneDef& Zones::power(uint8_t i)   { return kPower[i < POWER_COUNT ? i : POWER_COUNT - 1]; }
const ZoneDef& Zones::hr(uint8_t i)      { return kHr[i < HR_COUNT ? i : HR_COUNT - 1]; }
const ZoneDef& Zones::cadence(uint8_t i) { return kCadence[i < CAD_COUNT ? i : CAD_COUNT - 1]; }

uint8_t Zones::powerZoneFor(uint16_t watts, uint16_t ftp) {
  if (!ftp) return 0;
  // 32-bit intermediate: 1500 W against a 60 W FTP is 2500 %, which would wrap
  // a 16-bit multiply long before it got there.
  uint32_t pct = (uint32_t)watts * 100UL / ftp;
  for (uint8_t i = 0; i < POWER_COUNT; i++)
    if (pct <= kPower[i].hi) return i;
  return POWER_COUNT - 1;
}

uint8_t Zones::hrZoneFor(uint8_t bpm, uint8_t lthr) {
  if (!lthr) return 0;
  uint32_t pct = (uint32_t)bpm * 100UL / lthr;
  for (uint8_t i = 0; i < HR_COUNT; i++)
    if (pct <= kHr[i].hi) return i;
  return HR_COUNT - 1;
}

// The displayed bounds have to be the values that actually classify into the
// zone, which is not the same as the percentage arithmetic rounded off.
//
// At FTP 250, Z4 begins at 91 % = 227.5 W. Truncating gives 227, but
// powerZoneFor(227) computes 90 % and files it in Z3 - so the screen would name
// a target that lands one zone lower. The low bound therefore rounds *up*, and
// the high bound is one below the next zone's low bound, which also makes the
// printed bands contiguous with no gap between them.
static inline uint16_t ceilPct(uint16_t pct, uint16_t threshold) {
  return (uint16_t)(((uint32_t)pct * threshold + 99UL) / 100UL);
}

uint16_t Zones::powerLo(uint8_t i, uint16_t ftp) {
  return ceilPct(power(i).lo, ftp);
}

uint16_t Zones::powerHi(uint8_t i, uint16_t ftp) {
  const ZoneDef& z = power(i);
  if (z.hi == 255) return 0;                 // open-ended
  uint16_t nextLo = ceilPct((uint16_t)(z.hi + 1), ftp);
  return nextLo ? (uint16_t)(nextLo - 1) : 0;
}

uint16_t Zones::hrLo(uint8_t i, uint8_t lthr) {
  return ceilPct(hr(i).lo, lthr);
}

uint16_t Zones::hrHi(uint8_t i, uint8_t lthr) {
  const ZoneDef& z = hr(i);
  if (z.hi == 255) return 0;
  uint16_t nextLo = ceilPct((uint16_t)(z.hi + 1), lthr);
  return nextLo ? (uint16_t)(nextLo - 1) : 0;
}

uint8_t Zones::cadenceZoneFor(uint8_t rpm) {
  for (uint8_t i = 0; i < CAD_COUNT; i++)
    if (rpm <= kCadence[i].hi) return i;
  return CAD_COUNT - 1;
}

// Cadence bands are already absolute, so no scaling and no rounding question.
uint16_t Zones::cadenceLo(uint8_t i) { return cadence(i).lo; }
uint16_t Zones::cadenceHi(uint8_t i) {
  const ZoneDef& z = cadence(i);
  return z.hi == 255 ? 0 : z.hi;
}
