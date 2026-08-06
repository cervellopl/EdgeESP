#include <Arduino.h>
#include "config.h"
#include "ui/Display.h"
#include "ui/Ui.h"
#include "gps/UbloxGps.h"
#include "gps/GpsWarn.h"
#include "ride/RideComputer.h"
#include "ride/Recorder.h"
#include "sensors/BleSensors.h"
#include "sensors/Baro.h"
#include "power/Power.h"
#include "input/Buttons.h"
#include "link/PhoneLink.h"
#include "link/WebPortal.h"
#include "nav/Course.h"
#include "sensors/Weather.h"
#include "sensors/Compass.h"
#include "sensors/EnvHistory.h"
#include "ui/Beeper.h"
#include "ride/Workout.h"
#include "ride/TrainingLoad.h"
#include "ride/Drivetrain.h"
#include "Settings.h"

static EdgeDisplay  lcd;
static Ui           ui;
static UbloxGps     gps;
static GpsWatch     gpsWatch;
static RideComputer rideComputer;
static Recorder     recorder;
static Baro         baro;
static Buttons      buttons;

static uint32_t lastRideUpdateMs = 0;

// The ride found on the card at boot, held until the rider answers the
// prompt. Static rather than stacked - it carries a whole RideState.
static RideCheckpoint g_pendingCkpt;

// Course parsing blocks for a second or two; this feeds the progress bar.
static void courseProgress(uint8_t pct) { ui.showLoadProgress(pct); }

// --------------------------------------------------------------------------
static void splash(const char* line1, const char* line2) {
  lcd.fillScreen(0x0000);
  lcd.setTextDatum(lgfx::textdatum_t::middle_center);
  lcd.setFont(&fonts::Font7);
  lcd.setTextColor(0xFD20, 0x0000);
  lcd.setTextSize(1.4f);
  lcd.drawString("0.0", lcd.width() / 2, lcd.height() / 2 - 40);
  lcd.setTextSize(1.0f);
  lcd.setFont(&fonts::Font4);
  lcd.setTextColor(0xFFFF, 0x0000);
  lcd.drawString(line1, lcd.width() / 2, lcd.height() / 2 + 30);
  lcd.setFont(&fonts::Font2);
  lcd.setTextColor(0x8410, 0x0000);
  lcd.drawString(line2, lcd.width() / 2, lcd.height() / 2 + 60);
}

static void bootLine(int idx, const char* text, bool ok) {
  lcd.setFont(&fonts::Font2);
  lcd.setTextDatum(lgfx::textdatum_t::top_left);
  lcd.setTextColor(ok ? 0x07E0 : 0xF800, 0x0000);
  lcd.drawString(ok ? "OK  " : "--  ", 20, 210 + idx * 18);
  lcd.setTextColor(0xFFFF, 0x0000);
  lcd.drawString(text, 60, 210 + idx * 18);
}

// --------------------------------------------------------------------------
static void startRide() {
  if (!recorder.active()) {
    if (!recorder.startRide(rideComputer.state()))
      ui.toast(recorder.mounted() ? "Could not open file" : "No SD card - not saving");
    else
      ui.toast("Recording");
  }
  rideComputer.start();
}

static void stopRide() {
  rideComputer.pause();
  ui.toast("Paused - hold LAP to save");
}

static void saveRide() {
  rideComputer.stop();

  // Bank the training stress before anything is cleared. Only on save: a
  // discarded ride must not leave a mark on months of history.
  float tss = ui.currentRideTss();
  if (tss > 0) {
    g_load.addTss(tss);
    g_load.save();
  }

  if (recorder.active()) {
    bool ok = recorder.stopRide(rideComputer);
    char b[48];
    if (ok && tss > 0) snprintf(b, sizeof(b), "Ride saved  %.0f TSS", tss);
    else               snprintf(b, sizeof(b), ok ? "Ride saved" : "Save failed");
    ui.toast(b, 4000);
  } else {
    ui.toast(tss > 0 ? "Not recorded, but TSS logged" : "Nothing to save");
  }

  // What a bike computer does when you finish: put the ride up. Anything worth
  // knowing about it is on one screen, and the rider did not have to go looking.
  ui.gotoPage(Page::Summary);
  g_beeper.done();
}

