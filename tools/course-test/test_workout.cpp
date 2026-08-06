// Host-side exercise of the workout engine: presets, the .wko parser, step
// advance, and target compliance.
#include "ride/Workout.h"
#include "Settings.h"
#include <Preferences.h>
#include <stdio.h>
#include <math.h>

unsigned long g_fakeMillis = 0;
Preferences::Ent  Preferences::s_ents[48] = {};
Preferences::Blob Preferences::s_blobs[4] = {};

static int failures = 0, checks = 0;
static void ok(bool cond, const char* what, const char* detail = "") {
  checks++;
  if (!cond) { failures++; printf("  FAIL  %s  %s\n", what, detail); }
  else       { printf("  ok    %s %s\n", what, detail); }
}
static void eqi(long got, long want, const char* what) {
  char d[96];
  snprintf(d, sizeof(d), "(got %ld, want %ld)", got, want);
  ok(got == want, what, d);
}
static void head(const char* s) { printf("\n== %s ==\n", s); }

static Workout wk;
static RideState st;

static void writeFile(const char* path, const char* body) {
  FILE* f = fopen(path, "wb");
  fwrite(body, 1, strlen(body), f);
  fclose(f);
}

// Feed the engine one second at a time at a given power.
static void run(Workout& w, int seconds, uint16_t power = 0, float mps = 0,
                RideStatus status = RideStatus::Running) {
  for (int i = 0; i < seconds; i++) {
    st.status = status;
    st.hasPwr = power > 0;
    st.power = power;
    st.power3s = power;
    st.distance += mps;
    w.update(st, 1000);
  }
}

