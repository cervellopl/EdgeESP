#include "ui/Ui.h"
#include <SD.h>
#include <WiFi.h>
#include <Preferences.h>
#include <time.h>

// --- palette --------------------------------------------------------------
static const uint16_t C_BG      = 0x0000;
static const uint16_t C_FG      = 0xFFFF;
static const uint16_t C_DIM     = 0x8410;
static const uint16_t C_LINE    = 0x2124;
static const uint16_t C_ACCENT  = 0xFD20;   // amber, readable in sunlight
static const uint16_t C_COURSE  = 0x2D7F;   // blue, sits behind the ridden track
static const uint16_t C_GOOD    = 0x07E0;
static const uint16_t C_WARN    = 0xFFE0;
static const uint16_t C_BAD     = 0xF800;
static const uint16_t C_BAR     = 0x18E3;

static const int STATUS_H = 26;

// One battery glyph, drawn wherever a battery needs drawing. The status bar had
// this inline; the resume prompt hides the status bar, and a second copy would
// have been a second set of thresholds to drift out of step.
static uint16_t batteryColour(uint8_t pct) {
  return pct > 40 ? C_GOOD : pct > 15 ? C_WARN : C_BAD;
}

static void drawBattery(lgfx::LGFXBase* g, int x, int y, int w, int h,
                        uint8_t pct, bool charging, uint16_t bg) {
  if (pct > 100) pct = 100;
  uint16_t col = batteryColour(pct);
  g->drawRect(x, y, w, h, C_DIM);
  g->fillRect(x + w, y + h / 4, max(2, h / 4), h / 2, C_DIM);   // the terminal
  int fill = (w - 4) * pct / 100;
  if (fill > 0) g->fillRect(x + 2, y + 2, fill, h - 4, col);
  if (charging) {
    // A bolt would be nicer, but at fourteen pixels tall a plus reads better.
    g->setFont(&fonts::Font2);
    g->setTextDatum(lgfx::textdatum_t::middle_center);
    g->setTextColor(fill > w / 2 ? C_BG : col, fill > w / 2 ? col : bg);
    g->drawString("+", x + w / 2, y + h / 2);
  }
}


// --- turn glyphs ----------------------------------------------------------
// Drawn rather than stored as bitmaps: one routine covers every turn type at
// any size, and it costs no flash for artwork.

static void thickLine(lgfx::LGFXBase* g, float x0, float y0, float x1, float y1,
                      float w, uint16_t col) {
  float dx = x1 - x0, dy = y1 - y0;
  float L = sqrtf(dx * dx + dy * dy);
  if (L < 0.5f) return;
  float nx = -dy / L * (w * 0.5f), ny = dx / L * (w * 0.5f);
  g->fillTriangle(x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, col);
  g->fillTriangle(x0 + nx, y0 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny, col);
}

// Arrow head at (x,y) pointing along `deg` (0 = up, clockwise positive).
static void arrowHead(lgfx::LGFXBase* g, float x, float y, float deg, float s,
                      uint16_t col) {
  float a = radians(deg);
  float ux = sinf(a), uy = -cosf(a);          // forward
  float px = -uy, py = ux;                    // perpendicular
  g->fillTriangle(x + ux * s, y + uy * s,
                  x - ux * s * 0.3f + px * s * 0.75f, y - uy * s * 0.3f + py * s * 0.75f,
                  x - ux * s * 0.3f - px * s * 0.75f, y - uy * s * 0.3f - py * s * 0.75f,
                  col);
}

// A turn glyph: a stem coming up from the rider, bending into the exit.
static void drawTurnGlyph(lgfx::LGFXBase* g, int cx, int cy, int r,
                          TurnType t, uint16_t col) {
  float w = max(3.0f, r * 0.22f);

  if (t == TurnType::UTurn) {
    float dx = r * 0.42f, top = cy - r * 0.45f;
    thickLine(g, cx + dx, cy + r * 0.9f, cx + dx, top, w, col);
    thickLine(g, cx + dx, top, cx - dx, top, w, col);
    thickLine(g, cx - dx, top, cx - dx, cy + r * 0.35f, w, col);
    arrowHead(g, cx - dx, cy + r * 0.55f, 180, r * 0.42f, col);
    return;
  }
  if (t == TurnType::Finish) {
    // Chequered square.
    int s = r / 2, c = s / 2;
    for (int i = 0; i < 4; i++)
      for (int j = 0; j < 4; j++)
        g->fillRect(cx - s + i * c, cy - s + j * c, c, c,
                    ((i + j) & 1) ? col : C_BG);
    g->drawRect(cx - s, cy - s, s * 2, s * 2, col);
    return;
  }
  if (t == TurnType::Generic) {
    g->fillCircle(cx, cy, r * 0.34f, col);
    g->drawCircle(cx, cy, r * 0.62f, col);
    return;
  }

  float exit;
  switch (t) {
    case TurnType::SlightLeft:  exit = -40; break;
    case TurnType::Left:        exit = -90; break;
    case TurnType::SharpLeft:   exit = -138; break;
    case TurnType::SlightRight: exit = 40;  break;
    case TurnType::Right:       exit = 90;  break;
    case TurnType::SharpRight:  exit = 138; break;
    default:                    exit = 0;   break;   // Straight
  }

  if (t == TurnType::Straight) {
    thickLine(g, cx, cy + r * 0.9f, cx, cy - r * 0.45f, w, col);
    arrowHead(g, cx, cy - r * 0.55f, 0, r * 0.42f, col);
    return;
  }

  // Stem up to the corner, then out along the exit bearing.
  float len = r * 0.72f;
  float a = radians(exit);
  float ex = cx + sinf(a) * len, ey = cy - cosf(a) * len;
  thickLine(g, cx, cy + r * 0.9f, cx, cy, w, col);
  thickLine(g, cx, cy, ex, ey, w, col);
  arrowHead(g, ex, ey, exit, r * 0.4f, col);
}

// Compass arrow used for "the route is over there" when off course.
static void drawBearingArrow(lgfx::LGFXBase* g, int cx, int cy, int r,
                             float relDeg, uint16_t col) {
  float a = radians(relDeg);
  float ux = sinf(a), uy = -cosf(a);
  thickLine(g, cx - ux * r * 0.7f, cy - uy * r * 0.7f,
               cx + ux * r * 0.4f, cy + uy * r * 0.4f, max(3.0f, r * 0.22f), col);
  arrowHead(g, cx + ux * r * 0.55f, cy + uy * r * 0.55f, relDeg, r * 0.45f, col);
  g->drawCircle(cx, cy, r, C_LINE);
}

const char* clockDirection(float relDeg) {
  while (relDeg < 0) relDeg += 360.0f;
  int h = (int)((relDeg + 15.0f) / 30.0f) % 12;
  static const char* k[12] = {"12 o'clock", "1 o'clock", "2 o'clock", "3 o'clock",
                              "4 o'clock", "5 o'clock", "6 o'clock", "7 o'clock",
                              "8 o'clock", "9 o'clock", "10 o'clock", "11 o'clock"};
  return k[h];
}

// --- weather glyphs -------------------------------------------------------
static void drawCloudShape(lgfx::LGFXBase* g, int cx, int cy, int r, uint16_t col) {
  g->fillCircle(cx - r * 0.45f, cy + r * 0.10f, r * 0.42f, col);
  g->fillCircle(cx + r * 0.40f, cy + r * 0.14f, r * 0.36f, col);
  g->fillCircle(cx - r * 0.02f, cy - r * 0.22f, r * 0.52f, col);
  g->fillRect(cx - r * 0.46f, cy + r * 0.06f, r * 0.90f, r * 0.44f, col);
}

static void drawWeatherIcon(lgfx::LGFXBase* g, int cx, int cy, int r,
                            WxIcon ic, uint16_t col) {
  switch (ic) {
    case WxIcon::Clear:
      g->fillCircle(cx, cy, r * 0.44f, col);
      for (int i = 0; i < 8; i++) {
        float a = radians(i * 45.0f);
        thickLine(g, cx + sinf(a) * r * 0.62f, cy - cosf(a) * r * 0.62f,
                     cx + sinf(a) * r * 0.95f, cy - cosf(a) * r * 0.95f,
                     max(2.0f, r * 0.10f), col);
      }
      break;
    case WxIcon::PartCloud:
      g->fillCircle(cx + r * 0.34f, cy - r * 0.40f, r * 0.34f, col);
      drawCloudShape(g, cx - r * 0.10f, cy + r * 0.14f, r * 0.92f, col);
      break;
    case WxIcon::Cloud:
      drawCloudShape(g, cx, cy - r * 0.05f, r, col);
      break;
    case WxIcon::Fog:
      drawCloudShape(g, cx, cy - r * 0.30f, r * 0.85f, col);
      for (int i = 0; i < 3; i++)
        g->fillRect(cx - r * 0.7f + (i & 1 ? r * 0.2f : 0), cy + r * 0.42f + i * r * 0.20f,
                    r * 1.3f, max(2, (int)(r * 0.09f)), col);
      break;
    case WxIcon::Drizzle:
    case WxIcon::Rain: {
      drawCloudShape(g, cx, cy - r * 0.28f, r * 0.88f, col);
      int drops = ic == WxIcon::Rain ? 4 : 3;
      for (int i = 0; i < drops; i++) {
        float x = cx - r * 0.52f + i * (r * 1.04f / (drops - 1));
        thickLine(g, x + r * 0.08f, cy + r * 0.40f, x - r * 0.06f, cy + r * 0.82f,
                  max(2.0f, r * 0.09f), col);
      }
      break;
    }
    case WxIcon::Snow:
      drawCloudShape(g, cx, cy - r * 0.28f, r * 0.88f, col);
      for (int i = 0; i < 3; i++) {
        float x = cx - r * 0.45f + i * r * 0.45f, y = cy + r * 0.60f;
        for (int k = 0; k < 3; k++) {
          float a = radians(k * 60.0f);
          thickLine(g, x - sinf(a) * r * 0.15f, y - cosf(a) * r * 0.15f,
                       x + sinf(a) * r * 0.15f, y + cosf(a) * r * 0.15f,
                       max(2.0f, r * 0.07f), col);
        }
      }
      break;
    case WxIcon::Storm:
      drawCloudShape(g, cx, cy - r * 0.30f, r * 0.88f, col);
      g->fillTriangle(cx + r * 0.10f, cy + r * 0.30f, cx - r * 0.28f, cy + r * 0.86f,
                      cx + r * 0.02f, cy + r * 0.50f, col);
      g->fillTriangle(cx + r * 0.26f, cy + r * 0.34f, cx - r * 0.02f, cy + r * 0.90f,
                      cx + r * 0.18f, cy + r * 0.52f, col);
      break;
  }
}

// Pick a round scale-bar length that fits the target width, in whatever units
// the rider uses. 500 m and a quarter mile are both round; 402 m is not.
static float pickScaleBar(float targetM, char* label, size_t ln) {
  const Settings& S = g_settings;
  static const float kM[] = {25, 50, 100, 200, 500, 1000, 2000, 5000, 10000};
  static const float kI[] = {30.48f, 60.96f, 152.4f, 304.8f, 402.336f,
                             804.672f, 1609.344f, 3218.688f, 8046.72f};
  const float* tbl = S.imperial() ? kI : kM;
  float chosen = tbl[0];
  for (int i = 0; i < 9; i++) if (tbl[i] <= targetM) chosen = tbl[i];
  if (chosen < S.shortCutoffM())
    snprintf(label, ln, "%.0f %s", S.distShort(chosen), S.distShortUnit());
  else
    snprintf(label, ln, "%.3g %s", S.distLong(chosen), S.distLongUnit());
  return chosen;
}

// Distance with its unit, switching to the long unit past the cutoff. Every
// distance the rider reads goes through here so metric and imperial cannot
// drift apart.
static void fmtDist(float m, char* out, size_t n) {
  if (isnan(m)) { snprintf(out, n, "--"); return; }
  const Settings& S = g_settings;
  if (m < S.shortCutoffM())
    snprintf(out, n, "%.0f %s", S.distShort(m), S.distShortUnit());
  else
    snprintf(out, n, "%.1f %s", S.distLong(m), S.distLongUnit());
}

static void fmtTime(uint32_t ms, char* out, size_t n) {
  uint32_t s = ms / 1000;
  uint32_t h = s / 3600, m = (s % 3600) / 60;
  s %= 60;
  if (h) snprintf(out, n, "%lu:%02lu:%02lu", (unsigned long)h, (unsigned long)m, (unsigned long)s);
  else   snprintf(out, n, "%lu:%02lu", (unsigned long)m, (unsigned long)s);
}

const char* fieldName(Field f) {
  switch (f) {
    case Field::Speed:       return "SPEED";
    case Field::AvgSpeed:    return "AVG SPEED";
    case Field::MaxSpeed:    return "MAX SPEED";
    case Field::LapSpeed:    return "LAP SPEED";
    case Field::Distance:    return "DISTANCE";
    case Field::LapDistance: return "LAP DIST";
    case Field::Timer:       return "TIMER";
    case Field::Elapsed:     return "ELAPSED";
    case Field::LapTime:     return "LAP TIME";
    case Field::Clock:       return "TIME";
    case Field::Altitude:    return "ELEVATION";
    case Field::Ascent:      return "ASCENT";
    case Field::Descent:     return "DESCENT";
    case Field::Grade:       return "GRADE";
    case Field::HeartRate:   return "HEART RATE";
    case Field::Cadence:     return "CADENCE";
    case Field::Power:       return "POWER";
    case Field::Power3s:     return "POWER 3s";
    case Field::Power30s:    return "POWER 30s";
    case Field::NormPower:   return "NORM POWER";
    case Field::Calories:    return "CALORIES";
    case Field::Temperature: return "TEMP";
    case Field::Battery:     return "BATTERY";
    case Field::LapNumber:   return "LAP";
    case Field::GpsAccuracy: return "GPS ACC";
    case Field::CourseRemaining: return "TO GO";
    case Field::CourseAscent:    return "CLIMB LEFT";
    case Field::CourseEta:       return "ETA";
    case Field::CourseOffset:    return "OFF COURSE";
    case Field::NextTurn:        return "NEXT TURN";
    case Field::Headwind:        return "HEADWIND";
    case Field::WindSpeed:       return "WIND";
    case Field::WindDir:         return "WIND FROM";
    case Field::AirTemp:         return "AIR TEMP";
    case Field::Sunset:          return "SUNSET";
    case Field::Heading:         return "HEADING";
    case Field::ToStart:         return "TO START";
    case Field::PowerZone:       return "POWER ZONE";
    case Field::HrZone:          return "HR ZONE";
    case Field::CadenceZone:     return "CAD ZONE";
    case Field::GearRatio:       return "GEAR";
    case Field::Development:     return "DEVELOPMENT";
    default: return "";
  }
}

void Ui::begin(EdgeDisplay* lcd) {
  _lcd = lcd;
  _lcd->setRotation(1);              // 480x320 landscape
  _lcd->fillScreen(C_BG);
  _cell.setPsram(true);
  _cell.setColorDepth(16);
  loadLayouts();
  layoutFor(_layout[0].count);
}

// --------------------------------------------------------------------------
// Page layouts: defaults, NVS, and the on-device editor.
// --------------------------------------------------------------------------
static const Field kDefault0[] = {Field::Speed, Field::Distance, Field::Timer};
static const Field kDefault1[] = {Field::AvgSpeed, Field::HeartRate, Field::Cadence,
                                  Field::Power, Field::Ascent, Field::Grade};
// Field counts the grid can lay out cleanly: 1-3 stack full width, 4+ go in
// two columns, so an odd count above three would leave a hole.
static const uint8_t kCounts[] = {1, 2, 3, 4, 6, 8};
static Preferences s_pagePrefs;

void Ui::resetLayout(uint8_t page) {
  const Field* src = page == 0 ? kDefault0 : kDefault1;
  uint8_t n = page == 0 ? 3 : 6;
  _layout[page].count = n;
  for (uint8_t i = 0; i < MAX_SLOTS; i++)
    _layout[page].f[i] = i < n ? src[i] : Field::Speed;
}

void Ui::loadLayouts() {
  resetLayout(0);
  resetLayout(1);
  s_pagePrefs.begin("pages", true);
  for (uint8_t p = 0; p < 2; p++) {
    uint8_t buf[1 + MAX_SLOTS] = {0};
    char key[4] = {'p', (char)('0' + p), 0, 0};
    if (s_pagePrefs.getBytes(key, buf, sizeof(buf)) != sizeof(buf)) continue;
    // Anything stored by an older build, or a corrupt read, must not be able to
    // index past the end of the field table.
    if (buf[0] < 1 || buf[0] > MAX_SLOTS) continue;
    bool sane = true;
    for (uint8_t i = 0; i < MAX_SLOTS; i++)
      if (buf[1 + i] >= (uint8_t)Field::COUNT) { sane = false; break; }
    if (!sane) continue;
    _layout[p].count = buf[0];
    for (uint8_t i = 0; i < MAX_SLOTS; i++) _layout[p].f[i] = (Field)buf[1 + i];
  }
  // A mask stored by a build with fewer pages would leave the new ones off,
  // so anything above the old page count is forced on.
  uint32_t all = ((uint32_t)1 << (uint8_t)Page::COUNT) - 1;
  uint32_t stored = s_pagePrefs.getULong("mask", all);
  uint8_t storedCount = s_pagePrefs.getUChar("npages", (uint8_t)Page::COUNT);
  if (storedCount < (uint8_t)Page::COUNT)
    stored |= all & ~(((uint32_t)1 << storedCount) - 1);
  _pageMask = (stored & all) ? (stored & all) : all;
  s_pagePrefs.end();
}

void Ui::saveLayouts() {
  s_pagePrefs.begin("pages", false);
  for (uint8_t p = 0; p < 2; p++) {
    uint8_t buf[1 + MAX_SLOTS];
    buf[0] = _layout[p].count;
    for (uint8_t i = 0; i < MAX_SLOTS; i++) buf[1 + i] = (uint8_t)_layout[p].f[i];
    char key[4] = {'p', (char)('0' + p), 0, 0};
    s_pagePrefs.putBytes(key, buf, sizeof(buf));
  }
  s_pagePrefs.putULong("mask", _pageMask);
  s_pagePrefs.putUChar("npages", (uint8_t)Page::COUNT);
  s_pagePrefs.end();
}

void Ui::openFieldEditor() {
  // Start on whichever data page the rider is looking at, if any.
  _editPage = (_page == Page::Ride2) ? 1 : 0;
  _editRow = 0;
  _editOpen = true;
  _editChoosing = false;
  _menuOpen = false;
  _pickerOpen = false;
  _fullRedraw = true;
}

void Ui::bind(RideComputer* rc, Recorder* rec, BleSensors* s, Baro* b, Power* p,
              Course* c, Weather* w, Compass* cm) {
  _rc = rc; _rec = rec; _sensors = s; _baro = b; _power = p;
  _course = c; _wx = w; _compass = cm;
}

void Ui::layoutFor(uint8_t n) {
  if (n < 1) n = 1;
  if (n > MAX_SLOTS) n = MAX_SLOTS;
  uint16_t h = _lcd->height() - STATUS_H;
  if (n <= 3) { _cols = 1; _rows = n; }
  else        { _cols = 2; _rows = (n + 1) / 2; }
  _cellW = _lcd->width() / _cols;
  _cellH = h / _rows;
  _layoutCount = n;
  _cell.deleteSprite();
  _cell.createSprite(_cellW, _cellH);
}

// --------------------------------------------------------------------------
void Ui::toast(const char* text, uint16_t ms) {
  strncpy(_toast, text, sizeof(_toast) - 1);
  _toastUntil = millis() + ms;
}

void Ui::toastLap(const LapRecord& L) {
  const Settings& S = g_settings;
  char tm[16], b[48];
  fmtTime(L.movingMs, tm, sizeof(tm));
  snprintf(b, sizeof(b), "Lap %u   %.2f %s   %s   %.1f %s", L.index,
           S.distLong(L.distance), S.distLongUnit(), tm,
           S.speed(L.avgSpeed), S.speedUnit());
  toast(b, 4000);
}

void Ui::notify(const char* title, const char* body) {
  strncpy(_notifTitle, title, sizeof(_notifTitle) - 1);
  strncpy(_notifBody,  body,  sizeof(_notifBody) - 1);
  _notifUntil = millis() + 12000;
  _alertSticky = false;
}

void Ui::alert(const char* title, const char* body) {
  strncpy(_notifTitle, title, sizeof(_notifTitle) - 1);
  strncpy(_notifBody,  body,  sizeof(_notifBody) - 1);
  _notifUntil = 0xFFFFFFFF;     // until explicitly cleared
  _alertSticky = true;
}

void Ui::clearAlert() {
  if (!_alertSticky) return;
  _alertSticky = false;
  _notifUntil = 0;
  _fullRedraw = true;
}

// --------------------------------------------------------------------------
static const char* kMenu[] = {
  "Start / Stop", "Lap", "Save ride", "Discard ride",
  "Start workout", "Stop workout",
  "Settings", "Edit data fields", "Pages",
  "Load course", "Navigate back to start", "Clear course",
  "Pair sensors", "Calibrate compass", "Wi-Fi on/off", "Reset totals",
  "Sleep", "Close"};
static const uint8_t kMenuCount = sizeof(kMenu) / sizeof(kMenu[0]);

UiAction Ui::menuSelect() {
  _menuOpen = false;
  _fullRedraw = true;
  switch (_menuIndex) {
    case 0:  return UiAction::StartStop;
    case 1:  return UiAction::Lap;
    case 2:  return UiAction::SaveRide;
    case 3:  return UiAction::DiscardRide;
    case 4:  return UiAction::LoadWorkout;
    case 5:  return UiAction::StopWorkout;
    case 6:  return UiAction::OpenSettings;
    case 7:  return UiAction::EditFields;
    case 8:  return UiAction::SelectPages;
    case 9:  return UiAction::LoadCourse;
    case 10: return UiAction::NavigateBack;
    case 11: return UiAction::ClearCourse;
    case 12: return UiAction::PairSensors;
    case 13: return UiAction::CalibrateCompass;
    case 14: return UiAction::ToggleWifi;
    case 15: return UiAction::ResetRide;
    case 16: return UiAction::Sleep;
    default: return UiAction::None;
  }
}

void Ui::scanFiles(const char* dirPath, const char* ext) {
  _fileCount = 0;
  File dir = SD.open(dirPath);
  if (!dir) return;
  size_t el = strlen(ext);
  for (File f = dir.openNextFile(); f && _fileCount < 16; f = dir.openNextFile()) {
    if (f.isDirectory()) continue;
    const char* n = f.name();
    const char* base = strrchr(n, '/');
    if (base) n = base + 1;
    size_t l = strlen(n);
    if (l <= el) continue;
    if (strcasecmp(n + l - el, ext) != 0) continue;
    strncpy(_files[_fileCount], n, sizeof(_files[0]) - 1);
    _files[_fileCount][sizeof(_files[0]) - 1] = 0;
    _fileCount++;
  }
  dir.close();
}

void Ui::openCoursePicker() {
  _pickerMode = PickerMode::Course;
  _presetCount = 0;
  scanFiles(COURSE_DIR, ".gpx");
  _pickerIndex = 0;
  _pickerOpen = true;
  _menuOpen = false;
  _fullRedraw = true;
}

void Ui::openWorkoutPicker() {
  _pickerMode = PickerMode::Workout;
  // Built-ins first so the feature works with no card and nothing to write.
  _presetCount = Workout::presetCount();
  scanFiles(WORKOUT_DIR, ".wko");
  _pickerIndex = 0;
  _pickerOpen = true;
  _menuOpen = false;
  _fullRedraw = true;
}

// A zone page with no sensor behind it is a screen of nothing to cycle past,
// twice per lap of the rotation. Hide it until the sensor produces something.
bool Ui::pageAvailable(Page p) const {
  if (!(_pageMask & (1UL << (uint8_t)p))) return false;
  const RideState& s = _rc->state();
  if (p == Page::Zones_)  return s.hasPwr || s.pwrSamples > 0;
  if (p == Page::HrZones) return s.hasHr  || s.hrSum > 0;
  if (p == Page::CadZones) return s.hasCad || s.cadSum > 0 || s.coastingMs > 0;
  // The gear is derived from cadence, so without one there is nothing to show.
  if (p == Page::Gear)     return s.hasCad || s.cadSum > 0;
  if (p == Page::WxHist)   return _baro->present();
  return true;
}

Page Ui::stepPage(Page from, int dir) const {
  const uint8_t n = (uint8_t)Page::COUNT;
  uint8_t i = (uint8_t)from;
  // Bounded: if somehow nothing is available we come back to where we started
  // rather than spinning.
  for (uint8_t tries = 0; tries < n; tries++) {
    i = (uint8_t)((i + (dir > 0 ? 1 : n - 1)) % n);
    if (pageAvailable((Page)i)) return (Page)i;
  }
  return from;
}

void Ui::openCompassCalibration() {
  _calOpen = true;
  _menuOpen = false;
  _pickerOpen = false;
  _fullRedraw = true;
}