static void discardRide() {
  rideComputer.stop();
  recorder.discard();
  rideComputer.reset();
  ui.toast("Ride discarded");
}

static void doAction(UiAction a) {
  switch (a) {
    case UiAction::StartStop:
      if (rideComputer.state().status == RideStatus::Running ||
          rideComputer.state().status == RideStatus::AutoPaused) stopRide();
      else startRide();
      break;
    case UiAction::Lap:
      // During a workout the lap button is the step button, exactly as it is on
      // a Garmin. The step change raises a lap of its own, so each interval
      // lands as one row on the lap page and one lap in the file.
      if (g_workout.running()) g_workout.skipStep();
      else rideComputer.lap();
      break;

    case UiAction::LoadWorkout:
      ui.openWorkoutPicker();
      break;
    case UiAction::WorkoutChosen: {
      char buf[64];
      int8_t preset = ui.chosenWorkoutPreset();
      bool ok = preset >= 0 ? g_workout.loadPreset((uint8_t)preset)
                            : g_workout.loadFile(ui.chosenCourse());
      if (ok) {
        g_workout.start();
        snprintf(buf, sizeof(buf), "%s  %u steps", g_workout.name(), g_workout.stepCount());
        ui.toast(buf, 4000);
        ui.gotoPage(Page::Workout);
        g_beeper.done();
        if (!rideComputer.state().recording) startRide();
      } else {
        snprintf(buf, sizeof(buf), "Load failed: %s", g_workout.lastError());
        ui.toast(buf, 4000);
      }
      break;
    }
    case UiAction::StopWorkout:
      if (g_workout.loaded()) { g_workout.stop(); g_workout.clear(); ui.toast("Workout stopped"); }
      else ui.toast("No workout running");
      break;
    case UiAction::SaveRide:    saveRide(); break;
    case UiAction::DiscardRide: discardRide(); break;
    case UiAction::ResetRide:
      if (!recorder.active()) { rideComputer.reset(); ui.toast("Totals reset"); }
      else ui.toast("Stop the ride first");
      break;
    case UiAction::LoadCourse:
      if (!recorder.mounted()) { ui.toast("No SD card"); break; }
      if (!SD.exists(COURSE_DIR)) SD.mkdir(COURSE_DIR);
      ui.openCoursePicker();
      break;
    case UiAction::CourseChosen: {
      char buf[64];
      if (g_course.load(ui.chosenCourse(), courseProgress)) {
        snprintf(buf, sizeof(buf), "%s  %.1f km", g_course.name(),
                 g_course.totalDistance() / 1000.0f);
        ui.toast(buf, 4000);
        ui.gotoPage(Page::Nav);
        g_beeper.done();
      } else {
        snprintf(buf, sizeof(buf), "Load failed: %s", g_course.lastError());
        ui.toast(buf, 4000);
      }
      break;
    }
    case UiAction::NavigateBack: {
      // No basemap means we cannot invent a way home - but we can always
      // retrace the way we came, and the breadcrumb is already in memory.
      char buf[64];
      if (g_course.buildFromTrack(rideComputer.track(), rideComputer.trackCount(),
                                  "Back to start")) {
        snprintf(buf, sizeof(buf), "Retracing %.1f km home",
                 g_course.totalDistance() / 1000.0f);
        ui.toast(buf, 4000);
        ui.gotoPage(Page::Nav);
        g_beeper.done();
      } else {
        ui.toast(g_course.lastError(), 3000);
      }
      break;
    }
    case UiAction::ClearCourse:
      g_course.clear();
      ui.clearAlert();
      ui.toast("Course cleared");
      break;
    case UiAction::PairSensors:
      g_sensors.startScan(20);
      ui.toast("Scanning for sensors...", 4000);
      break;
    case UiAction::OpenSettings:
      ui.openSettings();
      break;
    case UiAction::EditFields:
      ui.openFieldEditor();
      break;
    case UiAction::SelectPages:
      ui.openPageSelect();
      break;

    case UiAction::ResumeRide: {
      // Put the totals back first: the recorder needs somewhere to keep
      // adding to, and the rider needs to see their ride is still there.
      rideComputer.restore(g_pendingCkpt.state, g_pendingCkpt.profile);
      if (recorder.resumeRide(g_pendingCkpt)) {
        ui.toast("Ride resumed - press ENTER to roll", 5000);
        ui.gotoPage(Page::Summary);
        g_beeper.done();
      } else {
        rideComputer.reset();
        ui.toast("Could not reopen the ride file", 4000);
      }
      break;
    }
    case UiAction::FinishAbandonedRide: {
      rideComputer.restore(g_pendingCkpt.state, g_pendingCkpt.profile);
      bool ok = recorder.finishAbandoned(g_pendingCkpt, rideComputer);
      float tss = ui.currentRideTss();
      if (ok && tss > 0) { g_load.addTss(tss); g_load.save(); }
      ui.toast(ok ? "Ride saved" : "Could not finish the ride file", 4000);
      rideComputer.stop();
      ui.gotoPage(Page::Summary);
      g_beeper.done();
      break;
    }
    case UiAction::CalibrateCompass:
      if (!g_compass.present()) ui.toast("No magnetometer fitted", 3000);
      else { g_compass.startCalibration(); ui.openCompassCalibration(); }
      break;
    case UiAction::ToggleWifi:
      g_portal.toggle();
      ui.setWifiInfo(g_portal.running(), g_portal.ip());
      ui.toast(g_portal.running() ? g_portal.ip() : "Wi-Fi off", 5000);
      break;
    case UiAction::Sleep:
      if (rideComputer.state().recording) ui.toast("Stop the ride first");
      else { ui.toast("Sleeping", 800); ui.render(); delay(800); g_power.deepSleep(); }
      break;
    default: break;
  }
}

