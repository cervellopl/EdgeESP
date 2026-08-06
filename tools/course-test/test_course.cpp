// Host-side exercise of the real Course parser + snapping code.
#include "nav/Course.h"
#include <stdio.h>
#include <math.h>

static int failures = 0, checks = 0;

static void ok(bool cond, const char* what, const char* detail = "") {
  checks++;
  if (!cond) { failures++; printf("  FAIL  %s  %s\n", what, detail); }
  else       { printf("  ok    %s %s\n", what, detail); }
}
static void near(double got, double want, double tolPct, const char* what) {
  char d[128];
  snprintf(d, sizeof(d), "(got %.2f, want %.2f, %+.2f%%)", got, want,
           want != 0 ? (got - want) / want * 100.0 : 0.0);
  ok(fabs(got - want) <= fabs(want) * tolPct / 100.0 + 1e-6, what, d);
}

static const double LAT0 = 50.0;
static const double M_LATD = 111229.02758399208;
static const double M_LOND = 71695.7664872427;
static double lonFor(double m) { return m / M_LOND; }
static double latFor(double m) { return m / M_LATD; }

static Course c;

// Course::update now takes a LatLon, so the two halves of a position
// cannot be handed to it in the wrong order or the wrong frame.
static void upd(Course& co, double lat, double lon, uint32_t dt,
                float hdg = NAN) {
  co.update({lat, lon}, dt, hdg);
}

static void head(const char* s) { printf("\n== %s ==\n", s); }