// Row map for the editor list:
//   0                 page selector
//   1 .. count        the slots themselves
//   count+1           field count
//   count+2           reset this page
//   count+3           done
UiAction Ui::handleEditorButton(const ButtonEvent& ev) {
  PageLayout& L = _layout[_editPage];
  const uint8_t rows = L.count + 4;
  const uint8_t nFields = (uint8_t)Field::COUNT;

  if (_editChoosing) {
    switch (ev.btn) {
      case BTN_UP:    _chooseIdx = (_chooseIdx + nFields - 1) % nFields; break;
      case BTN_DOWN:  _chooseIdx = (_chooseIdx + 1) % nFields; break;
      case BTN_ENTER:
        L.f[_editSlot] = (Field)_chooseIdx;
        _editChoosing = false;
        break;
      case BTN_BACK:  _editChoosing = false; break;
      default: break;
    }
    _fullRedraw = true;
    return UiAction::None;
  }

  switch (ev.btn) {
    case BTN_UP:   _editRow = (_editRow + rows - 1) % rows; break;
    case BTN_DOWN: _editRow = (_editRow + 1) % rows; break;

    case BTN_ENTER:
      if (_editRow == 0) {
        _editPage ^= 1;
        _editRow = 0;                       // the new page may be shorter
      } else if (_editRow <= L.count) {
        _editSlot = _editRow - 1;
        _chooseIdx = (uint8_t)L.f[_editSlot];
        _chooseTop = 0;
        _editChoosing = true;
      } else if (_editRow == L.count + 1) {
        uint8_t i = 0;
        while (i < sizeof(kCounts) - 1 && kCounts[i] != L.count) i++;
        L.count = kCounts[(i + 1) % sizeof(kCounts)];
        // The list just changed length under the cursor. Keep it on the row the
        // rider is actually operating rather than letting it slide onto "Done".
        _editRow = L.count + 1;
      } else if (_editRow == L.count + 2) {
        // No toast here: the editor owns the screen, so a banner would only
        // surface after closing. The list visibly changing is the feedback.
        resetLayout(_editPage);
        _editRow = 0;
      } else {
        saveLayouts();
        _editOpen = false;
        gotoPage(_editPage == 0 ? Page::Ride1 : Page::Ride2);
        toast("Fields saved");
      }
      break;

    case BTN_BACK:
      // Every change is already applied to the live layout, so leaving keeps
      // it. "Reset this page" is the way back out.
      saveLayouts();
      _editOpen = false;
      gotoPage(_editPage == 0 ? Page::Ride1 : Page::Ride2);
      break;

    default: break;
  }
  _fullRedraw = true;
  return UiAction::None;
}

// --------------------------------------------------------------------------
// The interrupted-ride prompt. Both answers keep the data: one carries on
// writing to the file, the other closes it off so it can be uploaded. There is
// deliberately no option that throws the ride away.
void Ui::offerResume(const RideCheckpoint& c) {
  _resumeOpen = true;
  _resumeDistance = (float)c.state.distance;
  _resumeMovingMs = c.state.movingMs;
  _resumeUnix = c.state.startUnix;
  const char* base = strrchr(c.fitPath, '/');
  strncpy(_resumeFile, base ? base + 1 : c.fitPath, sizeof(_resumeFile) - 1);
  _fullRedraw = true;
}

void Ui::drawResumePrompt() {
  const Settings& S = g_settings;
  int w = _lcd->width(), h = _lcd->height();
  _lcd->fillScreen(C_BG);
  char b[64];

  _lcd->setFont(&fonts::Font4);
  _lcd->setTextDatum(lgfx::textdatum_t::top_center);
  _lcd->setTextColor(C_ACCENT, C_BG);
  _lcd->drawString("UNFINISHED RIDE", w / 2, 14);

  _lcd->setFont(&fonts::Font2);
  _lcd->setTextColor(C_DIM, C_BG);
  _lcd->drawString("the last ride ended without being saved", w / 2, 44);

  // The battery matters more here than anywhere else: a flat one is the
  // most likely reason the ride stopped, and it decides whether carrying on
  // is even worth starting.
  uint8_t pct = _power->percent();
  bool charging = _power->charging();
  drawBattery(_lcd, w - 58, 12, 34, 15, pct, charging, C_BG);
  _lcd->setFont(&fonts::Font2);
  _lcd->setTextDatum(lgfx::textdatum_t::middle_right);
  _lcd->setTextColor(batteryColour(pct), C_BG);
  snprintf(b, sizeof(b), "%u%%", pct);
  _lcd->drawString(b, w - 64, 19);

  // The two figures that tell the rider whether this is their ride.
  int by = 76, bh = 84;
  _lcd->fillRect(20, by, w - 40, bh, C_BAR);
  snprintf(b, sizeof(b), "%.2f", S.distLong(_resumeDistance));
  _lcd->setFont(&fonts::Font7);
  _lcd->setTextDatum(lgfx::textdatum_t::middle_left);
  _lcd->setTextColor(C_FG, C_BAR);
  _lcd->setTextSize(1.1f);
  _lcd->drawString(b, 36, by + bh / 2);
  int dw = (int)(_lcd->textWidth(b, &fonts::Font7) * 1.1f);
  _lcd->setTextSize(1.0f);
  _lcd->setFont(&fonts::Font2);
  _lcd->setTextDatum(lgfx::textdatum_t::bottom_left);
  _lcd->setTextColor(C_DIM, C_BAR);
  _lcd->drawString(S.distLongUnit(), 42 + dw, by + bh - 14);

  fmtTime(_resumeMovingMs, b, sizeof(b));
  _lcd->setFont(&fonts::Font7);
  _lcd->setTextDatum(lgfx::textdatum_t::middle_right);
  _lcd->setTextColor(C_FG, C_BAR);
  _lcd->drawString(b, w - 36, by + bh / 2);

  _lcd->setFont(&fonts::Font2);
  _lcd->setTextDatum(lgfx::textdatum_t::top_center);
  _lcd->setTextColor(C_DIM, C_BG);
  if (_resumeUnix > 1600000000UL) {
    time_t t = (time_t)_resumeUnix;
    struct tm tmv;
    localtime_r(&t, &tmv);
    snprintf(b, sizeof(b), "started %04d-%02d-%02d %02d:%02d   %s",
             tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
             tmv.tm_hour, tmv.tm_min, _resumeFile);
  } else {
    snprintf(b, sizeof(b), "%s", _resumeFile);
  }
  _lcd->drawString(b, w / 2, by + bh + 8);

  int oy = by + bh + 34;
  _lcd->fillRoundRect(24, oy, w / 2 - 40, 46, 6, C_GOOD);
  _lcd->setFont(&fonts::Font4);
  _lcd->setTextDatum(lgfx::textdatum_t::middle_center);
  _lcd->setTextColor(C_BG, C_GOOD);
  _lcd->drawString("ENTER", 24 + (w / 2 - 40) / 2, oy + 16);
  _lcd->setFont(&fonts::Font2);
  _lcd->drawString("carry on riding", 24 + (w / 2 - 40) / 2, oy + 36);

  _lcd->fillRoundRect(w / 2 + 16, oy, w / 2 - 40, 46, 6, C_BAR);
  _lcd->setFont(&fonts::Font4);
  _lcd->setTextColor(C_ACCENT, C_BAR);
  _lcd->drawString("BACK", w / 2 + 16 + (w / 2 - 40) / 2, oy + 16);
  _lcd->setFont(&fonts::Font2);
  _lcd->setTextColor(C_DIM, C_BAR);
  _lcd->drawString("save and finish it", w / 2 + 16 + (w / 2 - 40) / 2, oy + 36);

  // Say what the battery means for the choice, rather than leaving the
  // rider to work it out from a percentage.
  _lcd->setFont(&fonts::Font2);
  _lcd->setTextDatum(lgfx::textdatum_t::top_center);
  int wy = oy + 58;
  if (charging) {
    _lcd->setTextColor(C_GOOD, C_BG);
    snprintf(b, sizeof(b), "on charge at %u%% - safe to carry on", pct);
    _lcd->drawString(b, w / 2, wy);
  } else if (pct <= 20) {
    _lcd->setTextColor(C_BAD, C_BG);
    snprintf(b, sizeof(b), "battery %u%% - finishing it now is the safer answer", pct);
    _lcd->drawString(b, w / 2, wy);
  } else if (pct <= 40) {
    _lcd->setTextColor(C_WARN, C_BG);
    snprintf(b, sizeof(b), "battery %u%% - enough for a while yet", pct);
    _lcd->drawString(b, w / 2, wy);
  }

  _lcd->setTextDatum(lgfx::textdatum_t::bottom_center);
  _lcd->setTextColor(C_DIM, C_BG);
  _lcd->drawString("either way the ride is kept", w / 2, h - 4);
}

// --------------------------------------------------------------------------
// Page selection. Seventeen pages is a lot to cycle past when half of them do
// not apply to how you ride.
const char* Ui::pageName(Page p) {
  switch (p) {
    case Page::Ride1:    return "Ride 1";
    case Page::Ride2:    return "Ride 2";
    case Page::Summary:  return "Summary";
    case Page::Workout:  return "Workout";
    case Page::Zones_:   return "Power zones";
    case Page::HrZones:  return "HR zones";
    case Page::CadZones: return "Cadence zones";
    case Page::Gear:     return "Gear";
    case Page::Laps:     return "Laps";
    case Page::Load:     return "Training load";
    case Page::Map:      return "Map";
    case Page::Nav:      return "Navigation";
    case Page::Compass:  return "Compass";
    case Page::Wx:       return "Weather";
    case Page::WxHist:   return "Weather history";
    case Page::Profile:  return "Elevation profile";
    case Page::Status:   return "Status";
    default:             return "";
  }
}

uint8_t Ui::enabledPageCount() const {
  uint8_t n = 0;
  for (uint8_t i = 0; i < (uint8_t)Page::COUNT; i++)
    if (_pageMask & (1UL << i)) n++;
  return n;
}

void Ui::gotoPage(Page p) {
  // A page the rider switched off is not somewhere to be dragged, however
  // urgent the firmware thinks the news is. The banner still shows.
  if (!pageAvailable(p)) return;
  _page = p;
  _fullRedraw = true;
}

void Ui::openPageSelect() {
  _pagesOpen = true;
  _pagesRow = 0;
  _pagesRefused = false;
  _menuOpen = false;
  _fullRedraw = true;
}

UiAction Ui::handlePageSelectButton(const ButtonEvent& ev) {
  const uint8_t nPages = (uint8_t)Page::COUNT;
  const uint8_t rows = nPages + 1;          // the pages, then Done

  auto close = [&]() {
    saveLayouts();
    _pagesOpen = false;
    _fullRedraw = true;
    // The rider may have just switched off the page they were standing on.
    if (!pageAvailable(_page)) _page = stepPage(_page, +1);
  };

  switch (ev.btn) {
    case BTN_UP:   _pagesRow = (_pagesRow + rows - 1) % rows; _pagesRefused = false; break;
    case BTN_DOWN: _pagesRow = (_pagesRow + 1) % rows; _pagesRefused = false; break;
    case BTN_ENTER:
      if (_pagesRow >= nPages) { close(); return UiAction::None; }
      else {
        uint32_t bit = 1UL << _pagesRow;
        if (_pageMask & bit) {
          // Refuse to switch off the last one: an empty rotation is a device
          // with nothing on the screen and no way back.
          if (enabledPageCount() <= 1) { _pagesRefused = true; break; }
          _pageMask &= ~bit;
        } else {
          _pageMask |= bit;
        }
        _pagesRefused = false;
      }
      break;
    case BTN_BACK: close(); return UiAction::None;
    default: break;
  }
  _fullRedraw = true;
  return UiAction::None;
}

void Ui::drawPageSelect() {
  const uint8_t nPages = (uint8_t)Page::COUNT;
  int w = _lcd->width(), h = _lcd->height();
  _lcd->fillScreen(C_BG);

  _lcd->setFont(&fonts::Font4);
  _lcd->setTextDatum(lgfx::textdatum_t::top_left);
  _lcd->setTextColor(C_ACCENT, C_BG);
  _lcd->drawString("PAGES", 12, 4);
  _lcd->setFont(&fonts::Font2);
  _lcd->setTextDatum(lgfx::textdatum_t::top_right);
  _lcd->setTextColor(C_DIM, C_BG);
  _lcd->drawString("UP/DOWN move   ENTER toggle   BACK done", w - 12, 10);

  // Two columns, filled down then across, so UP and DOWN still walk the list
  // in one order with only two buttons to do it.
  const uint8_t rowsPerCol = 9;
  int top = 32, rowH = 30, colW = w / 2;

  for (uint8_t i = 0; i <= nPages; i++) {
    uint8_t col = i / rowsPerCol, row = i % rowsPerCol;
    int x = col * colW, y = top + row * rowH;
    bool sel = (i == _pagesRow);
    bool done = (i == nPages);
    bool on = !done && (_pageMask & (1UL << i));

    _lcd->fillRect(x + 6, y, colW - 12, rowH - 3, sel ? C_ACCENT : C_BG);
    _lcd->setFont(&fonts::Font2);
    _lcd->setTextDatum(lgfx::textdatum_t::middle_left);
    _lcd->setTextColor(sel ? C_BG : (done ? C_ACCENT : (on ? C_FG : C_DIM)),
                       sel ? C_ACCENT : C_BG);
    _lcd->drawString(done ? "Done" : pageName((Page)i), x + 18, y + rowH / 2);

    if (!done) {
      _lcd->setTextDatum(lgfx::textdatum_t::middle_right);
      _lcd->setTextColor(sel ? C_BG : (on ? C_GOOD : C_LINE), sel ? C_ACCENT : C_BG);
      _lcd->drawString(on ? "on" : "off", x + colW - 18, y + rowH / 2);
    }
  }

  _lcd->setFont(&fonts::Font2);
  _lcd->setTextDatum(lgfx::textdatum_t::bottom_left);
  char b[72];
  if (_pagesRefused) {
    _lcd->setTextColor(C_WARN, C_BG);
    _lcd->drawString("at least one page has to stay on", 12, h - 3);
  } else {
    _lcd->setTextColor(C_DIM, C_BG);
    snprintf(b, sizeof(b),
             "%u of %u pages in the rotation   -   sensor pages hide themselves too",
             enabledPageCount(), nPages);
    _lcd->drawString(b, 12, h - 3);
  }
}

// --------------------------------------------------------------------------
// Settings. Reached from the menu rather than the page rotation: nobody wants
// to land on a settings screen while cycling pages down a descent.
enum SetRow : uint8_t {
  SET_UNITS = 0, SET_RIDER, SET_BIKE, SET_FTP, SET_LTHR, SET_WHEEL, SET_DRIVE,
  SET_MAPZOOM, SET_BACKLIGHT,
  SET_AUTOPAUSE, SET_AUTOLAP, SET_RESET, SET_DONE, SET_COUNT
};

void Ui::openSettings() {
  _setOpen = true;
  _setRow = 0;
  _menuOpen = false;
  _fullRedraw = true;
}

UiAction Ui::handleSettingsButton(const ButtonEvent& ev) {
  Settings& S = g_settings;
  int dir = 0;

  switch (ev.btn) {
    case BTN_UP:    _setRow = (_setRow + SET_COUNT - 1) % SET_COUNT; break;
    case BTN_DOWN:  _setRow = (_setRow + 1) % SET_COUNT; break;
    case BTN_ENTER: dir = +1; break;
    case BTN_LAP:   dir = -1; break;   // the only spare button, so it decrements
    case BTN_BACK:
      S.save();
      _setOpen = false;
      _fullRedraw = true;
      toast("Settings saved");
      return UiAction::None;
    default: break;
  }

  if (dir) {
    switch (_setRow) {
      case SET_UNITS:     S.stepUnits(dir); break;
      case SET_RIDER:     S.stepRider(dir); break;
      case SET_BIKE:      S.stepBike(dir); break;
      case SET_FTP:       S.stepFtp(dir); break;
      case SET_LTHR:      S.stepLthr(dir); break;
      case SET_WHEEL:     S.stepWheel(dir); break;
      case SET_DRIVE:     S.stepDrivetrain(dir); g_drive.setPreset(S.drivetrain); break;
      case SET_MAPZOOM:   S.stepMapZoom(dir); break;
      case SET_BACKLIGHT:
        S.stepBacklight(dir);
        // Apply immediately: choosing a brightness you cannot see is absurd.
        _power->setBacklight(S.backlight);
        break;
      case SET_AUTOPAUSE: S.stepAutoPause(dir); break;
      case SET_AUTOLAP:   S.stepAutoLap(dir); break;
      case SET_RESET:
        if (dir > 0) {
          S.resetToDefaults();
          _power->setBacklight(S.backlight);
        }
        break;
      case SET_DONE:
        if (dir > 0) {
          S.save();
          _setOpen = false;
          toast("Settings saved");
        }
        break;
      default: break;
    }
  }
  _fullRedraw = true;
  return UiAction::None;
}

void Ui::drawSettings() {
  const Settings& S = g_settings;
  int w = _lcd->width(), h = _lcd->height();
  _lcd->fillScreen(C_BG);

  _lcd->setFont(&fonts::Font4);
  _lcd->setTextDatum(lgfx::textdatum_t::top_left);
  _lcd->setTextColor(C_ACCENT, C_BG);
  _lcd->drawString("SETTINGS", 12, 6);
  _lcd->setFont(&fonts::Font2);
  _lcd->setTextDatum(lgfx::textdatum_t::top_right);
  _lcd->setTextColor(C_DIM, C_BG);
  _lcd->drawString("UP/DOWN move   ENTER +   LAP -   BACK save", w - 12, 12);

  const char* names[SET_COUNT] = {
    "Units", "Rider weight", "Bike weight", "FTP", "Threshold HR", "Wheel circumference",
    "Gearing", "Map zoom",
    "Backlight", "Auto pause", "Auto lap", "Reset all to defaults", "Done"};

  char val[32], note[52];
  int top = 34, rh = 24;

  for (uint8_t r = 0; r < SET_COUNT; r++) {
    int y = top + r * rh;
    bool sel = (r == _setRow);
    _lcd->fillRect(8, y, w - 16, rh - 2, sel ? C_ACCENT : C_BG);
    uint16_t fg = sel ? C_BG : C_FG;
    uint16_t acc = sel ? C_BG : C_ACCENT;
    uint16_t bgc = sel ? C_ACCENT : C_BG;

    note[0] = 0;
    switch (r) {
      case SET_UNITS:
        snprintf(val, sizeof(val), "%s", S.imperial() ? "Imperial" : "Metric");
        snprintf(note, sizeof(note), S.imperial() ? "mph, mi, ft, F" : "km/h, km, m, C");
        break;
      case SET_RIDER:
        snprintf(val, sizeof(val), "%.1f %s", S.mass(S.riderKg), S.massUnit());
        snprintf(note, sizeof(note), "used for the climbing and calorie model");
        break;
      case SET_BIKE:
        snprintf(val, sizeof(val), "%.1f %s", S.mass(S.bikeKg), S.massUnit());
        snprintf(note, sizeof(note), "total %.1f %s with rider",
                 S.mass(S.totalMassKg()), S.massUnit());
        break;
      case SET_FTP:
        snprintf(val, sizeof(val), "%u W", S.ftpWatts);
        snprintf(note, sizeof(note), "built-in workouts are written as %% of this");
        break;
      case SET_LTHR:
        snprintf(val, sizeof(val), "%u bpm", S.lthrBpm);
        snprintf(note, sizeof(note), "used for TSS when there is no power meter");
        break;
      case SET_WHEEL:
        snprintf(val, sizeof(val), "%u mm", S.wheelMm);
        snprintf(note, sizeof(note), "for a BLE speed sensor; 700x25c is 2105");
        break;
      case SET_DRIVE:
        snprintf(val, sizeof(val), "%s", Drivetrain::preset(S.drivetrain).name);
        snprintf(note, sizeof(note), "chainrings and cassette, for the gear page");
        break;
      case SET_MAPZOOM:
        if (S.mapZoomAuto()) {
          snprintf(val, sizeof(val), "Auto");
          snprintf(note, sizeof(note), "sizes itself to the next two minutes of riding");
        } else {
          char z[20];
          fmtDist(S.mapZoomSpanM(), z, sizeof(z));
          snprintf(val, sizeof(val), "%s", z);
          snprintf(note, sizeof(note), "fixed width across the map page");
        }
        break;
      case SET_BACKLIGHT:
        snprintf(val, sizeof(val), "%u%%", (unsigned)(S.backlight * 100 / 255));
        snprintf(note, sizeof(note), "the biggest single draw on the battery");
        break;
      case SET_AUTOPAUSE:
        snprintf(val, sizeof(val), "%s", S.autoPause ? "On" : "Off");
        snprintf(note, sizeof(note), "stop the timer when you stop rolling");
        break;
      case SET_AUTOLAP:
        if (!S.autoLapM) snprintf(val, sizeof(val), "Off");
        else snprintf(val, sizeof(val), "%.3g %s", S.distLong(S.autoLapM), S.distLongUnit());
        break;
      default:
        val[0] = 0;
        break;
    }

    _lcd->setFont(&fonts::Font2);
    _lcd->setTextDatum(lgfx::textdatum_t::middle_left);
    _lcd->setTextColor(fg, bgc);
    _lcd->drawString(names[r], 20, y + rh / 2);
    if (note[0]) {
      _lcd->setTextColor(sel ? C_BG : C_DIM, bgc);
      _lcd->drawString(note, 196, y + rh / 2);
    }
    if (val[0]) {
      _lcd->setTextDatum(lgfx::textdatum_t::middle_right);
      _lcd->setTextColor(acc, bgc);
      _lcd->drawString(val, w - 22, y + rh / 2);
    }
  }

  _lcd->setFont(&fonts::Font2);
  _lcd->setTextDatum(lgfx::textdatum_t::bottom_left);
  _lcd->setTextColor(C_DIM, C_BG);
  _lcd->drawString("Units convert what is displayed; recorded .fit files are "
                   "always SI", 12, h - 4);
}

// --------------------------------------------------------------------------
UiAction Ui::handleButton(const ButtonEvent& ev) {
  if (ev.btn == BTN_NONE) return UiAction::None;

  // Any button dismisses a sticky off-course banner.
  if (_alertSticky) clearAlert();

  // The recovery prompt owns the device until it is answered: there is an
  // open file on the card and no sensible way to carry on around it.
  if (_resumeOpen) {
    if (ev.btn == BTN_ENTER) { _resumeOpen = false; _fullRedraw = true;
                               return UiAction::ResumeRide; }
    if (ev.btn == BTN_BACK)  { _resumeOpen = false; _fullRedraw = true;
                               return UiAction::FinishAbandonedRide; }
    return UiAction::None;
  }
  if (_pagesOpen) return handlePageSelectButton(ev);
  if (_setOpen)  return handleSettingsButton(ev);
  if (_editOpen) return handleEditorButton(ev);

  if (_calOpen) {
    if (ev.btn == BTN_ENTER) {
      bool ok = _compass->saveCalibration();
      toast(ok ? "Compass calibrated" : "Not enough coverage - try again", 3500);
      _calOpen = false;
      _fullRedraw = true;
      if (ok) gotoPage(Page::Compass);
    } else if (ev.btn == BTN_BACK) {
      _compass->cancelCalibration();
      _calOpen = false;
      _fullRedraw = true;
      toast("Calibration cancelled");
    }
    return UiAction::None;
  }

  if (_pickerOpen) {
    uint8_t total = _presetCount + _fileCount;
    switch (ev.btn) {
      case BTN_UP:
        if (total) _pickerIndex = (_pickerIndex + total - 1) % total;
        break;
      case BTN_DOWN:
        if (total) _pickerIndex = (_pickerIndex + 1) % total;
        break;
      case BTN_ENTER: {
        if (!total) { _pickerOpen = false; _fullRedraw = true; break; }
        bool isPreset = _pickerIndex < _presetCount;
        _chosenPreset = isPreset ? (int8_t)_pickerIndex : -1;
        if (!isPreset) {
          snprintf(_chosenPath, sizeof(_chosenPath), "%s/%s",
                   _pickerMode == PickerMode::Course ? COURSE_DIR : WORKOUT_DIR,
                   _files[_pickerIndex - _presetCount]);
        }
        _pickerOpen = false;
        _fullRedraw = true;
        return _pickerMode == PickerMode::Course ? UiAction::CourseChosen
                                                 : UiAction::WorkoutChosen;
      }
      case BTN_BACK:
        _pickerOpen = false; _fullRedraw = true;
        break;
      default: break;
    }
    return UiAction::None;
  }

  if (_menuOpen) {
    switch (ev.btn) {
      case BTN_UP:    _menuIndex = (_menuIndex + kMenuCount - 1) % kMenuCount; break;
      case BTN_DOWN:  _menuIndex = (_menuIndex + 1) % kMenuCount; break;
      case BTN_ENTER: return menuSelect();
      case BTN_BACK:  _menuOpen = false; _fullRedraw = true; break;
      default: break;
    }
    return UiAction::None;
  }

  switch (ev.btn) {
    case BTN_UP:
      _page = stepPage(_page, -1);
      _fullRedraw = true;
      break;
    case BTN_DOWN:
      _page = stepPage(_page, +1);
      _fullRedraw = true;
      break;
    case BTN_ENTER:
      // Short press is the timer key, long press opens the menu.
      if (ev.longPress) { _menuOpen = true; _menuIndex = 0; _fullRedraw = true; }
      else return UiAction::StartStop;
      break;
    case BTN_LAP:
      return ev.longPress ? UiAction::SaveRide : UiAction::Lap;
    case BTN_BACK:
      if (ev.longPress) return UiAction::Sleep;
      _menuOpen = true; _menuIndex = 0; _fullRedraw = true;
      break;
    default: break;
  }
  return UiAction::None;
}