static void handlePhone() {
  switch (g_phone.takeCommand()) {
    case PhoneCommand::Start:   doAction(UiAction::StartStop); break;
    case PhoneCommand::Stop:    stopRide(); break;
    case PhoneCommand::Lap:     doAction(UiAction::Lap); break;
    case PhoneCommand::Save:    saveRide(); break;
    case PhoneCommand::Discard: discardRide(); break;
    case PhoneCommand::Pair:    doAction(UiAction::PairSensors); break;
    case PhoneCommand::Wifi:    doAction(UiAction::ToggleWifi); break;
    case PhoneCommand::Sleep:   doAction(UiAction::Sleep); break;
    default: break;
  }
  char title[24], body[64];
  if (g_phone.takeNotification(title, sizeof(title), body, sizeof(body))) {
    ui.notify(title, body);
    g_power.noteActivity();
  }
}

// Navigation events are the only thing allowed to interrupt the rider
// unprompted, so each one gets a distinct sound as well as a banner.
static void handleCourseEvents() {
  char title[24], body[64];
  CourseEvent ev = g_course.takeEvent();
  const NavCue* q = g_course.nextTurn();

  // One helper for the two approach stages - they differ only in the sound.
  auto announceTurn = [&](bool loud) {
    if (!q) return;
    const char* what = (q->named && q->name[0]) ? q->name : Course::turnText(q->type);
    snprintf(title, sizeof(title), "%s", Course::turnText(q->type));
    float d = g_course.distanceToNextTurn();
    if (d < 1000) snprintf(body, sizeof(body), "%s in %.0f m", what, d);
    else          snprintf(body, sizeof(body), "%s in %.1f km", what, d / 1000.0f);
    ui.notify(title, body);
    if (loud) g_beeper.cue(); else g_beeper.chirp();
    g_power.noteActivity();
  };

  switch (ev) {
    case CourseEvent::WentOffCourse: {
      float rel = g_course.relativeBearingToRoute();
      if (!isnan(rel))
        snprintf(body, sizeof(body), "route %.0f m away, %s",
                 g_course.crossTrack(), clockDirection(rel));
      else
        snprintf(body, sizeof(body), "%.0f m from the route", g_course.crossTrack());
      ui.alert("OFF COURSE", body);
      ui.gotoPage(Page::Map);
      g_beeper.alert();
      g_power.noteActivity();
      break;
    }
    case CourseEvent::BackOnCourse:
      ui.clearAlert();
      ui.toast("Back on course", 3000);
      g_beeper.chirp();
      break;

    case CourseEvent::TurnFar:  announceTurn(false); break;
    case CourseEvent::TurnNear: announceTurn(true);  break;
    case CourseEvent::TurnNow:
      if (q) {
        ui.notify(Course::turnText(q->type),
                  (q->named && q->name[0]) ? q->name : "now");
        g_beeper.cue();
        g_power.noteActivity();
        // Put the map up for the corner itself - the geometry is the
        // instruction when there is no street name to read.
        if (ui.page() != Page::Map && ui.page() != Page::Nav) ui.gotoPage(Page::Map);
      }
      break;

    case CourseEvent::Finished:
      ui.notify("Course complete", g_course.name());
      g_beeper.done();
      break;
    default: break;
  }
}

