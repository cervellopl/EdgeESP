#include "input/Buttons.h"
#include "config.h"

static const uint32_t DEBOUNCE_MS = 25;
static const uint32_t LONG_MS     = 700;

void Buttons::begin() {
  pinMode(BTN_ADC_PIN, INPUT);
  analogSetPinAttenuation(BTN_ADC_PIN, ADC_11db);
}

Button Buttons::classify(uint16_t mv) {
  if (mv <  150) return BTN_LAP;
  if (mv <  450) return BTN_DOWN;
  if (mv <  820) return BTN_UP;
  if (mv < 1350) return BTN_ENTER;
  if (mv < 2100) return BTN_BACK;
  return BTN_NONE;
}

const char* Buttons::name(Button b) {
  switch (b) {
    case BTN_LAP:   return "LAP";
    case BTN_DOWN:  return "DOWN";
    case BTN_UP:    return "UP";
    case BTN_ENTER: return "ENTER";
    case BTN_BACK:  return "BACK";
    default:        return "-";
  }
}

ButtonEvent Buttons::poll() {
  ButtonEvent ev;
  Button raw = classify(analogReadMilliVolts(BTN_ADC_PIN));

  if (raw != _candidate) {
    _candidate = raw;
    _changedMs = millis();
    return ev;
  }
  if (millis() - _changedMs < DEBOUNCE_MS || raw == _stable) {
    // Long press fires while still held, so holding LAP to reset feels instant.
    if (_stable != BTN_NONE && !_longFired && millis() - _downMs >= LONG_MS) {
      _longFired = true;
      ev.btn = _stable;
      ev.longPress = true;
    }
    return ev;
  }

  // Debounced transition.
  if (_stable != BTN_NONE && raw == BTN_NONE && !_longFired) {
    ev.btn = _stable;              // short press, reported on release
    ev.longPress = false;
  }
  if (raw != BTN_NONE) {
    _downMs = millis();
    _longFired = false;
  }
  _stable = raw;
  return ev;
}