// --------------------------------------------------------------------------
void Ui::formatField(Field f, char* out, size_t n, const char** label, const char** unit) {
  const RideState& s = _rc->state();
  const Settings& S = g_settings;
  *label = fieldName(f);
  *unit = "";
  switch (f) {
    case Field::Speed:       snprintf(out, n, "%.1f", S.speed(s.speed));       *unit = S.speedUnit(); break;
    case Field::AvgSpeed:    snprintf(out, n, "%.1f", S.speed(s.avgSpeed));    *unit = S.speedUnit(); break;
    case Field::MaxSpeed:    snprintf(out, n, "%.1f", S.speed(s.maxSpeed));    *unit = S.speedUnit(); break;
    case Field::LapSpeed:    snprintf(out, n, "%.1f", S.speed(s.lapAvgSpeed)); *unit = S.speedUnit(); break;
    case Field::Distance:    snprintf(out, n, "%.2f", S.distLong(s.distance));    *unit = S.distLongUnit(); break;
    case Field::LapDistance: snprintf(out, n, "%.2f", S.distLong(s.lapDistance)); *unit = S.distLongUnit(); break;
    case Field::Timer:       fmtTime(s.movingMs, out, n); break;
    case Field::Elapsed:     fmtTime(s.elapsedMs, out, n); break;
    case Field::LapTime:     fmtTime(s.lapMovingMs, out, n); break;
    case Field::Clock: {
      if (s.fix.timeValid) {
        time_t t = (time_t)s.fix.unixTime;
        struct tm tmv; localtime_r(&t, &tmv);
        snprintf(out, n, "%02d:%02d", tmv.tm_hour, tmv.tm_min);
      } else snprintf(out, n, "--:--");
      break;
    }
    case Field::Altitude:    snprintf(out, n, "%.0f", S.elev(isnan(s.altitude) ? 0 : s.altitude)); *unit = S.elevUnit(); break;
    case Field::Ascent:      snprintf(out, n, "%.0f", S.elev(s.ascent));  *unit = S.elevUnit(); break;
    case Field::Descent:     snprintf(out, n, "%.0f", S.elev(s.descent)); *unit = S.elevUnit(); break;
    case Field::Grade:       snprintf(out, n, "%.1f", s.grade);   *unit = "%";  break;
    case Field::HeartRate:   s.hasHr  ? snprintf(out, n, "%u", s.hr)      : snprintf(out, n, "--"); *unit = "bpm"; break;
    case Field::Cadence:     s.hasCad ? snprintf(out, n, "%u", s.cadence) : snprintf(out, n, "--"); *unit = "rpm"; break;
    case Field::Power:       s.hasPwr ? snprintf(out, n, "%u", s.power)   : snprintf(out, n, "--"); *unit = "W"; break;
    case Field::Power3s:     s.hasPwr ? snprintf(out, n, "%u", s.power3s) : snprintf(out, n, "--"); *unit = "W"; break;
    case Field::Power30s:    s.hasPwr ? snprintf(out, n, "%u", s.power30s): snprintf(out, n, "--"); *unit = "W"; break;
    case Field::NormPower:   s.hasPwr ? snprintf(out, n, "%u", s.normalizedPower) : snprintf(out, n, "--"); *unit = "W"; break;
    case Field::Calories:    snprintf(out, n, "%.0f", s.calories); *unit = "kcal"; break;
    case Field::Temperature: isnan(s.temperature) ? snprintf(out, n, "--")
                                                  : snprintf(out, n, "%.0f", S.temp(s.temperature));
                             *unit = S.tempUnit(); break;
    case Field::Battery:     snprintf(out, n, "%u", s.batteryPct); *unit = "%"; break;
    case Field::LapNumber:   snprintf(out, n, "%u", s.lapCount); break;
    case Field::GpsAccuracy: s.fix.valid ? snprintf(out, n, "%.0f", S.distShort(s.fix.hAcc))
                                         : snprintf(out, n, "--");
                             *unit = S.distShortUnit(); break;

    case Field::CourseRemaining:
      _course->loaded() ? snprintf(out, n, "%.1f", S.distLong(_course->distanceRemaining()))
                        : snprintf(out, n, "--");
      *unit = S.distLongUnit(); break;
    case Field::CourseAscent:
      _course->loaded() ? snprintf(out, n, "%.0f", S.elev(_course->ascentRemaining()))
                        : snprintf(out, n, "--");
      *unit = S.elevUnit(); break;
    case Field::CourseEta: {
      // Time to the end of the course at the ride's average speed so far.
      float v = s.avgSpeed > 1.0f ? s.avgSpeed : s.speed;
      if (_course->loaded() && v > 1.0f)
        fmtTime((uint32_t)(_course->distanceRemaining() / v * 1000.0f), out, n);
      else snprintf(out, n, "--:--");
      break;
    }
    case Field::CourseOffset:
      _course->loaded() ? snprintf(out, n, "%.0f", S.distShort(_course->crossTrack()))
                        : snprintf(out, n, "--");
      *unit = S.distShortUnit(); break;
    case Field::NextTurn: {
      float d = _course->loaded() ? _course->distanceToNextTurn() : NAN;
      if (isnan(d)) snprintf(out, n, "--");
      else if (d < S.shortCutoffM()) { snprintf(out, n, "%.0f", S.distShort(d)); *unit = S.distShortUnit(); }
      else { snprintf(out, n, "%.1f", S.distLong(d)); *unit = S.distLongUnit(); }
      break;
    }

    case Field::Headwind: {
      // Signed: positive is a headwind, negative a tailwind. One glance tells
      // you whether the wind is the reason today feels hard.
      float hw = _wx->headwind(_heading);
      isnan(hw) ? snprintf(out, n, "--") : snprintf(out, n, "%+.0f", S.speed(hw));
      *unit = S.speedUnit(); break;
    }
    case Field::WindSpeed: {
      WeatherNow w = _wx->now();
      isnan(w.windMps) ? snprintf(out, n, "--") : snprintf(out, n, "%.0f", S.speed(w.windMps));
      *unit = S.speedUnit(); break;
    }
    case Field::WindDir: {
      WeatherNow w = _wx->now();
      w.windFromDeg < 0 ? snprintf(out, n, "--") : snprintf(out, n, "%d", w.windFromDeg);
      *unit = "deg"; break;
    }
    case Field::AirTemp: {
      WeatherNow w = _wx->now();
      isnan(w.tempC) ? snprintf(out, n, "--") : snprintf(out, n, "%.0f", S.temp(w.tempC));
      *unit = S.tempUnit(); break;
    }
    // Each field names one sensor. A single "ZONE" that switched sources would
    // change meaning without changing its label.
    case Field::PowerZone:
      s.hasPwr ? snprintf(out, n, "%u", Zones::powerZoneFor(s.power, S.ftpWatts) + 1)
               : snprintf(out, n, "--");
      break;
    case Field::HrZone:
      s.hasHr ? snprintf(out, n, "%u", Zones::hrZoneFor(s.hr, S.lthrBpm) + 1)
              : snprintf(out, n, "--");
      break;
    case Field::CadenceZone:
      // Coasting shows as 0 rather than being folded into the bottom zone.
      if (!s.hasCad)       snprintf(out, n, "--");
      else if (!s.cadence) snprintf(out, n, "0");
      else                 snprintf(out, n, "%u", Zones::cadenceZoneFor(s.cadence) + 1);
      break;
    case Field::GearRatio:
      g_drive.valid() ? snprintf(out, n, "%ux%u", g_drive.ringTeeth(), g_drive.sprocketTeeth())
                      : snprintf(out, n, "--");
      break;
    case Field::Development:
      g_drive.valid() ? snprintf(out, n, "%.2f", g_drive.development())
                      : snprintf(out, n, "--");
      *unit = "m"; break;
    case Field::Heading:
      isnan(_heading) ? snprintf(out, n, "--")
                      : snprintf(out, n, "%03d", (int)lroundf(_heading) % 360);
      *unit = "deg"; break;
    case Field::ToStart: {
      const RideState& st = _rc->state();
      if (_rc->trackCount() && st.fix.valid) {
        const TrackPoint* t0 = &_rc->track()[0];
        double d = haversine(st.fix.latLon(), t0->latLon());
        if (d < S.shortCutoffM()) {
          snprintf(out, n, "%.0f", S.distShort(d)); *unit = S.distShortUnit();
        } else {
          snprintf(out, n, "%.2f", S.distLong(d));  *unit = S.distLongUnit();
        }
      } else { snprintf(out, n, "--"); *unit = S.distLongUnit(); }
      break;
    }
    case Field::Sunset: {
      WeatherNow w = _wx->now();
      if (w.sunsetUnix) {
        time_t t = (time_t)w.sunsetUnix;
        struct tm tmv; localtime_r(&t, &tmv);
        snprintf(out, n, "%02d:%02d", tmv.tm_hour, tmv.tm_min);
      } else snprintf(out, n, "--:--");
      break;
    }
    default: snprintf(out, n, "--"); break;
  }
}

void Ui::drawCell(uint8_t idx, uint8_t cx, uint8_t cy, uint16_t px, uint16_t py, Field f) {
  char val[16]; const char *label, *unit;
  formatField(f, val, sizeof(val), &label, &unit);

  if (!_fullRedraw && strcmp(val, _lastVal[idx]) == 0) return;
  strncpy(_lastVal[idx], val, sizeof(_lastVal[0]) - 1);

  _cell.fillSprite(C_BG);
  _cell.drawFastHLine(0, _cellH - 1, _cellW, C_LINE);
  if (cx + 1 < _cols) _cell.drawFastVLine(_cellW - 1, 0, _cellH, C_LINE);

  // label
  _cell.setFont(&fonts::Font2);
  _cell.setTextColor(C_DIM, C_BG);
  _cell.setTextDatum(lgfx::textdatum_t::top_left);
  _cell.drawString(label, 6, 2);
  if (unit[0]) {
    _cell.setTextDatum(lgfx::textdatum_t::top_right);
    _cell.setTextColor(C_DIM, C_BG);
    _cell.drawString(unit, _cellW - 6, 2);
  }

  // value: the seven-segment face reads best at a glance on a bar
  _cell.setFont(&fonts::Font7);
  _cell.setTextDatum(lgfx::textdatum_t::middle_center);
  float scale = (_cols == 1) ? 1.5f : (_cellH > 90 ? 1.1f : 0.85f);
  // Long strings like "1:23:45" need to shrink to fit the cell width.
  while (scale > 0.5f && _cell.textWidth(val, &fonts::Font7) * scale > _cellW - 12) scale -= 0.05f;
  _cell.setTextSize(scale);
  _cell.setTextColor(C_FG, C_BG);
  _cell.drawString(val, _cellW / 2, _cellH / 2 + 6);
  _cell.setTextSize(1.0f);

  _cell.pushSprite(_lcd, px, py);
}

void Ui::drawGrid(const Field* fields, uint8_t n) {
  // Compare against the count the current geometry was built for. Comparing the
  // sprite against _cellW can never fire - layoutFor() sets both together - so
  // a page with a different field count would have kept the previous grid and
  // drawn its extra cells off the bottom of the screen.
  if (n != _layoutCount || _cell.width() != _cellW || _cell.height() != _cellH) {
    layoutFor(n);
    _lcd->fillRect(0, STATUS_H, _lcd->width(), _lcd->height() - STATUS_H, C_BG);
    memset(_lastVal, 0, sizeof(_lastVal));
  }
  for (uint8_t i = 0; i < n; i++) {
    uint8_t cx = _cols == 1 ? 0 : i % _cols;
    uint8_t cy = _cols == 1 ? i : i / _cols;
    drawCell(i, cx, cy, cx * _cellW, STATUS_H + cy * _cellH, fields[i]);
  }
}

// --------------------------------------------------------------------------
void Ui::drawStatusBar() {
  const RideState& s = _rc->state();
  _lcd->fillRect(0, 0, _lcd->width(), STATUS_H, C_BAR);
  _lcd->setFont(&fonts::Font2);
  _lcd->setTextDatum(lgfx::textdatum_t::middle_left);

  // GPS: four bars scaled by satellites in the solution
  int bars = !s.fix.valid ? 0 : s.fix.numSV >= 10 ? 4 : s.fix.numSV >= 8 ? 3 : s.fix.numSV >= 6 ? 2 : 1;
  for (int i = 0; i < 4; i++) {
    int h = 4 + i * 4;
    _lcd->fillRect(6 + i * 6, STATUS_H - 4 - h, 4, h, i < bars ? C_GOOD : C_LINE);
  }
  _lcd->setTextColor(s.fix.valid ? C_FG : C_DIM, C_BAR);
  char sv[8]; snprintf(sv, sizeof(sv), "%u", s.fix.numSV);
  _lcd->drawString(sv, 34, STATUS_H / 2);

  // sensor chips
  int x = 60;
  auto chip = [&](const char* t, bool on, uint16_t onCol) {
    _lcd->setTextColor(on ? onCol : C_LINE, C_BAR);
    _lcd->drawString(t, x, STATUS_H / 2);
    x += _lcd->textWidth(t) + 10;
  };
  chip("HR",  s.hasHr,  C_ACCENT);
  chip("CAD", s.hasCad, C_ACCENT);
  chip("PWR", s.hasPwr, C_ACCENT);
  if (_course->loaded())
    chip("CRS", true, _course->offCourse() ? C_BAD : C_GOOD);
  if (_wx->valid()) {
    // Headwind is the one weather number worth carrying on every page.
    float hw = _wx->headwind(_heading);
    char t[12];
    if (!isnan(hw)) snprintf(t, sizeof(t), "%+.0f", g_settings.speed(hw));
    else            snprintf(t, sizeof(t), "WX");
    chip(t, true, _wx->stale() ? C_LINE
                 : (isnan(hw) ? C_ACCENT : (hw > 1.0f ? C_BAD : hw < -1.0f ? C_GOOD : C_WARN)));
  }
  if (_wifiOn) chip("WiFi", true, C_ACCENT);

  // recording state
  const char* st = "READY";
  uint16_t stc = C_DIM;
  switch (s.status) {
    case RideStatus::Running:    st = "REC";    stc = C_BAD;  break;
    case RideStatus::AutoPaused: st = "AUTO-P"; stc = C_WARN; break;
    case RideStatus::Paused:     st = "PAUSED"; stc = C_WARN; break;
    case RideStatus::Stopped:    st = "STOP";   stc = C_DIM;  break;
    default: break;
  }
  _lcd->setTextDatum(lgfx::textdatum_t::middle_center);
  _lcd->setTextColor(stc, C_BAR);
  _lcd->drawString(st, _lcd->width() / 2 + 60, STATUS_H / 2);
  if (s.status == RideStatus::Running && (millis() / 600) % 2 == 0)
    _lcd->fillCircle(_lcd->width() / 2 + 44, STATUS_H / 2, 5, C_BAD);

  // clock
  if (s.fix.timeValid) {
    time_t t = (time_t)s.fix.unixTime;
    struct tm tmv; localtime_r(&t, &tmv);
    char c[8]; snprintf(c, sizeof(c), "%02d:%02d", tmv.tm_hour, tmv.tm_min);
    _lcd->setTextDatum(lgfx::textdatum_t::middle_right);
    _lcd->setTextColor(C_FG, C_BAR);
    _lcd->drawString(c, _lcd->width() - 52, STATUS_H / 2);
  }

  // battery
  drawBattery(_lcd, _lcd->width() - 44, 6, 32, 14,
              s.batteryPct, s.charging, C_BAR);
}

// --------------------------------------------------------------------------
void Ui::drawMap() {
  int top = STATUS_H, h = _lcd->height() - STATUS_H - 22, w = _lcd->width();
  _lcd->fillRect(0, top, w, h + 22, C_BG);
  // Rider-centred whenever there is a reason to be: navigating a course, or
  // a zoom level the rider chose. Otherwise fitting the whole ride tells
  // them more than an arbitrary window would.
  if (_course->loaded() || !g_settings.mapZoomAuto()) drawMapNav(top, h, w);
  else                                               drawMapFitTrack(top, h, w);
}

void Ui::drawMapFitTrack(int top, int h, int w) {
  const RideState& s = _rc->state();
  uint16_t n = _rc->trackCount();
  if (n < 2) {
    _lcd->setFont(&fonts::Font4);
    _lcd->setTextColor(C_DIM, C_BG);
    _lcd->setTextDatum(lgfx::textdatum_t::middle_center);
    _lcd->drawString(s.fix.valid ? "Start riding to draw a track"
                                 : "Waiting for GPS fix", w / 2, top + h / 2);
    return;
  }

  float minLat, maxLat, minLon, maxLon;
  _rc->trackBounds(minLat, maxLat, minLon, maxLon);
  float midLat = (minLat + maxLat) * 0.5f;
  float lonScale = cosf(radians(midLat));            // keep the aspect ratio honest

  float spanX = (maxLon - minLon) * lonScale;
  float spanY = (maxLat - minLat);
  if (spanX <= 0) spanX = 1e-5f;
  if (spanY <= 0) spanY = 1e-5f;

  int pad = 14;
  float sc = min((w - 2 * pad) / spanX, (h - 2 * pad) / spanY);
  float ox = (w - spanX * sc) * 0.5f;
  float oy = top + (h - spanY * sc) * 0.5f;

  auto toPx = [&](float lat, float lon, int& px, int& py) {
    px = (int)(ox + (lon - minLon) * lonScale * sc);
    py = (int)(oy + (maxLat - lat) * sc);
  };

  const TrackPoint* tp = _rc->track();
  int px0, py0, px1, py1;
  toPx(tp[0].lat, tp[0].lon, px0, py0);
  for (uint16_t i = 1; i < n; i++) {
    toPx(tp[i].lat, tp[i].lon, px1, py1);
    _lcd->drawLine(px0, py0, px1, py1, C_ACCENT);
    _lcd->drawLine(px0, py0 + 1, px1, py1 + 1, C_ACCENT);   // 2 px, readable on a bar
    px0 = px1; py0 = py1;
  }
  _lcd->fillCircle(px0, py0, 5, C_FG);
  _lcd->drawCircle(px0, py0, 7, C_BAD);

  toPx(tp[0].lat, tp[0].lon, px1, py1);
  _lcd->fillRect(px1 - 3, py1 - 3, 6, 6, C_GOOD);

  if (s.fix.valid && s.speed > 1.0f) {
    float a = radians(s.fix.heading);
    _lcd->drawLine(px0, py0, px0 + (int)(18 * sinf(a)), py0 - (int)(18 * cosf(a)), C_BAD);
  }

  // scale bar: pick a round distance that fits in ~100 px
  float mPerPx = 111320.0f / sc;
  char lbl[16];
  float chosen = pickScaleBar(100 * mPerPx, lbl, sizeof(lbl));
  int barPx = (int)(chosen / mPerPx);
  int by = _lcd->height() - 14;
  _lcd->drawFastHLine(10, by, barPx, C_DIM);
  _lcd->drawFastVLine(10, by - 4, 8, C_DIM);
  _lcd->drawFastVLine(10 + barPx, by - 4, 8, C_DIM);
  _lcd->setFont(&fonts::Font2);
  _lcd->setTextColor(C_DIM, C_BG);
  _lcd->setTextDatum(lgfx::textdatum_t::bottom_left);
  _lcd->drawString(lbl, 14 + barPx, by + 6);

  _lcd->setTextDatum(lgfx::textdatum_t::bottom_right);
  char d[28];
  snprintf(d, sizeof(d), "%.2f %s  %u pts", g_settings.distLong(s.distance),
           g_settings.distLongUnit(), n);
  _lcd->setTextColor(C_FG, C_BG);
  _lcd->drawString(d, w - 8, _lcd->height() - 4);
}

void Ui::drawMapNav(int top, int h, int w) {
  const RideState& s = _rc->state();
  if (!s.fix.valid) {
    _lcd->setFont(&fonts::Font4);
    _lcd->setTextColor(C_DIM, C_BG);
    _lcd->setTextDatum(lgfx::textdatum_t::middle_center);
    _lcd->drawString("Waiting for GPS fix", w / 2, top + h / 2);
    return;
  }

  // A fixed level if the rider picked one, otherwise sized to roughly the next
  // two minutes of riding, so the view opens out as you go faster.
  float spanM = g_settings.mapZoomSpanM();
  if (spanM <= 0)
    spanM = constrain(s.speed * NAV_ZOOM_SECONDS, NAV_ZOOM_MIN_M, NAV_ZOOM_MAX_M);
  float sc = w / spanM;                            // pixels per metre

  // Projected here rather than through the course, so the rider-centred view
  // works with no course loaded at all.
  double lat0 = s.fix.lat, lon0 = s.fix.lon;
  float latScale = 111132.0f;
  float lonScale = 111320.0f * cosf(radians((float)lat0));
  int cxp = w / 2, cyp = top + h / 2;

  auto toPx = [&](LatLon p, int& px, int& py) {
    px = cxp + (int)((p.lon - lon0) * lonScale * sc);
    py = cyp - (int)((p.lat - lat0) * latScale * sc);
  };

  // --- course line ---
  const CoursePoint* cp = _course->points();
  uint16_t n = _course->loaded() ? _course->pointCount() : 0;
  int px0 = 0, py0 = 0, px1, py1;
  bool have = false;
  int margin = 40;
  for (uint16_t i = 0; i < n; i++) {
    toPx(cp[i].latLon(), px1, py1);
    bool vis = px1 > -margin && px1 < w + margin && py1 > top - margin && py1 < top + h + margin;
    if (have && (vis || (px0 > -margin && px0 < w + margin))) {
      // Ridden part of the course is dimmed so the road ahead stands out.
      uint16_t col = cp[i].dist < _course->alongDistance() ? C_LINE : C_COURSE;
      _lcd->drawLine(px0, py0, px1, py1, col);
      _lcd->drawLine(px0 + 1, py0, px1 + 1, py1, col);
      _lcd->drawLine(px0, py0 + 1, px1, py1 + 1, col);
    }
    px0 = px1; py0 = py1; have = true;
  }

  // --- turn markers: every visible cue gets its own arrow, the imminent one
  // larger and in the accent colour. A bare dot tells you something happens
  // here; the arrow tells you what, which is the whole point of looking down.
  for (uint8_t i = 0; i < _course->upcomingCount(); i++) {
    const NavCue* q = _course->upcoming(i);
    toPx(q->latLon(), px1, py1);
    if (px1 < 0 || px1 > w || py1 < top || py1 > top + h) continue;

    bool first = (i == 0);
    uint16_t col = first ? C_ACCENT : C_WARN;
    int gr = first ? 13 : 9;
    _lcd->fillCircle(px1, py1, first ? 6 : 4, col);
    if (first) _lcd->drawCircle(px1, py1, 9, col);

    // Keep the glyph on screen when the cue sits near the top edge by flipping
    // it below the marker.
    int gyy = py1 - (first ? 26 : 18);
    if (gyy - gr < top + 4) gyy = py1 + (first ? 26 : 18);
    drawTurnGlyph(_lcd, px1, gyy, gr, q->type, col);
  }

  // --- ridden track on top ---
  const TrackPoint* tp = _rc->track();
  uint16_t tn = _rc->trackCount();
  have = false;
  for (uint16_t i = 0; i < tn; i++) {
    toPx(tp[i].latLon(), px1, py1);
    if (have) {
      _lcd->drawLine(px0, py0, px1, py1, C_ACCENT);
      _lcd->drawLine(px0, py0 + 1, px1, py1 + 1, C_ACCENT);
    }
    px0 = px1; py0 = py1; have = true;
  }

  // --- off-course tether: a red line to where you should be ---
  if (_course->loaded() && _course->offCourse()) {
    uint16_t idx = _course->nearestIndex();
    toPx(cp[idx].latLon(), px1, py1);
    _lcd->drawLine(cxp, cyp, px1, py1, C_BAD);
    _lcd->drawCircle(px1, py1, 6, C_BAD);
  }

  // --- rider arrow, always dead centre ---
  float a = radians(s.fix.heading);
  float ca = cosf(a), sa = sinf(a);
  auto rot = [&](float lx, float ly, int& ox, int& oy) {
    ox = cxp + (int)(lx * ca + ly * sa);
    oy = cyp + (int)(-lx * sa + ly * ca);
  };
  int ax, ay, bx, by2, cx2, cy2;
  rot(0, -11, ax, ay); rot(-7, 8, bx, by2); rot(7, 8, cx2, cy2);
  _lcd->fillTriangle(ax, ay, bx, by2, cx2, cy2, C_FG);
  _lcd->drawTriangle(ax, ay, bx, by2, cx2, cy2, C_BG);

  // --- guidance band across the top ---
  char b[48];
  if (_course->loaded()) {
    drawGuidanceStrip(top, w);
    fmtDist(_course->distanceRemaining(), b, sizeof(b));
    strncat(b, " to go", sizeof(b) - strlen(b) - 1);
  } else {
    // No course to guide along, so the corner reports the ride instead.
    fmtDist(s.distance, b, sizeof(b));
  }
  _lcd->setFont(&fonts::Font2);
  _lcd->setTextDatum(lgfx::textdatum_t::bottom_right);
  _lcd->setTextColor(C_FG, C_BG);
  _lcd->drawString(b, w - 8, _lcd->height() - 4);

  // scale bar
  char lbl[16];
  float chosen = pickScaleBar(120.0f / sc, lbl, sizeof(lbl));
  int barPx = (int)(chosen * sc);
  int byy = _lcd->height() - 14;
  _lcd->drawFastHLine(10, byy, barPx, C_DIM);
  _lcd->drawFastVLine(10, byy - 4, 8, C_DIM);
  _lcd->drawFastVLine(10 + barPx, byy - 4, 8, C_DIM);
  _lcd->setTextDatum(lgfx::textdatum_t::bottom_left);
  _lcd->setTextColor(C_DIM, C_BG);
  _lcd->drawString(lbl, 14 + barPx, byy + 6);
}

