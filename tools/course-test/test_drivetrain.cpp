// Host-side exercise of gear inference from speed and cadence.
#include "ride/Drivetrain.h"
#include <stdio.h>
#include <math.h>
#include <string.h>

unsigned long g_fakeMillis = 0;

static int failures = 0, checks = 0;
static void ok(bool cond, const char* what, const char* detail = "") {
  checks++;
  if (!cond) { failures++; printf("  FAIL  %s  %s\n", what, detail); }
  else       { printf("  ok    %s %s\n", what, detail); }
}
static void near(double got, double want, double tol, const char* what) {
  char d[128];
  snprintf(d, sizeof(d), "(got %.3f, want %.3f)", got, want);
  ok(fabs(got - want) <= tol, what, d);
}
static void head(const char* s) { printf("\n== %s ==\n", s); }

static Drivetrain dt;
static const uint16_t WHEEL = 2105;      // 700x25c

// The speed a given gear produces at a given cadence, which is how a rider
// would actually be moving in it.
static float speedFor(uint8_t ring, uint8_t sprocket, uint8_t cadence) {
  return Drivetrain::developmentOf(ring, sprocket, WHEEL) * cadence / 60.0f;
}

// Feed the gear long enough for the hysteresis to settle.
static void hold(Drivetrain& d, uint8_t ring, uint8_t sprocket, uint8_t cadence,
                 int seconds = 3) {
  for (int i = 0; i < seconds * 10; i++)
    d.update(speedFor(ring, sprocket, cadence), cadence, WHEEL, 100);
}

