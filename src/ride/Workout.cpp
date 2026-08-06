#include "ride/Workout.h"
#include "Settings.h"
#include <SD.h>
#include <ctype.h>
#include <string.h>
#include <math.h>

Workout g_workout;

// --------------------------------------------------------------------------
// Built-in sessions. These exist so the feature works with no SD card and no
// file to write - a rider should be able to pick "4x4 VO2max" and go.
// Powers are percentages of FTP rather than watts; see expandPreset().
namespace {

struct PresetStep {
  StepKind kind; StepDuration dur; uint32_t value;
  StepTarget target; uint16_t lo, hi;   // percent of FTP, or bpm/rpm
  uint8_t repeat;                        // 0 or 1 = once
  const char* name;
};

struct Preset { const char* name; const PresetStep* steps; uint8_t count; };

#define MIN(m) ((m) * 60000UL)
#define SEC(s) ((s) * 1000UL)

const PresetStep kVo2[] = {
  {StepKind::Warmup,   StepDuration::Time, MIN(12), StepTarget::Power,  50,  65, 1, "Warm up"},
  {StepKind::Work,     StepDuration::Time, MIN(4),  StepTarget::Power, 106, 120, 4, "VO2max"},
  {StepKind::Rest,     StepDuration::Time, MIN(4),  StepTarget::Power,  40,  55, 4, "Recover"},
  {StepKind::Cooldown, StepDuration::Time, MIN(10), StepTarget::Power,  40,  55, 1, "Cool down"},
};
const PresetStep kThreshold[] = {
  {StepKind::Warmup,   StepDuration::Time, MIN(15), StepTarget::Power,  50,  65, 1, "Warm up"},
  {StepKind::Work,     StepDuration::Time, MIN(8),  StepTarget::Power,  95, 105, 3, "Threshold"},
  {StepKind::Rest,     StepDuration::Time, MIN(5),  StepTarget::Power,  45,  55, 3, "Recover"},
  {StepKind::Cooldown, StepDuration::Time, MIN(10), StepTarget::Power,  40,  55, 1, "Cool down"},
};
const PresetStep kSweetSpot[] = {
  {StepKind::Warmup,   StepDuration::Time, MIN(12), StepTarget::Power,  50,  65, 1, "Warm up"},
  {StepKind::Work,     StepDuration::Time, MIN(12), StepTarget::Power,  88,  94, 3, "Sweet spot"},
  {StepKind::Rest,     StepDuration::Time, MIN(5),  StepTarget::Power,  45,  55, 3, "Recover"},
  {StepKind::Cooldown, StepDuration::Time, MIN(10), StepTarget::Power,  40,  55, 1, "Cool down"},
};
const PresetStep k3030[] = {
  {StepKind::Warmup,   StepDuration::Time, MIN(12), StepTarget::Power,  50,  65, 1, "Warm up"},
  {StepKind::Work,     StepDuration::Time, SEC(30), StepTarget::Power, 115, 135, 20, "On"},
  {StepKind::Rest,     StepDuration::Time, SEC(30), StepTarget::Power,  40,  55, 20, "Off"},
  {StepKind::Cooldown, StepDuration::Time, MIN(10), StepTarget::Power,  40,  55, 1, "Cool down"},
};
const PresetStep kSprints[] = {
  {StepKind::Warmup,   StepDuration::Time, MIN(15), StepTarget::Power,  50,  65, 1, "Warm up"},
  {StepKind::Work,     StepDuration::Time, SEC(20), StepTarget::None,    0,   0, 8, "SPRINT"},
  {StepKind::Rest,     StepDuration::Time, MIN(4),  StepTarget::Power,  40,  50, 8, "Recover"},
  {StepKind::Cooldown, StepDuration::Time, MIN(10), StepTarget::Power,  40,  55, 1, "Cool down"},
};
const PresetStep kEndurance[] = {
  {StepKind::Warmup,   StepDuration::Time, MIN(10), StepTarget::Power,  45,  60, 1, "Warm up"},
  {StepKind::Work,     StepDuration::Open, 0,       StepTarget::Power,  65,  75, 1, "Endurance"},
  {StepKind::Cooldown, StepDuration::Time, MIN(10), StepTarget::Power,  40,  55, 1, "Cool down"},
};
const PresetStep kCadence[] = {
  {StepKind::Warmup,   StepDuration::Time, MIN(10), StepTarget::None,    0,   0, 1, "Warm up"},
  {StepKind::Work,     StepDuration::Time, MIN(1),  StepTarget::Cadence, 100, 115, 6, "High cadence"},
  {StepKind::Rest,     StepDuration::Time, MIN(2),  StepTarget::Cadence,  80,  90, 6, "Easy spin"},
  {StepKind::Cooldown, StepDuration::Time, MIN(8),  StepTarget::None,    0,   0, 1, "Cool down"},
};

const Preset kPresets[] = {
  {"4 x 4 min VO2max",     kVo2,       4},
  {"3 x 8 min threshold",  kThreshold, 4},
  {"3 x 12 min sweet spot",kSweetSpot, 4},
  {"20 x 30/30 s",         k3030,      4},
  {"8 x 20 s sprints",     kSprints,   4},
  {"Endurance, open",      kEndurance, 3},
  {"Cadence pyramid",      kCadence,   4},
};
const uint8_t kPresetCount = sizeof(kPresets) / sizeof(kPresets[0]);

}  // namespace

