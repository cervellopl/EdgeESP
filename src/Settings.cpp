#include "Settings.h"
#include <Preferences.h>

Settings g_settings;

static Preferences s_prefs;

void Settings::resetToDefaults() {
  units     = Units::Metric;
  riderKg   = RIDER_WEIGHT_KG;
  bikeKg    = BIKE_WEIGHT_KG;
  wheelMm   = WHEEL_CIRCUMFERENCE_MM;
  ftpWatts  = RIDER_FTP_W;
  lthrBpm   = RIDER_LTHR_BPM;
  drivetrain = DRIVETRAIN_DEFAULT;
  mapZoom   = MAP_ZOOM_DEFAULT;
  backlight = BACKLIGHT_DEFAULT;
  autoPause = AUTOPAUSE_ON;
  autoLapM  = AUTOLAP_DISTANCE_M;
}

void Settings::load() {
  resetToDefaults();
  s_prefs.begin("settings", true);
  units     = (Units)(s_prefs.getUChar("units", (uint8_t)units) ? 1 : 0);
  riderKg   = s_prefs.getFloat("rider", riderKg);
  bikeKg    = s_prefs.getFloat("bike", bikeKg);
  wheelMm   = s_prefs.getUShort("wheel", wheelMm);
  ftpWatts  = s_prefs.getUShort("ftp", ftpWatts);
  lthrBpm   = s_prefs.getUChar("lthr", lthrBpm);
  drivetrain = s_prefs.getUChar("drv", drivetrain);
  mapZoom   = s_prefs.getUChar("mapz", mapZoom);
  backlight = s_prefs.getUChar("bl", backlight);
  autoPause = s_prefs.getBool("ap", autoPause);
  autoLapM  = s_prefs.getUShort("al", autoLapM);
  s_prefs.end();

  // A value out of range here would quietly poison the power model or the speed
  // sensor, so clamp on the way in rather than trusting what was stored.
  if (!(riderKg >= 30.0f && riderKg <= 200.0f)) riderKg = RIDER_WEIGHT_KG;
  if (!(bikeKg  >= 3.0f  && bikeKg  <= 40.0f))  bikeKg  = BIKE_WEIGHT_KG;
  if (wheelMm < 900 || wheelMm > 2500) wheelMm = WHEEL_CIRCUMFERENCE_MM;
  if (ftpWatts < 60 || ftpWatts > 600) ftpWatts = RIDER_FTP_W;
  if (lthrBpm < 100 || lthrBpm > 220) lthrBpm = RIDER_LTHR_BPM;
  if (drivetrain >= DRIVETRAIN_PRESET_COUNT) drivetrain = DRIVETRAIN_DEFAULT;
  if (mapZoom >= MAP_ZOOM_COUNT) mapZoom = MAP_ZOOM_DEFAULT;
  if (backlight < 10) backlight = 10;
  if (autoLapM > 50000) autoLapM = 0;
}

void Settings::save() {
  s_prefs.begin("settings", false);
  s_prefs.putUChar("units", (uint8_t)units);
  s_prefs.putFloat("rider", riderKg);
  s_prefs.putFloat("bike", bikeKg);
  s_prefs.putUShort("wheel", wheelMm);
  s_prefs.putUShort("ftp", ftpWatts);
  s_prefs.putUChar("lthr", lthrBpm);
  s_prefs.putUChar("drv", drivetrain);
  s_prefs.putUChar("mapz", mapZoom);
  s_prefs.putUChar("bl", backlight);
  s_prefs.putBool("ap", autoPause);
  s_prefs.putUShort("al", autoLapM);
  s_prefs.end();
}

// --------------------------------------------------------------------------
void Settings::stepUnits(int dir) {
  (void)dir;
  units = imperial() ? Units::Metric : Units::Imperial;
}

// Weights step in whatever unit is on the screen, so the displayed number moves
// by a round amount instead of drifting in fractions of a pound.
void Settings::stepRider(int dir) {
  if (imperial()) {
    float lb = mass(riderKg) + dir * 1.0f;
    riderKg = constrain(lb / 2.204623f, 30.0f, 200.0f);
  } else {
    riderKg = constrain(riderKg + dir * 0.5f, 30.0f, 200.0f);
  }
}

void Settings::stepBike(int dir) {
  if (imperial()) {
    float lb = mass(bikeKg) + dir * 0.5f;
    bikeKg = constrain(lb / 2.204623f, 3.0f, 40.0f);
  } else {
    bikeKg = constrain(bikeKg + dir * 0.1f, 3.0f, 40.0f);
  }
}

void Settings::stepWheel(int dir) {
  int v = (int)wheelMm + dir * 5;
  wheelMm = (uint16_t)constrain(v, 900, 2500);
}

void Settings::stepFtp(int dir) {
  int v = (int)ftpWatts + dir * 5;
  ftpWatts = (uint16_t)constrain(v, 60, 600);
}

void Settings::stepLthr(int dir) {
  int v = (int)lthrBpm + dir;
  lthrBpm = (uint8_t)constrain(v, 100, 220);
}

void Settings::stepDrivetrain(int dir) {
  int n = DRIVETRAIN_PRESET_COUNT;
  drivetrain = (uint8_t)(((int)drivetrain + (dir >= 0 ? 1 : n - 1)) % n);
}

// Round in whatever units are on screen: 500 m and a quarter mile are both
// sensible things to ask a map for, 402 m is not.
float Settings::mapZoomLevelM(uint8_t level) const {
  if (level == 0 || level >= MAP_ZOOM_COUNT) return 0.0f;
  static const float kM[MAP_ZOOM_COUNT - 1] = {
    100.0f, 200.0f, 300.0f, 500.0f, 800.0f, 1200.0f, 2000.0f, 5000.0f};
  static const float kI[MAP_ZOOM_COUNT - 1] = {   // 300ft 500ft 1000ft 1/4mi 1/2mi 1mi 2mi 3mi
    91.44f, 152.4f, 304.8f, 402.336f, 804.672f, 1609.344f, 3218.688f, 4828.032f};
  return imperial() ? kI[level - 1] : kM[level - 1];
}

void Settings::stepMapZoom(int dir) {
  int n = MAP_ZOOM_COUNT;
  mapZoom = (uint8_t)(((int)mapZoom + (dir >= 0 ? 1 : n - 1)) % n);
}

void Settings::stepBacklight(int dir) {
  int v = (int)backlight + dir * 15;
  backlight = (uint8_t)constrain(v, 10, 255);
}

void Settings::stepAutoPause(int dir) {
  (void)dir;
  autoPause = !autoPause;
}

void Settings::stepAutoLap(int dir) {
  // Round distances in the unit being ridden: 5 km and 5 mi are both sensible
  // lap marks, 8.05 km is not.
  static const uint16_t kMetric[]   = {0, 1000, 2000, 5000, 10000, 20000};
  static const uint16_t kImperial[] = {0, 1609, 3219, 8047, 16093, 32187};
  const uint16_t* tbl = imperial() ? kImperial : kMetric;
  const int n = 6;

  int idx = 0, best = 1 << 30;
  for (int i = 0; i < n; i++) {
    int d = abs((int)tbl[i] - (int)autoLapM);
    if (d < best) { best = d; idx = i; }
  }
  idx = (idx + (dir >= 0 ? 1 : n - 1)) % n;
  autoLapM = tbl[idx];
}
