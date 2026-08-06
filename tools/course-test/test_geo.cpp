// Host-side exercise of the geodesy and bearing helpers behind the compass page.
#include "nav/Geo.h"
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
  snprintf(d, sizeof(d), "(got %.4f, want %.4f)", got, want);
  ok(fabs(got - want) <= tol, what, d);
}
static void isName(float deg, const char* want) {
  char d[64];
  const char* got = cardinalName(deg);
  snprintf(d, sizeof(d), "(%.2f deg -> %s, want %s)", deg, got, want);
  ok(!strcmp(got, want), "cardinal", d);
}
static void head(const char* s) { printf("\n== %s ==\n", s); }

int main() {
  head("haversine");
  // Warszawa Centralna to Krakow Glowny, about 253 km.
  near(haversine({52.2287, 21.0033}, {50.0677, 19.9450}), 252900, 3000, "Warszawa - Krakow");
  near(haversine({50.0, 20.0}, {50.0, 20.0}), 0, 0.001, "zero for the same point");
  // One degree of latitude is about 111.2 km anywhere.
  near(haversine({50.0, 20.0}, {51.0, 20.0}), 111195, 300, "one degree of latitude");
  // One degree of longitude shrinks with the cosine of latitude.
  near(haversine({50.0, 20.0}, {50.0, 21.0}), 71460, 400, "one degree of longitude at 50N");
  near(haversine({0.0, 0.0}, {0.0, 1.0}), 111195, 300, "one degree of longitude at the equator");

  head("bearingDeg - the four cardinals");
  near(bearingDeg({50.0, 20.0}, {51.0, 20.0}), 0,   0.01, "due north");
  near(bearingDeg({50.0, 20.0}, {50.0, 21.0}), 90,  0.5,  "due east");
  near(bearingDeg({50.0, 20.0}, {49.0, 20.0}), 180, 0.01, "due south");
  near(bearingDeg({50.0, 20.0}, {50.0, 19.0}), 270, 0.5,  "due west");

  head("bearingDeg - range and wrap");
  // Equal steps of +0.5 lat and -0.5 lon do NOT give 315: at 50N a degree of
  // longitude is only about 64% of a degree of latitude, so the bearing sits
  // much closer to north. atan2(-0.5*71474, 0.5*111195) = -32.7 -> 327.3.
  double nw = bearingDeg({50.0, 20.0}, {50.5, 19.5});
  printf("       north-west reads %.2f\n", nw);
  ok(nw > 180.0 && nw < 360.0, "north-west stays in 0..360, not negative");
  near(nw, 327.3, 0.6, "north-west bearing accounts for longitude convergence");
  for (int i = 0; i < 360; i += 7) {
    double la = 50.0 + 0.5 * cos(i * M_PI / 180.0);
    double lo = 20.0 + 0.5 * sin(i * M_PI / 180.0);
    double b = bearingDeg({50.0, 20.0}, {la, lo});
    if (b < 0 || b >= 360.0) { ok(false, "bearing left the 0..360 range"); break; }
  }
  ok(true, "every bearing around the circle stays in 0..360");

  head("bearingDeg - across the antimeridian and the poles");
  // Just west of 180 to just east of it: still essentially due east.
  near(bearingDeg({10.0, 179.9}, {10.0, -179.9}), 90, 1.0, "east across the antimeridian");
  near(bearingDeg({10.0, -179.9}, {10.0, 179.9}), 270, 1.0, "west across the antimeridian");
  near(bearingDeg({89.0, 0.0}, {89.0, 90.0}), 45.0, 2.0, "high latitude convergence");

  head("relativeBearing");
  near(relativeBearing(90, 90), 0, 0.001, "same direction");
  near(relativeBearing(180, 90), 90, 0.001, "target to the right");
  near(relativeBearing(0, 90), -90, 0.001, "target to the left");
  // The wrap is the whole point: 350 vs 10 is 20 degrees apart, not 340.
  near(relativeBearing(350, 10), -20, 0.001, "wrapping the short way round");
  near(relativeBearing(10, 350), 20, 0.001, "and the other way");
  near(relativeBearing(0, 180), -180, 0.001, "dead astern");
  ok(isnan(relativeBearing(NAN, 90)), "NAN bearing propagates");
  ok(isnan(relativeBearing(90, NAN)), "NAN heading propagates");

  head("cardinalName");
  isName(0, "N");     isName(45, "NE");   isName(90, "E");    isName(135, "SE");
  isName(180, "S");   isName(225, "SW");  isName(270, "W");   isName(315, "NW");
  isName(22.5f, "NNE"); isName(247.5f, "WSW");
  // Sector boundaries: each name owns 11.25 degrees either side of its bearing.
  isName(11.24f, "N");     isName(11.26f, "NNE");
  isName(348.74f, "NNW");  isName(348.76f, "N");
  isName(359.9f, "N");     isName(360.0f, "N");
  isName(-10.0f, "N");     isName(-90.0f, "W");
  isName(721.0f, "N");
  ok(!strcmp(cardinalName(NAN), "--"), "NAN reads as --");

  printf("\n%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
