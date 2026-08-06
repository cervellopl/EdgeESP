#pragma once
#include <Arduino.h>

// Five buttons on one ADC pin via a resistor ladder - the parallel LCD leaves
// no room for five GPIOs, and a ladder needs no extra chip.
//
//   3V3 --[10k]--+-- ADC
//                |
//   BTN_LAP   ---+--[   0R]-- GND   ~   0 mV
//   BTN_DOWN  ---+--[  1k0]-- GND   ~ 300 mV
//   BTN_UP    ---+--[  2k2]-- GND   ~ 595 mV
//   BTN_ENTER ---+--[  4k7]-- GND   ~1055 mV
//   BTN_BACK  ---+--[ 10k ]-- GND   ~1650 mV
//   (idle)                          ~3300 mV

enum Button : uint8_t { BTN_NONE = 0, BTN_LAP, BTN_DOWN, BTN_UP, BTN_ENTER, BTN_BACK };

struct ButtonEvent {
  Button btn = BTN_NONE;
  bool   longPress = false;
};

class Buttons {
 public:
  void begin();
  // Poll from the main loop. Returns one event per press, on release (or as
  // soon as the long-press threshold is crossed).
  ButtonEvent poll();
  bool isDown(Button b) const { return _stable == b; }
  uint32_t heldMs() const { return _stable ? millis() - _downMs : 0; }

  static const char* name(Button b);

 private:
  Button   _stable = BTN_NONE, _candidate = BTN_NONE;
  uint32_t _changedMs = 0, _downMs = 0;
  bool     _longFired = false;

  static Button classify(uint16_t mv);
};