// A lap is a lap whether the rider pressed the button or rode past the auto-lap
// mark, so both land here: written to the file, announced, and shown.
static void handleLapEvent() {
  FitSummary fs;
  if (!rideComputer.takeLapEvent(fs)) return;
  if (recorder.active()) recorder.markLap(fs);
  uint8_t n = rideComputer.lapRecordCount();
  if (n) ui.toastLap(rideComputer.lapRecord(n - 1));
  g_beeper.chirp();
  g_power.noteActivity();
}

// Workout steps double as laps, so the lap page and the FIT file both end up
// with one entry per interval without the rider touching anything.
static void handleWorkoutEvents() {
  char title[24], body[64];
  const WorkoutStep& st = g_workout.current();

  switch (g_workout.takeEvent()) {
    case WorkoutEvent::StepChanged: {
      rideComputer.lap();
      char dur[20];
      if (st.durType == StepDuration::Time) {
        uint32_t s = st.durValue / 1000;
        snprintf(dur, sizeof(dur), "%lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
      } else if (st.durType == StepDuration::Distance) {
        snprintf(dur, sizeof(dur), "%.1f %s", g_settings.distLong(st.durValue),
                 g_settings.distLongUnit());
      } else {
        snprintf(dur, sizeof(dur), "until LAP");
      }
      snprintf(title, sizeof(title), "%s", Workout::kindText(st.kind));
      if (st.target != StepTarget::None)
        snprintf(body, sizeof(body), "%s  at %u-%u %s", dur, st.lo, st.hi,
                 Workout::targetUnit(st.target));
      else
        snprintf(body, sizeof(body), "%s", dur);
      ui.notify(title, body);
      ui.gotoPage(Page::Workout);
      g_beeper.cue();
      g_power.noteActivity();
      break;
    }
    case WorkoutEvent::Finished:
      ui.notify("Workout complete", g_workout.name());
      g_beeper.done();
      g_power.noteActivity();
      break;
    case WorkoutEvent::WentOutOfTarget: {
      Compliance c = g_workout.compliance();
      snprintf(body, sizeof(body), "%u %s, target %u-%u", g_workout.currentValue(),
               Workout::targetUnit(st.target), st.lo, st.hi);
      ui.toast(c == Compliance::Below ? "Ease up? You are under target"
                                      : "Over target", 3000);
      g_beeper.chirp();
      break;
    }
    default: break;
  }
}

// Losing the GPS is the one failure the screen hides rather than shows: the
// speed goes to zero and the distance simply stops growing, which is exactly
// what a rest at the lights looks like. So a dropout long enough to be real is
// said out loud, once, and so is getting the fix back.
static void handleGpsEvents() {
  static bool sticky = false;         // did the GPS put the red banner up?
  char body[64];
  const GpsFix& f = gps.fix();
  bool riding = rideComputer.state().recording;

  switch (gpsWatch.takeEvent()) {
    case GpsEvent::Degraded:
      snprintf(body, sizeof(body), "%.0f %s on %u sats - distance will drift",
               g_settings.distShort(f.hAcc), g_settings.distShortUnit(), f.numSV);
      ui.notify("GPS accuracy poor", body);
      g_beeper.cue();
      g_power.noteActivity();
      break;

    case GpsEvent::Lost: {
      const char* what = f.fixType == 2 ? "2D only" : "no fix";
      snprintf(body, sizeof(body), "%s - distance is not counting", what);
      // Mid-ride this is worth the sticky banner: every second it goes unnoticed
      // is a second missing from the file. Parked, it is only worth saying.
      if (riding) { ui.alert("GPS SIGNAL LOST", body); sticky = true; g_beeper.alert(); }
      else        { ui.notify("GPS signal lost", body); g_beeper.cue(); }
      g_power.noteActivity();
      break;
    }

    case GpsEvent::Silent:
      // Nothing at all from the receiver is a wiring or power fault, not
      // weather, so it says something the rider can actually act on.
      ui.alert("GPS NOT RESPONDING", "receiver silent - check the wiring");
      sticky = true;
      g_beeper.alert();
      g_power.noteActivity();
      break;

    case GpsEvent::Reacquired: {
      uint16_t s = gpsWatch.outageSeconds();
      if (s >= 60) snprintf(body, sizeof(body), "GPS back after %u:%02u", s / 60, s % 60);
      else         snprintf(body, sizeof(body), "GPS back after %u s", s);
      // Only take down a banner this handler put up - the off-course alert is
      // sticky too, and it is still true.
      if (sticky) { ui.clearAlert(); sticky = false; }
      ui.toast(body, 3000);
      g_beeper.chirp();
      break;
    }

    default: break;
  }
}

// The battery is the one failure that takes the ride with it, so the warnings
// are graded: a heads-up, then a real alert that also buys some runtime back,
// then closing the ride off while there is still power to do it with.
static void handlePowerEvents() {
  char body[64];
  float hrs = g_power.hoursRemaining();
  bool riding = rideComputer.state().recording;

  switch (g_power.takeEvent()) {
    case PowerEvent::Low:
      if (!isnan(hrs)) snprintf(body, sizeof(body), "%u%%, roughly %.1f h left",
                                g_power.percent(), hrs);
      else             snprintf(body, sizeof(body), "%u%% remaining", g_power.percent());
      ui.notify("Battery low", body);
      g_beeper.cue();
      g_power.noteActivity();
      break;

    case PowerEvent::VeryLow:
      // The backlight is the biggest single draw, so turning it down is the
      // one thing that actually buys time. Say so rather than doing it quietly.
      g_power.setBacklight(BACKLIGHT_DIM);
      snprintf(body, sizeof(body), "%u%% - screen dimmed to save power",
               g_power.percent());
      ui.alert("BATTERY VERY LOW", body);
      g_beeper.alert();
      g_power.noteActivity();
      break;

    case PowerEvent::Critical:
      if (riding) {
        // Close the ride properly while there is still power to write the
        // header and the CRC. The checkpoint would survive a hard cut, but a
        // finished file needs no recovery prompt and uploads as it is.
        saveRide();
        ui.alert("BATTERY CRITICAL", "ride saved and closed");
      } else {
        ui.alert("BATTERY CRITICAL", "charge before starting a ride");
      }
      g_beeper.alert();
      g_power.noteActivity();
      break;

    case PowerEvent::Charging:
      ui.clearAlert();
      // Give back the brightness the warning took away.
      g_power.setBacklight(g_settings.backlight);
      ui.toast("Charging", 3000);
      break;

    default: break;
  }
}

static void handleWeatherEvents() {
  char body[64];
  switch (g_weather.takeEvent()) {
    case WeatherEvent::RainSoon: {
      int16_t m = g_weather.minutesToRain();
      snprintf(body, sizeof(body), "forecast says about %d min out", m);
      ui.notify("Rain ahead", body);
      g_beeper.cue();
      g_power.noteActivity();
      break;
    }
    case WeatherEvent::RainStopping:
      ui.toast("Rain easing off", 4000);
      break;
    default: break;
  }
}

// --------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.printf("\n%s %s booting\n", DEVICE_NAME, FIRMWARE_VERSION);

  // Probe the panel before LovyanGFX claims the bus. One line of serial output
  // tells you which -DPANEL_xxx to build with.
  dumpPanelId();

  // Settings first: the backlight level and the units every later screen uses
  // both come from here.
  g_settings.load();

  lcd.init();
  g_power.begin();                       // owns the backlight PWM
  g_power.setBacklight(g_settings.backlight);
  splash(DEVICE_NAME, "GPS bike computer - " FIRMWARE_VERSION);
  delay(400);

  bootLine(0, "Display 480x320 16-bit parallel", true);

  bool sdOk = recorder.begin();
  bootLine(1, sdOk ? "microSD mounted" : "microSD not found", sdOk);

  bool baroOk = baro.begin();
  bootLine(2, baroOk ? "BME280 barometer" : "No barometer (GPS altitude)", baroOk);

  bool magOk = g_compass.begin();
  char ml[56];
  if (magOk) snprintf(ml, sizeof(ml), "%s compass%s", g_compass.chipName(),
                      g_compass.calibrated() ? "" : " - needs calibration");
  else snprintf(ml, sizeof(ml), "No magnetometer (GPS course heading)");
  bootLine(3, ml, magOk);

  bool gpsOk = gps.begin(Serial1, GPS_RX_PIN, GPS_TX_PIN, GPS_BAUD_TARGET);
  char gl[48];
  snprintf(gl, sizeof(gl), gpsOk ? "u-blox @ %lu baud, UBX %d Hz"
                                 : "GPS not responding (@ %lu)",
           (unsigned long)gps.detectedBaud(), GPS_NAV_RATE_HZ);
  bootLine(4, gl, gpsOk);

  g_sensors.begin();
  g_phone.begin();
  bootLine(5, "BLE sensors + phone link", true);

  g_load.begin();
  g_drive.setPreset(g_settings.drivetrain);
  g_portal.begin(&rideComputer);
  buttons.begin();
  g_beeper.begin();
  rideComputer.begin();
  if (sdOk && !SD.exists(COURSE_DIR)) SD.mkdir(COURSE_DIR);

  ui.begin(&lcd);
  ui.bind(&rideComputer, &recorder, &g_sensors, &baro, &g_power, &g_course,
          &g_weather, &g_compass);
  delay(900);

  // Anything left on the card from a ride the power cut short.
  if (sdOk && recorder.findCheckpoint(g_pendingCkpt)) {
    ui.offerResume(g_pendingCkpt);
    g_beeper.alert();
  }

  lastRideUpdateMs = millis();
}

