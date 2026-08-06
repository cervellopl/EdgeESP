#include "gps/GpsWarn.h"

uint8_t gpsWarnTier(bool valid, float hAcc, uint32_t staleS) {
  // Silence is a different failure from a bad sky. No NAV-PVT at all means the
  // wiring, the power or the module, and no amount of waiting under an open
  // sky will fix it - so it outranks everything the solution itself says.
  if (staleS >= GPS_SILENT_S) return 3;
  if (!valid) return 2;
  // The same figure course snapping refuses to work with. A fix looser than
  // this still draws a plausible position and quietly ruins the distance.
  if (hAcc >= GPS_ACC_WARN_M) return 1;
  return 0;
}

uint16_t gpsWarnHoldSeconds(uint8_t tier) {
  switch (tier) {
    case 1: return GPS_DEGRADED_HOLD_S;
    case 2: return GPS_LOST_HOLD_S;
    case 3: return GPS_SILENT_HOLD_S;
    default: return 0;
  }
}

GpsEvent gpsWarnEvent(uint8_t tier) {
  switch (tier) {
    case 1: return GpsEvent::Degraded;
    case 2: return GpsEvent::Lost;
    case 3: return GpsEvent::Silent;
    default: return GpsEvent::None;
  }
}

void GpsWatch::update(bool valid, float hAcc, uint32_t staleS, uint32_t dtMs) {
  float dt = dtMs / 1000.0f;
  if (dt <= 0 || dt > 10) dt = 1.0f;

  // Waiting for the first fix of the day is normal, and the boot screen has
  // already said whether the receiver answered at all.
  if (!_hadFix) {
    if (valid) _hadFix = true;
    return;
  }

  uint8_t t = gpsWarnTier(valid, hAcc, staleS);
  if (t == 0) {
    _goodS += dt;
  } else {
    _goodS = 0;
    _badS += dt;
    if (t > _worst) _worst = t;
  }

  // The outage clock is not reset by a single good sample, only by a recovery
  // that holds. Under a city street a fix pops in and out every few seconds;
  // counting from zero each time it appears would mean a rider who never has a
  // usable position is never told so.
  if (_goodS >= GPS_FIX_HOLD_S) {
    if (_tier) {
      _lastOutageS = (uint16_t)_badS;
      _pending = GpsEvent::Reacquired;
    }
    _tier = _worst = 0;
    _badS = 0;
  } else if (_worst > _tier && _badS >= gpsWarnHoldSeconds(_worst)) {
    // Escalation is not made to serve its predecessor's dwell again: a dropout
    // that becomes a dead receiver has already waited long enough.
    _tier = _worst;
    _pending = gpsWarnEvent(_tier);
  }
}

GpsEvent GpsWatch::takeEvent() {
  GpsEvent e = _pending;
  _pending = GpsEvent::None;
  return e;
}