uint8_t Workout::presetCount() { return kPresetCount; }
const char* Workout::presetName(uint8_t i) {
  return i < kPresetCount ? kPresets[i].name : "";
}

const char* Workout::kindText(StepKind k) {
  switch (k) {
    case StepKind::Warmup:   return "WARM UP";
    case StepKind::Work:     return "WORK";
    case StepKind::Rest:     return "RECOVER";
    case StepKind::Cooldown: return "COOL DOWN";
    default: return "";
  }
}

const char* Workout::targetUnit(StepTarget t) {
  switch (t) {
    case StepTarget::Power:     return "W";
    case StepTarget::HeartRate: return "bpm";
    case StepTarget::Cadence:   return "rpm";
    default: return "";
  }
}

// --------------------------------------------------------------------------
void Workout::clear() {
  _count = _idx = 0;
  _name[0] = 0;
  _running = _finished = false;
  resetStep();
  _pending = WorkoutEvent::None;
}

void Workout::resetStep() {
  _stepMs = 0;
  _stepM = 0;
  _haveDistance = false;
  _compliance = Compliance::NoTarget;
  _value = 0;
  _outTimer = _inTimer = 0;
  _flaggedOut = false;
}

bool Workout::addStep(const WorkoutStep& s) {
  if (_count >= WORKOUT_MAX_STEPS) return false;
  _steps[_count++] = s;
  return true;
}

bool Workout::loadPreset(uint8_t i) {
  _error[0] = 0;
  if (i >= kPresetCount) { snprintf(_error, sizeof(_error), "no such workout"); return false; }
  clear();
  const Preset& p = kPresets[i];
  strncpy(_name, p.name, sizeof(_name) - 1);

  // Interleave the repeated pair: the table lists work and rest once each with
  // a repeat count, but the rider does them alternately.
  uint8_t k = 0;
  while (k < p.count) {
    uint8_t reps = p.steps[k].repeat ? p.steps[k].repeat : 1;
    // How many consecutive entries share this repeat count?
    uint8_t group = 1;
    while (k + group < p.count && p.steps[k + group].repeat == p.steps[k].repeat &&
           reps > 1) group++;
    for (uint8_t r = 0; r < reps; r++) {
      for (uint8_t g = 0; g < group; g++) {
        const PresetStep& ps = p.steps[k + g];
        WorkoutStep w;
        w.kind = ps.kind; w.durType = ps.dur; w.durValue = ps.value;
        w.target = ps.target;
        if (ps.target == StepTarget::Power) {
          // Presets are written as a share of threshold so they suit any rider;
          // resolve them against the FTP in settings.
          w.lo = (uint16_t)((uint32_t)ps.lo * g_settings.ftpWatts / 100);
          w.hi = (uint16_t)((uint32_t)ps.hi * g_settings.ftpWatts / 100);
        } else {
          w.lo = ps.lo; w.hi = ps.hi;
        }
        strncpy(w.name, ps.name, sizeof(w.name) - 1);
        if (!addStep(w)) { snprintf(_error, sizeof(_error), "too many steps"); return false; }
      }
    }
    k += group;
  }
  return _count > 0;
}

// --------------------------------------------------------------------------
// File format, one step per line:
//
//   name   4x4 VO2max
//   ftp    260
//   warmup time 12:00
//   repeat 4
//   work   time 4:00  power 280 320
//   rest   time 4:00  power 110 150
//   end
//   cooldown time 10:00
//
// Anything after # is a comment. Targets are absolute watts/bpm/rpm here -
// a file is written for a specific rider, unlike the presets.
namespace {

uint32_t parseTime(const char* s) {
  // "4:00" or "240"
  const char* colon = strchr(s, ':');
  if (colon) return ((uint32_t)atoi(s) * 60UL + (uint32_t)atoi(colon + 1)) * 1000UL;
  return (uint32_t)(atof(s) * 1000.0);
}

uint32_t parseDist(const char* s) {
  float v = atof(s);
  if (strstr(s, "km") || strstr(s, "KM")) v *= 1000.0f;
  else if (strstr(s, "mi") || strstr(s, "MI")) v *= 1609.344f;
  return (uint32_t)v;
}

// Split a line into whitespace-separated tokens, in place.
uint8_t tokenize(char* line, char* tok[], uint8_t maxTok) {
  uint8_t n = 0;
  char* p = line;
  while (*p && n < maxTok) {
    while (*p == ' ' || *p == '\t') p++;
    if (!*p) break;
    tok[n++] = p;
    while (*p && *p != ' ' && *p != '\t') p++;
    if (*p) *p++ = 0;
  }
  return n;
}

bool eq(const char* a, const char* b) { return strcasecmp(a, b) == 0; }

}  // namespace