int main() {
  head("the gearing table");
  ok(Drivetrain::presetCount() == DRIVETRAIN_PRESET_COUNT, "preset count matches config.h");
  for (uint8_t i = 0; i < Drivetrain::presetCount(); i++) {
    const DrivetrainSpec& s = Drivetrain::preset(i);
    char d[80];
    snprintf(d, sizeof(d), "\"%s\" %ux%u", s.name, s.ringCount, s.sprocketCount);
    ok(s.ringCount >= 1 && s.ringCount <= 3 &&
       s.sprocketCount >= 8 && s.sprocketCount <= 12, d);
    // Sprockets must ascend, or "one harder" walks the wrong way.
    bool ordered = true;
    for (uint8_t c = 1; c < s.sprocketCount; c++)
      if (s.sprockets[c] <= s.sprockets[c - 1]) ordered = false;
    ok(ordered, "sprockets run smallest to largest");
  }
  ok(!strcmp(Drivetrain::preset(200).name, Drivetrain::preset(0).name),
     "an out-of-range preset clamps rather than reading past the table");

  head("the arithmetic");
  // 50x17 on a 2105 mm wheel: ratio 2.94, development 6.19 m, ~77.6 gear inches.
  near(Drivetrain::ratioOf(50, 17), 2.941, 0.002, "ratio is teeth over teeth");
  near(Drivetrain::developmentOf(50, 17, WHEEL), 6.19, 0.02,
       "development is metres per crank revolution");
  near(Drivetrain::gearInchesOf(50, 17, WHEEL), 77.6, 0.5, "gear inches");
  // A bigger wheel is a bigger gear for the same sprockets.
  ok(Drivetrain::gearInchesOf(50, 17, 2326) > Drivetrain::gearInchesOf(50, 17, WHEEL),
     "a 29er reads a taller gear than a 700x25c");
  near(Drivetrain::ratioOf(50, 0), 0.0, 0.001, "a zero sprocket does not divide by zero");

  head("identifying the gear you are actually in");
  dt.setPreset(0);                       // 50/34 x 11-28
  const DrivetrainSpec& s0 = dt.spec();
  hold(dt, 50, 17, 90);
  printf("       50x17 at 90 rpm -> %ux%u, %.2f ratio, %.1f in\n",
         dt.ringTeeth(), dt.sprocketTeeth(), dt.ratio(), dt.gearInches());
  ok(dt.valid(), "a gear is identified");
  ok(dt.ringTeeth() == 50 && dt.sprocketTeeth() == 17, "the right one");
  near(dt.ratio(), Drivetrain::ratioOf(50, 17), 0.02, "measured ratio matches");
  near(dt.development(), 6.19, 0.05, "development");

  // Every combination in the table must be recoverable from its own speed.
  head("every gear round-trips");
  int wrong = 0;
  for (uint8_t r = 0; r < s0.ringCount; r++) {
    for (uint8_t c = 0; c < s0.sprocketCount; c++) {
      dt.reset();
      hold(dt, s0.rings[r], s0.sprockets[c], 85);
      if (!dt.valid() || dt.sprocketTeeth() != s0.sprockets[c]) {
        printf("       %ux%u came back as %ux%u\n", s0.rings[r], s0.sprockets[c],
               dt.ringTeeth(), dt.sprocketTeeth());
        wrong++;
      }
    }
  }
  // The sprocket is the reliable half: with the ring known, the ratio pins it.
  ok(wrong == 0, "the sprocket is recovered for every combination");

  head("coasting and crawling produce no gear");
  dt.reset();
  hold(dt, 50, 17, 90);
  ok(dt.valid(), "a gear to start from");
  dt.update(8.0f, 0, WHEEL, 100);
  ok(!dt.valid(), "zero cadence is coasting, not an infinite gear");
  dt.reset();
  hold(dt, 50, 17, 90);
  dt.update(0.5f, 90, WHEEL, 100);
  ok(!dt.valid(), "and a crawl is too slow to measure");
  dt.reset();
  dt.update(8.0f, 15, WHEEL, 100);
  ok(!dt.valid(), "so is a very low cadence");

  head("a ratio that matches nothing is refused");
  dt.reset();
  // 8 m/s at 90 rpm on this wheel is a ratio of about 2.5 - fine. But a
  // ridiculous speed for the cadence means the wheel size or cassette is wrong,
  // and naming a gear would be fiction.
  for (int i = 0; i < 40; i++) dt.update(30.0f, 60, WHEEL, 100);
  ok(!dt.valid(), "an impossible ratio names no gear");

  head("hysteresis stops the display flickering");
  dt.reset();
  hold(dt, 50, 17, 90);
  uint8_t was = dt.sprocketTeeth();
  // One stray sample from the neighbouring sprocket must not move the display.
  dt.update(speedFor(50, 16, 90), 90, WHEEL, 100);
  ok(dt.sprocketTeeth() == was, "a single odd sample is ignored");
  // A real shift, held, does move it.
  hold(dt, 50, 16, 90);
  ok(dt.sprocketTeeth() == 16, "a sustained change is taken");

  head("cross-chaining");
  dt.reset();
  hold(dt, 50, s0.sprockets[s0.sprocketCount - 1], 80);   // big ring, biggest cog
  printf("       %ux%u -> cross-chained %s\n", dt.ringTeeth(), dt.sprocketTeeth(),
         dt.crossChained() ? "yes" : "no");
  ok(dt.crossChained(), "big ring on the biggest cog is flagged");
  dt.reset();
  hold(dt, 50, 15, 90);
  ok(!dt.crossChained(), "the middle of the block is not");

  head("a 1x drivetrain can never cross-chain");
  uint8_t oneByIdx = 0;
  for (uint8_t i = 0; i < Drivetrain::presetCount(); i++)
    if (Drivetrain::preset(i).ringCount == 1) { oneByIdx = i; break; }
  dt.setPreset(oneByIdx);
  const DrivetrainSpec& s1 = dt.spec();
  ok(s1.ringCount == 1, "found a 1x preset");
  dt.reset();
  hold(dt, s1.rings[0], s1.sprockets[s1.sprocketCount - 1], 70);
  ok(dt.valid(), "the biggest cog is identified");
  ok(!dt.crossChained(), "and it is not cross-chaining - there is nowhere else to be");
  ok(!dt.ambiguous(), "with one ring there is nothing to be ambiguous about");

  head("changing the gearing resets the reading");
  dt.setPreset(0);
  hold(dt, 50, 17, 90);
  ok(dt.valid(), "a gear is showing");
  dt.setPreset(4);
  ok(!dt.valid(), "switching drivetrain clears it rather than showing a stale gear");

  printf("\n%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
