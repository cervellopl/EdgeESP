#pragma once
#include <Arduino.h>
#include "config.h"

// The decision of when the GPS is worth complaining about, kept out of the UBX
// driver so it can be tested on the host. Noticing a dropout is the easy half;
// the difficulty is not nagging about the ones every bridge, underpass and
// tree tunnel produces, while still speaking up when the sky is genuinely gone.

enum class GpsEvent : uint8_t { None, Degraded, Lost, Silent, Reacquired };

// 0 = fine, 1 = a fix too loose to navigate on, 2 = no fix at all,
// 3 = the receiver itself has stopped talking.
uint8_t gpsWarnTier(bool valid, float hAcc, uint32_t staleS);

// How long a tier must hold before it is believed. Graded on purpose: a
// receiver that has gone quiet is a fault and says so almost at once, while a
// wandering accuracy figure is given long enough to prove it is not just a
// row of trees.
uint16_t gpsWarnHoldSeconds(uint8_t tier);

// The event a tier raises when it is first reached.
GpsEvent gpsWarnEvent(uint8_t tier);

// The dwell machine around those thresholds. Deliberately asymmetric, like the
// battery's: a warning is earned by a sustained outage and released only by a
// fix that comes back and stays back.
class GpsWatch {
 public:
  // Call about once a second with the current fix and how long the receiver has
  // been silent; dtMs is the interval since the last call.
  void update(bool valid, float hAcc, uint32_t staleS, uint32_t dtMs);

  // Pop the next event, or None when there is nothing to say.
  GpsEvent takeEvent();

  uint8_t  tier() const { return _tier; }
  bool     warning() const { return _tier > 0; }
  // Length of the outage in progress, or of the one that just ended.
  uint16_t outageSeconds() const {
    return _tier ? (uint16_t)_badS : _lastOutageS;
  }

 private:
  uint8_t  _tier = 0;      // what has been warned about and still stands
  uint8_t  _worst = 0;     // worst tier seen during this outage
  float    _badS = 0, _goodS = 0;
  uint16_t _lastOutageS = 0;
  // Nothing is wrong until something has been right: a receiver that has never
  // seen the sky is a cold start, not a failure.
  bool     _hadFix = false;
  GpsEvent _pending = GpsEvent::None;
};
