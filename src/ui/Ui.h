#pragma once
#include "ui/Display.h"
#include "ride/RideComputer.h"
#include "ride/Recorder.h"
#include "sensors/BleSensors.h"
#include "sensors/Baro.h"
#include "power/Power.h"
#include "input/Buttons.h"
#include "nav/Course.h"
#include "sensors/Weather.h"
#include "sensors/EnvHistory.h"
#include "sensors/Compass.h"
#include "nav/Geo.h"
#include "Settings.h"
#include "ride/Workout.h"
#include "ride/TrainingLoad.h"
#include "ride/Drivetrain.h"
#include "ride/RideCheckpoint.h"

// Data fields the user can put on the ride pages. Order matters only for the
// settings picker.
enum class Field : uint8_t {
  Speed, AvgSpeed, MaxSpeed, LapSpeed,
  Distance, LapDistance,
  Timer, Elapsed, LapTime, Clock,
  Altitude, Ascent, Descent, Grade,
  HeartRate, Cadence, Power, Power3s, Power30s, NormPower,
  Calories, Temperature, Battery, LapNumber, GpsAccuracy,
  CourseRemaining, CourseAscent, CourseEta, CourseOffset, NextTurn,
  Headwind, WindSpeed, WindDir, AirTemp, Sunset,
  Heading, ToStart, PowerZone, HrZone, CadenceZone, GearRatio, Development,
  COUNT
};

enum class Page : uint8_t {
  Ride1, Ride2, Summary, Workout, Zones_, HrZones, CadZones, Gear, Laps,
  Load, Map, Nav, Compass, Wx, WxHist, Profile, Status, COUNT
};

// Which sensor a zone page speaks for.
enum class ZoneSource : uint8_t { Power, Hr, Cadence };

// Actions the UI asks main.cpp to perform - the UI never touches the recorder
// or the radios directly.
enum class UiAction : uint8_t {
  None, StartStop, Lap, SaveRide, DiscardRide,
  PairSensors, ToggleWifi, Sleep, ResetRide, CalibrateCompass, EditFields,
  OpenSettings, SelectPages,
  ResumeRide, FinishAbandonedRide,   // answers to the recovery prompt
  LoadCourse,      // open the file picker
  CourseChosen,    // picker confirmed; read chosenCourse()
  ClearCourse,
  NavigateBack,    // turn the ride's own breadcrumb into a route home
  LoadWorkout,     // open the workout picker
  WorkoutChosen,   // picker confirmed; read chosenWorkoutPreset()/chosenCourse()
  StopWorkout
};

class Ui {
 public:
  void begin(EdgeDisplay* lcd);
  void bind(RideComputer* rc, Recorder* rec, BleSensors* sensors, Baro* baro,
            Power* power, Course* course, Weather* weather, Compass* compass);
  // Heading the wind and compass are resolved against; NAN when unknown.
  // fromCompass records which source it came from, so the page can say.
  void setHeading(float deg, bool fromCompass) {
    _heading = deg; _headingFromMag = fromCompass;
  }
  void openCompassCalibration();
  void openFieldEditor();
  void openSettings();
  void openPageSelect();
  // Offer a ride that was interrupted by the power going away.
  void offerResume(const RideCheckpoint& c);
  bool resumeOpen() const { return _resumeOpen; }
  static const char* pageName(Page p);

  static const uint8_t MAX_SLOTS = 8;

  // Returns an action for main.cpp to execute, or None.
  UiAction handleButton(const ButtonEvent& ev);
  void render();                          // call at ~10 Hz; only redraws deltas

  void toast(const char* text, uint16_t ms = 2500);
  // Lap banner in the rider's units, shown for both manual and auto laps.
  void toastLap(const LapRecord& lap);
  // TSS for the ride in progress, and whether it came from power or heart rate.
  // Public because saving a ride banks it into the training history.
  float currentRideTss(bool* fromPower = nullptr) const;
  void notify(const char* title, const char* body);   // phone notification banner
  // Sticky red banner that stays until cleared - used for off-course.
  void alert(const char* title, const char* body);
  void clearAlert();

  void setWifiInfo(bool on, const char* ip) { _wifiOn = on; strncpy(_wifiIp, ip, sizeof(_wifiIp) - 1); }
  // The GPS watchdog's held tier, so the status bar can say what the banner
  // said after the banner has gone. 0 = nothing wrong, and outageS is then the
  // length of the last dropout rather than one in progress.
  void setGpsWarning(uint8_t tier, uint16_t outageS) {
    _gpsTier = tier; _gpsOutS = outageS;
  }

  // File picker, shared between courses and workouts
  void openCoursePicker();
  void openWorkoutPicker();
  const char* chosenCourse() const { return _chosenPath; }
  // >= 0 when the workout picker landed on a built-in rather than a file.
  int8_t chosenWorkoutPreset() const { return _chosenPreset; }
  // Progress screen used while a course is parsed off the SD card.
  void showLoadProgress(uint8_t pct);

