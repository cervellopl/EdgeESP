// Host-side exercise of the training zone boundaries.
#include "ride/Zones.h"
#include <stdio.h>
#include <string.h>

unsigned long g_fakeMillis = 0;

static int failures = 0, checks = 0;
static void ok(bool cond, const char* what, const char* detail = "") {
  checks++;
  if (!cond) { failures++; printf("  FAIL  %s  %s\n", what, detail); }
  else       { printf("  ok    %s %s\n", what, detail); }
}
static void head(const char* s) { printf("\n== %s ==\n", s); }

static void inPZone(uint16_t w, uint16_t ftp, uint8_t want) {
  uint8_t got = Zones::powerZoneFor(w, ftp);
  char d[96];
  snprintf(d, sizeof(d), "%u W at FTP %u -> Z%u (want Z%u)", w, ftp, got + 1, want + 1);
  ok(got == want, "power zone", d);
}
static void inHZone(uint8_t bpm, uint8_t lthr, uint8_t want) {
  uint8_t got = Zones::hrZoneFor(bpm, lthr);
  char d[96];
  snprintf(d, sizeof(d), "%u bpm at LTHR %u -> Z%u (want Z%u)", bpm, lthr, got + 1, want + 1);
  ok(got == want, "hr zone", d);
}

int main() {
  head("power zones at a round 200 W FTP");
  // Percent boundaries land on whole watts here, so the edges are unambiguous.
  inPZone(0,   200, 0);
  inPZone(110, 200, 0);   // 55 %, top of Z1
  inPZone(112, 200, 1);   // 56 %, bottom of Z2
  inPZone(150, 200, 1);   // 75 %
  inPZone(152, 200, 2);   // 76 %
  inPZone(180, 200, 2);   // 90 %
  inPZone(182, 200, 3);   // 91 %
  inPZone(200, 200, 3);   // FTP itself is threshold, not VO2max
  inPZone(210, 200, 3);   // 105 %
  inPZone(212, 200, 4);   // 106 %
  inPZone(240, 200, 4);   // 120 %
  inPZone(242, 200, 5);   // 121 %
  inPZone(300, 200, 5);   // 150 %
  inPZone(302, 200, 6);   // 151 %
  inPZone(1400, 200, 6);  // a sprint is still Z7

  head("no gap between the bands");
  // Every watt from 0 to 3x FTP must land somewhere, and zones must never
  // travel backwards as power rises.
  uint8_t prev = 0;
  bool monotonic = true;
  for (uint16_t wt = 0; wt <= 600; wt++) {
    uint8_t z = Zones::powerZoneFor(wt, 200);
    if (z < prev) monotonic = false;
    if (z >= Zones::POWER_COUNT) monotonic = false;
    prev = z;
  }
  ok(monotonic, "zone never decreases as power rises, and never overruns");

  head("arithmetic that would overflow a 16-bit multiply");
  // 1500 W against a 60 W FTP is 2500 %. w * 100 is 150000, past a uint16.
  inPZone(1500, 60, 6);
  inPZone(2000, 60, 6);
  ok(Zones::powerZoneFor(30, 60) == 0, "half of a small FTP is still Z1");

  head("degenerate inputs");
  ok(Zones::powerZoneFor(250, 0) == 0, "no FTP gives Z1 rather than dividing by zero");
  ok(Zones::hrZoneFor(150, 0) == 0, "no LTHR likewise");
  ok(Zones::powerZoneFor(0, 200) == 0, "zero watts is Z1");

  head("heart-rate zones at LTHR 170");
  inHZone(0,   170, 0);
  inHZone(136, 170, 0);   // 80 %
  inHZone(138, 170, 1);   // 81 %
  inHZone(151, 170, 1);   // 88.8 %
  inHZone(153, 170, 2);   // 90 %
  inHZone(158, 170, 2);   // 92.9 %
  inHZone(160, 170, 3);   // 94.1 %
  inHZone(168, 170, 3);   // 98.8 %
  inHZone(170, 170, 4);   // threshold itself is the top zone
  inHZone(190, 170, 4);

  head("displayed bounds match the zone the value lands in");
  // Round-trip: the low bound of each zone must classify back into that zone.
  bool consistent = true;
  for (uint8_t i = 0; i < Zones::POWER_COUNT; i++) {
    uint16_t lo = Zones::powerLo(i, 250);
    if (Zones::powerZoneFor(lo, 250) != i) {
      printf("       Z%u low bound %u W classifies as Z%u\n",
             i + 1, lo, Zones::powerZoneFor(lo, 250) + 1);
      consistent = false;
    }
  }
  ok(consistent, "every power zone's low bound falls inside it");

  consistent = true;
  for (uint8_t i = 0; i < Zones::POWER_COUNT; i++) {
    uint16_t hi = Zones::powerHi(i, 250);
    if (hi && Zones::powerZoneFor(hi, 250) != i) consistent = false;
  }
  ok(consistent, "and every high bound too");

  ok(Zones::powerHi(Zones::POWER_COUNT - 1, 250) == 0, "the top zone is open-ended");
  // 91 % of 250 is 227.5. Rounding down would print 227, which classifies as
  // Z3 - the screen would name a target that lands in the zone below.
  ok(Zones::powerLo(3, 250) == 228, "Z4 starts at the first watt that is really Z4");
  ok(Zones::powerHi(2, 250) == 227, "so Z3 tops out at 227, not at a rounded 225");

  // Printed bands must be contiguous: no watt may fall in a gap between them.
  bool contiguous = true;
  for (uint8_t i = 0; i + 1 < Zones::POWER_COUNT; i++)
    if (Zones::powerHi(i, 250) + 1 != Zones::powerLo(i + 1, 250)) contiguous = false;
  ok(contiguous, "each power band ends exactly where the next begins");

  contiguous = true;
  for (uint8_t i = 0; i + 1 < Zones::HR_COUNT; i++)
    if (Zones::hrHi(i, 170) + 1 != Zones::hrLo(i + 1, 170)) contiguous = false;
  ok(contiguous, "and the same for heart rate");

  consistent = true;
  for (uint8_t i = 0; i < Zones::HR_COUNT; i++)
    if (Zones::hrZoneFor((uint8_t)Zones::hrLo(i, 170), 170) != i) consistent = false;
  ok(consistent, "the same holds for heart-rate bounds");

  head("cadence zones are absolute rpm");
  auto inCZone = [&](uint8_t rpm, uint8_t want) {
    uint8_t got = Zones::cadenceZoneFor(rpm);
    char d[80];
    snprintf(d, sizeof(d), "%u rpm -> Z%u (want Z%u)", rpm, got + 1, want + 1);
    ok(got == want, "cadence zone", d);
  };
  inCZone(1,   0);
  inCZone(59,  0);
  inCZone(60,  1);
  inCZone(74,  1);
  inCZone(75,  2);
  inCZone(89,  2);
  inCZone(90,  3);
  inCZone(104, 3);
  inCZone(105, 4);
  inCZone(200, 4);

  // Zero rpm is coasting. The classifier still has to return a valid index so
  // it can never index out of range, but the accumulator keeps it out of the
  // zones - a long descent must not read as an hour of grinding.
  ok(Zones::cadenceZoneFor(0) < Zones::CAD_COUNT, "zero rpm still returns a valid index");
  ok(Zones::cadenceLo(0) == 1, "but the bottom zone starts at 1 rpm, not 0");

  contiguous = true;
  for (uint8_t i = 0; i + 1 < Zones::CAD_COUNT; i++)
    if (Zones::cadenceHi(i) + 1 != Zones::cadenceLo(i + 1)) contiguous = false;
  ok(contiguous, "cadence bands are contiguous too");
  ok(Zones::cadenceHi(Zones::CAD_COUNT - 1) == 0, "the top cadence zone is open-ended");

  consistent = true;
  for (uint8_t i = 0; i < Zones::CAD_COUNT; i++) {
    if (Zones::cadenceZoneFor((uint8_t)Zones::cadenceLo(i)) != i) consistent = false;
    uint16_t hi = Zones::cadenceHi(i);
    if (hi && Zones::cadenceZoneFor((uint8_t)hi) != i) consistent = false;
  }
  ok(consistent, "every printed cadence bound falls inside its own zone");

  prev = 0;
  monotonic = true;
  for (uint16_t r = 1; r <= 255; r++) {
    uint8_t z = Zones::cadenceZoneFor((uint8_t)r);
    if (z < prev || z >= Zones::CAD_COUNT) monotonic = false;
    prev = z;
  }
  ok(monotonic, "cadence zone never decreases as rpm rises");

  head("names and codes");
  ok(!strcmp(Zones::power(0).code, "Z1"), "first power zone is Z1");
  ok(!strcmp(Zones::power(6).code, "Z7"), "last is Z7");
  ok(!strcmp(Zones::power(3).name, "Threshold"), "Z4 is threshold");
  ok(!strcmp(Zones::hr(4).code, "Z5"), "last HR zone is Z5");
  // Reading past the end must clamp rather than wander off the table.
  ok(!strcmp(Zones::power(200).code, "Z7"), "an out-of-range power index clamps");
  ok(!strcmp(Zones::hr(200).code, "Z5"), "an out-of-range HR index clamps");

  printf("\n%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