int main() {
  // ---------------------------------------------------------------- A
  head("a_straight.gpx - plain Garmin track");
  ok(c.load("a_straight.gpx"), "loads");
  printf("       name=\"%s\" pts=%u cues=%u\n", c.name(), c.pointCount(), c.cueCount());
  ok(!strcmp(c.name(), "Straight Ten"), "track name parsed");
  ok(c.pointCount() == 1001, "kept every 10 m point");
  near(c.totalDistance(), 9969.18, 1.0, "total distance");

  // snap dead on the line at 5 km
  upd(c, LAT0, lonFor(5000), 1000);
  { char d[64]; snprintf(d, sizeof(d), "(%.6f m)", c.crossTrack());
    ok(c.crossTrack() < 0.01f, "cross-track on the line is sub-centimetre", d); }
  near(c.alongDistance(), 5000, 1.0, "along distance at 5 km");
  ok(c.progressPct() == 50, "progress 50%");
  near(c.distanceRemaining(), 4969, 2.0, "distance remaining");

  // ---------------------------------------------------------------- off course
  head("off-course state machine");
  ok(!c.offCourse(), "starts on course");
  // 100 m north of the 5 km mark
  for (int i = 0; i < OFF_COURSE_HOLD_S - 1; i++)
    upd(c, LAT0 + latFor(100), lonFor(5000), 1000);
  near(c.crossTrack(), 100.0, 2.0, "cross-track 100 m");
  ok(!c.offCourse(), "not yet - hold time not elapsed");
  upd(c, LAT0 + latFor(100), lonFor(5000), 1000);
  ok(c.offCourse(), "off course after the hold");
  ok(c.takeEvent() == CourseEvent::WentOffCourse, "WentOffCourse event fired");
  ok(c.takeEvent() == CourseEvent::None, "event is consumed once");

  // a brief return does not clear it
  upd(c, LAT0, lonFor(5000), 1000);
  ok(c.offCourse(), "one good sample does not clear the alert");
  for (int i = 0; i < ON_COURSE_HOLD_S; i++) upd(c, LAT0, lonFor(5010), 1000);
  ok(!c.offCourse(), "cleared after sustained return");
  ok(c.takeEvent() == CourseEvent::BackOnCourse, "BackOnCourse event fired");

  // just inside the threshold must not trip
  for (int i = 0; i < 20; i++)
    upd(c, LAT0 + latFor(OFF_COURSE_THRESHOLD_M - 5), lonFor(5000 + i * 10), 1000);
  ok(!c.offCourse(), "35 m offset stays on course");

  // ---------------------------------------------------------------- finish
  head("finish detection");
  for (int i = 0; i < 3; i++) upd(c, LAT0, lonFor(9990), 1000);
  ok(c.finished(), "finished near the end");

  // ---------------------------------------------------------------- B
  head("b_oneline.gpx - no newlines anywhere");
  ok(c.load("b_oneline.gpx"), "loads");
  ok(c.pointCount() == 1001, "same point count as the pretty-printed file");
  near(c.totalDistance(), 9969.18, 1.0, "same distance");

  // ---------------------------------------------------------------- C
  head("c_selfclose.gpx - <trkpt .../> with lon before lat");
  ok(c.load("c_selfclose.gpx"), "loads");
  ok(c.pointCount() == 1001, "self-closing tags committed");
  near(c.totalDistance(), 9969.18, 1.0, "distance unaffected by attribute order");

  // ---------------------------------------------------------------- J
  head("j_turns.gpx - detecting corners from geometry");
  ok(c.load("j_turns.gpx"), "loads");
  upd(c, LAT0, lonFor(0), 1000);
  printf("       %u turns ahead:\n", c.upcomingCount());
  for (uint8_t i = 0; i < c.upcomingCount(); i++) {
    const NavCue* q = c.upcoming(i);
    printf("         %6.0f m  %-12s  %+4d deg  \"%s\"\n",
           q->dist, Course::turnText(q->type), q->angle, q->name);
  }
  ok(c.upcomingCount() >= 3, "found three corners");
  if (c.upcomingCount() >= 3) {
    near(c.upcoming(0)->dist, 900,  6.0, "first corner position");
    ok(c.upcoming(0)->type == TurnType::Right, "first corner is a right");
    near(c.upcoming(1)->dist, 1800, 4.0, "second corner position");
    ok(c.upcoming(1)->type == TurnType::Left, "second corner is a left");
    near(c.upcoming(2)->dist, 2600, 3.0, "third corner position");
    ok(c.upcoming(2)->type == TurnType::SlightRight, "third corner is a bear right");
  }
  ok(c.nextTurn() == c.upcoming(0), "nextTurn is the first of them");
  near(c.distanceToNextTurn(), 900, 6.0, "distance to next turn");

  head("turn announcement staging");
  // Ride up to the first corner and watch the three stages fire once each.
  int nFar = 0, nNear = 0, nNow = 0;
  for (int m = 0; m <= 895; m += 5) {
    upd(c, LAT0, lonFor(m), 1000);
    switch (c.takeEvent()) {
      case CourseEvent::TurnFar:  nFar++;  break;
      case CourseEvent::TurnNear: nNear++; break;
      case CourseEvent::TurnNow:  nNow++;  break;
      default: break;
    }
  }
  printf("       far=%d near=%d now=%d\n", nFar, nNear, nNow);
  ok(nFar == 1,  "TurnFar fired exactly once");
  ok(nNear == 1, "TurnNear fired exactly once");
  ok(nNow == 1,  "TurnNow fired exactly once");

  head("j_named.gpx - a waypoint names the corner it sits on");
  ok(c.load("j_named.gpx"), "loads");
  upd(c, LAT0, lonFor(0), 1000);
  const NavCue* n0 = c.nextTurn();
  ok(n0 && n0->named, "first turn carries a name");
  if (n0) printf("       \"%s\" (%s) at %.0f m\n", n0->name, Course::turnText(n0->type), n0->dist);
  ok(n0 && !strcmp(n0->name, "Turn right onto Polna"), "name came from the waypoint");
  ok(c.upcomingCount() >= 3, "the waypoint merged into the corner, not beside it");

  // ---------------------------------------------------------------- M
  head("m_cuebend.gpx - cue points get an arrow from the geometry under them");
  ok(c.load("m_cuebend.gpx"), "loads");
  upd(c, LAT0, lonFor(0), 1000);
  printf("       %u cues ahead:\n", c.upcomingCount());
  const NavCue *atBend = nullptr, *atStraight = nullptr;
  for (uint8_t i = 0; i < c.upcomingCount(); i++) {
    const NavCue* q = c.upcoming(i);
    printf("         %6.0f m  %-12s  %+4d deg  \"%s\"\n",
           q->dist, Course::turnText(q->type), q->angle, q->name);
    if (!strcmp(q->name, "Feed stop")) atBend = q;
    if (!strcmp(q->name, "Water tap")) atStraight = q;
  }
  // The bend is 20 deg - under the 28 deg turn threshold, so nothing would be
  // detected here on geometry alone. The waypoint is what earns it an arrow.
  ok(atBend != nullptr, "the bend cue survived");
  ok(atBend && atBend->type == TurnType::SlightRight,
     "a nameless cue on a shallow bend becomes a bear right");
  ok(atBend && abs(atBend->angle) > 10 && abs(atBend->angle) < 30,
     "and carries the measured angle");
  ok(atStraight != nullptr, "the straight-road cue survived");
  ok(atStraight && atStraight->type == TurnType::Generic,
     "a cue on straight road stays a plain cue, not an invented turn");

  // ---------------------------------------------------------------- K
  head("k_bend.gpx - a 300 m radius sweep is not a turn");
  ok(c.load("k_bend.gpx"), "loads");
  upd(c, LAT0, lonFor(0), 1000);
  uint8_t bendTurns = 0;
  for (uint8_t i = 0; i < c.upcomingCount(); i++)
    if (c.upcoming(i)->type != TurnType::Finish) bendTurns++;
  printf("       %u turns reported on a 90 deg sweeping bend\n", bendTurns);
  ok(bendTurns == 0, "no false turns on a sweeping bend");

  // ---------------------------------------------------------------- L
  head("l_uturn.gpx - reversal is classified as a U-turn");
  ok(c.load("l_uturn.gpx"), "loads");
  upd(c, LAT0, lonFor(0), 1000);
  const NavCue* u = c.nextTurn();
  if (u) printf("       %.0f m  %s  %+d deg\n", u->dist, Course::turnText(u->type), u->angle);
  ok(u && u->type == TurnType::UTurn, "classified as a U-turn");
  near(u ? u->dist : 0, 500, 6.0, "at the far end");

  // ---------------------------------------------------------------- bearing
  head("return-to-route bearing");
  ok(c.load("a_straight.gpx"), "reload the straight course");
  // 100 m north of the line, riding east: the route is off the right shoulder.
  upd(c, LAT0 + latFor(100), lonFor(5000), 1000, 90.0f);
  printf("       absolute=%.1f deg  relative=%.1f deg\n",
         c.bearingToRoute(), c.relativeBearingToRoute());
  near(c.bearingToRoute(), 180.0, 3.0, "route lies due south");
  near(c.relativeBearingToRoute(), 90.0, 3.0, "which is 3 o'clock when heading east");
  upd(c, LAT0 + latFor(100), lonFor(5000), 1000, NAN);
  ok(isnan(c.relativeBearingToRoute()), "no relative bearing without a heading");

  // ---------------------------------------------------------------- breadcrumb
  head("buildFromTrack - route home from the ride's own breadcrumb");
  {
    static TrackPoint tp[200];
    for (int i = 0; i < 200; i++) { tp[i].lat = LAT0; tp[i].lon = lonFor(i * 20.0); }
    ok(c.buildFromTrack(tp, 200, "Back to start"), "builds");
    printf("       \"%s\"  %u pts  %.0f m\n", c.name(), c.pointCount(), c.totalDistance());
    ok(!strcmp(c.name(), "Back to start"), "named");
    near(c.totalDistance(), 3980, 2.0, "same length as the ridden track");
    // The course must start where the rider is now - the last breadcrumb.
    upd(c, LAT0, lonFor(199 * 20.0), 1000);
    near(c.alongDistance(), 0, 0.5, "rider starts at distance zero");
    upd(c, LAT0, lonFor(0), 1000);
    near(c.alongDistance(), 3980, 2.0, "ride start is the far end");
    ok(!c.buildFromTrack(tp, 1, "Too short"), "refuses a one-point track");
  }

  // ---------------------------------------------------------------- D
  head("d_cues.gpx - waypoint cues + <rtept> route");
  ok(c.load("d_cues.gpx"), "loads");
  ok(!strcmp(c.name(), "Cued Route"), "metadata name used");
  ok(c.pointCount() == 1001, "rtept parsed as course points");
  ok(c.cueCount() == 3, "three cues");
  const CourseCue* q = c.cues();
  printf("       cues: [0] %.0f m \"%s\"  [1] %.0f m \"%s\"  [2] %.0f m \"%s\"\n",
         q[0].dist, q[0].name, q[1].dist, q[1].name, q[2].dist, q[2].name);
  ok(q[0].dist < q[1].dist && q[1].dist < q[2].dist, "cues sorted by distance");
  ok(!strcmp(q[0].name, "Right onto Krakowska"), "first cue is the 2 km one");
  // This route is dead straight, so every guidance cue comes from a waypoint.
  upd(c, LAT0, lonFor(100), 1000);
  const NavCue* nx = c.nextTurn();
  ok(nx && !strcmp(nx->name, "Right onto Krakowska"), "nextTurn at the start");
  ok(nx && nx->type == TurnType::Right, "type read out of the waypoint text");
  near(c.distanceToNextTurn(), 1900, 3.0, "distance to next turn");
  upd(c, LAT0, lonFor(3000), 1000);
  nx = c.nextTurn();
  ok(nx && !strcmp(nx->name, "Continue straight"), "advances past a passed cue");

  upd(c, LAT0, lonFor(4900), 1000);
  ok(c.takeEvent() == CourseEvent::TurnNear, "TurnNear fires inside 150 m");
  upd(c, LAT0, lonFor(4920), 1000);
  ok(c.takeEvent() == CourseEvent::None, "and does not repeat at the same stage");

  // ---------------------------------------------------------------- E
  head("e_noele.gpx - no elevation data");
  ok(c.load("e_noele.gpx"), "loads");
  ok(c.pointCount() == 1001, "points parsed");
  near(c.totalAscent(), 0.0, 0.0, "no phantom ascent");
  ok(isnan(c.courseElevationAt(1000)), "elevation query returns NAN");

  // ---------------------------------------------------------------- F
  head("f_sawtooth.gpx - 10 x 100 m climbs");
  ok(c.load("f_sawtooth.gpx"), "loads");
  printf("       totalAscent=%.0f m\n", c.totalAscent());
  near(c.totalAscent(), 1000.0, 5.0, "total ascent");
  upd(c, LAT0, lonFor(0), 1000);
  near(c.ascentRemaining(), 1000.0, 5.0, "ascent remaining at the start");
  upd(c, LAT0, lonFor(9000), 1000);
  ok(c.ascentRemaining() < 200.0, "ascent remaining near the end is small");
  float e = c.courseElevationAt(250);   // quarter into the first 500 m ramp
  printf("       elevationAt(250 m)=%.1f\n", e);
  ok(e > 140 && e < 160, "interpolated elevation is sane");

  // ---------------------------------------------------------------- G
  head("g_big.gpx - 20001 points, forces decimation");
  ok(c.load("g_big.gpx"), "loads a course larger than the buffer");
  printf("       pts=%u (cap %d)  distance=%.0f m\n",
         c.pointCount(), COURSE_MAX_POINTS, c.totalDistance());
  ok(c.pointCount() <= COURSE_MAX_POINTS, "stayed inside the buffer");
  near(c.totalDistance(), 199383.65, 1.0, "distance survives decimation");

  // ---------------------------------------------------------------- H
  head("h_outback.gpx - out and back 12 m apart");
  ok(c.load("h_outback.gpx"), "loads");
  // Ride the outbound leg; the windowed search must not jump to the return leg.
  for (int m = 0; m <= 4000; m += 100) upd(c, LAT0, lonFor(m), 1000);
  printf("       along=%.0f m of %.0f m, xtrack=%.1f m\n",
         c.alongDistance(), c.totalDistance(), c.crossTrack());
  near(c.alongDistance(), 4000, 3.0, "still on the outbound leg");
  ok(!c.offCourse(), "not flagged off course");
  // Now the return leg, 12 m north.
  for (int m = 5000; m >= 3000; m -= 100) upd(c, LAT0 + latFor(12), lonFor(m), 1000);
  printf("       along=%.0f m after turning round\n", c.alongDistance());
  ok(c.alongDistance() > 6000, "tracked onto the return leg");

  // ---------------------------------------------------------------- I
  head("i_empty.gpx / missing file - failure paths");
  ok(!c.load("i_empty.gpx"), "rejects a GPX with no track points");
  printf("       error: \"%s\"\n", c.lastError());
  ok(!c.loaded(), "not marked loaded");
  ok(!c.load("does_not_exist.gpx"), "rejects a missing file");
  printf("       error: \"%s\"\n", c.lastError());
  upd(c, LAT0, lonFor(1000), 1000);   // must not crash with nothing loaded
  ok(true, "update() with no course is safe");

  printf("\n%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