// The band that sits over the map: what to do next, and how far away it is.
void Ui::drawGuidanceStrip(int top, int w) {
  const int H = 46;
  char b[48];

  if (_course->offCourse()) {
    _lcd->fillRect(0, top, w, H, C_BAD);
    float rel = _course->relativeBearingToRoute();
    if (!isnan(rel)) drawBearingArrow(_lcd, 26, top + H / 2, 17, rel, C_FG);
    _lcd->setFont(&fonts::Font4);
    _lcd->setTextDatum(lgfx::textdatum_t::top_left);
    _lcd->setTextColor(C_FG, C_BAD);
    _lcd->drawString("OFF COURSE", 52, top + 3);
    _lcd->setFont(&fonts::Font2);
    if (!isnan(rel)) snprintf(b, sizeof(b), "route %s", clockDirection(rel));
    else             snprintf(b, sizeof(b), "return to the route");
    _lcd->drawString(b, 52, top + 26);
    fmtDist(_course->crossTrack(), b, sizeof(b));
    _lcd->setFont(&fonts::Font4);
    _lcd->setTextDatum(lgfx::textdatum_t::middle_right);
    _lcd->drawString(b, w - 8, top + H / 2);
    return;
  }

  const NavCue* q = _course->nextTurn();
  if (!q) return;
  float d = _course->distanceToNextTurn();

  // The band goes amber inside the "turn ahead" call and red at the corner, so
  // urgency is readable without taking your eyes off the road for long.
  uint16_t bg = d <= NAV_ANNOUNCE_NOW_M  ? C_BAD
              : d <= NAV_ANNOUNCE_NEAR_M ? C_ACCENT : C_BAR;
  uint16_t fg = bg == C_ACCENT ? C_BG : C_FG;
  _lcd->fillRect(0, top, w, H, bg);

  drawTurnGlyph(_lcd, 26, top + H / 2, 18, q->type, fg);

  _lcd->setFont(&fonts::Font4);
  _lcd->setTextDatum(lgfx::textdatum_t::top_left);
  _lcd->setTextColor(fg, bg);
  _lcd->drawString(Course::turnText(q->type), 52, top + 2);

  if (q->named && q->name[0]) {
    _lcd->setFont(&fonts::Font2);
    _lcd->setTextDatum(lgfx::textdatum_t::top_left);
    _lcd->drawString(q->name, 52, top + 26);
  } else if (q->angle) {
    _lcd->setFont(&fonts::Font2);
    snprintf(b, sizeof(b), "%d deg", abs(q->angle));
    _lcd->drawString(b, 52, top + 26);
  }

  fmtDist(d, b, sizeof(b));
  _lcd->setFont(&fonts::Font4);
  _lcd->setTextDatum(lgfx::textdatum_t::middle_right);
  _lcd->drawString(b, w - 8, top + H / 2);

  // Countdown bar underneath, filling as the turn approaches.
  if (d <= NAV_ANNOUNCE_FAR_M) {
    int fill = (int)(w * (1.0f - d / NAV_ANNOUNCE_FAR_M));
    _lcd->fillRect(0, top + H - 3, constrain(fill, 0, w), 3, fg);
  }
}

// --------------------------------------------------------------------------
void Ui::drawNav() {
  int top = STATUS_H, w = _lcd->width();
  _lcd->fillRect(0, top, w, _lcd->height() - top, C_BG);
  _lcd->setFont(&fonts::Font4);
  _lcd->setTextDatum(lgfx::textdatum_t::middle_center);

  if (!_course->loaded()) {
    _lcd->setTextColor(C_DIM, C_BG);
    _lcd->drawString("No course loaded", w / 2, top + 90);
    _lcd->setFont(&fonts::Font2);
    _lcd->drawString("Menu > Load course, or upload a .gpx", w / 2, top + 125);
    _lcd->drawString("over Wi-Fi to " COURSE_DIR, w / 2, top + 145);
    if (_course->lastError()[0]) {
      _lcd->setTextColor(C_BAD, C_BG);
      _lcd->drawString(_course->lastError(), w / 2, top + 175);
    }
    return;
  }

  // title
  _lcd->setTextDatum(lgfx::textdatum_t::top_left);
  _lcd->setTextColor(C_ACCENT, C_BG);
  _lcd->drawString(_course->name(), 10, top + 4);

  // progress bar
  int barY = top + 34, barH = 16;
  uint8_t pct = _course->progressPct();
  _lcd->drawRect(10, barY, w - 20, barH, C_LINE);
  _lcd->fillRect(12, barY + 2, (w - 24) * pct / 100, barH - 4,
                 _course->offCourse() ? C_BAD : C_GOOD);
  _lcd->setFont(&fonts::Font2);
  _lcd->setTextDatum(lgfx::textdatum_t::middle_right);
  _lcd->setTextColor(C_FG, C_BG);
  char b[48];
  snprintf(b, sizeof(b), "%u%%", pct);
  _lcd->drawString(b, w - 14, barY + barH / 2);

  // --- the turn you are riding into, given the most room on the page ---
  const NavCue* q = _course->nextTurn();
  float dTurn = _course->distanceToNextTurn();
  int gy = top + 58, gh = 92;

  if (_course->offCourse()) {
    _lcd->fillRoundRect(8, gy, w - 16, gh, 8, C_BAD);
    float rel = _course->relativeBearingToRoute();
    if (!isnan(rel)) drawBearingArrow(_lcd, 56, gy + gh / 2, 34, rel, C_FG);
    _lcd->setFont(&fonts::Font4);
    _lcd->setTextDatum(lgfx::textdatum_t::top_left);
    _lcd->setTextColor(C_FG, C_BAD);
    _lcd->drawString("OFF COURSE", 104, gy + 12);
    _lcd->setFont(&fonts::Font2);
    if (!isnan(rel)) snprintf(b, sizeof(b), "route is %s, %.0f m away",
                              clockDirection(rel), _course->crossTrack());
    else             snprintf(b, sizeof(b), "route is %.0f m away", _course->crossTrack());
    _lcd->drawString(b, 104, gy + 42);
    _lcd->drawString("rejoin, or clear the course from the menu", 104, gy + 62);
  } else if (q) {
    uint16_t bg = dTurn <= NAV_ANNOUNCE_NOW_M  ? C_BAD
                : dTurn <= NAV_ANNOUNCE_NEAR_M ? C_ACCENT : C_BAR;
    uint16_t fg = bg == C_ACCENT ? C_BG : C_FG;
    _lcd->fillRoundRect(8, gy, w - 16, gh, 8, bg);
    drawTurnGlyph(_lcd, 56, gy + gh / 2, 34, q->type, fg);

    _lcd->setFont(&fonts::Font4);
    _lcd->setTextDatum(lgfx::textdatum_t::top_left);
    _lcd->setTextColor(fg, bg);
    _lcd->drawString(Course::turnText(q->type), 104, gy + 10);
    if (q->named && q->name[0]) {
      _lcd->setFont(&fonts::Font2);
      _lcd->drawString(q->name, 104, gy + 40);
    }

    // distance in the seven-segment face, the one number you glance at
    fmtDist(dTurn, b, sizeof(b));
    _lcd->setFont(&fonts::Font7);
    _lcd->setTextDatum(lgfx::textdatum_t::middle_right);
    float sc = 1.0f;
    while (sc > 0.4f && _lcd->textWidth(b, &fonts::Font7) * sc > 150) sc -= 0.05f;
    _lcd->setTextSize(sc);
    _lcd->drawString(b, w - 20, gy + gh / 2);
    _lcd->setTextSize(1.0f);

    if (dTurn <= NAV_ANNOUNCE_FAR_M) {
      int fill = (int)((w - 20) * (1.0f - dTurn / NAV_ANNOUNCE_FAR_M));
      _lcd->fillRect(10, gy + gh - 6, constrain(fill, 0, w - 20), 4, fg);
    }
  } else {
    _lcd->fillRoundRect(8, gy, w - 16, gh, 8, C_BAR);
    _lcd->setFont(&fonts::Font4);
    _lcd->setTextDatum(lgfx::textdatum_t::middle_center);
    _lcd->setTextColor(C_DIM, C_BAR);
    char nt[40];
    fmtDist(NAV_LOOKAHEAD_M, nt, sizeof(nt));
    snprintf(b, sizeof(b), "No turns in the next %s", nt);
    _lcd->drawString(b, w / 2, gy + gh / 2);
  }

  // --- the two after that, so you can plan ---
  int ly = gy + gh + 6;
  _lcd->setFont(&fonts::Font2);
  for (uint8_t i = 1; i < min<uint8_t>(3, _course->upcomingCount()); i++) {
    const NavCue* n2 = _course->upcoming(i);
    int y2 = ly + (i - 1) * 22;
    drawTurnGlyph(_lcd, 20, y2 + 10, 9, n2->type, C_DIM);
    _lcd->setTextDatum(lgfx::textdatum_t::middle_left);
    _lcd->setTextColor(C_DIM, C_BG);
    _lcd->drawString(n2->named && n2->name[0] ? n2->name : Course::turnText(n2->type),
                     38, y2 + 10);
    fmtDist(n2->dist - _course->alongDistance(), b, sizeof(b));
    _lcd->setTextDatum(lgfx::textdatum_t::middle_right);
    _lcd->drawString(b, w - 12, y2 + 10);
  }

  // four small numbers
  const RideState& s = _rc->state();
  struct { const char* label; char val[16]; const char* unit; } box[4];
  const Settings& S = g_settings;
  snprintf(box[0].val, 16, "%.1f", S.distLong(_course->distanceRemaining()));
  box[0].label = "TO GO"; box[0].unit = S.distLongUnit();
  snprintf(box[1].val, 16, "%.0f", S.elev(_course->ascentRemaining()));
  box[1].label = "CLIMB LEFT"; box[1].unit = S.elevUnit();
  float v = s.avgSpeed > 1.0f ? s.avgSpeed : s.speed;
  if (v > 1.0f) fmtTime((uint32_t)(_course->distanceRemaining() / v * 1000.0f), box[2].val, 16);
  else strcpy(box[2].val, "--:--");
  box[2].label = "ETA"; box[2].unit = "";
  snprintf(box[3].val, 16, "%.0f", S.distShort(_course->crossTrack()));
  box[3].label = "OFFSET"; box[3].unit = S.distShortUnit();

  int bw = w / 4, byy = ly + 48, bh = 58;
  _lcd->drawFastHLine(8, byy - 4, w - 16, C_LINE);
  for (int i = 0; i < 4; i++) {
    int x = i * bw;
    if (i) _lcd->drawFastVLine(x, byy + 4, bh - 8, C_LINE);
    _lcd->setFont(&fonts::Font2);
    _lcd->setTextDatum(lgfx::textdatum_t::top_center);
    _lcd->setTextColor(C_DIM, C_BG);
    _lcd->drawString(box[i].label, x + bw / 2, byy);
    _lcd->setFont(&fonts::Font7);
    _lcd->setTextDatum(lgfx::textdatum_t::middle_center);
    _lcd->setTextColor(i == 3 && _course->offCourse() ? C_BAD : C_FG, C_BG);
    float bs = 0.7f;
    while (bs > 0.35f && _lcd->textWidth(box[i].val, &fonts::Font7) * bs > bw - 10) bs -= 0.05f;
    _lcd->setTextSize(bs);
    _lcd->drawString(box[i].val, x + bw / 2, byy + 36);
    _lcd->setTextSize(1.0f);
  }

  // footer
  _lcd->setFont(&fonts::Font2);
  _lcd->setTextDatum(lgfx::textdatum_t::bottom_left);
  _lcd->setTextColor(C_DIM, C_BG);
  snprintf(b, sizeof(b), "%.1f %s course  %.0f %s climb  %u pts  %u cues",
           S.distLong(_course->totalDistance()), S.distLongUnit(),
           S.elev(_course->totalAscent()), S.elevUnit(),
           _course->pointCount(), _course->cueCount());
  _lcd->drawString(b, 10, _lcd->height() - 4);

  if (_course->finished()) {
    _lcd->setTextDatum(lgfx::textdatum_t::bottom_right);
    _lcd->setTextColor(C_GOOD, C_BG);
    _lcd->drawString("COURSE COMPLETE", w - 10, _lcd->height() - 4);
  }
}

// --------------------------------------------------------------------------
// Power zones. Where the effort has actually gone, which is a different
// question from how hard it felt at any one moment.
static uint16_t zoneColour(ZoneSource src, uint8_t z) {
  // Power and heart rate ramp cool to hot: higher is harder.
  static const uint16_t kPwr[7] = {0x39C7, 0x2D7F, 0x07E0, 0xFFE0, 0xFD20, 0xF800, 0xF81F};
  static const uint16_t kHr[5]  = {0x39C7, 0x2D7F, 0x07E0, 0xFD20, 0xF800};
  // Cadence diverges instead: grinding and over-spinning are both awkward, and
  // the comfortable place is in the middle. A hot-at-the-top ramp would imply
  // that turning the pedals faster is always harder work, which it is not.
  static const uint16_t kCad[5] = {0xFD20, 0xFFE0, 0x07E0, 0x2D7F, 0x781F};
  switch (src) {
    case ZoneSource::Power:   return kPwr[z < 7 ? z : 6];
    case ZoneSource::Hr:      return kHr[z < 5 ? z : 4];
    default:                  return kCad[z < 5 ? z : 4];
  }
}

void Ui::drawZonePage(ZoneSource src) {
  const RideState& s = _rc->state();
  const Settings& S = g_settings;
  int top = STATUS_H, w = _lcd->width(), h = _lcd->height();
  _lcd->fillRect(0, top, w, h - top, C_BG);

  bool isPwr = src == ZoneSource::Power;
  bool isHr  = src == ZoneSource::Hr;
  bool isCad = src == ZoneSource::Cadence;

  // Each page speaks only for its own sensor. Falling back to another would
  // mean a page that silently changes what it is showing.
  bool haveData = isPwr ? (s.hasPwr || s.pwrSamples > 0)
                : isHr  ? (s.hasHr  || s.hrSum > 0)
                        : (s.hasCad || s.cadSum > 0 || s.coastingMs > 0);
  char b[56];

  if (!haveData) {
    _lcd->setFont(&fonts::Font4);
    _lcd->setTextDatum(lgfx::textdatum_t::middle_center);
    _lcd->setTextColor(C_DIM, C_BG);
    _lcd->drawString(isPwr ? "No power meter"
                   : isHr  ? "No heart rate strap"
                           : "No cadence sensor", w / 2, top + 110);
    _lcd->setFont(&fonts::Font2);
    _lcd->drawString("Menu > Pair sensors", w / 2, top + 145);
    return;
  }

  uint8_t count      = isPwr ? Zones::POWER_COUNT : Zones::HR_COUNT;   // HR and cadence share a count
  const uint32_t* ms = isPwr ? s.zoneMs : isHr ? s.hrZoneMs : s.cadZoneMs;
  uint16_t value     = isPwr ? s.power  : isHr ? s.hr       : s.cadence;
  uint8_t curZone    = isPwr ? Zones::powerZoneFor(s.power, S.ftpWatts)
                     : isHr  ? Zones::hrZoneFor(s.hr, S.lthrBpm)
                             : Zones::cadenceZoneFor(s.cadence);
  bool haveNow       = isPwr ? s.hasPwr : isHr ? s.hasHr : s.hasCad;
  // Coasting is not a zone, so the live band must not colour itself as one.
  bool coasting      = isCad && haveNow && s.cadence == 0;

  auto zdef = [&](uint8_t i) -> const ZoneDef& {
    return isPwr ? Zones::power(i) : isHr ? Zones::hr(i) : Zones::cadence(i);
  };
  auto zlo = [&](uint8_t i) -> uint16_t {
    return isPwr ? Zones::powerLo(i, S.ftpWatts)
         : isHr  ? Zones::hrLo(i, S.lthrBpm) : Zones::cadenceLo(i);
  };
  auto zhi = [&](uint8_t i) -> uint16_t {
    return isPwr ? Zones::powerHi(i, S.ftpWatts)
         : isHr  ? Zones::hrHi(i, S.lthrBpm) : Zones::cadenceHi(i);
  };

  uint32_t total = 0;
  uint32_t busiest = 0;
  uint8_t busiestZ = 0;
  for (uint8_t i = 0; i < count; i++) {
    total += ms[i];
    if (ms[i] > busiest) { busiest = ms[i]; busiestZ = i; }
  }

  // --- current effort ---
  int cy = top, ch = 62;
  uint16_t cc = coasting ? C_DIM : zoneColour(src, curZone);
  _lcd->fillRect(0, cy, w, ch, C_BAR);
  if (haveNow) _lcd->fillRect(0, cy, 10, ch, cc);

  _lcd->setFont(&fonts::Font2);
  _lcd->setTextDatum(lgfx::textdatum_t::top_left);
  _lcd->setTextColor(C_DIM, C_BAR);
  if (isPwr)     snprintf(b, sizeof(b), "POWER ZONES   FTP %u W", S.ftpWatts);
  else if (isHr) snprintf(b, sizeof(b), "HEART RATE ZONES   LTHR %u bpm", (unsigned)S.lthrBpm);
  else           snprintf(b, sizeof(b), "CADENCE ZONES   rpm");
  _lcd->drawString(b, 22, cy + 4);

  // Ride averages, which is the context the live number needs.
  _lcd->setTextDatum(lgfx::textdatum_t::top_right);
  if (isPwr) {
    uint16_t avg = s.pwrSamples ? (uint16_t)(s.pwrSum / s.pwrSamples) : 0;
    snprintf(b, sizeof(b), "avg %u   NP %u   max %u", avg, s.normalizedPower, s.maxPower);
  } else if (isHr) {
    uint16_t avg = s.sampleCount && s.hrSum ? (uint16_t)(s.hrSum / s.sampleCount) : 0;
    snprintf(b, sizeof(b), "avg %u   max %u", avg, s.maxHr);
  } else {
    uint32_t pedalMs = 0;
    for (uint8_t i = 0; i < Zones::CAD_COUNT; i++) pedalMs += s.cadZoneMs[i];
    uint32_t withData = pedalMs + s.coastingMs;
    uint16_t avg = s.sampleCount && s.cadSum ? (uint16_t)(s.cadSum / s.sampleCount) : 0;
    snprintf(b, sizeof(b), "avg %u   max %u   pedalling %u%%", avg, s.maxCad,
             withData ? (unsigned)((uint64_t)pedalMs * 100 / withData) : 0);
  }
  _lcd->drawString(b, w - 12, cy + 4);

  if (haveNow) {
    _lcd->setFont(&fonts::Font4);
    _lcd->setTextColor(cc, C_BAR);
    if (coasting) {
      _lcd->drawString("COASTING", 22, cy + 26);
    } else {
      const ZoneDef& z = zdef(curZone);
      snprintf(b, sizeof(b), "%s  %s", z.code, z.name);
      _lcd->drawString(b, 22, cy + 26);
    }

    snprintf(b, sizeof(b), "%u", value);
    _lcd->setFont(&fonts::Font7);
    _lcd->setTextDatum(lgfx::textdatum_t::middle_right);
    _lcd->setTextColor(coasting ? C_DIM : C_FG, C_BAR);
    _lcd->setTextSize(1.0f);
    _lcd->drawString(b, w - 60, cy + ch / 2);
    _lcd->setFont(&fonts::Font2);
    _lcd->setTextDatum(lgfx::textdatum_t::middle_left);
    _lcd->setTextColor(C_DIM, C_BAR);
    _lcd->drawString(isPwr ? "W" : isHr ? "bpm" : "rpm", w - 52, cy + ch / 2);
  } else {
    _lcd->setFont(&fonts::Font4);
    _lcd->setTextColor(C_DIM, C_BAR);
    _lcd->drawString("sensor away", 22, cy + 26);
  }

  // --- distribution ---
  int ry = cy + ch + 6;
  int rh = (h - 20 - ry) / count;
  if (rh > 30) rh = 30;

  const int xCode = 12, xName = 44, xRange = 168, xTime = 250, xBar = 264;
  int barW = w - xBar - 54;

  for (uint8_t i = 0; i < count; i++) {
    const ZoneDef& z = zdef(i);
    int y = ry + i * rh;
    bool isNow = haveNow && !coasting && (i == curZone);
    uint16_t col = zoneColour(src, i);
    uint16_t bg = isNow ? C_BAR : C_BG;
    if (isNow) _lcd->fillRect(4, y, w - 8, rh - 2, C_BAR);

    _lcd->setFont(&fonts::Font2);
    _lcd->setTextDatum(lgfx::textdatum_t::middle_left);
    _lcd->fillRect(xCode, y + rh / 2 - 8, 26, 16, col);
    _lcd->setTextColor(C_BG, col);
    _lcd->drawString(z.code, xCode + 4, y + rh / 2);

    _lcd->setTextColor(isNow ? C_FG : C_DIM, bg);
    _lcd->drawString(z.name, xName, y + rh / 2);

    uint16_t lo = zlo(i), hi = zhi(i);
    if (hi) snprintf(b, sizeof(b), "%u-%u", lo, hi);
    else    snprintf(b, sizeof(b), "%u+", lo);
    _lcd->setTextDatum(lgfx::textdatum_t::middle_right);
    _lcd->setTextColor(C_DIM, bg);
    _lcd->drawString(b, xRange + 60, y + rh / 2);

    // The bar is scaled to the busiest zone, not to total time: a ride spent
    // mostly in one zone would otherwise show six invisible slivers.
    int bw2 = busiest ? (int)((int64_t)barW * ms[i] / busiest) : 0;
    _lcd->drawRect(xBar, y + rh / 2 - 8, barW, 16, C_LINE);
    if (bw2 > 0) _lcd->fillRect(xBar + 1, y + rh / 2 - 7, max(1, bw2 - 2), 14, col);

    fmtTime(ms[i], b, sizeof(b));
    _lcd->setTextDatum(lgfx::textdatum_t::middle_left);
    _lcd->setTextColor(ms[i] ? C_FG : C_LINE, bg);
    _lcd->drawString(b, xTime, y + rh / 2);

    _lcd->setTextDatum(lgfx::textdatum_t::middle_right);
    _lcd->setTextColor(ms[i] ? C_FG : C_LINE, bg);
    snprintf(b, sizeof(b), "%u%%", total ? (unsigned)((uint64_t)ms[i] * 100 / total) : 0);
    _lcd->drawString(b, w - 8, y + rh / 2);
  }

  // --- footer ---
  _lcd->setFont(&fonts::Font2);
  _lcd->setTextDatum(lgfx::textdatum_t::bottom_left);
  _lcd->setTextColor(C_DIM, C_BG);
  fmtTime(total, b, sizeof(b));
  char foot[64];
  snprintf(foot, sizeof(foot), "%s with data", b);
  _lcd->drawString(foot, 12, h - 3);

  if (isCad && s.coastingMs) {
    // Worth its own line: it is time on the bike that no cadence zone claims.
    fmtTime(s.coastingMs, b, sizeof(b));
    _lcd->setTextDatum(lgfx::textdatum_t::bottom_center);
    _lcd->setTextColor(C_DIM, C_BG);
    snprintf(foot, sizeof(foot), "coasting %s", b);
    _lcd->drawString(foot, w / 2, h - 3);
  }

  if (total) {
    const ZoneDef& z = zdef(busiestZ);
    _lcd->setTextDatum(lgfx::textdatum_t::bottom_right);
    _lcd->setTextColor(zoneColour(src, busiestZ), C_BG);
    snprintf(foot, sizeof(foot), "mostly %s  %s", z.code, z.name);
    _lcd->drawString(foot, w - 12, h - 3);
  }
}

