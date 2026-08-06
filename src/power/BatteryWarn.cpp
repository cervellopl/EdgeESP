#include "power/BatteryWarn.h"

uint8_t batteryWarnTier(uint8_t pct, bool critical) {
  if (critical) return 3;
  if (pct <= BATT_WARN_LOW_PCT) return 2;
  if (pct <= BATT_WARN_PCT) return 1;
  return 0;
}

bool batteryWarnClears(uint8_t heldTier, uint8_t pct, bool critical) {
  if (heldTier == 0) return false;
  // Critical is a voltage judgement, not a percentage one, and it holds until
  // the voltage itself recovers.
  if (heldTier == 3 && critical) return false;
  // A tier is only released once the reading has climbed clear of the level
  // that raised it. Without the margin, a cell sagging on every climb would
  // warn at the bottom of each one.
  uint8_t raisedAt = (heldTier >= 2) ? BATT_WARN_LOW_PCT : BATT_WARN_PCT;
  return pct >= (uint8_t)(raisedAt + BATT_WARN_HYSTERESIS_PCT);
}

PowerEvent batteryWarnEvent(uint8_t tier) {
  switch (tier) {
    case 1: return PowerEvent::Low;
    case 2: return PowerEvent::VeryLow;
    case 3: return PowerEvent::Critical;
    default: return PowerEvent::None;
  }
}
