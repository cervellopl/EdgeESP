#pragma once
#include <Arduino.h>
#include "ride/RideComputer.h"

// BLE peripheral role: what a phone (or the bundled Web Bluetooth page) talks to.
//
//   Nordic UART Service  - line protocol, telemetry out / commands in
//   Battery Service      - so the phone's own UI shows the head unit's charge
//   Device Information   - model + firmware strings
//
// Commands accepted on the RX characteristic, one per write, newline optional:
//   START | STOP | LAP | SAVE | DISCARD | PAIR | WIFI | SLEEP
//   TIME <unix>            set the clock when there is no GPS fix yet
//   NOTIFY <title>|<body>  show a banner (incoming call, message, ...)
//   RATE <ms>              telemetry period, 0 disables

enum class PhoneCommand : uint8_t {
  None, Start, Stop, Lap, Save, Discard, Pair, Wifi, Sleep
};

class PhoneLink {
 public:
  void begin();
  void update(const RideState& s);        // pushes telemetry at the current rate
  bool connected() const;

  // Pop the next queued command from the phone.
  PhoneCommand takeCommand();
  // Non-null while an unread notification is pending.
  bool takeNotification(char* title, size_t tn, char* body, size_t bn);

  void sendLine(const char* s);
  void setBattery(uint8_t pct);

 private:
  uint32_t _lastTelemetryMs = 0;
};

extern PhoneLink g_phone;
