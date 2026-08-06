// Host-side exercise of TSS scoring and the fitness/fatigue model.
#include "ride/TrainingLoad.h"
#include <Preferences.h>
#include <stdio.h>
#include <math.h>
#include <string.h>

unsigned long g_fakeMillis = 0;
Preferences::Ent  Preferences::s_ents[48] = {};
Preferences::Blob Preferences::s_blobs[4] = {};

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

static TrainingLoad tl;
static const uint32_t DAY = 86400UL;
static const uint32_t D0  = 1754000000UL;    // a plausible 2025 timestamp

int main() {
  head("power TSS is Coggan's definition");
  // An hour at exactly threshold is 100 by construction. If this drifts, every
  // number on the page and every day of history drifts with it.
  near(TrainingLoad::powerTss(3600000UL, 250, 250), 100.0, 0.01, "one hour at FTP is 100");
  near(TrainingLoad::powerTss(1800000UL, 250, 250), 50.0, 0.01, "half an hour is 50");
  // Intensity enters squared: 80 % of FTP for an hour is 64, not 80.
  near(TrainingLoad::powerTss(3600000UL, 200, 250), 64.0, 0.01, "an hour at 0.8 IF is 64");
  near(TrainingLoad::powerTss(3600000UL, 275, 250), 121.0, 0.02, "an hour at 1.1 IF is 121");
  near(TrainingLoad::powerTss(7200000UL, 175, 250), 98.0, 0.05, "two hours at 0.7 IF");
  near(TrainingLoad::intensityFactor(200, 250), 0.8, 0.001, "intensity factor");

  ok(TrainingLoad::powerTss(3600000UL, 250, 0) == 0, "no FTP, no score");
  ok(TrainingLoad::powerTss(3600000UL, 0, 250) == 0, "no power, no score");
  ok(TrainingLoad::powerTss(0, 250, 250) == 0, "no time, no score");

  head("heart-rate TSS");
  near(TrainingLoad::hrTss(3600000UL, 165, 165), 100.0, 0.01, "an hour at LTHR is 100");
  near(TrainingLoad::hrTss(3600000UL, 148, 165), 80.5, 0.5, "an hour below threshold");
  ok(TrainingLoad::hrTss(3600000UL, 150, 0) == 0, "no LTHR, no score");

  head("a day of history");
  tl.clearHistory();
  ok(!tl.haveDate(), "no date until told one");
  tl.setNow(D0);
  ok(tl.haveDate(), "date accepted");
  tl.addTss(80);
  near(tl.dayTss(0), 80.0, 0.05, "today holds it");
  tl.addTss(30);
  near(tl.dayTss(0), 110.0, 0.05, "two rides in a day add up");
  near(tl.weekTss(), 110.0, 0.05, "and the week total");

  head("rolling over midnight");
  tl.setNow(D0 + DAY);
  near(tl.dayTss(1), 110.0, 0.05, "yesterday is where it was");
  near(tl.dayTss(0), 0.0, 0.001, "today starts empty");
  tl.addTss(50);
  near(tl.weekTss(), 160.0, 0.05, "the week spans both");

  // A garbled fix or a backwards clock must not shuffle the history.
  tl.setNow(D0);
  near(tl.dayTss(0), 50.0, 0.05, "a backwards clock is ignored");
  tl.setNow(1000000UL);
  near(tl.dayTss(0), 50.0, 0.05, "a pre-2020 timestamp is ignored");

  head("a long break clears the window");
  // Jumping from D0+1 to D0+3 shifts twice: the D0+1 entry is now two days ago
  // and the D0 entry three, with the two skipped days left empty.
  tl.setNow(D0 + DAY * 3);
  near(tl.dayTss(0), 0.0, 0.001, "the day we jumped to is empty");
  near(tl.dayTss(1), 0.0, 0.001, "so is the day skipped over");
  near(tl.dayTss(2), 50.0, 0.05, "the older entries slid back by two");
  near(tl.dayTss(3), 110.0, 0.05, "and are all still in range");
  tl.setNow(D0 + DAY * 200);
  near(tl.weekTss(), 0.0, 0.001, "two hundred days away leaves nothing");
  near(tl.ctl(), 0.0, 0.001, "and no fitness");

  head("fitness and fatigue converge on a steady diet");
  tl.clearHistory();
  tl.setNow(D0);
  // 100 TSS every day for a long time: both curves should approach 100, with
  // the 7-day one much closer than the 42-day one after 60 days.
  for (int d = 0; d < 60; d++) {
    tl.setNow(D0 + DAY * d);
    tl.addTss(100);
  }
  printf("       after 60 days at 100/day: ctl=%.1f atl=%.1f tsb=%.1f\n",
         tl.ctl(), tl.atl(), tl.tsb());
  ok(tl.atl() > 95.0f, "fatigue is nearly at the daily load");
  ok(tl.ctl() > 50.0f && tl.ctl() < 95.0f, "fitness is still climbing toward it");
  ok(tl.ctl() < tl.atl(), "fitness lags fatigue while load is constant");
  ok(tl.tsb() < 0, "and form is negative under constant training");

  head("rest days lift form");
  float loadedTsb = tl.tsb();
  for (int d = 60; d < 67; d++) tl.setNow(D0 + DAY * d);   // a week off, no TSS
  printf("       after a week off: ctl=%.1f atl=%.1f tsb=%+.1f\n",
         tl.ctl(), tl.atl(), tl.tsb());
  ok(tl.tsb() > loadedTsb, "form improves with rest");
  ok(tl.tsb() > 0, "a week off puts form positive");
  ok(tl.atl() < tl.ctl(), "fatigue falls away faster than fitness");

  head("an unsaved ride can be folded in");
  float before = tl.tsb();
  float after = tl.tsb(150);
  printf("       tsb now %+.1f, with a 150 TSS ride %+.1f\n", before, after);
  ok(after < before, "a hard ride today lowers projected form");
  ok(tl.ctl(150) > tl.ctl(), "and raises fitness");
  near(tl.ctl(0), tl.ctl(), 0.001, "zero extra changes nothing");

  head("bad input is refused");
  tl.clearHistory();
  tl.setNow(D0);
  tl.addTss(-50);
  near(tl.dayTss(0), 0.0, 0.001, "a negative score is ignored");
  tl.addTss(NAN);
  near(tl.dayTss(0), 0.0, 0.001, "NaN is ignored");
  tl.addTss(0);
  near(tl.dayTss(0), 0.0, 0.001, "zero adds nothing");
  // The daily bucket is a uint16 of tenths, so it has to saturate rather than wrap.
  for (int i = 0; i < 100; i++) tl.addTss(500);
  printf("       after 100 x 500 TSS: today reads %.0f\n", tl.dayTss(0));
  ok(tl.dayTss(0) > 6000.0f && tl.dayTss(0) <= 6553.6f, "the bucket saturates, never wraps");

  head("save and reload");
  Preferences::wipe();
  tl.clearHistory();
  tl.setNow(D0);
  tl.addTss(120);
  tl.setNow(D0 + DAY);
  tl.addTss(60);
  float ctlBefore = tl.ctl();
  tl.save();

  TrainingLoad t2;
  t2.begin();
  near(t2.dayTss(0), 60.0, 0.05, "today survived");
  near(t2.dayTss(1), 120.0, 0.05, "yesterday survived");
  near(t2.ctl(), ctlBefore, 0.01, "and the curves come back the same");
  // Reloading must not reset the calendar, or the next ride lands on the wrong day.
  t2.setNow(D0 + DAY);
  near(t2.dayTss(1), 120.0, 0.05, "the stored date carried over, so nothing shifted");

  head("a corrupt store starts clean");
  Preferences::wipe();
  Preferences::poke("load", "day", 4000000000.0);
  TrainingLoad t3;
  t3.begin();
  ok(!t3.haveDate(), "an impossible stored date is discarded");
  near(t3.weekTss(), 0.0, 0.001, "with an empty history");

  printf("\n%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