bool Workout::loadFile(const char* path) {
  _error[0] = 0;
  File f = SD.open(path, FILE_READ);
  if (!f) { snprintf(_error, sizeof(_error), "cannot open file"); return false; }
  clear();
  strncpy(_name, strrchr(path, '/') ? strrchr(path, '/') + 1 : path, sizeof(_name) - 1);

  char line[128];
  uint8_t li = 0;
  int repeatStart = -1, repeatN = 1;
  bool overflow = false;

  auto handleLine = [&]() {
    line[li] = 0;
    li = 0;
    char* hash = strchr(line, '#');
    if (hash) *hash = 0;
    char* tok[8];
    uint8_t n = tokenize(line, tok, 8);
    if (!n) return;

    if (eq(tok[0], "name")) {
      // Everything after the keyword, spaces restored by tokenize's nulls.
      _name[0] = 0;
      for (uint8_t i = 1; i < n; i++) {
        if (i > 1) strncat(_name, " ", sizeof(_name) - strlen(_name) - 1);
        strncat(_name, tok[i], sizeof(_name) - strlen(_name) - 1);
      }
      return;
    }
    if (eq(tok[0], "repeat")) {
      repeatStart = _count;
      repeatN = n > 1 ? atoi(tok[1]) : 1;
      if (repeatN < 1) repeatN = 1;
      return;
    }
    if (eq(tok[0], "end") || eq(tok[0], "endrepeat")) {
      if (repeatStart >= 0 && repeatN > 1) {
        uint8_t blockLen = _count - repeatStart;
        for (int r = 1; r < repeatN; r++)
          for (uint8_t i = 0; i < blockLen; i++)
            if (!addStep(_steps[repeatStart + i])) { overflow = true; return; }
      }
      repeatStart = -1;
      repeatN = 1;
      return;
    }

    WorkoutStep w;
    if      (eq(tok[0], "warmup"))   w.kind = StepKind::Warmup;
    else if (eq(tok[0], "work"))     w.kind = StepKind::Work;
    else if (eq(tok[0], "rest") || eq(tok[0], "recover")) w.kind = StepKind::Rest;
    else if (eq(tok[0], "cooldown")) w.kind = StepKind::Cooldown;
    else return;                                   // unknown keyword, skip it

    uint8_t i = 1;
    if (i < n && eq(tok[i], "time"))      { w.durType = StepDuration::Time;
                                            if (i + 1 < n) w.durValue = parseTime(tok[++i]); i++; }
    else if (i < n && (eq(tok[i], "dist") || eq(tok[i], "distance")))
                                          { w.durType = StepDuration::Distance;
                                            if (i + 1 < n) w.durValue = parseDist(tok[++i]); i++; }
    else if (i < n && (eq(tok[i], "open") || eq(tok[i], "lap")))
                                          { w.durType = StepDuration::Open; i++; }
    else                                  { w.durType = StepDuration::Open; }

    if (i < n) {
      if      (eq(tok[i], "power"))   w.target = StepTarget::Power;
      else if (eq(tok[i], "hr"))      w.target = StepTarget::HeartRate;
      else if (eq(tok[i], "cadence")) w.target = StepTarget::Cadence;
      if (w.target != StepTarget::None) {
        if (i + 1 < n) w.lo = (uint16_t)atoi(tok[i + 1]);
        if (i + 2 < n) w.hi = (uint16_t)atoi(tok[i + 2]);
        if (w.hi < w.lo) { uint16_t t = w.lo; w.lo = w.hi; w.hi = t; }
      }
    }
    strncpy(w.name, kindText(w.kind), sizeof(w.name) - 1);
    if (!addStep(w)) overflow = true;
  };

  int c;
  uint8_t buf[256];
  int have = 0, pos = 0;
  for (;;) {
    if (pos >= have) {
      have = f.read(buf, sizeof(buf));
      pos = 0;
      if (have <= 0) break;
    }
    c = buf[pos++];
    if (c == '\n' || c == '\r') { if (li) handleLine(); }
    else if (li < sizeof(line) - 1) line[li++] = (char)c;
  }
  if (li) handleLine();
  f.close();

  if (overflow) snprintf(_error, sizeof(_error), "too many steps (max %d)", WORKOUT_MAX_STEPS);
  if (_count == 0) {
    snprintf(_error, sizeof(_error), "no steps found");
    clear();
    return false;
  }
  return true;
}

