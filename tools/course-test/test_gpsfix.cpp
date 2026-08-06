// Host-side exercise of the GPS dropout warning: its thresholds, its dwell
// times, and above all the flickering fix a city street produces.
#include "gps/GpsWarn.h"
#include <stdio.h>

unsigned long g_fakeMillis = 0;

static int failures = 0, checks = 0;
static void ok(bool cond, const char* what, const char* detail = "") {
  checks++;
  if (!cond) { failures++; printf("  FAIL  %s  %s\n", what, detail); }
  else       { printf("  ok    %s %s\n", what, detail); }
}
static void head(const char* s) { printf("\n== %s ==\n", s); }

// Readings a rider actually gets, named rather than spelled out at each call.
static const float ACC_GOOD = 4.0f, ACC_LOOSE = GPS_ACC_WARN_M + 5.0f;

// One second of a healthy 3D fix.
static GpsEvent good(GpsWatch& w)   { w.update(true,  ACC_GOOD,  0, 1000); return w.takeEvent(); }
// One second of a fix the navigation would refuse to snap with.
static GpsEvent loose(GpsWatch& w)  { w.update(true,  ACC_LOOSE, 0, 1000); return w.takeEvent(); }
// One second with no fix, but the receiver still reporting.
static GpsEvent nofix(GpsWatch& w)  { w.update(false, 99.0f,     1, 1000); return w.takeEvent(); }
// One second with nothing coming out of the receiver at all.
static GpsEvent silent(GpsWatch& w) { w.update(false, 99.0f, GPS_SILENT_S, 1000); return w.takeEvent(); }

// Count the events of one kind over n seconds of the same conditions.
static int run(GpsWatch& w, GpsEvent (*step)(GpsWatch&), int n, GpsEvent want) {
  int seen = 0;
  for (int i = 0; i < n; i++) if (step(w) == want) seen++;
  return seen;
}

// A watch that has already seen the sky, which is the state every warning
// needs: the machine says nothing until a first fix has arrived.
static GpsWatch armed() {
  GpsWatch w;
  for (int i = 0; i < 3; i++) good(w);
  return w;
}

