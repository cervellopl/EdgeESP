#pragma once
#include <Arduino.h>
#include "config.h"

// The decision of when to warn about the battery, kept free of the ADC and the
// PWM so it can be tested. Hysteresis is the whole difficulty here: a reading
// that wobbles across a threshold must warn once, not once a second.

enum class PowerEvent : uint8_t { None, Low, VeryLow, Critical, Charging };

// 0 = fine, 1 = low, 2 = very low, 3 = critical.
uint8_t batteryWarnTier(uint8_t pct, bool critical);

// Whether a tier already warned about should be let go, given a fresh reading.
// Deliberately asymmetric with batteryWarnTier(): warnings go up the moment
// they are earned and come down only once the reading is clear of them.
bool batteryWarnClears(uint8_t heldTier, uint8_t pct, bool critical);

// The event a tier raises when it is first reached.
PowerEvent batteryWarnEvent(uint8_t tier);