int main() {
  g_settings.resetToDefaults();
  g_settings.ftpWatts = 250;
  st = RideState();

  head("built-in presets");
  ok(Workout::presetCount() >= 5, "several presets exist");
  ok(wk.loadPreset(0), "the first preset loads");
  printf("       \"%s\" -> %u steps\n", wk.name(), wk.stepCount());
  // Warm up + 4x(work, rest) + cool down.
  eqi(wk.stepCount(), 10, "4x4 expands to ten steps");
  ok(wk.step(0).kind == StepKind::Warmup, "starts with a warm up");
  ok(wk.step(1).kind == StepKind::Work, "then work");
  ok(wk.step(2).kind == StepKind::Rest, "then recovery");
  ok(wk.step(9).kind == StepKind::Cooldown, "ends with a cool down");
  eqi(wk.step(1).durValue, 4 * 60000L, "the interval is four minutes");

  head("preset power targets scale with FTP");
  // The VO2 step is written as 106-120 % of threshold.
  eqi(wk.step(1).lo, 250 * 106 / 100, "lower target at 250 W FTP");
  eqi(wk.step(1).hi, 250 * 120 / 100, "upper target at 250 W FTP");
  g_settings.ftpWatts = 300;
  wk.loadPreset(0);
  eqi(wk.step(1).lo, 300 * 106 / 100, "a different FTP moves the target");
  g_settings.ftpWatts = 250;
  wk.loadPreset(0);

  ok(!wk.loadPreset(99), "an out-of-range preset is refused");

  head("every preset expands within the step limit");
  for (uint8_t i = 0; i < Workout::presetCount(); i++) {
    Workout w2;
    bool good = w2.loadPreset(i);
    char d[80];
    snprintf(d, sizeof(d), "\"%s\" -> %u steps", Workout::presetName(i), w2.stepCount());
    ok(good && w2.stepCount() > 0 && w2.stepCount() <= WORKOUT_MAX_STEPS, d);
  }

  head(".wko file parsing");
  writeFile("t_basic.wko",
            "# a comment line\n"
            "name Test Session\n"
            "warmup time 10:00\n"
            "repeat 3\n"
            "work time 2:30 power 280 320\n"
            "rest time 1:00 power 100 140\n"
            "end\n"
            "cooldown time 5:00\n");
  ok(wk.loadFile("t_basic.wko"), "loads");
  printf("       \"%s\" -> %u steps\n", wk.name(), wk.stepCount());
  ok(!strcmp(wk.name(), "Test Session"), "name with spaces survives");
  eqi(wk.stepCount(), 8, "repeat 3 expands the block");
  eqi(wk.step(1).durValue, 150000L, "2:30 parses to 150 s");
  eqi(wk.step(1).lo, 280, "target low");
  eqi(wk.step(1).hi, 320, "target high");
  ok(wk.step(1).target == StepTarget::Power, "power target");
  ok(wk.step(2).kind == StepKind::Rest, "the pair alternates through the repeat");
  ok(wk.step(3).kind == StepKind::Work, "second repetition starts again with work");
  ok(wk.step(7).kind == StepKind::Cooldown, "the tail after end is kept");

  head("durations, targets and defaults");
  writeFile("t_mixed.wko",
            "name Mixed\n"
            "warmup time 300\n"          // bare seconds
            "work dist 5km hr 150 165\n"
            "work dist 800 cadence 95 105\n"
            "work open power 200 240\n"
            "work time 1:00\n");         // no target
  ok(wk.loadFile("t_mixed.wko"), "loads");
  eqi(wk.step(0).durValue, 300000L, "bare seconds parse as time");
  ok(wk.step(1).durType == StepDuration::Distance, "dist step");
  eqi(wk.step(1).durValue, 5000, "5km parses to metres");
  ok(wk.step(1).target == StepTarget::HeartRate, "hr target");
  eqi(wk.step(2).durValue, 800, "bare metres");
  ok(wk.step(2).target == StepTarget::Cadence, "cadence target");
  ok(wk.step(3).durType == StepDuration::Open, "open step");
  ok(wk.step(4).target == StepTarget::None, "a step with no target");

  head("malformed input is survivable");
  writeFile("t_junk.wko", "name Junk\nnonsense here\nwork time 1:00\nwibble\n");
  ok(wk.loadFile("t_junk.wko"), "loads what it can");
  eqi(wk.stepCount(), 1, "unknown keywords are skipped, not fatal");
  writeFile("t_empty.wko", "name Nothing\n# no steps\n");
  ok(!wk.loadFile("t_empty.wko"), "a file with no steps is refused");
  printf("       error: \"%s\"\n", wk.lastError());
  ok(!wk.loadFile("t_missing.wko"), "a missing file is refused");
  writeFile("t_swap.wko", "name Swapped\nwork time 1:00 power 320 280\n");
  wk.loadFile("t_swap.wko");
  ok(wk.step(0).lo == 280 && wk.step(0).hi == 320, "a reversed range is put right");

  head("step advance on time");
  st = RideState();
  writeFile("t_run.wko",
            "name Run\n"
            "warmup time 0:10\n"
            "work time 0:20 power 200 250\n"
            "cooldown time 0:10\n");
  wk.loadFile("t_run.wko");
  wk.start();
  ok(wk.running(), "running after start");
  ok(wk.takeEvent() == WorkoutEvent::StepChanged, "starting announces the first step");
  eqi(wk.currentIndex(), 0, "on the first step");
  run(wk, 9, 220);
  eqi(wk.currentIndex(), 0, "still on it at 9 s");
  eqi(wk.stepRemainingMs(), 1000, "one second left");
  run(wk, 1, 220);
  eqi(wk.currentIndex(), 1, "advanced at 10 s");
  ok(wk.takeEvent() == WorkoutEvent::StepChanged, "and said so");
  run(wk, 20, 220);
  eqi(wk.currentIndex(), 2, "advanced again");
  run(wk, 10, 220);
  ok(wk.finished(), "workout finished");
  ok(!wk.running(), "and stopped running");
  ok(wk.takeEvent() == WorkoutEvent::Finished, "raised the finish event");

  head("a paused ride holds the workout");
  wk.loadFile("t_run.wko");
  wk.start(); wk.takeEvent();
  run(wk, 30, 220, 0, RideStatus::AutoPaused);
  eqi(wk.currentIndex(), 0, "thirty paused seconds advance nothing");
  eqi(wk.stepElapsedMs(), 0, "and the step timer did not move");
  run(wk, 10, 220);
  eqi(wk.currentIndex(), 1, "it resumes when the ride does");

  head("distance steps and open steps");
  st = RideState();
  writeFile("t_dist.wko", "name Dist\nwork dist 100\nwork open power 200 240\nwork time 0:05\n");
  wk.loadFile("t_dist.wko");
  wk.start(); wk.takeEvent();
  run(wk, 9, 0, 10.0f);            // 90 m
  eqi(wk.currentIndex(), 0, "still on the distance step at 90 m");
  run(wk, 2, 0, 10.0f);
  eqi(wk.currentIndex(), 1, "advanced past 100 m");
  run(wk, 120, 220, 10.0f);
  eqi(wk.currentIndex(), 1, "an open step never times out");
  wk.skipStep();
  eqi(wk.currentIndex(), 2, "LAP ends an open step");

  head("target compliance");
  st = RideState();
  writeFile("t_target.wko", "name Target\nwork time 10:00 power 200 250\n");
  wk.loadFile("t_target.wko");
  wk.start(); wk.takeEvent();
  run(wk, 1, 225);
  ok(wk.compliance() == Compliance::InRange, "225 W is inside 200-250");
  run(wk, 1, 180);
  ok(wk.compliance() == Compliance::Below, "180 W is under");
  run(wk, 1, 300);
  ok(wk.compliance() == Compliance::Above, "300 W is over");
  eqi(wk.currentValue(), 300, "reports the value it judged");

  // No sensor means no judgement - an absent power meter must not read as zero
  // watts and trigger an endless "too easy".
  st.hasPwr = false; st.power = 0; st.power3s = 0;
  st.status = RideStatus::Running;
  wk.update(st, 1000);
  ok(wk.compliance() == Compliance::NoTarget, "no sensor, no compliance");

  head("out-of-target alerting has a dwell");
  wk.loadFile("t_target.wko");
  wk.start(); wk.takeEvent();
  run(wk, WORKOUT_TARGET_DWELL_S - 1, 150);
  ok(wk.takeEvent() == WorkoutEvent::None, "a brief dip says nothing");
  run(wk, 2, 150);
  ok(wk.takeEvent() == WorkoutEvent::WentOutOfTarget, "a sustained one complains");
  run(wk, 10, 150);
  ok(wk.takeEvent() == WorkoutEvent::None, "and does not nag every second");
  run(wk, WORKOUT_TARGET_REARM_S + 1, 225);
  ok(wk.takeEvent() == WorkoutEvent::BackInTarget, "recovering is acknowledged");

  head("total time");
  wk.loadFile("t_run.wko");
  eqi(wk.totalTimeMs(), 40000L, "sums the timed steps");
  wk.loadFile("t_dist.wko");
  eqi(wk.totalTimeMs(), 0, "unknown when a step is open-ended");

  head("clear");
  wk.clear();
  ok(!wk.loaded(), "cleared");
  ok(!wk.running(), "not running");
  st.status = RideStatus::Running;
  wk.update(st, 1000);            // must not crash with nothing loaded
  ok(true, "update with no workout is safe");

  printf("\n%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