// --------------------------------------------------------------------------
void loop() {
  uint32_t now = millis();

  // --- input ---
  ButtonEvent ev = buttons.poll();
  if (ev.btn != BTN_NONE) {
    g_power.noteActivity();
    doAction(ui.handleButton(ev));
  }

  // --- sensors ---
  gps.update();
  baro.update();
  g_compass.update();
  g_power.update();
  g_beeper.tick();
  g_weather.tick();
  handlePhone();
  // Before the lap handler: a step change raises the lap it is paired with.
  handleWorkoutEvents();
  handleLapEvent();
  handleCourseEvents();
  handleGpsEvents();
  handlePowerEvents();
  handleWeatherEvents();
  g_portal.handle();

  // --- ride maths at a fixed 10 Hz ---
  if (now - lastRideUpdateMs >= 100) {
    uint32_t dt = now - lastRideUpdateMs;
    lastRideUpdateMs = now;

    rideComputer.setSensors(g_sensors.hasHr(), g_sensors.hr(),
                            g_sensors.hasCadence(), g_sensors.cadence(),
                            g_sensors.hasPower(), g_sensors.power());

    // Heading sources, in order of trust. GPS course is already true north and
    // is rock steady at speed, but it is meaningless below walking pace - which
    // is exactly when someone looks at a compass. The magnetometer covers that
    // gap if one is fitted. Air density comes from the on-board barometer
    // rather than the forecast: local, current, already measured.
    const GpsFix& fix = gps.fix();
    float heading = NAN;
    bool fromMag = false;
    if (fix.valid && fix.speed > 1.5f) {
      heading = fix.heading;
    } else if (g_compass.present() && g_compass.calibrated()) {
      heading = g_compass.trueHeading();
      fromMag = !isnan(heading);
    }
    ui.setHeading(heading, fromMag);
    rideComputer.setAir(g_weather.headwind(heading),
                        Weather::airDensity(baro.pressure(), baro.temperature()));

    rideComputer.update(fix, baro.altitude(), baro.temperature(), dt);

    // A wheel sensor is far better here than GPS: the ratio is a division by
    // cadence, which magnifies both the lag and the wander in a GPS speed.
    float gearSpeed = g_sensors.hasWheelSpeed() ? g_sensors.wheelSpeed()
                                                : rideComputer.state().speed;
    g_drive.update(gearSpeed, g_sensors.hasCadence() ? g_sensors.cadence() : 0,
                   g_settings.wheelMm, dt);

    RideState& s = rideComputer.state();
    s.batteryMv  = g_power.millivolts();
    s.batteryPct = g_power.percent();
    s.charging   = g_power.charging();

    g_workout.update(s, dt);
    recorder.tick(s);
    g_phone.update(s);
  }

  // --- once a second: altitude reference, history, low-battery guard ---
  static uint32_t lastSecond = 0;
  if (now - lastSecond >= 1000) {
    uint32_t secDt = now - lastSecond;
    lastSecond = now;
    const RideState& s = rideComputer.state();

    // The GPS watchdog runs on this beat rather than the 10 Hz one: everything
    // it decides is measured in seconds, and it reads the receiver directly so
    // a module that has stopped talking altogether still counts.
    gpsWatch.update(s.fix.valid, s.fix.hAcc, gps.staleSeconds(), secDt);
    // The banner is a moment; the bar is for the rest of the outage.
    ui.setGpsWarning(gpsWatch.tier(), gpsWatch.outageSeconds());
    g_phone.setGpsWarning(gpsWatch.tier(), gpsWatch.outageSeconds());

    if (s.fix.valid && s.fix.hAcc < 15.0f) baro.calibrateToGps(s.fix.altMSL);
    // Rolls the daily buckets over at midnight even on a ride that spans it.
    if (s.fix.timeValid) g_load.setNow(s.fix.unixTime);

    // Environment history rate-limits itself to one sample a minute, and runs
    // whether or not a ride is recording - the pressure trend is most useful
    // when it has been watching for a while before you set off.
    if (baro.present())
      g_env.sample(baro.temperature(), baro.pressure(), baro.humidity(), now);

    // Course snapping runs at 1 Hz. Faster buys nothing - the off-course
    // thresholds are measured in seconds - and the full-course fallback search
    // is the one genuinely expensive thing in the loop.
    // Heading is only meaningful while actually moving; standing still it is
    // noise, and a spinning "route is at 7 o'clock" arrow is worse than none.
    if (g_course.loaded() && s.fix.valid && s.fix.hAcc < 30.0f)
      g_course.update(s.fix.latLon(), 1000,
                      s.speed > 1.5f ? s.fix.heading : NAN);

  }

  // --- render ~10 Hz ---
  static uint32_t lastRender = 0;
  if (now - lastRender >= 100) {
    lastRender = now;
    ui.render();
  }

  g_power.tickIdle(rideComputer.state().recording);
  delay(2);
}
