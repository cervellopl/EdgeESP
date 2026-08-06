#pragma once
#include <Arduino.h>
#include "config.h"
#include "ride/RideComputer.h"

// Structured workouts: a flat list of steps, each with something that ends it
// and optionally a range to hold while it runs.
//
// Repeats are expanded at load time rather than nested, so the runtime is a
// plain index walk with nothing to get wrong at 4 a.m. on an interval.

enum class StepKind     : uint8_t { Warmup, Work, Rest, Cooldown };
enum class StepDuration : uint8_t { Time, Distance, Open };   // Open = press LAP
enum class StepTarget   : uint8_t { None, Power, HeartRate, Cadence };

struct WorkoutStep {
  StepKind     kind     = StepKind::Work;
  StepDuration durType  = StepDuration::Open;
  uint32_t     durValue = 0;        // milliseconds, or metres
  StepTarget   target   = StepTarget::None;
  uint16_t     lo = 0, hi = 0;
  char         name[18] = {0};      // optional, from the file
};

enum class WorkoutEvent : uint8_t {
  None, StepChanged, Finished, WentOutOfTarget, BackInTarget
};
enum class Compliance : uint8_t { NoTarget, Below, InRange, Above };

class Workout {
 public:
  // --- loading ---
  static uint8_t     presetCount();
  static const char* presetName(uint8_t i);
  bool loadPreset(uint8_t i);
  bool loadFile(const char* path);          // "/workouts/vo2.wko"
  void clear();

  bool loaded() const { return _count > 0; }
  const char* name() const { return _name; }
  const char* lastError() const { return _error; }
  uint8_t stepCount() const { return _count; }
  const WorkoutStep& step(uint8_t i) const { return _steps[i < _count ? i : 0]; }
  uint32_t totalTimeMs() const;             // 0 if any step is open-ended

  // --- running ---
  void start();
  void stop();
  bool running() const { return _running; }
  bool finished() const { return _finished; }
  void skipStep();                          // LAP, or an Open step ending

  // Call at the ride update rate while the ride is live.
  void update(const RideState& s, uint32_t dtMs);

  uint8_t currentIndex() const { return _idx; }
  const WorkoutStep& current() const { return _steps[_idx < _count ? _idx : 0]; }
  const WorkoutStep* next() const { return _idx + 1 < _count ? &_steps[_idx + 1] : nullptr; }

  uint32_t stepElapsedMs() const { return _stepMs; }
  uint32_t stepRemainingMs() const;         // Time steps only, else 0
  float    stepRemainingM() const;          // Distance steps only, else 0
  float    stepProgress() const;            // 0..1; 0 for Open steps
  float    totalProgress() const;           // 0..1 across the whole session

  Compliance compliance() const { return _compliance; }
  uint16_t   currentValue() const { return _value; }   // the metric being targeted

  WorkoutEvent takeEvent();

  static const char* kindText(StepKind k);
  static const char* targetUnit(StepTarget t);

 private:
  WorkoutStep _steps[WORKOUT_MAX_STEPS];
  uint8_t     _count = 0, _idx = 0;
  char        _name[28] = {0};
  char        _error[40] = {0};

  bool     _running = false, _finished = false;
  uint32_t _stepMs = 0;
  double   _stepM = 0;
  double   _lastDistance = 0;
  bool     _haveDistance = false;

  Compliance _compliance = Compliance::NoTarget;
  uint16_t   _value = 0;
  float      _outTimer = 0, _inTimer = 0;
  bool       _flaggedOut = false;
  WorkoutEvent _pending = WorkoutEvent::None;

  void advance();
  void resetStep();
  bool addStep(const WorkoutStep& s);
};

extern Workout g_workout;
