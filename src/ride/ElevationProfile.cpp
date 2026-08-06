#include "ride/ElevationProfile.h"
#include <math.h>
#include <string.h>

static const int16_t ABSENT = INT16_MIN;

void ElevationProfile::clear() {
  _count = 0;
  _interval = BASE_INTERVAL_M;
  _nextDist = 0;
  _minE = _maxE = NAN;
}

float ElevationProfile::elevationAt(uint16_t i) const {
  if (i >= _count || _buf[i] == ABSENT) return NAN;
  return _buf[i] * 0.5f;
}

void ElevationProfile::decimate() {
  // Keep every second point and double the spacing. The shape survives; only
  // the resolution halves, which is exactly the right thing to trade away.
  for (uint16_t i = 0; i < SAMPLES / 2; i++) _buf[i] = _buf[i * 2];
  _count = SAMPLES / 2;
  _interval *= 2.0f;
  _nextDist = (double)_count * _interval;
}

void ElevationProfile::recomputeRange() {
  _minE = _maxE = NAN;
  for (uint16_t i = 0; i < _count; i++) {
    if (_buf[i] == ABSENT) continue;
    float e = _buf[i] * 0.5f;
    _minE = isnan(_minE) ? e : min(_minE, e);
    _maxE = isnan(_maxE) ? e : max(_maxE, e);
  }
}

bool ElevationProfile::sample(double distanceM, float altitudeM) {
  if (isnan(altitudeM) || altitudeM < -500.0f || altitudeM > 9000.0f) return false;
  if (distanceM < 0) return false;

  int16_t v = (int16_t)lroundf(altitudeM * 2.0f);

  if (_count == 0) {
    _buf[_count++] = v;
    _nextDist = _interval;
    _minE = _maxE = altitudeM;
    return true;
  }
  if (distanceM < _nextDist) return false;

  bool stored = false;
  // A GPS jump can skip several intervals at once; fill them rather than
  // letting the x axis drift out of step with the distance it claims to show.
  for (uint16_t guard = 0; guard < SAMPLES && distanceM >= _nextDist; guard++) {
    if (_count >= SAMPLES) decimate();
    _buf[_count++] = v;
    _nextDist += _interval;
    stored = true;
  }

  if (stored) {
    _minE = isnan(_minE) ? altitudeM : min(_minE, altitudeM);
    _maxE = isnan(_maxE) ? altitudeM : max(_maxE, altitudeM);
  }
  return stored;
}

float ElevationProfile::gradeAt(uint16_t i, float windowM) const {
  if (_count < 2 || i >= _count) return NAN;
  uint16_t stepsNeeded = (uint16_t)max(1.0f, windowM / _interval);

  // Centre the window where there is room, so a climb is coloured at the point
  // it is actually steep rather than a window-length before it.
  uint16_t half = stepsNeeded / 2;
  uint16_t a = i > half ? i - half : 0;
  uint16_t b = min<uint16_t>(_count - 1, a + stepsNeeded);
  if (b <= a) return NAN;
  a = b > stepsNeeded ? b - stepsNeeded : 0;

  float ea = elevationAt(a), eb = elevationAt(b);
  if (isnan(ea) || isnan(eb)) return NAN;
  float run = (b - a) * _interval;
  if (run <= 0) return NAN;
  return (eb - ea) / run * 100.0f;
}
