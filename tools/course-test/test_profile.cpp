// Host-side exercise of the distance-indexed elevation profile.
#include "ride/ElevationProfile.h"
#include <stdio.h>
#include <math.h>

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

static ElevationProfile ep;

// Ride a given distance, altitude a function of distance, feeding samples far
// more often than the profile stores them - as the firmware does.
static void ride(ElevationProfile& p, double from, double to, double stepM,
                 float (*elev)(double)) {
  for (double d = from; d <= to; d += stepM) p.sample(d, elev(d));
}

static float flat(double)      { return 100.0f; }
static float ramp(double d)    { return 100.0f + (float)(d * 0.05); }   // 5 % climb
static float descent(double d) { return 500.0f - (float)(d * 0.08); }   // 8 % down

int main() {
  head("sampling is indexed by distance, not by call count");
  ep.clear();
  ok(ep.count() == 0, "starts empty");
  ep.sample(0, 100.0f);
  ok(ep.count() == 1, "the first sample is stored at once");
  // Standing still must not add points, however long you stand there.
  for (int i = 0; i < 500; i++) ep.sample(0, 100.0f);
  ok(ep.count() == 1, "five hundred stationary calls add nothing");
  // This is the whole reason the profile is distance-indexed: an hour at a
  // cafe would otherwise occupy most of the graph.
  ep.sample(ElevationProfile::BASE_INTERVAL_M - 0.1, 100.0f);
  ok(ep.count() == 1, "just short of the interval, still nothing");
  ep.sample(ElevationProfile::BASE_INTERVAL_M, 101.0f);
  ok(ep.count() == 2, "one interval covered, one point stored");

  head("reading back");
  ep.clear();
  ride(ep, 0, 200, 1.0, ramp);
  printf("       %u points every %.0f m, covering %.0f m\n",
         ep.count(), ep.intervalM(), ep.coveredM());
  ok(ep.count() == 11, "200 m at 20 m spacing is eleven points");
  near(ep.distanceAt(0), 0, 0.01, "first point at zero");
  near(ep.distanceAt(10), 200, 0.01, "last point at 200 m");
  near(ep.coveredM(), 200, 0.01, "covered distance");
  near(ep.elevationAt(0), 100.0, 0.3, "elevation at the start");
  near(ep.elevationAt(10), 110.0, 0.3, "and at the end of a 5 % ramp");
  ok(isnan(ep.elevationAt(99)), "reading past the end gives NAN, not a stale value");

  head("half-metre storage resolution");
  ep.clear();
  ep.sample(0, 100.24f);
  near(ep.elevationAt(0), 100.0, 0.26, "rounds to the nearest half metre");
  ep.clear();
  ep.sample(0, 100.26f);
  near(ep.elevationAt(0), 100.5, 0.26, "and rounds up when it should");

  head("range");
  ep.clear();
  ride(ep, 0, 1000, 1.0, ramp);       // 100 m up to 150 m
  near(ep.minElevation(), 100.0, 0.3, "lowest point");
  near(ep.maxElevation(), 150.0, 0.3, "highest point");

  head("gradient");
  ep.clear();
  ride(ep, 0, 2000, 1.0, ramp);
  float g = ep.gradeAt(50);
  printf("       mid-ramp gradient reads %.2f %%\n", g);
  near(g, 5.0, 0.4, "a 5 % ramp reads as 5 %");
  ep.clear();
  ride(ep, 0, 2000, 1.0, descent);
  g = ep.gradeAt(50);
  printf("       mid-descent gradient reads %.2f %%\n", g);
  near(g, -8.0, 0.5, "an 8 % descent reads negative");
  ep.clear();
  ride(ep, 0, 2000, 1.0, flat);
  near(ep.gradeAt(50), 0.0, 0.3, "flat is flat");

  head("bad input is refused");
  ep.clear();
  ep.sample(0, 100.0f);
  uint16_t before = ep.count();
  ep.sample(100, NAN);
  ok(ep.count() == before, "a NAN altitude stores nothing");
  ep.sample(200, -900.0f);
  ep.sample(300, 20000.0f);
  ok(ep.count() == before, "impossible altitudes store nothing either");
  ep.sample(-50, 100.0f);
  ok(ep.count() == before, "so does a negative distance");

  head("the interval doubles instead of the buffer overflowing");
  ep.clear();
  // Far enough to force several decimations.
  double far_ = ElevationProfile::SAMPLES * ElevationProfile::BASE_INTERVAL_M * 9.0;
  ride(ep, 0, far_, 5.0, ramp);
  printf("       %.0f km ride -> %u points every %.0f m, covering %.1f km\n",
         far_ / 1000.0, ep.count(), ep.intervalM(), ep.coveredM() / 1000.0);
  ok(ep.count() <= ElevationProfile::SAMPLES, "never exceeds the buffer");
  ok(ep.count() > ElevationProfile::SAMPLES / 2, "and still uses most of it");
  ok(ep.intervalM() > ElevationProfile::BASE_INTERVAL_M, "the interval grew");
  // The x axis has to keep telling the truth after decimation, or the graph
  // silently claims a 100 km ride was 12 km.
  near(ep.coveredM(), far_, far_ * 0.05, "covered distance still matches the ride");
  near(ep.elevationAt(0), 100.0, 1.0, "the start survived");
  near(ep.elevationAt(ep.count() - 1), 100.0f + far_ * 0.05, far_ * 0.05 * 0.05,
       "and so did the end");

  head("gradient survives decimation");
  // Same 5 % ramp, but now sampled at a much coarser interval.
  g = ep.gradeAt(ep.count() / 2);
  printf("       after decimation the ramp reads %.2f %%\n", g);
  near(g, 5.0, 0.5, "still a 5 % ramp");

  head("a jump in distance fills the gap");
  ep.clear();
  ep.sample(0, 100.0f);
  // A GPS glitch or a tunnel exit can advance distance by hundreds of metres
  // between calls; the x axis must not fall behind the distance it claims.
  ep.sample(500, 120.0f);
  printf("       after a 500 m jump: %u points covering %.0f m\n",
         ep.count(), ep.coveredM());
  near(ep.coveredM(), 500, ElevationProfile::BASE_INTERVAL_M, "the axis kept up");
  ok(ep.count() > 20, "the gap was filled rather than skipped");

  head("clear");
  ep.clear();
  ok(ep.count() == 0, "emptied");
  near(ep.intervalM(), ElevationProfile::BASE_INTERVAL_M, 0.01, "interval back to base");
  ok(isnan(ep.minElevation()), "range forgotten");
  ok(isnan(ep.gradeAt(0)), "no gradient without data");

  printf("\n%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
