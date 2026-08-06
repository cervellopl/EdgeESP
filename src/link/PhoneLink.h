#pragma once
#include <Arduino.h>
#include "ride/RideComputer.h"

// BLE peripheral role: what a phone (or the bundled Web Bluetooth page) talks to.
//
//   Nordic UART Service  - line protocol, telemetry out / commands in
//   Battery Service      - so the phone's own UI shows the head unit's charge
//   Device Information   - model + firmware strings
//
// Telemetry goes out as newline-delimited JSON, one object per notification.
// The ride frame is sent at the current rate; the course frame and the GPS
// warning frame are separate objects sent only when they have something to
// say, because one combined line would overrun a 185-byte MTU:
//
//   {"gw":<tier>,"gout":<seconds>}   1 = accuracy poor, 2 = no fix,
//                                    3 = receiver silent; 0 = recovered, and
//                                    gout is then how long it had been out
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
  // The GPS watchdog's held tier and how long the outage has run, mirrored to
  // the phone so a rider following the ride on a bar-mounted screen sees the
  // same thing the head unit does.
  void setGpsWarning(uint8_t tier, uint16_t outageS) {
    _gpsTier = tier; _gpsOutS = outageS;
  }

 private:
  uint32_t _lastTelemetryMs = 0;
  uint8_t  _gpsTier = 0, _lastGpsTierSent = 0;
  uint16_t _gpsOutS = 0;
};

extern PhoneLink g_phone;
