// Host-side exercise of the battery warning thresholds and their hysteresis.
#include "power/BatteryWarn.h"
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

static const char* tierName(uint8_t t) {
  switch (t) { case 0: return "fine"; case 1: return "low";
               case 2: return "very low"; default: return "critical"; }
}

// The state machine as Power runs it, so the tests exercise the real sequence
// rather than the two helpers in isolation.
struct Warner {
  uint8_t tier = 0;
  bool    wasCharging = false;
  PowerEvent step(uint8_t pct, bool critical, bool charging) {
    if (charging) {
      PowerEvent e = PowerEvent::None;
      if (tier || !wasCharging) {
        tier = 0;
        if (!wasCharging) e = PowerEvent::Charging;
      }
      wasCharging = true;
      return e;
    }
    wasCharging = false;
    uint8_t t = batteryWarnTier(pct, critical);
    if (t > tier) { tier = t; return batteryWarnEvent(t); }
    if (t < tier && batteryWarnClears(tier, pct, critical)) tier = t;
    return PowerEvent::None;
  }
};

int main() {
  head("which tier a reading belongs to");
  ok(batteryWarnTier(100, false) == 0, "a full battery is fine");
  ok(batteryWarnTier(BATT_WARN_PCT + 1, false) == 0, "just above the first threshold");
  ok(batteryWarnTier(BATT_WARN_PCT, false) == 1, "on it counts as low");
  ok(batteryWarnTier(BATT_WARN_LOW_PCT + 1, false) == 1, "still low above the second");
  ok(batteryWarnTier(BATT_WARN_LOW_PCT, false) == 2, "on the second is very low");
  ok(batteryWarnTier(0, false) == 2, "empty without the voltage flag is still very low");
  // Critical is a voltage judgement and outranks the percentage entirely.
  ok(batteryWarnTier(80, true) == 3, "the critical flag wins whatever the percentage says");

  head("each warning fires once on the way down");
  Warner w;
  int fired[4] = {0, 0, 0, 0};
  for (int pct = 100; pct >= 0; pct--) {
    PowerEvent e = w.step((uint8_t)pct, pct <= 3, false);
    if (e == PowerEvent::Low)      fired[1]++;
    if (e == PowerEvent::VeryLow)  fired[2]++;
    if (e == PowerEvent::Critical) fired[3]++;
  }
  printf("       low=%d verylow=%d critical=%d\n", fired[1], fired[2], fired[3]);
  ok(fired[1] == 1, "low warned exactly once");
  ok(fired[2] == 1, "very low warned exactly once");
  ok(fired[3] == 1, "critical warned exactly once");

  head("a reading wobbling on the threshold does not nag");
  w = Warner();
  ok(w.step(BATT_WARN_PCT, false, false) == PowerEvent::Low, "warns on the way down");
  int extra = 0;
  // A cell sags under load on every climb and recovers on every descent. Two
  // percent of wobble must not produce a warning per hill.
  for (int i = 0; i < 50; i++) {
    if (w.step((uint8_t)(BATT_WARN_PCT + (i % 2 ? 2 : 0)), false, false) != PowerEvent::None)
      extra++;
  }
  printf("       %d further warnings across fifty wobbles\n", extra);
  ok(extra == 0, "and says nothing more while it hovers there");

  head("it re-arms once the reading is genuinely clear");
  w = Warner();
  w.step(BATT_WARN_PCT, false, false);
  // Just short of the margin is not clear enough.
  w.step((uint8_t)(BATT_WARN_PCT + BATT_WARN_HYSTERESIS_PCT - 1), false, false);
  ok(w.step(BATT_WARN_PCT, false, false) == PowerEvent::None,
     "still held just inside the margin");
  w.step((uint8_t)(BATT_WARN_PCT + BATT_WARN_HYSTERESIS_PCT), false, false);
  ok(w.step(BATT_WARN_PCT, false, false) == PowerEvent::Low,
     "released once clear, so a real second drop warns again");

  head("charging answers the warning");
  w = Warner();
  ok(w.step(8, false, false) == PowerEvent::VeryLow, "very low on battery");
  ok(w.step(8, false, true) == PowerEvent::Charging, "plugging in says so");
  ok(w.step(8, false, true) == PowerEvent::None, "and does not repeat");
  ok(w.tier == 0, "the warning is cleared while charging");
  // Unplugging at a level that is still low must warn again: the rider needs to
  // know the charge did not take.
  ok(w.step(8, false, false) == PowerEvent::VeryLow, "unplugging still low warns again");

  head("skipping straight past a tier");
  w = Warner();
  // A sudden sag under load can jump several tiers between samples.
  ok(w.step(5, false, false) == PowerEvent::VeryLow, "a jump from full lands on very low");
  ok(w.tier == 2, "and holds that tier, not the one it skipped");
  ok(w.step(5, true, false) == PowerEvent::Critical, "then critical when the voltage says so");

  head("critical holds until the voltage recovers");
  w = Warner();
  w.step(12, true, false);
  ok(w.tier == 3, "critical held");
  // The percentage climbing is not enough while the voltage flag is still set.
  ok(!batteryWarnClears(3, 90, true), "a high percentage does not clear a critical voltage");
  ok(batteryWarnClears(3, BATT_WARN_LOW_PCT + BATT_WARN_HYSTERESIS_PCT, false),
     "but it clears once the voltage recovers and the level is clear");

  head("tier names line up with events");
  for (uint8_t t = 0; t <= 3; t++) {
    PowerEvent e = batteryWarnEvent(t);
    char d[64];
    snprintf(d, sizeof(d), "tier %u (%s)", t, tierName(t));
    ok((t == 0) == (e == PowerEvent::None), d);
  }

  printf("\n%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