// --------------------------------------------------------------------------
uint32_t Workout::totalTimeMs() const {
  uint32_t t = 0;
  for (uint8_t i = 0; i < _count; i++) {
    if (_steps[i].durType != StepDuration::Time) return 0;   // open-ended
    t += _steps[i].durValue;
  }
  return t;
}

void Workout::start() {
  if (!loaded()) return;
  _idx = 0;
  _running = true;
  _finished = false;
  resetStep();
  _pending = WorkoutEvent::StepChanged;
}

void Workout::stop() {
  _running = false;
  _pending = WorkoutEvent::None;
}

void Workout::advance() {
  if (_idx + 1 >= _count) {
    _running = false;
    _finished = true;
    _pending = WorkoutEvent::Finished;
    return;
  }
  _idx++;
  resetStep();
  _pending = WorkoutEvent::StepChanged;
}

void Workout::skipStep() {
  if (_running) advance();
}

uint32_t Workout::stepRemainingMs() const {
  const WorkoutStep& s = current();
  if (s.durType != StepDuration::Time) return 0;
  return _stepMs >= s.durValue ? 0 : s.durValue - _stepMs;
}

float Workout::stepRemainingM() const {
  const WorkoutStep& s = current();
  if (s.durType != StepDuration::Distance) return 0;
  float rem = (float)s.durValue - (float)_stepM;
  return rem < 0 ? 0 : rem;
}

float Workout::stepProgress() const {
  const WorkoutStep& s = current();
  if (!s.durValue) return 0;
  if (s.durType == StepDuration::Time)
    return constrain((float)_stepMs / (float)s.durValue, 0.0f, 1.0f);
  if (s.durType == StepDuration::Distance)
    return constrain((float)_stepM / (float)s.durValue, 0.0f, 1.0f);
  return 0;
}

float Workout::totalProgress() const {
  if (!_count) return 0;
  return constrain((_idx + stepProgress()) / (float)_count, 0.0f, 1.0f);
}

void Workout::update(const RideState& s, uint32_t dtMs) {
  if (!_running || !_count) return;

  // Only a running ride advances a workout. An auto-paused rider at a junction
  // is not doing their interval, and the timer should not pretend otherwise.
  bool live = (s.status == RideStatus::Running);

  // Distance has to track deltas even while paused so the baseline stays right.
  if (!_haveDistance) { _lastDistance = s.distance; _haveDistance = true; }
  double dDist = s.distance - _lastDistance;
  if (dDist < 0) dDist = 0;
  _lastDistance = s.distance;

  if (live) {
    _stepMs += dtMs;
    _stepM += dDist;
  }

  // --- target compliance ---
  const WorkoutStep& st = current();
  if (st.target == StepTarget::None) {
    _compliance = Compliance::NoTarget;
    _value = 0;
    _outTimer = _inTimer = 0;
    _flaggedOut = false;
  } else {
    bool have = false;
    switch (st.target) {
      // Three-second power is what a rider can actually hold a range against;
      // instantaneous power swings far too much to chase.
      case StepTarget::Power:
        _value = s.power3s ? s.power3s : s.power; have = s.hasPwr; break;
      case StepTarget::HeartRate:
        _value = s.hr; have = s.hasHr; break;
      case StepTarget::Cadence:
        _value = s.cadence; have = s.hasCad; break;
      default: break;
    }
    if (!have) {
      _compliance = Compliance::NoTarget;
    } else {
      _compliance = _value < st.lo ? Compliance::Below
                  : _value > st.hi ? Compliance::Above : Compliance::InRange;
    }

    float dt = dtMs / 1000.0f;
    if (live && _compliance != Compliance::NoTarget) {
      if (_compliance == Compliance::InRange) { _inTimer += dt; _outTimer = 0; }
      else                                    { _outTimer += dt; _inTimer = 0; }

      if (!_flaggedOut && _outTimer >= WORKOUT_TARGET_DWELL_S) {
        _flaggedOut = true;
        if (_pending == WorkoutEvent::None) _pending = WorkoutEvent::WentOutOfTarget;
      } else if (_flaggedOut && _inTimer >= WORKOUT_TARGET_REARM_S) {
        _flaggedOut = false;
        if (_pending == WorkoutEvent::None) _pending = WorkoutEvent::BackInTarget;
      }
    }
  }

  // --- step end ---
  if (!live) return;
  if (st.durType == StepDuration::Time && _stepMs >= st.durValue) advance();
  else if (st.durType == StepDuration::Distance && _stepM >= st.durValue) advance();
  // Open steps end only when the rider presses LAP.
}

WorkoutEvent Workout::takeEvent() {
  WorkoutEvent e = _pending;
  _pending = WorkoutEvent::None;
  return e;
}
