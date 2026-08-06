#include "ride/TrainingLoad.h"
#include <Preferences.h>
#include <string.h>
#include <math.h>

TrainingLoad g_load;

static Preferences s_prefs;
static const float CTL_DAYS = 42.0f;
static const float ATL_DAYS = 7.0f;

void TrainingLoad::begin() {
  clearHistory();
  s_prefs.begin("load", true);
  size_t got = s_prefs.getBytes("hist", _tss10, sizeof(_tss10));
  _newestDay = s_prefs.getULong("day", 0);
  s_prefs.end();
  if (got != sizeof(_tss10)) clearHistory();
  // A stored day far in the future means a bad clock wrote it; a fresh start is
  // better than a history that can never roll forward again.
  if (_newestDay > 100000UL) clearHistory();
}

void TrainingLoad::save() {
  s_prefs.begin("load", false);
  s_prefs.putBytes("hist", _tss10, sizeof(_tss10));
  s_prefs.putULong("day", _newestDay);
  s_prefs.end();
}

void TrainingLoad::clearHistory() {
  memset(_tss10, 0, sizeof(_tss10));
  _newestDay = 0;
}

void TrainingLoad::setNow(uint32_t unixTime) {
  // Anything before 2020 is the GPS not having a fix yet, not a real date.
  if (unixTime < 1577836800UL) return;
  uint32_t day = unixTime / 86400UL;

  if (_newestDay == 0) { _newestDay = day; return; }
  if (day <= _newestDay) return;              // same day, or the clock slipped back

  uint32_t shift = day - _newestDay;
  if (shift >= DAYS) {
    memset(_tss10, 0, sizeof(_tss10));        // been away longer than the window
  } else {
    memmove(&_tss10[0], &_tss10[shift], (DAYS - shift) * sizeof(uint16_t));
    memset(&_tss10[DAYS - shift], 0, shift * sizeof(uint16_t));
  }
  _newestDay = day;
}

void TrainingLoad::addTss(float tss) {
  if (!(tss > 0)) return;                     // written to reject NaN as well
  uint32_t v = (uint32_t)_tss10[DAYS - 1] + (uint32_t)lroundf(tss * 10.0f);
  _tss10[DAYS - 1] = (uint16_t)(v > 65535 ? 65535 : v);
}

float TrainingLoad::dayTss(uint16_t daysAgo) const {
  if (daysAgo >= DAYS) return 0;
  return _tss10[DAYS - 1 - daysAgo] / 10.0f;
}

float TrainingLoad::weekTss() const {
  float t = 0;
  for (uint16_t i = 0; i < 7; i++) t += dayTss(i);
  return t;
}

float TrainingLoad::peak(uint16_t days) const {
  float m = 0;
  for (uint16_t i = 0; i < days && i < DAYS; i++) m = max(m, dayTss(i));
  return m;
}

// Walked from the oldest day forward rather than stored, so a gap in riding or
// a restored backup both settle to the right answer on their own.
float TrainingLoad::ctl(float extraToday) const {
  float v = 0;
  for (uint16_t i = 0; i < DAYS; i++) {
    float t = _tss10[i] / 10.0f;
    if (i == DAYS - 1) t += extraToday;
    v += (t - v) / CTL_DAYS;
  }
  return v;
}

float TrainingLoad::atl(float extraToday) const {
  float v = 0;
  for (uint16_t i = 0; i < DAYS; i++) {
    float t = _tss10[i] / 10.0f;
    if (i == DAYS - 1) t += extraToday;
    v += (t - v) / ATL_DAYS;
  }
  return v;
}

// --------------------------------------------------------------------------
// TSS = (seconds * NP * IF) / (FTP * 3600) * 100, and IF = NP / FTP, so this
// reduces to seconds * NP^2 / (FTP^2 * 36).
float TrainingLoad::powerTss(uint32_t movingMs, uint16_t np, uint16_t ftp) {
  if (!ftp || !np || !movingMs) return 0;
  double sec = movingMs / 1000.0;
  double intensity = (double)np / (double)ftp;
  return (float)(sec * intensity * intensity / 36.0);
}

// The heart-rate stand-in. Deliberately a separate function with a separate
// name: it is a different measurement that happens to share a scale, and
// treating the two as interchangeable is how training logs quietly go wrong.
float TrainingLoad::hrTss(uint32_t movingMs, uint8_t avgHr, uint8_t lthr) {
  if (!lthr || !avgHr || !movingMs) return 0;
  double hours = movingMs / 3600000.0;
  double ratio = (double)avgHr / (double)lthr;
  return (float)(hours * ratio * ratio * 100.0);
}
