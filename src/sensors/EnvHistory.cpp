#include "sensors/EnvHistory.h"
#include <string.h>
#include <math.h>

EnvHistory g_env;

void EnvHistory::clear() {
  _count = 0;
  _lastMs = 0;
  _started = false;
  _minT = _maxT = NAN;
}

bool EnvHistory::sample(float tempC, float pressureHpa, float humidity, uint32_t nowMs) {
  if (_started && (uint32_t)(nowMs - _lastMs) < INTERVAL_MS) return false;
  // Take the first sample immediately rather than after a minute of nothing.
  _started = true;
  _lastMs = nowMs;

  Sample s;
  s.t10 = (isnan(tempC) || tempC < -100 || tempC > 100)
            ? INT16_MIN : (int16_t)lroundf(tempC * 10.0f);
  s.p10 = (isnan(pressureHpa) || pressureHpa < 300 || pressureHpa > 1200)
            ? 0 : (uint16_t)lroundf(pressureHpa * 10.0f);
  s.rh  = (isnan(humidity) || humidity < 0 || humidity > 100)
            ? 255 : (uint8_t)lroundf(humidity);

  if (_count < SAMPLES) {
    _buf[_count++] = s;
  } else {
    // Full: drop the oldest. Eight hours is already a long ride, and the
    // interesting end of a weather history is the recent end.
    memmove(&_buf[0], &_buf[1], sizeof(Sample) * (SAMPLES - 1));
    _buf[SAMPLES - 1] = s;
  }

  if (s.t10 != INT16_MIN) {
    float t = s.t10 / 10.0f;
    // Recomputed from the kept window rather than carried forever, so a cold
    // start this morning does not haunt the range all afternoon.
    _minT = isnan(_minT) ? t : min(_minT, t);
    _maxT = isnan(_maxT) ? t : max(_maxT, t);
  }
  return true;
}

bool EnvHistory::get(uint16_t i, float& tempC, float& pressureHpa, float& humidity) const {
  if (i >= _count) return false;
  const Sample& s = _buf[i];
  tempC        = s.t10 == INT16_MIN ? NAN : s.t10 / 10.0f;
  pressureHpa  = s.p10 == 0 ? NAN : s.p10 / 10.0f;
  humidity     = s.rh == 255 ? NAN : (float)s.rh;
  return true;
}

bool EnvHistory::pressureTrend(float& changeHpa, uint16_t& spanMin) const {
  if (_count < 2) return false;
  // Walk in from both ends: a sample with no pressure reading must not be
  // mistaken for a pressure of zero.
  int32_t oldest = -1, newest = -1;
  for (uint16_t i = 0; i < _count; i++)
    if (_buf[i].p10) { oldest = i; break; }
  for (int32_t i = (int32_t)_count - 1; i >= 0; i--)
    if (_buf[i].p10) { newest = i; break; }
  if (oldest < 0 || newest <= oldest) return false;

  changeHpa = (_buf[newest].p10 - _buf[oldest].p10) / 10.0f;
  spanMin = (uint16_t)(newest - oldest);
  return spanMin >= 5;
}

float EnvHistory::changePerThreeHours() const {
  float change;
  uint16_t span;
  if (!pressureTrend(change, span) || span < 60) return NAN;
  return change * (180.0f / (float)span);
}

const char* EnvHistory::trendWord() const {
  float r = changePerThreeHours();
  if (isnan(r)) return "";
  // The wording meteorologists use for a three-hour tendency.
  float a = fabsf(r);
  if (a < 0.5f) return "steady";
  const char* rate = a >= 3.5f ? "very rapidly" : a >= 2.0f ? "quickly" : "slowly";
  static char buf[28];
  snprintf(buf, sizeof(buf), "%s %s", r > 0 ? "rising" : "falling", rate);
  return buf;
}
