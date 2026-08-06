// Host-side exercise of lap bookkeeping: records, the ring buffer, the best-lap
// pick, and the event that carries a finished lap to the FIT writer.
#include "ride/RideComputer.h"
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
static void near(double got, double want, double tolPct, const char* what) {
  char d[128];
  snprintf(d, sizeof(d), "(got %.2f, want %.2f)", got, want);
  ok(fabs(got - want) <= fabs(want) * tolPct / 100.0 + 1e-6, what, d);
}
static void head(const char* s) { printf("\n== %s ==\n", s); }

static const double LAT0 = 50.0;
static const double M_LON = 71695.766;   // metres per degree of longitude at 50N

// Ride east at a steady speed, one second per call, exercising the real
// distance and timing accumulation rather than poking the counters.
//
// cadConnected is separate from cad because a sensor reporting 0 rpm is not the
// same thing as no sensor: the first is freewheeling, the second is nothing at
// all, and the firmware has to tell them apart.
static void ride(RideComputer& rc, int seconds, float mps, uint8_t hr = 0,
                 uint16_t pwr = 0, uint8_t cad = 0, int cadConnected = -1) {
  static double metres = 0;
  bool hasCad = cadConnected < 0 ? (cad != 0) : (cadConnected != 0);
  for (int i = 0; i < seconds; i++) {
    metres += mps;
    GpsFix f;
    f.valid = true; f.fixType = 3; f.numSV = 10; f.hAcc = 2.0f;
    f.lat = LAT0; f.lon = metres / M_LON;
    f.speed = mps; f.heading = 90.0f;
    f.timeValid = true; f.unixTime = 1754000000UL + (uint32_t)(metres / mps);
    rc.setSensors(hr != 0, hr, hasCad, cad, pwr != 0, pwr);
    g_fakeMillis += 1000;
    rc.update(f, NAN, NAN, 1000);
  }
}

// File scope, not a local: RideComputer is large enough that a stack copy in
// main is wasteful, and a function-local static would want a thread-safe guard.
static RideComputer rc;

