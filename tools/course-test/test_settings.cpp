// Host-side exercise of the unit conversions and setting steps.
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
  snprintf(d, sizeof(d), "(got %.4f, want %.4f)", got, want);
  ok(fabs(got - want) <= tol, what, d);
}
static void isStr(const char* got, const char* want, const char* what) {
  char d[64];
  snprintf(d, sizeof(d), "(\"%s\" want \"%s\")", got, want);
  ok(!strcmp(got, want), what, d);
}
static void head(const char* s) { printf("\n== %s ==\n", s); }

int main() {
  Settings& S = g_settings;

  head("metric conversions are pass-through except speed");
  S.resetToDefaults();
  ok(!S.imperial(), "metric by default");
  near(S.speed(10.0f), 36.0, 0.001, "10 m/s is 36 km/h");
  near(S.distLong(1000.0f), 1.0, 0.001, "1000 m is 1 km");
  near(S.distShort(250.0f), 250.0, 0.001, "metres pass through");
  near(S.elev(1000.0f), 1000.0, 0.001, "elevation passes through");
  near(S.temp(21.5f), 21.5, 0.001, "celsius passes through");
  near(S.mass(78.0f), 78.0, 0.001, "kilograms pass through");
  isStr(S.speedUnit(), "km/h", "speed unit");
  isStr(S.distLongUnit(), "km", "long distance unit");
  isStr(S.tempUnit(), "C", "temperature unit");
  near(S.shortCutoffM(), 1000.0, 0.001, "switches to km at 1000 m");

  head("imperial conversions");
  S.units = Units::Imperial;
  ok(S.imperial(), "imperial set");
  near(S.speed(10.0f), 22.36936, 0.001, "10 m/s is 22.37 mph");
  near(S.speed(4.4704f), 10.0, 0.001, "10 mph round trip");
  near(S.distLong(1609.344f), 1.0, 0.0001, "a mile is 1609.344 m");
  near(S.distShort(1.0f), 3.28084, 0.0001, "a metre is 3.28 ft");
  near(S.elev(1000.0f), 3280.84, 0.01, "1000 m is 3280.84 ft");
  near(S.temp(0.0f), 32.0, 0.001, "freezing");
  near(S.temp(100.0f), 212.0, 0.001, "boiling");
  near(S.temp(-40.0f), -40.0, 0.001, "the point where the scales meet");
  near(S.mass(70.0f), 154.3236, 0.001, "70 kg is 154.3 lb");
  isStr(S.speedUnit(), "mph", "speed unit");
  isStr(S.distShortUnit(), "ft", "short distance unit");
  isStr(S.massUnit(), "lb", "mass unit");
  // 1000 ft, so a distance reads in feet until it would exceed four figures.
  near(S.shortCutoffM(), 304.8, 0.01, "switches to miles at 1000 ft");

  head("weight steps in the unit on screen");
  S.resetToDefaults();
  S.riderKg = 78.0f;
  S.stepRider(+1);
  near(S.riderKg, 78.5, 0.001, "metric steps half a kilo up");
  S.stepRider(-1);
  near(S.riderKg, 78.0, 0.001, "and back down");

  S.units = Units::Imperial;
  S.riderKg = 70.0f;
  float lb0 = S.mass(S.riderKg);
  S.stepRider(+1);
  near(S.mass(S.riderKg), lb0 + 1.0, 0.002, "imperial steps a whole pound");
  S.stepRider(-1);
  near(S.riderKg, 70.0, 0.002, "and returns to where it started");

  head("clamping");
  S.resetToDefaults();
  S.riderKg = 200.0f;  S.stepRider(+1);
  near(S.riderKg, 200.0, 0.001, "rider weight stops at 200 kg");
  S.riderKg = 30.0f;   S.stepRider(-1);
  near(S.riderKg, 30.0, 0.001, "and at 30 kg");
  S.bikeKg = 40.0f;    S.stepBike(+1);
  near(S.bikeKg, 40.0, 0.001, "bike weight stops at 40 kg");
  S.bikeKg = 3.0f;     S.stepBike(-1);
  near(S.bikeKg, 3.0, 0.001, "and at 3 kg");
  S.wheelMm = 2500;    S.stepWheel(+1);
  ok(S.wheelMm == 2500, "wheel stops at 2500 mm");
  S.wheelMm = 900;     S.stepWheel(-1);
  ok(S.wheelMm == 900, "and at 900 mm");
  S.backlight = 255;   S.stepBacklight(+1);
  ok(S.backlight == 255, "backlight stops at full");
  S.backlight = 10;    S.stepBacklight(-1);
  ok(S.backlight == 10, "and never reaches zero - an unreadable screen is a brick");

  head("total mass feeds the power model");
  S.resetToDefaults();
  S.riderKg = 78.0f; S.bikeKg = 9.0f;
  near(S.totalMassKg(), 87.0, 0.001, "rider plus bike");

  head("auto lap cycles round distances");
  S.resetToDefaults();
  S.autoLapM = 5000;
  S.stepAutoLap(+1);
  ok(S.autoLapM == 10000, "5 km steps to 10 km");
  S.stepAutoLap(+1);
  ok(S.autoLapM == 20000, "then 20 km");
  S.stepAutoLap(+1);
  ok(S.autoLapM == 0, "then wraps to off");
  S.stepAutoLap(-1);
  ok(S.autoLapM == 20000, "and back down again");

  S.units = Units::Imperial;
  S.autoLapM = 0;
  S.stepAutoLap(+1);
  printf("       imperial first step -> %u m (%.2f mi)\n",
         S.autoLapM, S.distLong(S.autoLapM));
  near(S.distLong(S.autoLapM), 1.0, 0.01, "imperial laps are round miles, not 1 km");
  S.stepAutoLap(+1);
  near(S.distLong(S.autoLapM), 2.0, 0.01, "then two miles");

  head("map zoom levels");
  S.resetToDefaults();
  ok(S.mapZoomAuto(), "auto by default");
  near(S.mapZoomSpanM(), 0.0, 0.001, "auto reports no fixed span");
  S.stepMapZoom(+1);
  ok(!S.mapZoomAuto(), "stepping off auto gives a level");
  near(S.mapZoomSpanM(), 100.0, 0.5, "the first metric level is 100 m");
  // Every level must be a real span, and they must climb.
  float prev = 0;
  bool climbing = true;
  for (uint8_t i = 1; i < Settings::MAP_ZOOM_COUNT; i++) {
    float v = S.mapZoomLevelM(i);
    if (!(v > prev)) climbing = false;
    prev = v;
  }
  ok(climbing, "metric levels increase, none of them zero");
  near(S.mapZoomLevelM(Settings::MAP_ZOOM_COUNT - 1), 5000.0, 1.0, "the widest is 5 km");
  ok(S.mapZoomLevelM(0) == 0.0f, "level zero is auto, not a span");
  ok(S.mapZoomLevelM(200) == 0.0f, "an out-of-range level is not read past the table");

  // Imperial gets round imperial spans, not 100 m rendered as 328 ft.
  S.units = Units::Imperial;
  near(S.mapZoomLevelM(4), 402.336, 0.5, "a quarter mile");
  near(S.mapZoomLevelM(6), 1609.344, 0.5, "a mile");
  climbing = true; prev = 0;
  for (uint8_t i = 1; i < Settings::MAP_ZOOM_COUNT; i++) {
    float v = S.mapZoomLevelM(i);
    if (!(v > prev)) climbing = false;
    prev = v;
  }
  ok(climbing, "imperial levels increase too");

  S.resetToDefaults();
  S.mapZoom = Settings::MAP_ZOOM_COUNT - 1;
  S.stepMapZoom(+1);
  ok(S.mapZoomAuto(), "stepping past the widest wraps back to auto");
  S.stepMapZoom(-1);
  ok(S.mapZoom == Settings::MAP_ZOOM_COUNT - 1, "and back down again");

  head("auto pause toggles");
  S.resetToDefaults();
  bool was = S.autoPause;
  S.stepAutoPause(+1);
  ok(S.autoPause != was, "toggled");
  S.stepAutoPause(-1);
  ok(S.autoPause == was, "and back");

  head("save and load round trip");
  Preferences::wipe();
  S.resetToDefaults();
  S.units = Units::Imperial;
  S.riderKg = 82.5f; S.bikeKg = 7.4f; S.wheelMm = 2096;
  S.backlight = 120; S.autoPause = false; S.autoLapM = 8047;
  S.save();
  S.resetToDefaults();
  ok(!S.imperial(), "defaults really were restored before loading");
  S.load();
  ok(S.imperial(), "units survived");
  near(S.riderKg, 82.5, 0.01, "rider weight survived");
  near(S.bikeKg, 7.4, 0.01, "bike weight survived");
  ok(S.wheelMm == 2096, "wheel survived");
  ok(S.backlight == 120, "backlight survived");
  ok(S.autoPause == false, "auto pause survived");
  ok(S.autoLapM == 8047, "auto lap survived");

  head("a corrupt or ancient store must not poison the model");
  Preferences::wipe();
  Preferences::poke("settings", "rider", 5000.0);
  Preferences::poke("settings", "bike", 0.0);
  Preferences::poke("settings", "wheel", 100.0);
  Preferences::poke("settings", "bl", 2.0);
  Preferences::poke("settings", "al", 60000.0);
  Preferences::poke("settings", "mapz", 99.0);
  Preferences::poke("settings", "drv", 200.0);
  S.load();
  ok(S.mapZoom < Settings::MAP_ZOOM_COUNT, "an out-of-range map zoom falls back");
  ok(S.drivetrain < DRIVETRAIN_PRESET_COUNT, "so does an out-of-range drivetrain");
  near(S.riderKg, RIDER_WEIGHT_KG, 0.01, "absurd rider weight falls back");
  near(S.bikeKg, BIKE_WEIGHT_KG, 0.01, "zero bike weight falls back");
  ok(S.wheelMm == WHEEL_CIRCUMFERENCE_MM, "impossible wheel size falls back");
  ok(S.backlight >= 10, "backlight raised to a readable minimum");
  ok(S.autoLapM == 0, "an out-of-range auto lap becomes off");

  head("NaN in the store");
  Preferences::wipe();
  Preferences::poke("settings", "rider", NAN);
  S.load();
  // The check is written as a positive range test precisely so NaN fails it.
  near(S.riderKg, RIDER_WEIGHT_KG, 0.01, "NaN rider weight falls back");

  printf("\n%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