// --------------------------------------------------------------------------
// The end-of-ride summary a bike computer puts up when you press save. Every
// total in one place, which is the one thing the configurable ride pages
// cannot be - and it is shown automatically when a ride is saved.
void Ui::drawSummary() {
  const Settings& S = g_settings;
  const RideState& s = _rc->state();
  int top = STATUS_H, w = _lcd->width(), h = _lcd->height();
  _lcd->fillRect(0, top, w, h - top, C_BG);
  char b[64];

  // --- header: the two numbers anybody asks about first ---
  int hy = top, hh = 74;
  _lcd->fillRect(0, hy, w, hh, C_BAR);

  _lcd->setFont(&fonts::Font2);
  _lcd->setTextDatum(lgfx::textdatum_t::top_left);
  _lcd->setTextColor(C_ACCENT, C_BAR);
  _lcd->drawString(s.recording ? "RIDE IN PROGRESS" : "RIDE SUMMARY", 12, hy + 4);

  if (s.startUnix > 1600000000UL) {
    time_t t = (time_t)s.startUnix;
    struct tm tmv;
    localtime_r(&t, &tmv);
    snprintf(b, sizeof(b), "%04d-%02d-%02d  %02d:%02d",
             tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour, tmv.tm_min);
    _lcd->setTextDatum(lgfx::textdatum_t::top_right);
    _lcd->setTextColor(C_DIM, C_BAR);
    _lcd->drawString(b, w - 12, hy + 4);
  }

  snprintf(b, sizeof(b), "%.2f", S.distLong(s.distance));
  _lcd->setFont(&fonts::Font7);
  _lcd->setTextDatum(lgfx::textdatum_t::middle_left);
  _lcd->setTextColor(C_FG, C_BAR);
  _lcd->setTextSize(1.3f);
  _lcd->drawString(b, 12, hy + 44);
  int dw = (int)(_lcd->textWidth(b, &fonts::Font7) * 1.3f);
  _lcd->setTextSize(1.0f);
  _lcd->setFont(&fonts::Font4);
  _lcd->setTextDatum(lgfx::textdatum_t::bottom_left);
  _lcd->setTextColor(C_DIM, C_BAR);
  _lcd->drawString(S.distLongUnit(), 18 + dw, hy + hh - 14);

  fmtTime(s.movingMs, b, sizeof(b));
  _lcd->setFont(&fonts::Font7);
  _lcd->setTextDatum(lgfx::textdatum_t::middle_right);
  _lcd->setTextColor(C_FG, C_BAR);
  _lcd->setTextSize(1.0f);
  _lcd->drawString(b, w - 12, hy + 44);

  // --- the rest, built from whatever this ride actually has ---
  // Twenty slots for what is currently at most sixteen entries: sitting exactly
  // on the limit means the next stat added here vanishes silently.
  struct Stat { const char* label; char val[16]; const char* unit; };
  Stat st[20];
  uint8_t n = 0;
  static char sink[16];
  auto add = [&](const char* label, const char* unit) -> char* {
    if (n >= 20) return sink;
    st[n].label = label;
    st[n].unit = unit;
    return st[n++].val;
  };

  fmtTime(s.elapsedMs, add("ELAPSED", ""), 16);
  snprintf(add("AVG SPEED", S.speedUnit()), 16, "%.1f", S.speed(s.avgSpeed));
  snprintf(add("MAX SPEED", S.speedUnit()), 16, "%.1f", S.speed(s.maxSpeed));
  snprintf(add("ASCENT", S.elevUnit()), 16, "%.0f", S.elev(s.ascent));
  snprintf(add("DESCENT", S.elevUnit()), 16, "%.0f", S.elev(s.descent));
  snprintf(add("CALORIES", "kcal"), 16, "%.0f", s.calories);
  snprintf(add("LAPS", ""), 16, "%u", s.lapCount);

  if (s.sampleCount && s.hrSum) {
    snprintf(add("AVG HR", "bpm"), 16, "%u", (unsigned)(s.hrSum / s.sampleCount));
    snprintf(add("MAX HR", "bpm"), 16, "%u", s.maxHr);
  }
  if (s.pwrSamples) {
    snprintf(add("AVG POWER", "W"), 16, "%u", (unsigned)(s.pwrSum / s.pwrSamples));
    snprintf(add("NORM POWER", "W"), 16, "%u", s.normalizedPower);
    snprintf(add("MAX POWER", "W"), 16, "%u", s.maxPower);
  }
  if (s.sampleCount && s.cadSum)
    snprintf(add("AVG CADENCE", "rpm"), 16, "%u", (unsigned)(s.cadSum / s.sampleCount));

  bool fromPower = false;
  float tss = currentRideTss(&fromPower);
  if (tss > 0) snprintf(add(fromPower ? "TSS" : "hrTSS", ""), 16, "%.0f", tss);
  if (s.energyKj > 0) snprintf(add("WORK", "kJ"), 16, "%.0f", s.energyKj);

  if (!isnan(g_env.minTemp()))
    snprintf(add("TEMP", S.tempUnit()), 16, "%.0f-%.0f",
             S.temp(g_env.minTemp()), S.temp(g_env.maxTemp()));

  // --- grid, sized to what there is rather than a fixed shape ---
  int gy = hy + hh + 4;
  int gh = h - gy - 16;
  const uint8_t cols = 4;
  uint8_t rows = (n + cols - 1) / cols;
  if (!rows) rows = 1;
  int cw = w / cols;
  int rh = gh / rows;

  for (uint8_t i = 0; i < n; i++) {
    int cx = (i % cols) * cw;
    int cyy = gy + (i / cols) * rh;
    if (i % cols) _lcd->drawFastVLine(cx, cyy + 4, rh - 8, C_LINE);
    if (i >= cols) _lcd->drawFastHLine(cx + 4, cyy, cw - 8, C_LINE);

    _lcd->setFont(&fonts::Font2);
    _lcd->setTextDatum(lgfx::textdatum_t::top_center);
    _lcd->setTextColor(C_DIM, C_BG);
    _lcd->drawString(st[i].label, cx + cw / 2, cyy + 4);

    _lcd->setFont(&fonts::Font4);
    _lcd->setTextDatum(lgfx::textdatum_t::middle_center);
    _lcd->setTextColor(C_FG, C_BG);
    _lcd->drawString(st[i].val, cx + cw / 2, cyy + rh / 2 + 6);

    if (st[i].unit[0]) {
      _lcd->setFont(&fonts::Font2);
      _lcd->setTextDatum(lgfx::textdatum_t::bottom_center);
      _lcd->setTextColor(C_DIM, C_BG);
      _lcd->drawString(st[i].unit, cx + cw / 2, cyy + rh - 1);
    }
  }

  // --- footer ---
  _lcd->setFont(&fonts::Font2);
  _lcd->setTextDatum(lgfx::textdatum_t::bottom_left);
  _lcd->setTextColor(C_DIM, C_BG);
  if (s.recording)
    _lcd->drawString("hold LAP to finish and save", 12, h - 2);
  else if (_rec->fitPath()[0])
    _lcd->drawString(_rec->fitPath(), 12, h - 2);
  else
    _lcd->drawString("not recorded to the card", 12, h - 2);
}

// --------------------------------------------------------------------------
// Gear page. Nothing here is read from the drivetrain - the ratio is measured
// from speed and cadence and matched against the gearing in settings, so the
// page is careful about saying how sure it is.
void Ui::drawGear() {
  const Settings& S = g_settings;
  const RideState& s = _rc->state();
  const DrivetrainSpec& spec = g_drive.spec();
  int top = STATUS_H, w = _lcd->width(), h = _lcd->height();
  _lcd->fillRect(0, top, w, h - top, C_BG);
  char b[64];

  bool haveCad = s.hasCad;
  bool ok = g_drive.valid();

  // --- current gear ---
  int cy = top, ch = 92;
  _lcd->fillRect(0, cy, w, ch, C_BAR);
  _lcd->setFont(&fonts::Font2);
  _lcd->setTextDatum(lgfx::textdatum_t::top_left);
  _lcd->setTextColor(C_DIM, C_BAR);
  _lcd->drawString(spec.name, 12, cy + 4);

  if (!haveCad) {
    _lcd->setFont(&fonts::Font4);
    _lcd->setTextDatum(lgfx::textdatum_t::middle_center);
    _lcd->setTextColor(C_DIM, C_BAR);
    _lcd->drawString("No cadence sensor", w / 2, cy + ch / 2);
  } else if (!ok) {
    _lcd->setFont(&fonts::Font4);
    _lcd->setTextDatum(lgfx::textdatum_t::middle_center);
    _lcd->setTextColor(C_DIM, C_BAR);
    _lcd->drawString(s.cadence ? "Pedal a little harder" : "Coasting", w / 2, cy + ch / 2 - 8);
    _lcd->setFont(&fonts::Font2);
    _lcd->drawString("a gear needs cadence and speed together", w / 2, cy + ch / 2 + 20);
  } else {
    uint16_t col = g_drive.crossChained() ? C_WARN : C_FG;
    snprintf(b, sizeof(b), "%u x %u", g_drive.ringTeeth(), g_drive.sprocketTeeth());
    _lcd->setFont(&fonts::Font7);
    _lcd->setTextDatum(lgfx::textdatum_t::middle_left);
    _lcd->setTextColor(col, C_BAR);
    float sc = 1.5f;
    while (sc > 0.6f && _lcd->textWidth(b, &fonts::Font7) * sc > w - 210) sc -= 0.05f;
    _lcd->setTextSize(sc);
    _lcd->drawString(b, 14, cy + ch / 2 + 4);
    _lcd->setTextSize(1.0f);

    // Three ways of saying the same gear, because riders quote all of them.
    struct { const char* label; char val[14]; } m[3];
    snprintf(m[0].val, 14, "%.2f", g_drive.ratio());       m[0].label = "RATIO";
    snprintf(m[1].val, 14, "%.1f", g_drive.gearInches());  m[1].label = "GEAR IN";
    snprintf(m[2].val, 14, "%.2f", g_drive.development()); m[2].label = "DEV m";
    int mx = w - 200;
    for (int i = 0; i < 3; i++) {
      int x = mx + i * 64;
      _lcd->setFont(&fonts::Font2);
      _lcd->setTextDatum(lgfx::textdatum_t::top_center);
      _lcd->setTextColor(C_DIM, C_BAR);
      _lcd->drawString(m[i].label, x + 30, cy + 26);
      _lcd->setFont(&fonts::Font4);
      _lcd->setTextColor(C_FG, C_BAR);
      _lcd->drawString(m[i].val, x + 30, cy + 44);
    }

    _lcd->setFont(&fonts::Font2);
    _lcd->setTextDatum(lgfx::textdatum_t::bottom_left);
    if (g_drive.crossChained()) {
      _lcd->setTextColor(C_WARN, C_BAR);
      _lcd->drawString("cross-chained", 14, cy + ch - 3);
    } else if (g_drive.ambiguous()) {
      // Two combinations fit the measured ratio; the sprocket is sound, the
      // chainring is the better of two guesses.
      _lcd->setTextColor(C_DIM, C_BAR);
      _lcd->drawString("chainring uncertain", 14, cy + ch - 3);
    }
  }

  // --- the gear table ---
  uint8_t nr = spec.ringCount, nc = spec.sprocketCount;
  int ty = cy + ch + 10;
  int labelW = 34;
  int cw = (w - 16 - labelW) / nc;
  int chh = 30;

  _lcd->setFont(&fonts::Font2);
  _lcd->setTextDatum(lgfx::textdatum_t::middle_center);
  _lcd->setTextColor(C_DIM, C_BG);
  _lcd->drawString("t", 8 + labelW / 2, ty + 10);
  for (uint8_t c = 0; c < nc; c++) {
    snprintf(b, sizeof(b), "%u", spec.sprockets[c]);
    _lcd->drawString(b, 8 + labelW + c * cw + cw / 2, ty + 10);
  }

  for (uint8_t r = 0; r < nr; r++) {
    int y = ty + 22 + r * chh;
    snprintf(b, sizeof(b), "%u", spec.rings[r]);
    _lcd->setTextColor(C_ACCENT, C_BG);
    _lcd->drawString(b, 8 + labelW / 2, y + chh / 2);

    for (uint8_t c = 0; c < nc; c++) {
      int x = 8 + labelW + c * cw;
      bool cur = ok && r == (uint8_t)g_drive.ringIndex() && c == (uint8_t)g_drive.sprocketIndex();
      // Cross-chained combinations are marked in the table too, so the gears
      // to avoid are visible before you shift into one.
      bool bad = nr >= 2 && ((r == 0 && c >= nc - 2) || (r == nr - 1 && c <= 1));
      uint16_t bg = cur ? C_ACCENT : C_BG;
      if (cur) _lcd->fillRect(x + 1, y + 1, cw - 2, chh - 3, C_ACCENT);
      else     _lcd->drawRect(x + 1, y + 1, cw - 2, chh - 3, bad ? C_LINE : C_BAR);

      snprintf(b, sizeof(b), "%.0f", Drivetrain::gearInchesOf(spec.rings[r],
                                                              spec.sprockets[c], S.wheelMm));
      _lcd->setTextColor(cur ? C_BG : (bad ? C_LINE : C_FG), bg);
      _lcd->drawString(b, x + cw / 2, y + chh / 2);
    }
  }

  // --- footer ---
  _lcd->setFont(&fonts::Font2);
  _lcd->setTextDatum(lgfx::textdatum_t::bottom_left);
  _lcd->setTextColor(C_DIM, C_BG);
  _lcd->drawString("gear inches   estimated from speed and cadence", 12, h - 3);

  if (ok) {
    // What the next shift would do to your legs at this speed is the question
    // the table is usually being read to answer.
    _lcd->setTextDatum(lgfx::textdatum_t::bottom_right);
    int8_t c = g_drive.sprocketIndex();
    float spd = s.speed;
    if (c > 0) {
      float cad = spd * 60.0f / Drivetrain::developmentOf(g_drive.ringTeeth(),
                                                          spec.sprockets[c - 1], S.wheelMm);
      snprintf(b, sizeof(b), "one harder: %.0f rpm at this speed", cad);
      _lcd->drawString(b, w - 12, h - 3);
    }
  }
}

// --------------------------------------------------------------------------
// Training load. The ride's own stress score on top, the fitness/fatigue/form
// figures below it, and ninety days of history as the chart that makes those
// three numbers mean something.
float Ui::currentRideTss(bool* fromPower) const {
  const RideState& s = _rc->state();
  const Settings& S = g_settings;
  if (fromPower) *fromPower = false;
  if (!s.movingMs) return 0;

  if (s.pwrSamples && s.normalizedPower) {
    if (fromPower) *fromPower = true;
    return TrainingLoad::powerTss(s.movingMs, s.normalizedPower, S.ftpWatts);
  }
  if (s.sampleCount && s.hrSum) {
    uint8_t avgHr = (uint8_t)(s.hrSum / s.sampleCount);
    return TrainingLoad::hrTss(s.movingMs, avgHr, S.lthrBpm);
  }
  // Neither sensor: no score. The drag model behind the calorie figure guesses
  // at CdA and often has no wind, and a number invented from that would feed
  // months of training history while looking authoritative.
  return 0;
}

void Ui::drawLoad() {
  const RideState& s = _rc->state();
  const Settings& S = g_settings;
  int top = STATUS_H, w = _lcd->width(), h = _lcd->height();
  _lcd->fillRect(0, top, w, h - top, C_BG);

  char b[56];
  bool fromPower = false;
  float tss = currentRideTss(&fromPower);
  float ctl = g_load.ctl(tss), atl = g_load.atl(tss), tsb = ctl - atl;

  // --- this ride ---
  int ry = top, rh = 84;
  _lcd->fillRect(0, ry, w, rh, C_BAR);
  _lcd->setFont(&fonts::Font2);
  _lcd->setTextDatum(lgfx::textdatum_t::top_left);
  _lcd->setTextColor(C_ACCENT, C_BAR);
  _lcd->drawString(fromPower ? "THIS RIDE" : (tss > 0 ? "THIS RIDE  from heart rate"
                                                      : "THIS RIDE"), 12, ry + 4);

  struct { const char* label; char val[16]; } box[4];
  snprintf(box[0].val, 16, "%.0f", tss);                       box[0].label = "TSS";
  if (fromPower) {
    snprintf(box[1].val, 16, "%.2f",
             TrainingLoad::intensityFactor(s.normalizedPower, S.ftpWatts));
    box[1].label = "IF";
    snprintf(box[2].val, 16, "%u", s.normalizedPower);         box[2].label = "NP";
  } else {
    uint8_t avgHr = s.sampleCount && s.hrSum ? (uint8_t)(s.hrSum / s.sampleCount) : 0;
    snprintf(box[1].val, 16, "%u", avgHr);                     box[1].label = "AVG HR";
    snprintf(box[2].val, 16, "%u", S.lthrBpm);                 box[2].label = "LTHR";
  }
  snprintf(box[3].val, 16, "%.0f", s.energyKj);                box[3].label = "kJ";

  int bw = w / 4;
  for (int i = 0; i < 4; i++) {
    int x = i * bw;
    if (i) _lcd->drawFastVLine(x, ry + 24, rh - 30, C_LINE);
    _lcd->setFont(&fonts::Font2);
    _lcd->setTextDatum(lgfx::textdatum_t::top_center);
    _lcd->setTextColor(C_DIM, C_BAR);
    _lcd->drawString(box[i].label, x + bw / 2, ry + 24);
    _lcd->setFont(&fonts::Font7);
    _lcd->setTextDatum(lgfx::textdatum_t::middle_center);
    _lcd->setTextColor(C_FG, C_BAR);
    float sc = 0.85f;
    while (sc > 0.35f && _lcd->textWidth(box[i].val, &fonts::Font7) * sc > bw - 12) sc -= 0.05f;
    _lcd->setTextSize(sc);
    _lcd->drawString(box[i].val, x + bw / 2, ry + 58);
    _lcd->setTextSize(1.0f);
  }

  if (tss <= 0) {
    _lcd->setFont(&fonts::Font2);
    _lcd->setTextDatum(lgfx::textdatum_t::top_right);
    _lcd->setTextColor(C_WARN, C_BAR);
    _lcd->drawString("needs a power meter or HR strap", w - 12, ry + 4);
  }

  // --- fitness / fatigue / form ---
  int fy = ry + rh + 6, fh = 70;
  const char* fl[3] = {"FITNESS  CTL", "FATIGUE  ATL", "FORM  TSB"};
  float fv[3] = {ctl, atl, tsb};
  // Form is the one with a meaning attached to its sign: positive is fresh,
  // deeply negative is a hole.
  uint16_t fc[3] = {C_ACCENT, C_COURSE,
                    tsb > 5 ? C_GOOD : tsb > -10 ? C_FG : tsb > -25 ? C_WARN : C_BAD};
  int fw = w / 3;
  for (int i = 0; i < 3; i++) {
    int x = i * fw;
    if (i) _lcd->drawFastVLine(x, fy + 4, fh - 8, C_LINE);
    _lcd->setFont(&fonts::Font2);
    _lcd->setTextDatum(lgfx::textdatum_t::top_center);
    _lcd->setTextColor(C_DIM, C_BG);
    _lcd->drawString(fl[i], x + fw / 2, fy);
    snprintf(b, sizeof(b), i == 2 ? "%+.0f" : "%.0f", fv[i]);
    _lcd->setFont(&fonts::Font7);
    _lcd->setTextDatum(lgfx::textdatum_t::middle_center);
    _lcd->setTextColor(fc[i], C_BG);
    _lcd->setTextSize(1.0f);
    _lcd->drawString(b, x + fw / 2, fy + 44);
  }

  // --- ninety days ---
  int gy = fy + fh + 6, gh = h - gy - 20;
  int gl = 34, gw = w - gl - 10;
  const uint16_t N = TrainingLoad::DAYS;

  float maxTss = max(g_load.peak(N), 60.0f);
  float maxCtl = max(max(ctl, atl), 10.0f);
  float top2 = max(maxTss, maxCtl * 1.2f);

  _lcd->drawFastHLine(gl, gy + gh, gw, C_LINE);
  _lcd->drawFastVLine(gl, gy, gh, C_LINE);
  _lcd->setFont(&fonts::Font0);
  _lcd->setTextDatum(lgfx::textdatum_t::middle_right);
  _lcd->setTextColor(C_DIM, C_BG);
  snprintf(b, sizeof(b), "%.0f", top2);
  _lcd->drawString(b, gl - 3, gy + 4);

  // Daily TSS bars, oldest on the left.
  for (uint16_t i = 0; i < N; i++) {
    float t = g_load.dayTss(N - 1 - i);
    if (i == N - 1) t += tss;
    if (t <= 0) continue;
    int x = gl + 1 + i * (gw - 2) / N;
    int bwd = max(1, (gw - 2) / N - 1);
    int bh2 = (int)(gh * min(1.0f, t / top2));
    _lcd->fillRect(x, gy + gh - bh2, bwd, bh2, C_BAR);
  }

  // Fitness and fatigue curves, recomputed day by day across the window.
  float c = 0, a = 0;
  int pcx = -1, pcy = 0, pay = 0;
  for (uint16_t i = 0; i < N; i++) {
    float t = g_load.dayTss(N - 1 - i);
    if (i == N - 1) t += tss;
    c += (t - c) / 42.0f;
    a += (t - a) / 7.0f;
    int x = gl + 1 + i * (gw - 2) / N;
    int cy2 = gy + gh - (int)(gh * min(1.0f, c / top2));
    int ay2 = gy + gh - (int)(gh * min(1.0f, a / top2));
    if (pcx >= 0) {
      _lcd->drawLine(pcx, pay, x, ay2, C_COURSE);
      _lcd->drawLine(pcx, pcy, x, cy2, C_ACCENT);
      _lcd->drawLine(pcx, pcy + 1, x, cy2 + 1, C_ACCENT);
    }
    pcx = x; pcy = cy2; pay = ay2;
  }

  _lcd->setFont(&fonts::Font2);
  _lcd->setTextDatum(lgfx::textdatum_t::bottom_left);
  _lcd->setTextColor(C_DIM, C_BG);
  if (!g_load.haveDate())
    _lcd->drawString("no date yet - history starts at the first GPS fix", 12, h - 3);
  else
    _lcd->drawString("90 days", 12, h - 3);

  _lcd->setTextDatum(lgfx::textdatum_t::bottom_right);
  snprintf(b, sizeof(b), "week %.0f TSS   today %.0f", g_load.weekTss() + tss,
           g_load.dayTss(0) + tss);
  _lcd->drawString(b, w - 12, h - 3);
}