int main() {
  head("which tier a reading belongs to");
  ok(gpsWarnTier(true,  ACC_GOOD,  0) == 0, "a tight 3D fix is fine");
  ok(gpsWarnTier(true,  GPS_ACC_WARN_M - 1.0f, 0) == 0, "just inside the accuracy limit");
  ok(gpsWarnTier(true,  GPS_ACC_WARN_M, 0) == 1, "on it counts as too loose to navigate on");
  ok(gpsWarnTier(false, 99.0f, 0) == 2, "no fix outranks any accuracy figure");
  ok(gpsWarnTier(false, 99.0f, GPS_SILENT_S) == 3, "a silent receiver is its own failure");
  // A module can hold its last solution flags while having stopped talking, so
  // silence has to win over a fix that looks perfectly healthy.
  ok(gpsWarnTier(true, ACC_GOOD, GPS_SILENT_S) == 3,
     "and it wins even when the last message said 3D");

  head("nothing is said before the first fix of the day");
  GpsWatch cold;
  // A cold start indoors: no fix, and staleSeconds() reads 0xFFFF until the
  // first message ever arrives. Neither is news - the boot screen said it.
  int spoke = 0;
  for (int i = 0; i < 300; i++) {
    cold.update(false, 99.0f, 0xFFFF, 1000);
    if (cold.takeEvent() != GpsEvent::None) spoke++;
  }
  printf("       %d warnings across five minutes of cold start\n", spoke);
  ok(spoke == 0, "five minutes waiting for a first fix warns about nothing");
  ok(!cold.warning(), "and leaves no warning standing");

  head("a dropout shorter than the dwell says nothing");
  GpsWatch w = armed();
  // A bridge, an underpass, a row of trees: seconds, not minutes.
  ok(run(w, nofix, GPS_LOST_HOLD_S - 1, GpsEvent::Lost) == 0, "no fix, just under the hold");
  ok(run(w, good, GPS_FIX_HOLD_S, GpsEvent::Reacquired) == 0,
     "and coming back is not announced either, having never been announced lost");
  ok(!w.warning(), "no warning was ever raised");

  head("a real dropout warns exactly once");
  w = armed();
  int lost = run(w, nofix, 120, GpsEvent::Lost);
  printf("       %d warnings across two minutes with no fix\n", lost);
  ok(lost == 1, "warned once");
  ok(w.tier() == 2, "and holds the lost tier while it lasts");
  ok(w.outageSeconds() >= 119, "the outage clock runs for the whole dropout");

  head("getting it back is announced, with how long it was gone");
  int back = run(w, good, GPS_FIX_HOLD_S, GpsEvent::Reacquired);
  ok(back == 1, "reacquired said once");
  ok(!w.warning(), "the warning is down");
  // The five seconds of recovery are not part of the outage.
  printf("       outage reported as %u s\n", w.outageSeconds());
  ok(w.outageSeconds() == 120, "and reports the outage it actually was");
  ok(run(w, good, 60, GpsEvent::Reacquired) == 0, "a minute of good fix says nothing more");

  head("a second dropout warns again");
  ok(run(w, nofix, GPS_LOST_HOLD_S, GpsEvent::Lost) == 1,
     "the warning re-arms once the fix genuinely came back");

  head("a fix that flickers is still an outage");
  w = armed();
  // Under a city street the receiver hands out one usable second in five. The
  // outage clock must not restart on each of them, or a rider with no usable
  // position for ten minutes is never told.
  int flicker = 0;
  for (int i = 0; i < 60; i++) {
    GpsEvent e = (i % 5 == 4) ? good(w) : nofix(w);
    if (e == GpsEvent::Lost) flicker++;
  }
  printf("       %d warnings across a minute of one-in-five fixes\n", flicker);
  ok(flicker == 1, "the flicker is called what it is, once");
  ok(w.warning(), "and the warning still stands at the end of it");

  head("a recovery has to hold before it counts");
  w = armed();
  run(w, nofix, GPS_LOST_HOLD_S, GpsEvent::Lost);
  ok(run(w, good, GPS_FIX_HOLD_S - 1, GpsEvent::Reacquired) == 0,
     "one second short of the hold is not a recovery");
  ok(w.warning(), "so the warning stays up");
  ok(run(w, nofix, 1, GpsEvent::Lost) == 0, "and losing it again is not a fresh warning");
  ok(run(w, good, GPS_FIX_HOLD_S, GpsEvent::Reacquired) == 1, "the recovery that holds clears it");

  head("a sloppy fix is given longer to prove itself");
  w = armed();
  ok(run(w, loose, GPS_LOST_HOLD_S, GpsEvent::Degraded) == 0,
     "a wander that would have been a loss is not yet a complaint");
  ok(run(w, loose, GPS_DEGRADED_HOLD_S - GPS_LOST_HOLD_S, GpsEvent::Degraded) == 1,
     "but it is once it has held for its own dwell");
  ok(run(w, loose, 120, GpsEvent::Degraded) == 0, "and it is not repeated");

  head("escalation does not serve the same dwell twice");
  w = armed();
  run(w, nofix, GPS_LOST_HOLD_S, GpsEvent::Lost);
  // The receiver going quiet mid-dropout is new information and worth saying
  // at once - the sky is no longer the thing that is wrong.
  ok(run(w, silent, 1, GpsEvent::Silent) == 1, "a dead receiver escalates immediately");
  ok(w.tier() == 3, "and the tier follows it up");
  // Coming part of the way back is not coming back.
  ok(run(w, nofix, 30, GpsEvent::None) == 30, "dropping back to merely lost says nothing");
  ok(w.tier() == 3, "and does not lower the warning that stands");

  head("a silent receiver from a healthy start");
  w = armed();
  ok(run(w, silent, GPS_SILENT_HOLD_S - 1, GpsEvent::Silent) == 0, "just under its dwell");
  ok(run(w, silent, 1, GpsEvent::Silent) == 1, "and on it");
  ok(gpsWarnHoldSeconds(3) < gpsWarnHoldSeconds(2), "silence is believed sooner than a lost fix");
  ok(gpsWarnHoldSeconds(2) < gpsWarnHoldSeconds(1), "...and a lost fix sooner than a loose one");
  ok(gpsWarnHoldSeconds(0) == 0, "a healthy fix waits for nothing");

  head("tiers line up with events");
  ok(gpsWarnEvent(0) == GpsEvent::None, "tier 0 has nothing to say");
  ok(gpsWarnEvent(1) == GpsEvent::Degraded, "tier 1 is degraded");
  ok(gpsWarnEvent(2) == GpsEvent::Lost, "tier 2 is lost");
  ok(gpsWarnEvent(3) == GpsEvent::Silent, "tier 3 is silent");

  printf("\n%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
