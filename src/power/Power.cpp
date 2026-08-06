#include "power/Power.h"
#include "config.h"
#include <esp_sleep.h>
#include <driver/rtc_io.h>

Power g_power;

// Discharge curve for a single Li-ion cell under a light load. A linear
// voltage-to-percent map reads badly - it sits at "100%" then falls off a cliff.
static const struct { uint16_t mv; uint8_t pct; } kCurve[] = {
  {4180, 100}, {4100, 95}, {4000, 88}, {3900, 78}, {3820, 68}, {3750, 57},
  {3700, 46}, {3660, 36}, {3620, 26}, {3560, 16}, {3480, 8}, {3400, 3}, {3300, 0},
};

static uint8_t curvePct(uint16_t mv) {
  if (mv >= kCurve[0].mv) return 100;
  const size_t n = sizeof(kCurve) / sizeof(kCurve[0]);
  if (mv <= kCurve[n - 1].mv) return 0;
  for (size_t i = 1; i < n; i++) {
    if (mv >= kCurve[i].mv) {
      float f = (float)(mv - kCurve[i].mv) / (kCurve[i - 1].mv - kCurve[i].mv);
      return (uint8_t)(kCurve[i].pct + f * (kCurve[i - 1].pct - kCurve[i].pct));
    }
  }
  return 0;
}

void Power::begin() {
  analogSetPinAttenuation(VBAT_ADC_PIN, ADC_11db);
  ledcSetup(7, 20000, 8);          // 20 kHz, above hearing, no coil whine
  ledcAttachPin(LCD_BL, 7);
  ledcWrite(7, _bl);
  _lastActivityMs = millis();
  // update() rate-limits itself to twice a second, and at boot millis() can
  // still be under that - which would leave the first reading at zero and
  // the battery showing empty. Backdate the timer so this one is taken.
  _lastSampleMs = millis() - 1000;
  update();
  for (auto& h : _mvHistory) h = _mv;
}

void Power::setBacklight(uint8_t level) {
  _bl = level;
  _blTarget = level;
  ledcWrite(7, level);
}

void Power::noteActivity() {
  _lastActivityMs = millis();
  if (_dimmed) {
    _dimmed = false;
    ledcWrite(7, _blTarget);
    _bl = _blTarget;
  }
}

void Power::tickIdle(bool rideActive) {
  uint32_t idle = millis() - _lastActivityMs;
  if (!_dimmed && idle > BACKLIGHT_DIM_AFTER_MS) {
    _dimmed = true;
    _bl = BACKLIGHT_DIM;
    ledcWrite(7, _bl);
  }
  // Never sleep mid-ride, no matter how long the descent.
  if (!rideActive && idle > SLEEP_AFTER_IDLE_MS) deepSleep();
}

void Power::update() {
  uint32_t now = millis();
  if (now - _lastSampleMs < 500) return;
  _lastSampleMs = now;

  // Average 8 reads: the ESP32-S3 SAR ADC is noisy and the LCD backlight makes
  // it worse.
  uint32_t acc = 0;
  for (int i = 0; i < 8; i++) acc += analogReadMilliVolts(VBAT_ADC_PIN);
  uint16_t mv = (uint16_t)(acc / 8 * VBAT_DIVIDER);

  _mv = _mv ? (uint16_t)(_mv * 0.9f + mv * 0.1f) : mv;
  _pct = curvePct(_mv);
  // A cell that reads above the full threshold is being held there by the
  // charger, which is the only signal we have without a dedicated charge-status pin.
  _charging = _mv > BATT_FULL_MV + 40;

  if (now - _histMs > 60000) {
    _histMs = now;
    uint16_t oldest = _mvHistory[(_histIdx + 1) % 8];
    _mvHistory[_histIdx] = _mv;
    _histIdx = (_histIdx + 1) % 8;
    if (oldest && oldest > _mv) {
      float dropPerMin = (oldest - _mv) / 7.0f;
      if (dropPerMin > 0.2f) {
        _hoursLeft = ((float)_mv - BATT_EMPTY_MV) / dropPerMin / 60.0f;
      }
    } else if (_charging) {
      _hoursLeft = NAN;
    }
  }

  updateWarnings();
}

// Called from update(), so the warning tracks the same filtered reading the
// screen shows rather than a raw sample.
void Power::updateWarnings() {
  if (_charging) {
    // Plugging in answers the warning, whatever it was.
    if (_warnTier || !_wasCharging) {
      _warnTier = 0;
      if (!_wasCharging) _pending = PowerEvent::Charging;
    }
    _wasCharging = true;
    return;
  }
  _wasCharging = false;

  uint8_t t = batteryWarnTier(_pct, critical());
  if (t > _warnTier) {
    _warnTier = t;
    _pending = batteryWarnEvent(t);
  } else if (t < _warnTier && batteryWarnClears(_warnTier, _pct, critical())) {
    _warnTier = t;
  }
}

PowerEvent Power::takeEvent() {
  PowerEvent e = _pending;
  _pending = PowerEvent::None;
  return e;
}

void Power::deepSleep() {
  ledcWrite(7, 0);
  digitalWrite(LCD_RST, LOW);      // park the panel so it does not draw current
  // The button ladder pulls the ADC pin toward ground on any press, which is a
  // perfectly good wake source.
  rtc_gpio_pullup_en((gpio_num_t)BTN_ADC_PIN);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)BTN_ADC_PIN, 0);
  esp_deep_sleep_start();
}