// --------------------------------------------------------------------------
// Workout page. Built around one question: what am I meant to be doing right
// now, and for how much longer. Everything else is secondary.
void Ui::drawWorkout() {
  const Settings& S = g_settings;
  const RideState& s = _rc->state();
  int top = STATUS_H, w = _lcd->width(), h = _lcd->height();
  _lcd->fillRect(0, top, w, h - top, C_BG);

  char b[48];

  if (!g_workout.loaded()) {
    _lcd->setFont(&fonts::Font4);
    _lcd->setTextDatum(lgfx::textdatum_t::middle_center);
    _lcd->setTextColor(C_DIM, C_BG);
    _lcd->drawString("No workout loaded", w / 2, top + 90);
    _lcd->setFont(&fonts::Font2);
    _lcd->drawString("Menu > Start workout for a built-in session,", w / 2, top + 128);
    _lcd->drawString("or put a .wko file in " WORKOUT_DIR, w / 2, top + 148);
    if (g_workout.lastError()[0]) {
      _lcd->setTextColor(C_BAD, C_BG);
      _lcd->drawString(g_workout.lastError(), w / 2, top + 180);
    }
    return;
  }

  const WorkoutStep& st = g_workout.current();
  bool done = g_workout.finished();

  // --- header: name, step counter, overall progress ---
  _lcd->setFont(&fonts::Font2);
  _lcd->setTextDatum(lgfx::textdatum_t::top_left);
  _lcd->setTextColor(C_ACCENT, C_BG);
  _lcd->drawString(g_workout.name(), 10, top + 3);
  _lcd->setTextDatum(lgfx::textdatum_t::top_right);
  _lcd->setTextColor(C_DIM, C_BG);
  snprintf(b, sizeof(b), "step %u of %u",
           g_workout.currentIndex() + 1, g_workout.stepCount());
  _lcd->drawString(b, w - 10, top + 3);

  int pby = top + 20;
  _lcd->drawRect(10, pby, w - 20, 8, C_LINE);
  _lcd->fillRect(12, pby + 2, (int)((w - 24) * g_workout.totalProgress()), 4, C_ACCENT);
  // Tick at each step boundary, so the shape of the session is visible.
  for (uint8_t i = 1; i < g_workout.stepCount(); i++)
    _lcd->drawFastVLine(12 + (w - 24) * i / g_workout.stepCount(), pby, 8, C_BAR);

  if (done) {
    _lcd->setFont(&fonts::Font4);
    _lcd->setTextDatum(lgfx::textdatum_t::middle_center);
    _lcd->setTextColor(C_GOOD, C_BG);
    _lcd->drawString("WORKOUT COMPLETE", w / 2, top + 130);
    _lcd->setFont(&fonts::Font2);
    _lcd->setTextColor(C_DIM, C_BG);
    _lcd->drawString("Menu > Stop workout to clear it", w / 2, top + 165);
    return;
  }

  // --- the step you are in ---
  uint16_t kindCol = st.kind == StepKind::Work ? C_BAD
                   : st.kind == StepKind::Rest ? C_COURSE
                   : C_WARN;
  int sy = pby + 14, sh = 104;
  _lcd->fillRect(0, sy, w, sh, C_BAR);
  _lcd->fillRect(0, sy, 8, sh, kindCol);

  _lcd->setFont(&fonts::Font4);
  _lcd->setTextDatum(lgfx::textdatum_t::top_left);
  _lcd->setTextColor(kindCol, C_BAR);
  _lcd->drawString(Workout::kindText(st.kind), 20, sy + 6);
  if (st.name[0] && strcmp(st.name, Workout::kindText(st.kind)) != 0) {
    _lcd->setFont(&fonts::Font2);
    _lcd->setTextColor(C_DIM, C_BAR);
    _lcd->drawString(st.name, 20, sy + 34);
  }

  // The countdown, in the biggest figures on the page.
  if (st.durType == StepDuration::Time) {
    fmtTime(g_workout.stepRemainingMs(), b, sizeof(b));
  } else if (st.durType == StepDuration::Distance) {
    fmtDist(g_workout.stepRemainingM(), b, sizeof(b));
  } else {
    fmtTime(g_workout.stepElapsedMs(), b, sizeof(b));
  }
  _lcd->setFont(&fonts::Font7);
  _lcd->setTextDatum(lgfx::textdatum_t::middle_right);
  _lcd->setTextColor(C_FG, C_BAR);
  float sc = 1.5f;
  while (sc > 0.5f && _lcd->textWidth(b, &fonts::Font7) * sc > w - 200) sc -= 0.05f;
  _lcd->setTextSize(sc);
  _lcd->drawString(b, w - 16, sy + sh / 2);
  _lcd->setTextSize(1.0f);
  _lcd->setFont(&fonts::Font2);
  _lcd->setTextDatum(lgfx::textdatum_t::bottom_right);
  _lcd->setTextColor(C_DIM, C_BAR);
  _lcd->drawString(st.durType == StepDuration::Open ? "press LAP to finish"
                                                    : "remaining", w - 16, sy + sh - 4);

  // step progress
  _lcd->fillRect(8, sy + sh - 5, (int)((w - 16) * g_workout.stepProgress()), 4, kindCol);

  // --- target band ---
  int ty = sy + sh + 8, th = 74;
  if (st.target == StepTarget::None) {
    _lcd->setFont(&fonts::Font4);
    _lcd->setTextDatum(lgfx::textdatum_t::middle_center);
    _lcd->setTextColor(C_DIM, C_BG);
    _lcd->drawString("no target - just ride", w / 2, ty + th / 2);
  } else {
    Compliance c = g_workout.compliance();
    uint16_t col = c == Compliance::InRange ? C_GOOD
                 : c == Compliance::NoTarget ? C_DIM : C_BAD;

    _lcd->setFont(&fonts::Font2);
    _lcd->setTextDatum(lgfx::textdatum_t::top_left);
    _lcd->setTextColor(C_DIM, C_BG);
    snprintf(b, sizeof(b), "TARGET  %u - %u %s", st.lo, st.hi,
             Workout::targetUnit(st.target));
    _lcd->drawString(b, 12, ty);

    _lcd->setTextDatum(lgfx::textdatum_t::top_right);
    _lcd->setTextColor(col, C_BG);
    _lcd->drawString(c == Compliance::Below ? "TOO EASY"
                   : c == Compliance::Above ? "TOO HARD"
                   : c == Compliance::InRange ? "ON TARGET" : "no sensor", w - 12, ty);

    // Current value, large, coloured by compliance.
    if (c != Compliance::NoTarget) {
      snprintf(b, sizeof(b), "%u", g_workout.currentValue());
      _lcd->setFont(&fonts::Font7);
      _lcd->setTextDatum(lgfx::textdatum_t::middle_left);
      _lcd->setTextColor(col, C_BG);
      _lcd->setTextSize(1.0f);
      _lcd->drawString(b, 12, ty + 44);
      _lcd->setFont(&fonts::Font2);
      _lcd->setTextDatum(lgfx::textdatum_t::bottom_left);
      _lcd->setTextColor(C_DIM, C_BG);
      _lcd->drawString(Workout::targetUnit(st.target), 12 + _lcd->textWidth(b, &fonts::Font7) + 8,
                       ty + 62);
    }

    // A bar with the target band marked, and a needle where the rider is. The
    // scale runs to twice the top of the band so overshoot stays on screen.
    int gx = 150, gw = w - gx - 16, gy2 = ty + 30, gh2 = 26;
    float span = st.hi * 2.0f;
    if (span < 1) span = 1;
    _lcd->drawRect(gx, gy2, gw, gh2, C_LINE);
    int lo = gx + (int)(gw * st.lo / span), hi = gx + (int)(gw * st.hi / span);
    _lcd->fillRect(lo, gy2 + 1, max(2, hi - lo), gh2 - 2, C_LINE);
    _lcd->drawFastVLine(lo, gy2, gh2, C_GOOD);
    _lcd->drawFastVLine(hi, gy2, gh2, C_GOOD);
    if (c != Compliance::NoTarget) {
      int px = gx + (int)(gw * min((float)g_workout.currentValue(), span) / span);
      _lcd->fillRect(px - 2, gy2 - 3, 5, gh2 + 6, col);
    }
  }

  // --- what comes next ---
  const WorkoutStep* nx = g_workout.next();
  _lcd->setFont(&fonts::Font2);
  _lcd->setTextDatum(lgfx::textdatum_t::bottom_left);
  _lcd->setTextColor(C_DIM, C_BG);
  if (nx) {
    char dur[20];
    if (nx->durType == StepDuration::Time) fmtTime(nx->durValue, dur, sizeof(dur));
    else if (nx->durType == StepDuration::Distance) fmtDist(nx->durValue, dur, sizeof(dur));
    else snprintf(dur, sizeof(dur), "open");
    if (nx->target != StepTarget::None)
      snprintf(b, sizeof(b), "NEXT  %s  %s  %u-%u %s", Workout::kindText(nx->kind), dur,
               nx->lo, nx->hi, Workout::targetUnit(nx->target));
    else
      snprintf(b, sizeof(b), "NEXT  %s  %s", Workout::kindText(nx->kind), dur);
  } else {
    snprintf(b, sizeof(b), "last step");
  }
  _lcd->drawString(b, 12, h - 3);

  _lcd->setTextDatum(lgfx::textdatum_t::bottom_right);
  if (!g_workout.running()) {
    _lcd->setTextColor(C_WARN, C_BG);
    _lcd->drawString("not started - press ENTER to start the ride", w - 12, h - 3);
  } else if (s.status != RideStatus::Running) {
    _lcd->setTextColor(C_WARN, C_BG);
    _lcd->drawString("ride paused - workout held", w - 12, h - 3);
  } else {
    uint32_t tt = g_workout.totalTimeMs();
    if (tt) {
      fmtTime(tt, b, sizeof(b));
      char lbl[40];
      snprintf(lbl, sizeof(lbl), "%s session", b);
      _lcd->setTextColor(C_DIM, C_BG);
      _lcd->drawString(lbl, w - 12, h - 3);
    }
  }
  (void)S;
}

// --------------------------------------------------------------------------
// Lap summary. The lap being ridden gets the top band because that is the one
// the rider is acting on; completed laps sit underneath, newest first, so the
// one just finished is right below the one in progress.
void Ui::drawLaps() {
  const Settings& S = g_settings;
  const RideState& s = _rc->state();
  int top = STATUS_H, w = _lcd->width(), h = _lcd->height();
  _lcd->fillRect(0, top, w, h - top, C_BG);

  char b[40], t[24];
  LapRecord cur = _rc->currentLap();

  // --- current lap band ---
  int by = top, bh = 76;
  _lcd->fillRect(0, by, w, bh, C_BAR);
  _lcd->setFont(&fonts::Font2);
  _lcd->setTextDatum(lgfx::textdatum_t::top_left);
  _lcd->setTextColor(C_ACCENT, C_BAR);
  snprintf(b, sizeof(b), "LAP %u", cur.index);
  _lcd->drawString(b, 12, by + 4);
  _lcd->setTextColor(C_DIM, C_BAR);
  _lcd->drawString(s.status == RideStatus::Running ? "in progress" : "current",
                   64, by + 4);

  struct { const char* label; char val[16]; const char* unit; } box[4];
  snprintf(box[0].val, 16, "%.2f", S.distLong(cur.distance));
  box[0].label = "DISTANCE"; box[0].unit = S.distLongUnit();
  fmtTime(cur.movingMs, box[1].val, 16);
  box[1].label = "TIME"; box[1].unit = "";
  snprintf(box[2].val, 16, "%.1f", S.speed(cur.avgSpeed));
  box[2].label = "AVG"; box[2].unit = S.speedUnit();
  // The fourth box shows whatever the rider actually has a sensor for.
  if (s.hasPwr || cur.avgPower) {
    snprintf(box[3].val, 16, "%u", cur.avgPower);
    box[3].label = "AVG POWER"; box[3].unit = "W";
  } else if (s.hasHr || cur.avgHr) {
    snprintf(box[3].val, 16, "%u", cur.avgHr);
    box[3].label = "AVG HR"; box[3].unit = "bpm";
  } else {
    snprintf(box[3].val, 16, "%.0f", S.elev(cur.ascent));
    box[3].label = "ASCENT"; box[3].unit = S.elevUnit();
  }

  int bw = w / 4;
  for (int i = 0; i < 4; i++) {
    int x = i * bw;
    if (i) _lcd->drawFastVLine(x, by + 22, bh - 28, C_LINE);
    _lcd->setFont(&fonts::Font2);
    _lcd->setTextDatum(lgfx::textdatum_t::top_center);
    _lcd->setTextColor(C_DIM, C_BAR);
    _lcd->drawString(box[i].label, x + bw / 2, by + 22);
    _lcd->setFont(&fonts::Font7);
    _lcd->setTextDatum(lgfx::textdatum_t::middle_center);
    _lcd->setTextColor(C_FG, C_BAR);
    float sc = 0.8f;
    while (sc > 0.35f && _lcd->textWidth(box[i].val, &fonts::Font7) * sc > bw - 12) sc -= 0.05f;
    _lcd->setTextSize(sc);
    _lcd->drawString(box[i].val, x + bw / 2, by + 52);
    _lcd->setTextSize(1.0f);
    if (box[i].unit[0]) {
      _lcd->setFont(&fonts::Font2);
      _lcd->setTextDatum(lgfx::textdatum_t::bottom_center);
      _lcd->setTextColor(C_DIM, C_BAR);
      _lcd->drawString(box[i].unit, x + bw / 2, by + bh - 1);
    }
  }

  // --- completed laps ---
  uint8_t n = _rc->lapRecordCount();
  int hy = by + bh + 5;

  if (!n) {
    _lcd->setFont(&fonts::Font4);
    _lcd->setTextDatum(lgfx::textdatum_t::middle_center);
    _lcd->setTextColor(C_DIM, C_BG);
    _lcd->drawString("No completed laps yet", w / 2, hy + 58);
    _lcd->setFont(&fonts::Font2);
    _lcd->drawString(S.autoLapM ? "press LAP, or ride to the auto-lap mark"
                                : "press LAP to mark one", w / 2, hy + 88);
    return;
  }

  // Column right edges, shared by the header and every row.
  const int cLap = 14, cDist = 116, cTime = 198, cAvg = 276, cHr = 336,
            cPwr = 394, cAsc = 466;
  _lcd->setFont(&fonts::Font2);
  _lcd->setTextColor(C_DIM, C_BG);
  _lcd->setTextDatum(lgfx::textdatum_t::top_left);
  _lcd->drawString("LAP", cLap, hy);
  _lcd->setTextDatum(lgfx::textdatum_t::top_right);
  snprintf(b, sizeof(b), "DIST %s", S.distLongUnit()); _lcd->drawString(b, cDist, hy);
  _lcd->drawString("TIME", cTime, hy);
  snprintf(b, sizeof(b), "AVG %s", S.speedUnit());     _lcd->drawString(b, cAvg, hy);
  _lcd->drawString("HR", cHr, hy);
  _lcd->drawString("W", cPwr, hy);
  snprintf(b, sizeof(b), "UP %s", S.elevUnit());       _lcd->drawString(b, cAsc, hy);
  _lcd->drawFastHLine(8, hy + 17, w - 16, C_LINE);

  int ry = hy + 21, rh = 21;
  int maxRows = (h - 18 - ry) / rh;
  int best = _rc->bestLapIndex();

  // Newest first: the lap just finished is the one worth comparing.
  for (int i = 0; i < maxRows && i < n; i++) {
    int idx = n - 1 - i;
    const LapRecord& L = _rc->lapRecord(idx);
    int y = ry + i * rh;
    bool isBest = (idx == best);
    uint16_t bg = isBest ? C_LINE : C_BG;
    if (isBest) _lcd->fillRect(8, y - 1, w - 16, rh - 1, C_LINE);

    _lcd->setFont(&fonts::Font2);
    _lcd->setTextDatum(lgfx::textdatum_t::top_left);
    _lcd->setTextColor(isBest ? C_GOOD : C_FG, bg);
    snprintf(b, sizeof(b), "%u", L.index);
    _lcd->drawString(b, cLap, y);

    _lcd->setTextDatum(lgfx::textdatum_t::top_right);
    snprintf(b, sizeof(b), "%.2f", S.distLong(L.distance)); _lcd->drawString(b, cDist, y);
    fmtTime(L.movingMs, t, sizeof(t));                      _lcd->drawString(t, cTime, y);
    snprintf(b, sizeof(b), "%.1f", S.speed(L.avgSpeed));    _lcd->drawString(b, cAvg, y);

    _lcd->setTextColor(isBest ? C_GOOD : C_DIM, bg);
    L.avgHr    ? snprintf(b, sizeof(b), "%u", L.avgHr)    : snprintf(b, sizeof(b), "-");
    _lcd->drawString(b, cHr, y);
    L.avgPower ? snprintf(b, sizeof(b), "%u", L.avgPower) : snprintf(b, sizeof(b), "-");
    _lcd->drawString(b, cPwr, y);
    snprintf(b, sizeof(b), "%.0f", S.elev(L.ascent));
    _lcd->drawString(b, cAsc, y);
  }

  // --- footer ---
  _lcd->setFont(&fonts::Font2);
  _lcd->setTextDatum(lgfx::textdatum_t::bottom_left);
  _lcd->setTextColor(C_DIM, C_BG);
  uint16_t total = _rc->lapsCompleted();
  if (total > n) snprintf(b, sizeof(b), "%u laps  (showing last %u)", total, n);
  else           snprintf(b, sizeof(b), "%u completed lap%s", total, total == 1 ? "" : "s");
  _lcd->drawString(b, 12, h - 3);

  if (best >= 0) {
    const LapRecord& B = _rc->lapRecord(best);
    _lcd->setTextDatum(lgfx::textdatum_t::bottom_right);
    _lcd->setTextColor(C_GOOD, C_BG);
    snprintf(b, sizeof(b), "best  lap %u at %.1f %s",
             B.index, S.speed(B.avgSpeed), S.speedUnit());
    _lcd->drawString(b, w - 12, h - 3);
  }
}

// --------------------------------------------------------------------------
// Compass page. Track-up: the rose turns under a fixed index at the top, so
// whatever is straight ahead of the bike is always straight up on the screen.
static const int ROSE_R = 108;