int main() {
  g_settings.resetToDefaults();
  g_settings.autoLapM = 0;            // manual laps unless a test says otherwise

  FitSummary fs;

  head("a fresh computer has no laps");
  rc.begin();
  ok(rc.lapRecordCount() == 0, "no records");
  ok(rc.lapsCompleted() == 0, "no completed laps");
  ok(rc.bestLapIndex() == -1, "no best lap");
  ok(!rc.takeLapEvent(fs), "no pending event");
  ok(rc.currentLap().index == 1, "the ride starts on lap 1");

  head("one lap, ridden not faked");
  rc.start();
  ride(rc, 100, 10.0f, 150, 200, 90);      // 100 s at 10 m/s = 1000 m
  LapRecord live = rc.currentLap();
  near(live.distance, 1000, 2.0, "current lap distance");
  ok(live.movingMs == 100000, "current lap timer");
  near(live.avgSpeed, 10.0, 2.0, "current lap average speed");
  ok(live.avgHr == 150, "current lap average HR");
  ok(live.avgPower == 200, "current lap average power");
  ok(live.avgCadence == 90, "current lap average cadence");
  ok(live.calories > 0, "current lap accumulated energy");

  rc.lap();
  ok(rc.lapRecordCount() == 1, "one record kept");
  ok(rc.lapsCompleted() == 1, "one lap completed");
  const LapRecord& L1 = rc.lapRecord(0);
  ok(L1.index == 1, "recorded as lap 1");
  near(L1.distance, 1000, 2.0, "recorded distance");
  ok(L1.movingMs == 100000, "recorded time");
  ok(L1.avgHr == 150 && L1.avgPower == 200, "recorded averages");
  ok(rc.currentLap().index == 2, "now riding lap 2");
  near(rc.currentLap().distance, 0, 0.0, "the new lap starts empty");
  ok(rc.currentLap().avgPower == 0, "and its averages are cleared");

  head("the lap event carries the finished lap to the recorder");
  // This is the bug that made auto-laps vanish from the FIT file: the summary
  // must be the lap that ended, not the empty one that just began.
  ok(rc.takeLapEvent(fs), "event fired");
  near((double)fs.distance_cm / 100.0, 1000, 2.0, "summary has the finished distance");
  ok(fs.timer_ms == 100000, "summary has the finished time");
  ok(!rc.takeLapEvent(fs), "and is consumed exactly once");

  head("a second, slower lap");
  ride(rc, 200, 5.0f, 140, 150, 80);       // 200 s at 5 m/s = 1000 m
  rc.lap();
  ok(rc.lapRecordCount() == 2, "two records");
  near(rc.lapRecord(1).avgSpeed, 5.0, 2.0, "second lap is slower");
  ok(rc.bestLapIndex() == 0, "the first lap is still the best");

  head("a third, faster lap takes the crown");
  ride(rc, 50, 15.0f, 165, 280, 95);
  rc.lap();
  ok(rc.lapRecordCount() == 3, "three records");
  ok(rc.bestLapIndex() == 2, "the fastest lap wins");
  near(rc.lapRecord((uint8_t)rc.bestLapIndex()).avgSpeed, 15.0, 2.0, "best lap speed");

  head("a stray double press must not become the best lap");
  rc.lap();                                 // zero-distance lap
  ok(rc.lapRecordCount() == 4, "the empty lap is still recorded");
  near(rc.lapRecord(3).distance, 0, 0.0, "and it is empty");
  ok(rc.bestLapIndex() == 2, "but a 0 m lap cannot be the fastest");

  head("auto lap fires on distance, without anyone pressing anything");
  rc.reset();
  g_settings.autoLapM = 1000;
  rc.start();
  ride(rc, 250, 10.0f);                     // 2500 m: two auto laps
  printf("       completed=%u records=%u\n", rc.lapsCompleted(), rc.lapRecordCount());
  ok(rc.lapsCompleted() == 2, "two auto laps fired");
  near(rc.lapRecord(0).distance, 1000, 3.0, "first auto lap is a kilometre");
  ok(rc.takeLapEvent(fs), "an auto lap raises the event too");
  ok(fs.distance_cm > 90000, "and its summary is not empty");
  g_settings.autoLapM = 0;

  head("the ring buffer keeps the most recent laps");
  rc.reset();
  rc.start();
  for (int i = 0; i < RideComputer::MAX_LAP_RECORDS + 10; i++) {
    ride(rc, 10, 10.0f);
    rc.lap();
  }
  printf("       completed=%u records=%u first=%u last=%u\n",
         rc.lapsCompleted(), rc.lapRecordCount(),
         rc.lapRecord(0).index, rc.lapRecord(rc.lapRecordCount() - 1).index);
  ok(rc.lapsCompleted() == RideComputer::MAX_LAP_RECORDS + 10,
     "every lap is counted");
  ok(rc.lapRecordCount() == RideComputer::MAX_LAP_RECORDS,
     "but only the buffer's worth is kept");
  ok(rc.lapRecord(rc.lapRecordCount() - 1).index == RideComputer::MAX_LAP_RECORDS + 10,
     "the newest lap is the last record");
  ok(rc.lapRecord(0).index == 11, "the oldest ten were dropped");
  // Indices must stay strictly increasing after the shuffle.
  bool ordered = true;
  for (uint8_t i = 1; i < rc.lapRecordCount(); i++)
    if (rc.lapRecord(i).index != rc.lapRecord(i - 1).index + 1) ordered = false;
  ok(ordered, "records stay in order through the shift");

  head("time in zone lands in the right bucket");
  g_settings.ftpWatts = 250;
  g_settings.lthrBpm = 170;
  rc.reset();
  rc.start();
  // 200 W is 80 % of a 250 W FTP, which is Z3 (76-90 %).
  // 150 bpm is 88 % of a 170 bpm LTHR, which is HR Z2 (81-89 %).
  ride(rc, 60, 10.0f, 150, 200);
  const RideState& zs = rc.state();
  printf("       power zones ms:");
  for (uint8_t i = 0; i < Zones::POWER_COUNT; i++) printf(" %lu", (unsigned long)zs.zoneMs[i]);
  printf("\n       hr zones ms:   ");
  for (uint8_t i = 0; i < Zones::HR_COUNT; i++) printf(" %lu", (unsigned long)zs.hrZoneMs[i]);
  printf("\n");
  ok(zs.zoneMs[2] == 60000, "sixty seconds at 80 % FTP are all in power Z3");
  ok(zs.zoneMs[0] == 0 && zs.zoneMs[1] == 0 && zs.zoneMs[3] == 0,
     "and nothing leaked into the neighbouring power zones");
  ok(zs.hrZoneMs[1] == 60000, "sixty seconds at 88 % LTHR are all in HR Z2");
  ok(zs.hrZoneMs[0] == 0 && zs.hrZoneMs[2] == 0,
     "and nothing leaked into the neighbouring HR zones");

  // Riding harder must move the time somewhere else, not add it twice.
  ride(rc, 30, 10.0f, 175, 300);        // 120 % FTP = Z5, 103 % LTHR = HR Z5
  ok(zs.zoneMs[4] == 30000, "the harder half lands in power Z5");
  ok(zs.zoneMs[2] == 60000, "leaving the earlier Z3 total alone");
  ok(zs.hrZoneMs[4] == 30000, "and above threshold in HR Z5");

  uint32_t totalPwr = 0;
  for (uint8_t i = 0; i < Zones::POWER_COUNT; i++) totalPwr += zs.zoneMs[i];
  ok(totalPwr == 90000, "zone time sums to the time actually ridden");

  head("a paused ride stops filling zones");
  uint32_t before = zs.zoneMs[2];
  ride(rc, 20, 0.0f, 150, 200);         // standing still, auto-pause takes over
  printf("       Z3 went from %lu to %lu ms across 20 stopped seconds\n",
         (unsigned long)before, (unsigned long)zs.zoneMs[2]);
  // One sample can slip through as the status flips to auto-paused; twenty
  // seconds of coffee stop must not be filed as twenty seconds of tempo.
  ok(zs.zoneMs[2] - before <= 2000, "at most the transition sample is counted");

  head("no sensor, no zone time");
  rc.reset();
  rc.start();
  ride(rc, 30, 10.0f);                  // no HR, no power
  uint32_t any = 0;
  for (uint8_t i = 0; i < Zones::POWER_COUNT; i++) any += zs.zoneMs[i];
  for (uint8_t i = 0; i < Zones::HR_COUNT; i++) any += zs.hrZoneMs[i];
  ok(any == 0, "an absent sensor files nothing, rather than everything in Z1");

  head("coasting is not a cadence zone");
  rc.reset();
  rc.start();
  ride(rc, 40, 10.0f, 0, 0, 85);           // 85 rpm is cadence Z3 (75-89)
  ride(rc, 25, 12.0f, 0, 0, 0, 1);         // sensor connected, reporting 0: freewheeling
  const RideState& cs = rc.state();
  printf("       cadence zones ms:");
  for (uint8_t i = 0; i < Zones::CAD_COUNT; i++) printf(" %lu", (unsigned long)cs.cadZoneMs[i]);
  printf("   coasting %lu\n", (unsigned long)cs.coastingMs);
  ok(cs.cadZoneMs[2] == 40000, "pedalling at 85 rpm lands in cadence Z3");
  ok(cs.coastingMs == 25000, "and the freewheeling is counted as coasting");
  // The whole point of the split: without it, 25 s of descending would sit in
  // the bottom band and read as grinding.
  ok(cs.cadZoneMs[0] == 0, "zero rpm never lands in the grinding zone");

  uint32_t pedalling = 0;
  for (uint8_t i = 0; i < Zones::CAD_COUNT; i++) pedalling += cs.cadZoneMs[i];
  ok(pedalling + cs.coastingMs == 65000, "pedalling plus coasting is the whole ride");

  // And an absent sensor is still different from a connected one reading zero.
  rc.reset();
  rc.start();
  ride(rc, 20, 10.0f);
  ok(cs.coastingMs == 0, "no cadence sensor records no coasting either");

  head("reset clears everything");
  rc.reset();
  ok(rc.lapRecordCount() == 0, "records cleared");
  {
    uint32_t z = rc.state().coastingMs;
    for (uint8_t i = 0; i < Zones::POWER_COUNT; i++) z += rc.state().zoneMs[i];
    for (uint8_t i = 0; i < Zones::HR_COUNT; i++) z += rc.state().hrZoneMs[i];
    for (uint8_t i = 0; i < Zones::CAD_COUNT; i++) z += rc.state().cadZoneMs[i];
    ok(z == 0, "zone time and coasting cleared");
  }
  ok(rc.lapsCompleted() == 0, "counter cleared");
  ok(rc.bestLapIndex() == -1, "best lap cleared");
  ok(!rc.takeLapEvent(fs), "no stale event survives a reset");
  ok(rc.currentLap().index == 1, "back to lap 1");

  printf("\n%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
