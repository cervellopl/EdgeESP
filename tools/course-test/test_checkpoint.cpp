// Host-side exercise of the crash-recovery checkpoint.
#include "ride/RideCheckpoint.h"
#include "Settings.h"
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

// File scope: a RideCheckpoint carries a whole RideState and an elevation
// profile, which is exactly why the firmware heap-allocates it too.
static RideCheckpoint a, b;

int main() {
  head("sealing and validating");
  memset(&a, 0, sizeof(a));
  a = RideCheckpoint();
  a.state.distance = 42315.5;
  a.state.movingMs = 5400000UL;
  a.state.elapsedMs = 6000000UL;
  a.state.ascent = 615.0f;
  a.state.maxSpeed = 16.4f;
  a.state.lapCount = 9;
  a.state.startUnix = 1754000000UL;
  a.state.energyKj = 1180.0f;
  a.fitDataSize = 123456;
  strncpy(a.fitPath, "/rides/20260805_0731.fit", sizeof(a.fitPath) - 1);
  strncpy(a.gpxPath, "/rides/20260805_0731.gpx", sizeof(a.gpxPath) - 1);

  ok(!checkpointValid(a), "an unsealed checkpoint is not valid");
  checkpointSeal(a);
  ok(checkpointValid(a), "sealed, it is");
  ok(a.magic == RIDE_CKPT_MAGIC, "magic stamped");
  ok(a.version == RIDE_CKPT_VERSION, "version stamped");
  ok(a.stateSize == sizeof(RideState), "state size recorded");

  head("round trip through raw bytes");
  // This is what the SD write and read actually do.
  memcpy(&b, &a, sizeof(RideCheckpoint));
  ok(checkpointValid(b), "the copy validates");
  near(b.state.distance, 42315.5, 0.01, "distance survives");
  ok(b.state.movingMs == 5400000UL, "moving time survives");
  ok(b.state.lapCount == 9, "lap count survives");
  near(b.state.ascent, 615.0, 0.01, "ascent survives");
  ok(b.fitDataSize == 123456, "the FIT data size survives");
  ok(!strcmp(b.fitPath, "/rides/20260805_0731.fit"), "the file path survives");

  head("damage is caught");
  memcpy(&b, &a, sizeof(RideCheckpoint));
  b.state.distance += 1.0;
  ok(!checkpointValid(b), "a changed byte fails the CRC");

  memcpy(&b, &a, sizeof(RideCheckpoint));
  b.magic = 0xDEADBEEF;
  ok(!checkpointValid(b), "a foreign file is rejected");

  memcpy(&b, &a, sizeof(RideCheckpoint));
  b.version = RIDE_CKPT_VERSION + 1;
  ok(!checkpointValid(b), "a newer version is rejected");

  // The one that would be silent: a firmware update changes RideState, every
  // field after it shifts, and the totals come back as plausible nonsense.
  memcpy(&b, &a, sizeof(RideCheckpoint));
  b.stateSize = (uint16_t)(sizeof(RideState) + 4);
  b.crc = 0;
  {
    // Reseal by hand so only the size mismatch is under test, not the CRC.
    RideCheckpoint tmp = b;
    checkpointSeal(tmp);
    tmp.stateSize = (uint16_t)(sizeof(RideState) + 4);
    ok(!checkpointValid(tmp), "a RideState of a different size is rejected");
  }

  memcpy(&b, &a, sizeof(RideCheckpoint));
  b.fitPath[0] = 0;
  checkpointSeal(b);
  ok(!checkpointValid(b), "a checkpoint naming no file is useless and rejected");

  head("a half-written file");
  // A power cut mid-write leaves a truncated tail. Zeroing it stands in for
  // whatever was actually left behind; the CRC has to notice.
  memcpy(&b, &a, sizeof(RideCheckpoint));
  memset(((uint8_t*)&b) + sizeof(RideCheckpoint) / 2, 0,
         sizeof(RideCheckpoint) / 2 - sizeof(uint16_t));
  ok(!checkpointValid(b), "a torn write does not pass as a complete one");

  head("an all-zero file");
  memset(&b, 0, sizeof(b));
  ok(!checkpointValid(b), "a blank block is not a checkpoint");

  head("the elevation profile rides along");
  memset(&a, 0, sizeof(a));
  a = RideCheckpoint();
  strncpy(a.fitPath, "/rides/x.fit", sizeof(a.fitPath) - 1);
  for (int i = 0; i < 300; i++)
    a.profile.sample(i * 25.0, 100.0f + i * 0.5f);
  uint16_t n = a.profile.count();
  float top = a.profile.maxElevation();
  checkpointSeal(a);
  memcpy(&b, &a, sizeof(RideCheckpoint));
  ok(checkpointValid(b), "still valid with a profile in it");
  ok(b.profile.count() == n, "the profile point count survives");
  near(b.profile.maxElevation(), top, 0.6, "and its high point");
  near(b.profile.coveredM(), a.profile.coveredM(), 0.1, "and the distance it covers");

  head("size");
  // Written every fifteen seconds, so it is worth knowing what it costs.
  printf("       a checkpoint is %u bytes\n", (unsigned)sizeof(RideCheckpoint));
  ok(sizeof(RideCheckpoint) < 4096, "small enough to write often without hurting the card");

  printf("\n%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