  Page page() const { return _page; }
  // Honours the rider's page selection: a page they switched off is not
  // somewhere the firmware gets to drag them.
  void gotoPage(Page p);

 private:
  EdgeDisplay*  _lcd = nullptr;
  LGFX_Sprite   _cell;
  RideComputer* _rc = nullptr;
  Recorder*     _rec = nullptr;
  BleSensors*   _sensors = nullptr;
  Baro*         _baro = nullptr;
  Power*        _power = nullptr;
  Course*       _course = nullptr;
  Weather*      _wx = nullptr;
  Compass*      _compass = nullptr;
  float         _heading = NAN;
  bool          _headingFromMag = false;
  LGFX_Sprite   _rose;                 // compass rose, redrawn rotated each tick
  bool          _calOpen = false;

  Page     _page = Page::Ride1;
  bool     _menuOpen = false;
  uint8_t  _menuIndex = 0;
  bool     _fullRedraw = true;
  uint16_t _cellW = 0, _cellH = 0;
  uint8_t  _cols = 0, _rows = 0;
  uint8_t  _layoutCount = 0;      // field count the current geometry was built for

  // Per-page field sets, editable on the device and kept in NVS. Ride1 is the
  // "at a glance" page, Ride2 the detail one.
  struct PageLayout {
    uint8_t count;
    Field   f[MAX_SLOTS];
  };
  PageLayout _layout[2];

  // field editor
  bool    _editOpen = false, _editChoosing = false;
  uint8_t _editPage = 0, _editRow = 0, _editSlot = 0;
  uint8_t _chooseIdx = 0, _chooseTop = 0;

  // settings page
  bool    _setOpen = false;
  uint8_t _setRow = 0;

  // page selection, one bit per page
  uint32_t _pageMask = 0xFFFFFFFFUL;
  bool     _pagesOpen = false;
  uint8_t  _pagesRow = 0;
  bool     _pagesRefused = false;   // tried to switch the last one off

  // interrupted-ride prompt
  bool     _resumeOpen = false;
  float    _resumeDistance = 0;
  uint32_t _resumeMovingMs = 0, _resumeUnix = 0;
  char     _resumeFile[40] = {0};

  char _lastVal[8][16] = {{0}};
  char _toast[48] = {0};
  uint32_t _toastUntil = 0;
  char _notifTitle[24] = {0}, _notifBody[64] = {0};
  uint32_t _notifUntil = 0;
  bool _alertSticky = false;
  bool _wifiOn = false;
  char _wifiIp[16] = {0};
  uint8_t  _gpsTier = 0;
  uint16_t _gpsOutS = 0;

  // file picker
  enum class PickerMode : uint8_t { Course, Workout };
  bool    _pickerOpen = false;
  PickerMode _pickerMode = PickerMode::Course;
  uint8_t _pickerIndex = 0, _fileCount = 0, _presetCount = 0;
  char    _files[16][40];
  char    _chosenPath[56] = {0};
  int8_t  _chosenPreset = -1;

  void drawStatusBar();
  void drawGrid(const Field* fields, uint8_t n);
  void drawCell(uint8_t idx, uint8_t x, uint8_t y, uint16_t px, uint16_t py, Field f);
  void drawMap();
  void drawMapFitTrack(int top, int h, int w);
  void drawMapNav(int top, int h, int w);
  void drawGuidanceStrip(int top, int w);   // turn arrow + countdown over the map
  void drawNav();
  void drawWorkout();
  void drawLaps();
  // One renderer for all three zone pages - they differ only in which sensor
  // and which zone table they read.
  void drawZonePage(ZoneSource src);
  void drawGear();
  void drawSummary();
  void drawLoad();
  // A zone page with no sensor behind it is dead weight in the rotation.
  bool pageAvailable(Page p) const;
  Page stepPage(Page from, int dir) const;
  void drawCompass();
  void drawCompassCal();
  void drawWx();
  void drawWxHistory();
  void drawProfile();
  void drawStatus();
  void drawMenu();
  void drawPicker();
  void drawEditor();
  void drawFieldChooser();
  void loadLayouts();
  void saveLayouts();
  void resetLayout(uint8_t page);
  UiAction handleEditorButton(const ButtonEvent& ev);
  void drawSettings();
  UiAction handleSettingsButton(const ButtonEvent& ev);
  void drawPageSelect();
  UiAction handlePageSelectButton(const ButtonEvent& ev);
  void drawResumePrompt();
  uint8_t enabledPageCount() const;
  void drawBanner();
  void layoutFor(uint8_t n);

  void formatField(Field f, char* out, size_t n, const char** label, const char** unit);
  UiAction menuSelect();
  void scanFiles(const char* dir, const char* ext);
};

const char* fieldName(Field f);
// "2 o'clock" is a far better instruction on a bike than "63 degrees".
const char* clockDirection(float relDeg);
