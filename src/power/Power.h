#pragma once
#include <Arduino.h>
#include "config.h"
#include "power/BatteryWarn.h"

// Battery gauge, backlight and sleep. Assumes a single Li-ion cell behind a
// buck-boost regulator, with a 100k/100k divider on VBAT_ADC_PIN.
class Power {
 public:
  void begin();
  void update();

  uint16_t millivolts() const { return _mv; }
  uint8_t  percent()    const { return _pct; }
  bool     charging()   const { return _charging; }
  bool     critical()   const { return _mv && _mv < BATT_CRITICAL_MV; }
  // Hours left at the current draw, or NAN before enough history exists.
  float    hoursRemaining() const { return _hoursLeft; }

  void setBacklight(uint8_t level);       // 0..255
  uint8_t backlight() const { return _bl; }
  void noteActivity();                    // resets the dim/sleep timers
  void tickIdle(bool rideActive);         // dims, then sleeps

  // Pop the next battery warning, if the level has crossed a threshold.
  PowerEvent takeEvent();
  uint8_t warnTier() const { return _warnTier; }

  void deepSleep();                       // wakes on the button ladder going low

 private:
  uint16_t _mv = 0;
  uint8_t  _pct = 0, _bl = BACKLIGHT_DEFAULT, _blTarget = BACKLIGHT_DEFAULT;
  bool     _charging = false, _dimmed = false;
  uint32_t _lastActivityMs = 0, _lastSampleMs = 0;
  float    _hoursLeft = NAN;
  uint8_t  _warnTier = 0;
  bool     _wasCharging = false;
  PowerEvent _pending = PowerEvent::None;
  void     updateWarnings();

  uint16_t _mvHistory[8] = {0};
  uint8_t  _histIdx = 0;
  uint32_t _histMs = 0;
};

extern Power g_power;