void Ui::drawCompass() {
  int top = STATUS_H, w = _lcd->width();
  if (_fullRedraw) _lcd->fillRect(0, top, w, _lcd->height() - top, C_BG);

  int cx = 122, cy = top + 146;
  float hdg = _heading;
  bool haveHdg = !isnan(hdg);

  // Bearing to the ride's start: the first breadcrumb we recorded.
  const RideState& s = _rc->state();
  bool haveStart = _rc->trackCount() > 0 && s.fix.valid;
  float toStart = NAN, distStart = NAN;
  if (haveStart) {
    const TrackPoint* t0 = &_rc->track()[0];
    toStart   = (float)bearingDeg(s.fix.latLon(), t0->latLon());
    distStart = (float)haversine(s.fix.latLon(), t0->latLon());
  }

  // --- rose, into a sprite so rotating it does not strobe the whole page ---
  int d = ROSE_R * 2 + 2;
  if (_rose.width() != d) {
    _rose.deleteSprite();
    _rose.setPsram(true);
    _rose.setColorDepth(16);
    _rose.createSprite(d, d);
  }
  _rose.fillSprite(C_BG);
  int rc = ROSE_R + 1;

  // Everything on the rose is placed by true bearing; the whole dial is then
  // offset by the heading, which is what makes it track-up.
  auto at = [&](float bearing, float radius, int& px, int& py) {
    float a = radians(bearing - (haveHdg ? hdg : 0.0f));
    px = rc + (int)(sinf(a) * radius);
    py = rc - (int)(cosf(a) * radius);
  };

  _rose.drawCircle(rc, rc, ROSE_R, C_LINE);
  _rose.drawCircle(rc, rc, ROSE_R - 1, C_LINE);

  for (int b = 0; b < 360; b += 15) {
    int x0, y0, x1, y1;
    bool major = (b % 45) == 0;
    at(b, ROSE_R - 2, x0, y0);
    at(b, ROSE_R - (major ? 16 : 8), x1, y1);
    _rose.drawLine(x0, y0, x1, y1, major ? C_FG : C_LINE);
  }

  static const char* kPts[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  _rose.setFont(&fonts::Font4);
  _rose.setTextDatum(lgfx::textdatum_t::middle_center);
  for (int i = 0; i < 8; i++) {
    int px, py;
    at(i * 45.0f, ROSE_R - 32, px, py);
    _rose.setTextColor(i == 0 ? C_BAD : C_DIM, C_BG);
    _rose.drawString(kPts[i], px, py);
  }

  // The start, marked on the ring where it actually lies.
  if (!isnan(toStart) && haveHdg) {
    int px, py;
    at(toStart, ROSE_R - 54, px, py);
    _rose.fillCircle(px, py, 7, C_GOOD);
    _rose.setFont(&fonts::Font2);
    _rose.setTextColor(C_BG, C_GOOD);
    _rose.drawString("S", px, py);
    thickLine(&_rose, rc, rc, px, py, 4, C_GOOD);
    arrowHead(&_rose, px, py, toStart - hdg, 10, C_GOOD);
  }

  // Heading readout in the middle of the dial.
  _rose.setTextDatum(lgfx::textdatum_t::middle_center);
  if (haveHdg) {
    char b[8];
    snprintf(b, sizeof(b), "%03d", (int)lroundf(hdg) % 360);
    _rose.setFont(&fonts::Font7);
    _rose.setTextColor(C_FG, C_BG);
    _rose.setTextSize(0.9f);
    _rose.drawString(b, rc, rc - 6);
    _rose.setTextSize(1.0f);
    _rose.setFont(&fonts::Font4);
    _rose.setTextColor(C_ACCENT, C_BG);
    _rose.drawString(cardinalName(hdg), rc, rc + 30);
  } else {
    _rose.setFont(&fonts::Font4);
    _rose.setTextColor(C_DIM, C_BG);
    _rose.drawString("no heading", rc, rc - 8);
    _rose.setFont(&fonts::Font2);
    _rose.drawString(_compass->present() ? "calibrate the compass"
                                         : "start moving", rc, rc + 18);
  }
  _rose.pushSprite(_lcd, cx - rc, cy - rc);

  // Fixed index above the dial - this is the bike, and it never moves.
  _lcd->fillTriangle(cx, cy - ROSE_R - 2, cx - 9, cy - ROSE_R - 18,
                     cx + 9, cy - ROSE_R - 18, C_ACCENT);

  // --- right-hand panel ---
  int px0 = 252, pw = w - px0 - 8;
  _lcd->fillRect(px0, top, pw + 8, _lcd->height() - top, C_BG);
  char b[48];
  int y = top + 6;

  auto label = [&](const char* t, uint16_t col) {
    _lcd->setFont(&fonts::Font2);
    _lcd->setTextDatum(lgfx::textdatum_t::top_left);
    _lcd->setTextColor(col, C_BG);
    _lcd->drawString(t, px0, y);
    y += 17;
  };
  auto big = [&](const char* t, uint16_t col) {
    _lcd->setFont(&fonts::Font7);
    _lcd->setTextDatum(lgfx::textdatum_t::top_left);
    _lcd->setTextColor(col, C_BG);
    float sc = 0.85f;
    while (sc > 0.4f && _lcd->textWidth(t, &fonts::Font7) * sc > pw) sc -= 0.05f;
    _lcd->setTextSize(sc);
    _lcd->drawString(t, px0, y);
    _lcd->setTextSize(1.0f);
    y += (int)(48 * sc) + 4;
  };

  label(_headingFromMag ? "HEADING  magnetometer" : "HEADING  GPS course", C_DIM);
  if (haveHdg) {
    snprintf(b, sizeof(b), "%03d", (int)lroundf(hdg) % 360);
    big(b, C_FG);
    snprintf(b, sizeof(b), "%s true", cardinalName(hdg));
    label(b, C_ACCENT);
  } else {
    big("---", C_DIM);
    label(_compass->present() ? "uncalibrated" : "GPS only, needs movement", C_WARN);
  }

  y += 10;
  _lcd->drawFastHLine(px0, y, pw, C_LINE);
  y += 8;

  label("TO START", C_DIM);
  if (haveStart) {
    fmtDist(distStart, b, sizeof(b));
    big(b, C_GOOD);
    if (haveHdg) {
      float rel = toStart - hdg;
      while (rel > 180) rel -= 360;
      while (rel < -180) rel += 360;
      drawBearingArrow(_lcd, px0 + 22, y + 20, 20, rel, C_GOOD);
      _lcd->setFont(&fonts::Font2);
      _lcd->setTextDatum(lgfx::textdatum_t::middle_left);
      _lcd->setTextColor(C_FG, C_BG);
      snprintf(b, sizeof(b), "%03d  %s", (int)lroundf(toStart) % 360, clockDirection(rel));
      _lcd->drawString(b, px0 + 50, y + 14);
      _lcd->setTextColor(C_DIM, C_BG);
      _lcd->drawString(cardinalName(toStart), px0 + 50, y + 32);
    } else {
      snprintf(b, sizeof(b), "bearing %03d %s", (int)lroundf(toStart) % 360,
               cardinalName(toStart));
      label(b, C_DIM);
    }
  } else {
    big("--", C_DIM);
    label(s.fix.valid ? "no ride started yet" : "waiting for GPS", C_DIM);
  }

  // Straight line home is not the way home - say so where it is relevant.
  _lcd->setFont(&fonts::Font2);
  _lcd->setTextDatum(lgfx::textdatum_t::bottom_left);
  _lcd->setTextColor(C_DIM, C_BG);
  if (haveStart && distStart > 200)
    _lcd->drawString("straight line - Menu > Navigate back to start to retrace",
                     10, _lcd->height() - 4);
  else if (!_compass->present())
    _lcd->drawString("no magnetometer fitted - heading comes from GPS course",
                     10, _lcd->height() - 4);
}

// Full-screen modal: spin the unit through a turn while we watch the extremes.
void Ui::drawCompassCal() {
  int w = _lcd->width(), h = _lcd->height();
  _lcd->fillScreen(C_BG);
  _lcd->setFont(&fonts::Font4);
  _lcd->setTextDatum(lgfx::textdatum_t::top_center);
  _lcd->setTextColor(C_ACCENT, C_BG);
  _lcd->drawString("COMPASS CALIBRATION", w / 2, 10);

  _lcd->setFont(&fonts::Font2);
  _lcd->setTextColor(C_FG, C_BG);
  _lcd->drawString("Hold the unit level and turn slowly through a full circle",
                   w / 2, 40);
  _lcd->setTextColor(C_DIM, C_BG);
  _lcd->drawString("Away from your phone, the battery and anything steel",
                   w / 2, 60);

  // The dial fills in as each 30 degree sector is visited.
  int cx = w / 2, cy = 190, r = 78;
  uint16_t mask = _compass->sectorMask();
  for (int i = 0; i < 12; i++) {
    bool got = mask & (1u << i);
    float a0 = i * 30.0f - 90.0f;
    _lcd->fillArc(cx, cy, r - 22, r, a0 + 2, a0 + 28, got ? C_GOOD : C_LINE);
  }
  uint8_t cov = _compass->coverage();
  char b[40];
  snprintf(b, sizeof(b), "%u%%", cov);
  _lcd->setFont(&fonts::Font7);
  _lcd->setTextDatum(lgfx::textdatum_t::middle_center);
  _lcd->setTextColor(cov >= 75 ? C_GOOD : C_WARN, C_BG);
  _lcd->setTextSize(0.9f);
  _lcd->drawString(b, cx, cy);
  _lcd->setTextSize(1.0f);

  _lcd->setFont(&fonts::Font2);
  _lcd->setTextDatum(lgfx::textdatum_t::top_center);
  _lcd->setTextColor(C_DIM, C_BG);
  snprintf(b, sizeof(b), "raw  x %6d   y %6d   z %6d",
           _compass->rawX(), _compass->rawY(), _compass->rawZ());
  _lcd->drawString(b, cx, cy + r + 12);

  _lcd->setTextDatum(lgfx::textdatum_t::bottom_center);
  if (cov >= 75) {
    _lcd->setTextColor(C_GOOD, C_BG);
    _lcd->drawString("ENTER to save      BACK to cancel", cx, h - 8);
  } else {
    _lcd->setTextColor(C_WARN, C_BG);
    _lcd->drawString("keep turning - 75% of the circle is needed", cx, h - 8);
  }
}

// --------------------------------------------------------------------------
void Ui::drawWx() {
  int top = STATUS_H, w = _lcd->width(), h = _lcd->height() - top;
  _lcd->fillRect(0, top, w, h, C_BG);

  WeatherNow n = _wx->now();
  if (!n.valid) {
    _lcd->setFont(&fonts::Font4);
    _lcd->setTextDatum(lgfx::textdatum_t::middle_center);
    _lcd->setTextColor(C_DIM, C_BG);
    _lcd->drawString("No weather yet", w / 2, top + 100);
    _lcd->setFont(&fonts::Font2);
    _lcd->drawString("Connect the companion page over Bluetooth", w / 2, top + 134);
    _lcd->drawString("- it fetches a forecast for wherever you are", w / 2, top + 154);
    return;
  }

  bool old = _wx->stale();
  uint16_t fg = old ? C_DIM : C_FG;
  char b[64];
  const Settings& S = g_settings;

  // --- current conditions, left half ---
  drawWeatherIcon(_lcd, 46, top + 46, 32, Weather::iconFor(n.code), old ? C_DIM : C_ACCENT);

  _lcd->setFont(&fonts::Font7);
  _lcd->setTextDatum(lgfx::textdatum_t::middle_left);
  _lcd->setTextColor(fg, C_BG);
  snprintf(b, sizeof(b), "%.0f", isnan(n.tempC) ? 0.0f : S.temp(n.tempC));
  _lcd->setTextSize(1.3f);
  _lcd->drawString(b, 92, top + 42);
  int tw = _lcd->textWidth(b, &fonts::Font7) * 1.3f;
  _lcd->setTextSize(1.0f);
  _lcd->setFont(&fonts::Font4);
  _lcd->setTextDatum(lgfx::textdatum_t::top_left);
  _lcd->drawString(S.tempUnit(), 96 + tw, top + 18);

  _lcd->setFont(&fonts::Font2);
  _lcd->setTextColor(old ? C_DIM : C_ACCENT, C_BG);
  _lcd->drawString(n.desc[0] ? n.desc : Weather::textFor(n.code), 92, top + 70);
  _lcd->setTextColor(C_DIM, C_BG);
  int y = top + 90;
  if (!isnan(n.feelsC)) {
    snprintf(b, sizeof(b), "feels %.0f %s", S.temp(n.feelsC), S.tempUnit());
    _lcd->drawString(b, 14, y); y += 18;
  }
  if (!isnan(n.humidity)) {
    snprintf(b, sizeof(b), "humidity %.0f%%", n.humidity);
    _lcd->drawString(b, 14, y); y += 18;
  }
  if (!isnan(n.pressureHpa)) {
    snprintf(b, sizeof(b), "%.0f hPa", n.pressureHpa);
    _lcd->drawString(b, 14, y); y += 18;
  }

  // --- wind, right half: the part that actually changes how a ride feels ---
  int cx = w - 108, cy = top + 62, r = 46;
  _lcd->drawCircle(cx, cy, r, C_LINE);
  _lcd->drawCircle(cx, cy, r - 1, C_LINE);
  // the rider, always pointing up
  _lcd->fillTriangle(cx, cy - 11, cx - 7, cy + 8, cx + 7, cy + 8, C_DIM);

  float rel = _wx->relativeWindDeg(_heading);
  float hw  = _wx->headwind(_heading);
  float cw  = _wx->crosswind(_heading);

  if (!isnan(rel)) {
    // Arrow points the way the air is travelling, so a headwind draws an arrow
    // coming down at you from the top of the dial.
    float blow = rel + 180.0f;
    float a = radians(blow);
    uint16_t wc = hw > 1.0f ? C_BAD : (hw < -1.0f ? C_GOOD : C_WARN);
    float sx = cx - sinf(a) * (r - 6), sy = cy + cosf(a) * (r - 6);
    float ex = cx + sinf(a) * (r - 18), ey = cy - cosf(a) * (r - 18);
    thickLine(_lcd, sx, sy, ex, ey, 5, wc);
    arrowHead(_lcd, ex, ey, blow, 11, wc);
  } else {
    _lcd->setFont(&fonts::Font2);
    _lcd->setTextDatum(lgfx::textdatum_t::middle_center);
    _lcd->setTextColor(C_DIM, C_BG);
    _lcd->drawString("no heading", cx, cy + r + 12);
  }

  _lcd->setFont(&fonts::Font2);
  _lcd->setTextDatum(lgfx::textdatum_t::top_right);
  _lcd->setTextColor(C_DIM, C_BG);
  y = top + 8;
  if (!isnan(n.windMps)) {
    snprintf(b, sizeof(b), "wind %.0f %s", S.speed(n.windMps), S.speedUnit());
    _lcd->drawString(b, w - 8, y); y += 18;
  }
  if (!isnan(n.gustMps)) {
    snprintf(b, sizeof(b), "gusts %.0f", S.speed(n.gustMps));
    _lcd->drawString(b, w - 8, y); y += 18;
  }
  if (!isnan(hw)) {
    _lcd->setFont(&fonts::Font4);
    _lcd->setTextColor(hw > 1.0f ? C_BAD : (hw < -1.0f ? C_GOOD : C_WARN), C_BG);
    snprintf(b, sizeof(b), "%s %.0f", hw > 0 ? "HEAD" : "TAIL", S.speed(fabsf(hw)));
    _lcd->drawString(b, w - 8, y); y += 24;
    _lcd->setFont(&fonts::Font2);
    _lcd->setTextColor(C_DIM, C_BG);
    snprintf(b, sizeof(b), "cross %.0f from %s", S.speed(fabsf(cw)),
             cw > 0 ? "right" : "left");
    _lcd->drawString(b, w - 8, y);
  }

  // --- hourly strip ---
  int sy = top + 150, sh = 96;
  _lcd->drawFastHLine(8, sy - 6, w - 16, C_LINE);
  uint8_t hn = _wx->hourCount();
  if (hn >= 2) {
    float lo = 1e9f, hi = -1e9f, maxPr = 0.4f;
    for (uint8_t i = 0; i < hn; i++) {
      WeatherHour hh = _wx->hour(i);
      if (!hh.valid || isnan(hh.tempC)) continue;
      lo = min(lo, hh.tempC); hi = max(hi, hh.tempC);
      if (!isnan(hh.precipMm)) maxPr = max(maxPr, hh.precipMm);
    }
    if (hi - lo < 3) { float m = (hi + lo) / 2; lo = m - 1.5f; hi = m + 1.5f; }

    int colW = (w - 16) / hn;
    int gTop = sy + 16, gH = sh - 44;
    int prevX = -1, prevY = 0;

    // Local hour labels need the clock; without a fix they become +1h, +2h.
    const RideState& s = _rc->state();
    struct tm tmv;
    bool haveClock = s.fix.timeValid;
    if (haveClock) { time_t t = (time_t)s.fix.unixTime; localtime_r(&t, &tmv); }

    for (uint8_t i = 0; i < hn; i++) {
      WeatherHour hh = _wx->hour(i);
      int x = 8 + i * colW + colW / 2;

      // precipitation bar behind everything
      if (hh.valid && !isnan(hh.precipMm) && hh.precipMm > 0.01f) {
        int ph = (int)(gH * min(1.0f, hh.precipMm / maxPr));
        _lcd->fillRect(x - colW / 3, gTop + gH - ph, colW * 2 / 3, ph, C_COURSE);
      }
      if (hh.valid && !isnan(hh.tempC)) {
        int ty = gTop + gH - (int)((hh.tempC - lo) / (hi - lo) * gH);
        if (prevX >= 0) {
          _lcd->drawLine(prevX, prevY, x, ty, C_ACCENT);
          _lcd->drawLine(prevX, prevY + 1, x, ty + 1, C_ACCENT);
        }
        _lcd->fillCircle(x, ty, 2, C_ACCENT);
        prevX = x; prevY = ty;
      }

      _lcd->setFont(&fonts::Font0);
      _lcd->setTextDatum(lgfx::textdatum_t::top_center);
      _lcd->setTextColor(C_DIM, C_BG);
      if (haveClock) snprintf(b, sizeof(b), "%02d", (tmv.tm_hour + i) % 24);
      else           snprintf(b, sizeof(b), "+%u", i);
      _lcd->drawString(b, x, sy);
      if (hh.valid && !isnan(hh.tempC)) {
        snprintf(b, sizeof(b), "%.0f", S.temp(hh.tempC));
        _lcd->setTextDatum(lgfx::textdatum_t::bottom_center);
        _lcd->setTextColor(fg, C_BG);
        _lcd->drawString(b, x, sy + sh - 12);
      }
    }
    _lcd->setFont(&fonts::Font0);
    _lcd->setTextDatum(lgfx::textdatum_t::top_left);
    _lcd->setTextColor(C_COURSE, C_BG);
    snprintf(b, sizeof(b), "rain to %.1f mm/h", maxPr);
    _lcd->drawString(b, 10, sy);
  }

  // --- footer: daylight, rain warning, freshness ---
  _lcd->setFont(&fonts::Font2);
  _lcd->setTextDatum(lgfx::textdatum_t::bottom_left);
  const RideState& s = _rc->state();
  if (n.sunsetUnix && s.fix.timeValid) {
    time_t t = (time_t)n.sunsetUnix;
    struct tm tmv; localtime_r(&t, &tmv);
    long left = (long)n.sunsetUnix - (long)s.fix.unixTime;
    if (left > 0) snprintf(b, sizeof(b), "sunset %02d:%02d  (%ldh %02ldm of light)",
                           tmv.tm_hour, tmv.tm_min, left / 3600, (left % 3600) / 60);
    else          snprintf(b, sizeof(b), "sunset was %02d:%02d", tmv.tm_hour, tmv.tm_min);
    _lcd->setTextColor(left > 0 && left < 3600 ? C_WARN : C_DIM, C_BG);
    _lcd->drawString(b, 10, _lcd->height() - 4);
  }

  _lcd->setTextDatum(lgfx::textdatum_t::bottom_right);
  int16_t rain = _wx->minutesToRain();
  if (rain == 0) {
    _lcd->setTextColor(C_BAD, C_BG);
    _lcd->drawString("raining now", w - 10, _lcd->height() - 4);
  } else if (rain > 0 && rain <= WEATHER_RAIN_ALERT_MIN) {
    _lcd->setTextColor(C_WARN, C_BG);
    snprintf(b, sizeof(b), "rain in ~%d min", rain);
    _lcd->drawString(b, w - 10, _lcd->height() - 4);
  } else {
    _lcd->setTextColor(old ? C_WARN : C_DIM, C_BG);
    snprintf(b, sizeof(b), old ? "stale - updated %lu min ago" : "updated %lu min ago",
             (unsigned long)_wx->ageMinutes());
    _lcd->drawString(b, w - 10, _lcd->height() - 4);
  }
}

// --------------------------------------------------------------------------
// Temperature and pressure history from the on-board sensor. Two stacked
// graphs sharing an x axis: temperature is what you feel, pressure is what
// tells you whether the afternoon is about to go wrong.
void Ui::drawWxHistory() {
  const Settings& S = g_settings;
  int top = STATUS_H, w = _lcd->width(), h = _lcd->height();
  _lcd->fillRect(0, top, w, h - top, C_BG);
  char b[64];

  if (!_baro->present()) {
    _lcd->setFont(&fonts::Font4);
    _lcd->setTextDatum(lgfx::textdatum_t::middle_center);
    _lcd->setTextColor(C_DIM, C_BG);
    _lcd->drawString("No barometer fitted", w / 2, top + 110);
    _lcd->setFont(&fonts::Font2);
    _lcd->drawString("A BME280 on the I2C bus records this history", w / 2, top + 145);
    return;
  }

  uint16_t n = g_env.count();
  float chg; uint16_t span;
  bool haveTrend = g_env.pressureTrend(chg, span);

  // --- header ---
  int hy = top, hh = 56;
  _lcd->fillRect(0, hy, w, hh, C_BAR);
  _lcd->setFont(&fonts::Font2);
  _lcd->setTextDatum(lgfx::textdatum_t::top_left);
  _lcd->setTextColor(C_DIM, C_BAR);
  _lcd->drawString("ON-BOARD SENSOR", 12, hy + 3);

  float tNow = _baro->temperature();
  snprintf(b, sizeof(b), "%.0f", S.temp(tNow));
  _lcd->setFont(&fonts::Font7);
  _lcd->setTextDatum(lgfx::textdatum_t::middle_left);
  _lcd->setTextColor(C_FG, C_BAR);
  _lcd->setTextSize(1.0f);
  _lcd->drawString(b, 12, hy + 32);
  int tw = _lcd->textWidth(b, &fonts::Font7);
  _lcd->setFont(&fonts::Font2);
  _lcd->setTextColor(C_DIM, C_BAR);
  _lcd->drawString(S.tempUnit(), 18 + tw, hy + 38);

  int x2 = 100;
  _lcd->setTextDatum(lgfx::textdatum_t::top_left);
  if (!isnan(g_env.minTemp())) {
    snprintf(b, sizeof(b), "low %.0f   high %.0f",
             S.temp(g_env.minTemp()), S.temp(g_env.maxTemp()));
    _lcd->setTextColor(C_DIM, C_BAR);
    _lcd->drawString(b, x2, hy + 20);
  }
  if (!isnan(_baro->humidity())) {
    snprintf(b, sizeof(b), "humidity %.0f%%", _baro->humidity());
    _lcd->drawString(b, x2, hy + 36);
  }

  // Pressure and its tendency, which is the part that forecasts anything.
  _lcd->setTextDatum(lgfx::textdatum_t::top_right);
  snprintf(b, sizeof(b), "%.1f hPa", _baro->pressure());
  _lcd->setFont(&fonts::Font4);
  _lcd->setTextColor(C_FG, C_BAR);
  _lcd->drawString(b, w - 12, hy + 14);

  _lcd->setFont(&fonts::Font2);
  if (haveTrend) {
    const char* word = g_env.trendWord();
    uint16_t tc = chg < -1.0f ? C_BAD : chg > 1.0f ? C_GOOD : C_DIM;
    if (word[0]) snprintf(b, sizeof(b), "%+.1f hPa in %uh%02u  -  %s",
                          chg, span / 60, span % 60, word);
    else         snprintf(b, sizeof(b), "%+.1f hPa in %uh%02u", chg, span / 60, span % 60);
    _lcd->setTextColor(tc, C_BAR);
    _lcd->drawString(b, w - 12, hy + 38);
  } else {
    _lcd->setTextColor(C_DIM, C_BAR);
    _lcd->drawString("trend needs a few minutes", w - 12, hy + 38);
  }

  if (n < 2) {
    _lcd->setFont(&fonts::Font4);
    _lcd->setTextDatum(lgfx::textdatum_t::middle_center);
    _lcd->setTextColor(C_DIM, C_BG);
    _lcd->drawString("Collecting history", w / 2, top + 150);
    _lcd->setFont(&fonts::Font2);
    _lcd->drawString("one sample a minute, eight hours kept", w / 2, top + 180);
    return;
  }

  // --- ranges ---
  float tLo = 1e9f, tHi = -1e9f, pLo = 1e9f, pHi = -1e9f;
  for (uint16_t i = 0; i < n; i++) {
    float t, p, rh;
    g_env.get(i, t, p, rh);
    if (!isnan(t)) { tLo = min(tLo, t); tHi = max(tHi, t); }
    if (!isnan(p)) { pLo = min(pLo, p); pHi = max(pHi, p); }
  }
  if (tLo > tHi) { tLo = 0; tHi = 1; }
  if (tHi - tLo < 4.0f) { float m = (tHi + tLo) / 2; tLo = m - 2; tHi = m + 2; }
  if (pLo > pHi) { pLo = 1000; pHi = 1001; }
  if (pHi - pLo < 2.0f) { float m = (pHi + pLo) / 2; pLo = m - 1; pHi = m + 1; }

  const int gl = 40, gr = 10;
  int gw = w - gl - gr;

  auto plot = [&](int gy, int gh, bool temperature, uint16_t col, const char* label) {
    _lcd->drawFastVLine(gl, gy, gh, C_LINE);
    _lcd->drawFastHLine(gl, gy + gh, gw, C_LINE);
    _lcd->setFont(&fonts::Font0);
    _lcd->setTextDatum(lgfx::textdatum_t::middle_right);
    _lcd->setTextColor(C_DIM, C_BG);
    char t[12];
    float lo = temperature ? tLo : pLo, hi = temperature ? tHi : pHi;
    snprintf(t, sizeof(t), "%.0f", temperature ? S.temp(hi) : hi);
    _lcd->drawString(t, gl - 3, gy + 4);
    snprintf(t, sizeof(t), "%.0f", temperature ? S.temp(lo) : lo);
    _lcd->drawString(t, gl - 3, gy + gh - 4);

    _lcd->setFont(&fonts::Font2);
    _lcd->setTextDatum(lgfx::textdatum_t::top_left);
    _lcd->setTextColor(col, C_BG);
    _lcd->drawString(label, gl + 4, gy);

    int px = -1, py = 0;
    for (uint16_t i = 0; i < n; i++) {
      float tv, pv, rh;
      g_env.get(i, tv, pv, rh);
      float v = temperature ? tv : pv;
      if (isnan(v)) { px = -1; continue; }
      int x = gl + (n > 1 ? (int)((int32_t)i * (gw - 1) / (n - 1)) : 0);
      int y = gy + gh - (int)((v - lo) / (hi - lo) * gh);
      if (px >= 0) {
        _lcd->drawLine(px, py, x, y, col);
        _lcd->drawLine(px, py + 1, x, y + 1, col);
      }
      px = x; py = y;
    }
  };

  int ty = hy + hh + 6, th = 122;
  int py2 = ty + th + 10, ph = 72;

  char lbl[32];
  snprintf(lbl, sizeof(lbl), "TEMPERATURE (%s)", S.tempUnit());
  plot(ty, th, true, C_ACCENT, lbl);

  // The forecast's air temperature, for reference: the on-board sensor sits in
  // the sun and next to a battery, so the two rarely agree.
  WeatherNow wn = _wx->now();
  if (wn.valid && !isnan(wn.tempC) && wn.tempC >= tLo && wn.tempC <= tHi) {
    int y = ty + th - (int)((wn.tempC - tLo) / (tHi - tLo) * th);
    for (int x = gl; x < gl + gw; x += 6) _lcd->drawFastHLine(x, y, 3, C_COURSE);
    _lcd->setFont(&fonts::Font0);
    _lcd->setTextDatum(lgfx::textdatum_t::bottom_right);
    _lcd->setTextColor(C_COURSE, C_BG);
    _lcd->drawString("forecast air", gl + gw, y - 1);
  }

  plot(py2, ph, false, C_GOOD, "PRESSURE (hPa)");

  // --- footer ---
  _lcd->setFont(&fonts::Font2);
  _lcd->setTextDatum(lgfx::textdatum_t::bottom_left);
  _lcd->setTextColor(C_DIM, C_BG);
  uint16_t mins = g_env.spanMinutes();
  snprintf(b, sizeof(b), "%uh%02u of history   %u samples", mins / 60, mins % 60, n);
  _lcd->drawString(b, 12, h - 3);

  _lcd->setTextDatum(lgfx::textdatum_t::bottom_right);
  if (haveTrend && chg <= -2.0f) {
    _lcd->setTextColor(C_WARN, C_BG);
    _lcd->drawString("pressure dropping - weather may turn", w - 12, h - 3);
  } else {
    _lcd->setTextColor(C_DIM, C_BG);
    _lcd->drawString("one sample a minute", w - 12, h - 3);
  }
}

// --------------------------------------------------------------------------
// Colour bands for gradient, the scheme every climbing app uses: green is
// nothing, red is a wall. Descents get their own colour rather than being
// lumped in with flat.
static uint16_t gradeColour(float pct) {
  if (isnan(pct)) return C_LINE;
  if (pct < -1.0f) return 0x2D7F;   // descending, blue
  if (pct <  3.0f) return 0x05E0;   // green
  if (pct <  6.0f) return 0xFFE0;   // yellow
  if (pct <  9.0f) return 0xFD20;   // orange
  if (pct < 12.0f) return 0xF800;   // red
  return 0xF81F;                    // magenta, the silly stuff
}

// Elevation against distance. Plotting against time would give ten minutes at
// a cafe a sixth of the graph and squash a descent into nothing.
void Ui::drawProfile() {
  const Settings& S = g_settings;
  const RideState& s = _rc->state();
  int top = STATUS_H, h = _lcd->height() - STATUS_H, w = _lcd->width();
  _lcd->fillRect(0, top, w, h, C_BG);
  char b[80];

  bool onCourse = _course->loaded();

  // --- how far the graph spans, and a reader for elevation at a distance ---
  const ElevationProfile& prof = _rc->profile();
  float spanM = onCourse ? _course->totalDistance() : prof.coveredM();

  if (spanM < 10.0f) {
    _lcd->setFont(&fonts::Font4);
    _lcd->setTextDatum(lgfx::textdatum_t::middle_center);
    _lcd->setTextColor(C_DIM, C_BG);
    _lcd->drawString("No profile yet", w / 2, top + 110);
    _lcd->setFont(&fonts::Font2);
    _lcd->drawString(_baro->present() ? "ride a little, or load a course"
                                      : "no barometer - elevation comes from GPS",
                     w / 2, top + 145);
    return;
  }

  // Sampling by pixel column keeps the two sources on one code path.
  auto eleAt = [&](float dist) -> float {
    if (onCourse) return _course->courseElevationAt(dist);
    if (prof.intervalM() <= 0) return NAN;
    float fi = dist / prof.intervalM();
    uint16_t i = (uint16_t)fi;
    if (i + 1 >= prof.count()) return prof.elevationAt(prof.count() ? prof.count() - 1 : 0);
    float a = prof.elevationAt(i), c = prof.elevationAt(i + 1);
    if (isnan(a) || isnan(c)) return NAN;
    return a + (c - a) * (fi - i);
  };

  const int gL = 42, gR = 10;
  int gW = w - gL - gR;
  int gTop = top + 44, gH = h - 44 - 26;

  // --- vertical range over the whole span ---
  float lo = 1e9f, hi = -1e9f;
  for (int x = 0; x < gW; x++) {
    float e = eleAt(spanM * x / (gW - 1));
    if (isnan(e)) continue;
    lo = min(lo, e); hi = max(hi, e);
  }
  if (lo > hi) {
    _lcd->setFont(&fonts::Font4);
    _lcd->setTextDatum(lgfx::textdatum_t::middle_center);
    _lcd->setTextColor(C_DIM, C_BG);
    _lcd->drawString(onCourse ? "Course has no elevation data"
                              : "No elevation recorded", w / 2, top + 120);
    return;
  }
  if (hi - lo < 20.0f) { float m = (hi + lo) / 2; lo = m - 10; hi = m + 10; }

  // --- header ---
  _lcd->setFont(&fonts::Font2);
  _lcd->setTextDatum(lgfx::textdatum_t::top_left);
  _lcd->setTextColor(C_ACCENT, C_BG);
  _lcd->drawString(onCourse ? "COURSE PROFILE" : "RIDE PROFILE", 12, top + 3);

  _lcd->setTextDatum(lgfx::textdatum_t::top_right);
  _lcd->setTextColor(C_DIM, C_BG);
  char dist[20];
  fmtDist(spanM, dist, sizeof(dist));
  snprintf(b, sizeof(b), "%s   %.0f-%.0f %s", dist,
           S.elev(lo), S.elev(hi), S.elevUnit());
  _lcd->drawString(b, w - 12, top + 3);

  _lcd->setFont(&fonts::Font4);
  _lcd->setTextDatum(lgfx::textdatum_t::top_left);
  _lcd->setTextColor(C_FG, C_BG);
  snprintf(b, sizeof(b), "%.0f %s", S.elev(isnan(s.altitude) ? 0 : s.altitude), S.elevUnit());
  _lcd->drawString(b, 12, top + 19);

  _lcd->setTextDatum(lgfx::textdatum_t::top_right);
  _lcd->setTextColor(gradeColour(s.grade), C_BG);
  snprintf(b, sizeof(b), "%+.1f%%", s.grade);
  _lcd->drawString(b, w - 12, top + 19);

  _lcd->setFont(&fonts::Font2);
  _lcd->setTextDatum(lgfx::textdatum_t::top_center);
  _lcd->setTextColor(C_DIM, C_BG);
  if (onCourse)
    snprintf(b, sizeof(b), "climbed %.0f   %.0f %s to go",
             S.elev(s.ascent), S.elev(_course->ascentRemaining()), S.elevUnit());
  else
    snprintf(b, sizeof(b), "up %.0f   down %.0f %s",
             S.elev(s.ascent), S.elev(s.descent), S.elevUnit());
  _lcd->drawString(b, w / 2, top + 25);

  // --- axes ---
  _lcd->drawFastVLine(gL, gTop, gH, C_LINE);
  _lcd->drawFastHLine(gL, gTop + gH, gW, C_LINE);
  _lcd->setFont(&fonts::Font0);
  _lcd->setTextDatum(lgfx::textdatum_t::middle_right);
  _lcd->setTextColor(C_DIM, C_BG);
  snprintf(b, sizeof(b), "%.0f", S.elev(hi)); _lcd->drawString(b, gL - 3, gTop + 4);
  snprintf(b, sizeof(b), "%.0f", S.elev(lo)); _lcd->drawString(b, gL - 3, gTop + gH - 4);

  // --- the profile, one filled column per pixel, coloured by gradient ---
  float posM = onCourse ? _course->alongDistance() : spanM;
  int posX = gL + (int)((gW - 1) * constrain(posM / spanM, 0.0f, 1.0f));

  int prevY = -1;
  for (int x = 0; x < gW; x++) {
    float d = spanM * x / (gW - 1);
    float e = eleAt(d);
    if (isnan(e)) { prevY = -1; continue; }
    int px = gL + x;
    int y = gTop + gH - (int)((e - lo) / (hi - lo) * gH);

    // Gradient measured over a fixed ground distance, not a fixed pixel count,
    // so the colouring means the same thing at every zoom level.
    float grade;
    if (onCourse) {
      float d0 = max(0.0f, d - 50.0f), d1 = min(spanM, d + 50.0f);
      float e0 = eleAt(d0), e1 = eleAt(d1);
      grade = (isnan(e0) || isnan(e1) || d1 <= d0) ? NAN : (e1 - e0) / (d1 - d0) * 100.0f;
    } else {
      grade = prof.gradeAt((uint16_t)(d / max(1.0f, prof.intervalM())));
    }

    uint16_t col = gradeColour(grade);
    // Ridden ground is filled solid; ground still ahead is drawn dimmer, so a
    // course shows progress without a second chart.
    bool ahead = onCourse && d > posM;
    _lcd->drawFastVLine(px, y, gTop + gH - y, ahead ? C_BAR : col);
    if (ahead) _lcd->drawFastVLine(px, y, 2, col);
    if (prevY >= 0 && abs(y - prevY) > 1) {
      int y0 = min(y, prevY), y1 = max(y, prevY);
      _lcd->drawFastVLine(px, y0, y1 - y0, col);
    }
    prevY = y;
  }

  // Where you are, on a course.
  if (onCourse) {
    _lcd->drawFastVLine(posX, gTop, gH, C_FG);
    _lcd->fillTriangle(posX, gTop - 1, posX - 4, gTop - 8, posX + 4, gTop - 8, C_FG);
  }

  // --- gradient key ---
  static const struct { float g; const char* t; } kKey[] = {
    {-2, "down"}, {1, "0-3"}, {4, "3-6"}, {7, "6-9"}, {10, "9-12"}, {14, "12+"}};
  int kx = gL + 4, ky = gTop + gH + 6;
  _lcd->setFont(&fonts::Font0);
  _lcd->setTextDatum(lgfx::textdatum_t::top_left);
  for (auto& k : kKey) {
    _lcd->fillRect(kx, ky + 1, 10, 8, gradeColour(k.g));
    _lcd->setTextColor(C_DIM, C_BG);
    _lcd->drawString(k.t, kx + 13, ky + 1);
    kx += 13 + _lcd->textWidth(k.t) + 10;
  }

  // --- footer ---
  _lcd->setFont(&fonts::Font2);
  _lcd->setTextDatum(lgfx::textdatum_t::bottom_right);
  _lcd->setTextColor(C_DIM, C_BG);
  if (onCourse) {
    fmtDist(_course->distanceRemaining(), dist, sizeof(dist));
    snprintf(b, sizeof(b), "%s remaining", dist);
  } else {
    // The interval is worth showing: it is how the graph stays full-width on a
    // 300 km ride without a buffer sized for one.
    snprintf(b, sizeof(b), "%u points every %.0f %s", prof.count(),
             S.distShort(prof.intervalM()), S.distShortUnit());
  }
  _lcd->drawString(b, w - 12, h + top - 3);
}

void Ui::drawStatus() {
  int top = STATUS_H, w = _lcd->width();
  _lcd->fillRect(0, top, w, _lcd->height() - top, C_BG);
  _lcd->setFont(&fonts::Font2);
  _lcd->setTextDatum(lgfx::textdatum_t::top_left);

  const RideState& s = _rc->state();
  int y = top + 4;
  auto row = [&](const char* k, const char* v, uint16_t col) {
    _lcd->setTextColor(C_DIM, C_BG);  _lcd->drawString(k, 10, y);
    _lcd->setTextColor(col, C_BG);    _lcd->drawString(v, 170, y);
    y += 19;
  };
  char b[80];

  const Settings& S = g_settings;
  snprintf(b, sizeof(b), "%s  %u sats  hAcc %.1f %s  pDOP %.1f",
           s.fix.valid ? "3D FIX" : (s.fix.fixType == 2 ? "2D" : "no fix"),
           s.fix.numSV, S.distShort(s.fix.hAcc), S.distShortUnit(), s.fix.pDOP);
  row("GPS", b, s.fix.valid ? C_GOOD : C_WARN);

  snprintf(b, sizeof(b), "%.6f, %.6f", s.fix.lat, s.fix.lon);
  row("Position", s.fix.valid ? b : "-", C_FG);

  for (uint8_t i = 0; i < SLOT_COUNT; i++) {
    const SensorInfo& in = _sensors->info((SensorSlot)i);
    const char* kind = i == SLOT_HR ? "Heart rate" : i == SLOT_CSC ? "Speed/cadence" : "Power";
    if (in.paired) {
      if (in.battery != 0xFFFF)
        snprintf(b, sizeof(b), "%s  %s  batt %u%%", in.name, in.connected ? "connected" : "away", in.battery);
      else
        snprintf(b, sizeof(b), "%s  %s", in.name, in.connected ? "connected" : "away");
    } else {
      snprintf(b, sizeof(b), "not paired");
    }
    row(kind, b, in.connected ? C_GOOD : (in.paired ? C_WARN : C_DIM));
  }
  if (_sensors->scanning()) row("", "scanning for sensors...", C_ACCENT);

  if (_course->loaded())
    snprintf(b, sizeof(b), "%s  %.1f %s  %u%% done  %s",
             _course->name(), S.distLong(_course->totalDistance()), S.distLongUnit(),
             _course->progressPct(), _course->offCourse() ? "OFF COURSE" : "on course");
  else if (_course->lastError()[0])
    snprintf(b, sizeof(b), "load failed: %s", _course->lastError());
  else
    snprintf(b, sizeof(b), "none loaded");
  row("Course", b, !_course->loaded() ? C_DIM : (_course->offCourse() ? C_BAD : C_GOOD));

  if (!_compass->present())
    snprintf(b, sizeof(b), "not fitted - heading from GPS course only");
  else if (!_compass->calibrated())
    snprintf(b, sizeof(b), "%s  NOT CALIBRATED - Menu > Calibrate compass",
             _compass->chipName());
  else
    snprintf(b, sizeof(b), "%s  heading %03d  decl %+.1f",
             _compass->chipName(), (int)lroundf(_compass->trueHeading()) % 360,
             MAG_DECLINATION_DEG);
  row("Compass", b, !_compass->present() ? C_DIM
                  : (_compass->calibrated() ? C_GOOD : C_WARN));

  if (_baro->present())
    snprintf(b, sizeof(b), "%.1f hPa  %.0f %s  %.1f %s  (QNH %.1f)",
             _baro->pressure(), S.elev(_baro->altitude()), S.elevUnit(),
             S.temp(_baro->temperature()), S.tempUnit(), _baro->seaLevelHpa());
  else
    snprintf(b, sizeof(b), "not detected - using GPS altitude");
  row("Barometer", b, _baro->present() ? C_FG : C_DIM);

  if (_power->charging()) snprintf(b, sizeof(b), "%u%%  %u mV  charging", s.batteryPct, s.batteryMv);
  else if (!isnan(_power->hoursRemaining()))
    snprintf(b, sizeof(b), "%u%%  %u mV  ~%.1f h left", s.batteryPct, s.batteryMv, _power->hoursRemaining());
  else snprintf(b, sizeof(b), "%u%%  %u mV", s.batteryPct, s.batteryMv);
  row("Battery", b, s.batteryPct > 20 ? C_GOOD : C_BAD);

  if (_rec->mounted())
    snprintf(b, sizeof(b), "%llu MB card  %s  %lu records",
             _rec->cardSizeMb(), _rec->active() ? _rec->fitPath() : "idle",
             (unsigned long)_rec->recordCount());
  else snprintf(b, sizeof(b), "no card - ride will not be saved");
  row("Storage", b, _rec->mounted() ? C_FG : C_BAD);

  snprintf(b, sizeof(b), _wifiOn ? "on  %s" : "off", _wifiIp);
  row("Wi-Fi", b, _wifiOn ? C_GOOD : C_DIM);

  snprintf(b, sizeof(b), "%s  fw %s  heap %u KB  psram %u KB",
           DEVICE_NAME, FIRMWARE_VERSION, ESP.getFreeHeap() / 1024, ESP.getFreePsram() / 1024);
  row("Device", b, C_DIM);

  _lcd->setTextColor(C_DIM, C_BG);
  _lcd->setTextDatum(lgfx::textdatum_t::bottom_center);
  _lcd->drawString("UP/DOWN change page   ENTER start/stop   hold ENTER menu",
                   w / 2, _lcd->height() - 4);
}

// --------------------------------------------------------------------------
void Ui::drawMenu() {
  // The menu outgrew the screen: at 25 px an entry, seventeen of them wanted
  // 469 px of a 320 px display, and the ends were drawn off both edges. It
  // scrolls now, so entries can keep being added without vanishing.
  const int rowH = 24;
  int maxRows = (_lcd->height() - 62) / rowH;
  int visible = min<int>(kMenuCount, maxRows);
  int w = 300, h = visible * rowH + 44;
  int x = (_lcd->width() - w) / 2, y = (_lcd->height() - h) / 2;

  int first = 0;
  if (_menuIndex >= visible) first = _menuIndex - visible + 1;
  if (first + visible > kMenuCount) first = kMenuCount - visible;

  _lcd->fillRoundRect(x, y, w, h, 8, C_BAR);
  _lcd->drawRoundRect(x, y, w, h, 8, C_ACCENT);
  _lcd->setFont(&fonts::Font4);
  _lcd->setTextDatum(lgfx::textdatum_t::top_center);
  _lcd->setTextColor(C_ACCENT, C_BAR);
  _lcd->drawString("MENU", x + w / 2, y + 8);

  _lcd->setFont(&fonts::Font2);
  for (int i = 0; i < visible; i++) {
    uint8_t k = first + i;
    int iy = y + 38 + i * rowH;
    bool sel = k == _menuIndex;
    _lcd->fillRect(x + 6, iy, w - 20, rowH - 2, sel ? C_ACCENT : C_BAR);
    _lcd->setTextDatum(lgfx::textdatum_t::middle_left);
    _lcd->setTextColor(sel ? C_BG : C_FG, sel ? C_ACCENT : C_BAR);
    _lcd->drawString(kMenu[k], x + 18, iy + rowH / 2);
  }

  if (visible < kMenuCount) {
    int tY = y + 38, tH = visible * rowH;
    _lcd->drawRect(x + w - 12, tY, 6, tH, C_LINE);
    int knob = max(10, tH * visible / kMenuCount);
    int knobY = tY + (tH - knob) * first / max(1, kMenuCount - visible);
    _lcd->fillRect(x + w - 11, knobY, 4, knob, C_ACCENT);
  }
}

void Ui::drawPicker() {
  bool course = (_pickerMode == PickerMode::Course);
  uint8_t total = _presetCount + _fileCount;
  int w = 400, rows = total ? total : 1;
  int h = rows * 24 + 56;
  h = min(h, (int)_lcd->height() - 12);
  int x = (_lcd->width() - w) / 2, y = (_lcd->height() - h) / 2;
  _lcd->fillRoundRect(x, y, w, h, 8, C_BAR);
  _lcd->drawRoundRect(x, y, w, h, 8, C_ACCENT);

  _lcd->setFont(&fonts::Font4);
  _lcd->setTextDatum(lgfx::textdatum_t::top_center);
  _lcd->setTextColor(C_ACCENT, C_BAR);
  _lcd->drawString(course ? "LOAD COURSE" : "START WORKOUT", x + w / 2, y + 8);

  _lcd->setFont(&fonts::Font2);
  if (!total) {
    _lcd->setTextColor(C_DIM, C_BAR);
    _lcd->setTextDatum(lgfx::textdatum_t::middle_center);
    _lcd->drawString("No .gpx files in " COURSE_DIR, x + w / 2, y + h / 2);
    _lcd->drawString("Copy them to the card, or upload over Wi-Fi",
                     x + w / 2, y + h / 2 + 20);
    return;
  }

  // Keep the highlighted row on screen when the list is longer than the panel.
  int visible = (h - 46) / 24;
  int top = 0;
  if (_pickerIndex >= visible) top = _pickerIndex - visible + 1;

  for (int i = 0; i < visible && top + i < total; i++) {
    uint8_t k = top + i;
    int iy = y + 38 + i * 24;
    bool sel = k == _pickerIndex;
    bool preset = k < _presetCount;
    _lcd->fillRect(x + 6, iy, w - 12, 22, sel ? C_ACCENT : C_BAR);
    _lcd->setTextDatum(lgfx::textdatum_t::middle_left);
    _lcd->setTextColor(sel ? C_BG : C_FG, sel ? C_ACCENT : C_BAR);
    _lcd->drawString(preset ? Workout::presetName(k) : _files[k - _presetCount],
                     x + 18, iy + 11);
    if (preset) {
      _lcd->setTextDatum(lgfx::textdatum_t::middle_right);
      _lcd->setTextColor(sel ? C_BG : C_DIM, sel ? C_ACCENT : C_BAR);
      _lcd->drawString("built in", x + w - 18, iy + 11);
    }
  }
}

// Full-screen so eight slots plus the controls fit without scrolling.
void Ui::drawEditor() {
  PageLayout& L = _layout[_editPage];
  int w = _lcd->width(), h = _lcd->height();
  _lcd->fillScreen(C_BG);

  _lcd->setFont(&fonts::Font4);
  _lcd->setTextDatum(lgfx::textdatum_t::top_left);
  _lcd->setTextColor(C_ACCENT, C_BG);
  _lcd->drawString("DATA FIELDS", 12, 6);
  _lcd->setFont(&fonts::Font2);
  _lcd->setTextDatum(lgfx::textdatum_t::top_right);
  _lcd->setTextColor(C_DIM, C_BG);
  _lcd->drawString("UP/DOWN move   ENTER change   BACK done", w - 12, 12);

  const uint8_t rows = L.count + 4;
  int top = 34, rh = (h - top - 22) / rows;
  if (rh > 26) rh = 26;

  char val[16], line[48];
  const char *lab, *unit;

  for (uint8_t r = 0; r < rows; r++) {
    int y = top + r * rh;
    bool sel = (r == _editRow);
    _lcd->fillRect(8, y, w - 16, rh - 2, sel ? C_ACCENT : C_BG);
    uint16_t fg = sel ? C_BG : C_FG, dim = sel ? C_BG : C_DIM;
    _lcd->setFont(&fonts::Font2);
    _lcd->setTextDatum(lgfx::textdatum_t::middle_left);

    if (r == 0) {
      _lcd->setTextColor(dim, sel ? C_ACCENT : C_BG);
      _lcd->drawString("Page", 20, y + rh / 2);
      _lcd->setTextColor(fg, sel ? C_ACCENT : C_BG);
      _lcd->drawString(_editPage == 0 ? "Ride 1" : "Ride 2", 120, y + rh / 2);
    } else if (r <= L.count) {
      uint8_t slot = r - 1;
      snprintf(line, sizeof(line), "%u", slot + 1);
      _lcd->setTextColor(dim, sel ? C_ACCENT : C_BG);
      _lcd->drawString(line, 20, y + rh / 2);
      _lcd->setTextColor(fg, sel ? C_ACCENT : C_BG);
      _lcd->drawString(fieldName(L.f[slot]), 48, y + rh / 2);
      // Live value, so a name like "NORM POWER" is not just a guess.
      formatField(L.f[slot], val, sizeof(val), &lab, &unit);
      snprintf(line, sizeof(line), "%s %s", val, unit);
      _lcd->setTextDatum(lgfx::textdatum_t::middle_right);
      _lcd->setTextColor(sel ? C_BG : C_ACCENT, sel ? C_ACCENT : C_BG);
      _lcd->drawString(line, w - 22, y + rh / 2);
    } else if (r == L.count + 1) {
      _lcd->setTextColor(dim, sel ? C_ACCENT : C_BG);
      _lcd->drawString("Fields on page", 20, y + rh / 2);
      snprintf(line, sizeof(line), "%u", L.count);
      _lcd->setTextColor(fg, sel ? C_ACCENT : C_BG);
      _lcd->drawString(line, 160, y + rh / 2);
    } else if (r == L.count + 2) {
      _lcd->setTextColor(fg, sel ? C_ACCENT : C_BG);
      _lcd->drawString("Reset this page to defaults", 20, y + rh / 2);
    } else {
      _lcd->setTextColor(fg, sel ? C_ACCENT : C_BG);
      _lcd->drawString("Done", 20, y + rh / 2);
    }
  }

  // Miniature of the grid this page will actually draw.
  int pw = 92, ph = 62, px = w - pw - 12, py = h - ph - 8;
  _lcd->drawRect(px, py, pw, ph, C_LINE);
  uint8_t cols = L.count <= 3 ? 1 : 2;
  uint8_t rws  = L.count <= 3 ? L.count : (L.count + 1) / 2;
  for (uint8_t i = 0; i < L.count; i++) {
    int cx = (cols == 1) ? 0 : i % cols;
    int cy = (cols == 1) ? i : i / cols;
    _lcd->drawRect(px + 2 + cx * (pw - 4) / cols, py + 2 + cy * (ph - 4) / rws,
                   (pw - 4) / cols - 1, (ph - 4) / rws - 1, C_ACCENT);
  }
  _lcd->setFont(&fonts::Font2);
  _lcd->setTextDatum(lgfx::textdatum_t::bottom_left);
  _lcd->setTextColor(C_DIM, C_BG);
  _lcd->drawString("layout preview", px, py - 2);
}

void Ui::drawFieldChooser() {
  int w = _lcd->width(), h = _lcd->height();
  const uint8_t nFields = (uint8_t)Field::COUNT;
  const uint8_t VIS = 11;

  if (_chooseIdx < _chooseTop) _chooseTop = _chooseIdx;
  if (_chooseIdx >= _chooseTop + VIS) _chooseTop = _chooseIdx - VIS + 1;
  if (_chooseTop + VIS > nFields) _chooseTop = nFields > VIS ? nFields - VIS : 0;

  _lcd->fillScreen(C_BG);
  _lcd->setFont(&fonts::Font4);
  _lcd->setTextDatum(lgfx::textdatum_t::top_left);
  _lcd->setTextColor(C_ACCENT, C_BG);
  char t[40];
  snprintf(t, sizeof(t), "SLOT %u  -  %s", _editSlot + 1,
           _editPage == 0 ? "Ride 1" : "Ride 2");
  _lcd->drawString(t, 12, 6);
  _lcd->setFont(&fonts::Font2);
  _lcd->setTextDatum(lgfx::textdatum_t::top_right);
  _lcd->setTextColor(C_DIM, C_BG);
  _lcd->drawString("ENTER pick   BACK cancel", w - 12, 12);

  int top = 34, rh = 24;
  char val[16];
  const char *lab, *unit;
  for (uint8_t i = 0; i < VIS && _chooseTop + i < nFields; i++) {
    uint8_t fi = _chooseTop + i;
    int y = top + i * rh;
    bool sel = (fi == _chooseIdx);
    _lcd->fillRect(8, y, w - 26, rh - 2, sel ? C_ACCENT : C_BG);
    _lcd->setFont(&fonts::Font2);
    _lcd->setTextDatum(lgfx::textdatum_t::middle_left);
    _lcd->setTextColor(sel ? C_BG : C_FG, sel ? C_ACCENT : C_BG);
    _lcd->drawString(fieldName((Field)fi), 20, y + rh / 2);
    // Showing what each field reads right now is the fastest way to tell
    // POWER 3s from POWER 30s without a manual.
    formatField((Field)fi, val, sizeof(val), &lab, &unit);
    snprintf(t, sizeof(t), "%s %s", val, unit);
    _lcd->setTextDatum(lgfx::textdatum_t::middle_right);
    _lcd->setTextColor(sel ? C_BG : C_ACCENT, sel ? C_ACCENT : C_BG);
    _lcd->drawString(t, w - 36, y + rh / 2);
  }

  // Scroll bar - the list is longer than one screen.
  int trackY = top, trackH = VIS * rh;
  _lcd->drawRect(w - 16, trackY, 8, trackH, C_LINE);
  int knobH = max(12, trackH * VIS / nFields);
  int knobY = trackY + (trackH - knobH) * _chooseTop / max<uint8_t>(1, nFields - VIS);
  _lcd->fillRect(w - 15, knobY, 6, knobH, C_ACCENT);

  _lcd->setFont(&fonts::Font2);
  _lcd->setTextDatum(lgfx::textdatum_t::bottom_left);
  _lcd->setTextColor(C_DIM, C_BG);
  snprintf(t, sizeof(t), "%u of %u", _chooseIdx + 1, nFields);
  _lcd->drawString(t, 12, h - 4);
}

void Ui::showLoadProgress(uint8_t pct) {
  int w = 300, h = 90;
  int x = (_lcd->width() - w) / 2, y = (_lcd->height() - h) / 2;
  static uint8_t last = 255;
  if (pct == last) return;
  if (last == 255 || pct < last) {              // first call: draw the frame
    _lcd->fillRoundRect(x, y, w, h, 8, C_BAR);
    _lcd->drawRoundRect(x, y, w, h, 8, C_ACCENT);
    _lcd->setFont(&fonts::Font4);
    _lcd->setTextDatum(lgfx::textdatum_t::top_center);
    _lcd->setTextColor(C_FG, C_BAR);
    _lcd->drawString("Loading course", x + w / 2, y + 14);
  }
  last = pct == 100 ? 255 : pct;
  _lcd->drawRect(x + 20, y + 50, w - 40, 18, C_LINE);
  _lcd->fillRect(x + 22, y + 52, (w - 44) * pct / 100, 14, C_ACCENT);
  if (pct == 100) _fullRedraw = true;
}

void Ui::drawBanner() {
  uint32_t now = millis();
  if (now < _notifUntil) {
    int h = 46, y = STATUS_H + 2;
    uint16_t bg = _alertSticky ? C_BAD : C_ACCENT;
    _lcd->fillRoundRect(6, y, _lcd->width() - 12, h, 6, bg);
    _lcd->setTextColor(_alertSticky ? C_FG : C_BG, bg);
    _lcd->setFont(&fonts::Font2);
    _lcd->setTextDatum(lgfx::textdatum_t::top_left);
    _lcd->drawString(_notifTitle, 16, y + 4);
    _lcd->drawString(_notifBody, 16, y + 24);
    return;
  }
  if (now < _toastUntil) {
    _lcd->setFont(&fonts::Font2);
    int tw = _lcd->textWidth(_toast) + 24;
    int x = (_lcd->width() - tw) / 2, y = _lcd->height() - 40;
    _lcd->fillRoundRect(x, y, tw, 28, 6, C_ACCENT);
    _lcd->setTextColor(C_BG, C_ACCENT);
    _lcd->setTextDatum(lgfx::textdatum_t::middle_center);
    _lcd->drawString(_toast, x + tw / 2, y + 14);
  }
}

// --------------------------------------------------------------------------
void Ui::render() {
  static Page lastPage = Page::COUNT;
  static uint32_t lastStatus = 0;
  static bool lastBanner = false;

  if (_page != lastPage) { _fullRedraw = true; lastPage = _page; }
  if (_fullRedraw) {
    _lcd->fillScreen(C_BG);
    memset(_lastVal, 0, sizeof(_lastVal));
    lastStatus = 0;
  }

  bool fullScreenModal = _calOpen || _editOpen || _setOpen || _pagesOpen || _resumeOpen;
  if (!fullScreenModal && (millis() - lastStatus > 500 || _fullRedraw)) {
    lastStatus = millis();
    drawStatusBar();
  }

  // Calibration owns the whole screen, status bar included - the rider is
  // waving the unit around, not looking at their speed.
  if (_calOpen) { drawCompassCal(); _fullRedraw = false; return; }
  if (_resumeOpen) {
    if (_fullRedraw) drawResumePrompt();
    _fullRedraw = false;
    return;
  }
  if (_pagesOpen) {
    if (_fullRedraw) drawPageSelect();
    _fullRedraw = false;
    return;
  }
  if (_setOpen) {
    if (_fullRedraw) drawSettings();   // static values, so only on a keypress
    _fullRedraw = false;
    return;
  }

  // The editor needs the height for eight slots plus its controls, and it is
  // only ever opened while stopped. Repaint once a second so the live values
  // beside each field name stay live - they are the whole reason they are
  // there - but not fast enough for a full-screen repaint to read as flicker.
  if (_editOpen) {
    static uint32_t lastEdit = 0;
    if (_fullRedraw || millis() - lastEdit > 1000) {
      lastEdit = millis();
      _editChoosing ? drawFieldChooser() : drawEditor();
    }
    _fullRedraw = false;
    return;
  }

  // Modal overlays: nothing underneath repaints while one is up, otherwise a
  // changing speed field would punch a hole through it.
  if (_pickerOpen) { drawPicker(); _fullRedraw = false; return; }
  if (_menuOpen)   { drawMenu();   _fullRedraw = false; return; }

  switch (_page) {
    case Page::Ride1: drawGrid(_layout[0].f, _layout[0].count); break;
    case Page::Ride2: drawGrid(_layout[1].f, _layout[1].count); break;
    case Page::Map:      if (_fullRedraw || millis() % 1000 < 120) drawMap();     break;
    case Page::Nav:      if (_fullRedraw || millis() % 1000 < 120) drawNav();     break;
    case Page::Laps:     if (_fullRedraw || millis() % 1000 < 120) drawLaps();    break;
    // Interval countdowns want a faster tick than a ride page - the last five
    // seconds of a 30 s effort matter.
    case Page::Workout:  if (_fullRedraw || millis() % 250 < 120) drawWorkout();  break;
    case Page::Load:     if (_fullRedraw || millis() % 2000 < 120) drawLoad();    break;
    case Page::Zones_:   if (_fullRedraw || millis() % 1000 < 120) drawZonePage(ZoneSource::Power);   break;
    case Page::HrZones:  if (_fullRedraw || millis() % 1000 < 120) drawZonePage(ZoneSource::Hr);      break;
    case Page::CadZones: if (_fullRedraw || millis() % 1000 < 120) drawZonePage(ZoneSource::Cadence); break;
    case Page::Gear:     if (_fullRedraw || millis() % 500 < 120) drawGear();      break;
    case Page::Summary:  if (_fullRedraw || millis() % 1000 < 120) drawSummary();  break;
    // A needle that updates once a second reads as broken, so this one runs
    // faster. The rose goes through a sprite, so it costs a push, not a repaint.
    case Page::Compass:  if (_fullRedraw || millis() % 250 < 120) drawCompass();  break;
    case Page::Wx:       if (_fullRedraw || millis() % 2000 < 120) drawWx();      break;
    case Page::WxHist:   if (_fullRedraw || millis() % 5000 < 120) drawWxHistory(); break;
    case Page::Profile:  if (_fullRedraw || millis() % 2000 < 120) drawProfile(); break;
    case Page::Status:   if (_fullRedraw || millis() % 1000 < 120) drawStatus();  break;
    default: break;
  }

  bool banner = millis() < _notifUntil || millis() < _toastUntil;
  if (banner) drawBanner();
  else if (lastBanner) _fullRedraw = true;   // clear whatever the banner covered
  lastBanner = banner;

  _fullRedraw = false;
}
