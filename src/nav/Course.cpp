#include "nav/Course.h"
#include <SD.h>
#include <math.h>
#include <ctype.h>

Course g_course;

// Bearing in the local metric frame: 0 = +y (north), clockwise positive.
static inline float bearingXY(float x0, float y0, float x1, float y1) {
  return degrees(atan2f(x1 - x0, y1 - y0));
}
static inline float wrap180(float d) {
  while (d > 180.0f) d -= 360.0f;
  while (d < -180.0f) d += 360.0f;
  return d;
}

// --------------------------------------------------------------------------
// Buffered character reader. GPX files routinely put the whole track on one
// line, so a line-oriented reader is useless here.
namespace {
class Reader {
 public:
  explicit Reader(File& f) : _f(f), _total(f.size()) {}
  int next() {
    if (_pos >= _len) {
      int n = _f.read(_buf, sizeof(_buf));
      if (n <= 0) return -1;
      _len = n; _pos = 0;
    }
    _read++;
    return _buf[_pos++];
  }
  uint8_t progress() const { return _total ? (uint8_t)(_read * 100ULL / _total) : 0; }

 private:
  File& _f;
  uint8_t _buf[512];
  int _len = 0, _pos = 0;
  uint32_t _read = 0, _total = 0;
};

// Pull a double out of an XML attribute, e.g. lat="50.061" -> 50.061
bool attr(const char* tag, const char* key, double& out) {
  const char* p = strstr(tag, key);
  if (!p) return false;
  p += strlen(key);
  while (*p == ' ' || *p == '=') p++;
  if (*p == '"' || *p == '\'') p++;
  out = atof(p);
  return true;
}

void trimCopy(char* dst, size_t n, const char* src) {
  while (*src == ' ' || *src == '\t' || *src == '\n' || *src == '\r') src++;
  strncpy(dst, src, n - 1);
  dst[n - 1] = 0;
  for (int i = (int)strlen(dst) - 1; i >= 0; i--) {
    if (dst[i] == ' ' || dst[i] == '\n' || dst[i] == '\r' || dst[i] == '\t') dst[i] = 0;
    else break;
  }
}
}  // namespace

// --------------------------------------------------------------------------
bool Course::alloc() {
  clear();
  size_t ptBytes  = sizeof(CoursePoint) * COURSE_MAX_POINTS;
  size_t cueBytes = sizeof(CourseCue) * COURSE_MAX_CUES;
  _pts  = (CoursePoint*)(psramFound() ? ps_malloc(ptBytes)  : malloc(ptBytes));
  _cues = (CourseCue*)  (psramFound() ? ps_malloc(cueBytes) : malloc(cueBytes));
  if (!_pts || !_cues) {
    snprintf(_error, sizeof(_error), "out of memory");
    clear();
    return false;
  }
  return true;
}

void Course::clear() {
  if (_pts)  { free(_pts);  _pts = nullptr; }
  if (_cues) { free(_cues); _cues = nullptr; }
  _count = _cueCount = 0;
  _name[0] = _file[0] = 0;
  _totalAscent = 0;
  _nearIdx = 0; _crossTrack = 0; _along = 0;
  _snapX = _snapY = 0;
  _bearingToRoute = _heading = NAN;
  _off = _finished = false;
  _offTimer = _onTimer = 0;
  _pending = CourseEvent::None;
  _upCount = 0;
  _lastScanAlong = -1e9f;
  _stagedDist = NAN;
  _stage = 0;
}

// --------------------------------------------------------------------------
// Turning the ride's own breadcrumb into a route home. Everything downstream -
// snapping, off-course alerts, turn detection - then works unchanged, which is
// the whole reason this is only twenty lines.
bool Course::buildFromTrack(const TrackPoint* tp, uint16_t n, const char* label) {
  _error[0] = 0;
  if (!tp || n < 2) { snprintf(_error, sizeof(_error), "not enough track yet"); return false; }
  if (!alloc()) return false;

  // Reversed: the rider's current position becomes the start of the course.
  _lat0 = tp[n - 1].lat; _lon0 = tp[n - 1].lon;
  double phi = radians(_lat0);
  _latScale = (float)(111132.92 - 559.82 * cos(2 * phi) + 1.175 * cos(4 * phi));
  _lonScale = (float)(111412.84 * cos(phi) - 93.5 * cos(3 * phi));

  for (int32_t i = n - 1; i >= 0 && _count < COURSE_MAX_POINTS; i--) {
    MetresXY m = toXY(tp[i].latLon());
    if (_count) {
      float dx = m.x - _pts[_count - 1].xy.x, dy = m.y - _pts[_count - 1].xy.y;
      if (dx * dx + dy * dy < COURSE_MIN_SPACING_M * COURSE_MIN_SPACING_M) continue;
    }
    CoursePoint& p = _pts[_count++];
    p.lat_e7 = (int32_t)llround((double)tp[i].lat * 1e7);
    p.lon_e7 = (int32_t)llround((double)tp[i].lon * 1e7);
    p.xy = m; p.dist = 0;
    p.ele = INT16_MIN;                     // the breadcrumb carries no elevation
  }
  if (_count < 2) { snprintf(_error, sizeof(_error), "not enough track yet"); clear(); return false; }

  strncpy(_name, label, sizeof(_name) - 1);
  strncpy(_file, "(breadcrumb)", sizeof(_file) - 1);
  finishLoad();
  return true;
}

MetresXY Course::toXY(LatLon p) const {
  return { (float)((p.lon - _lon0) * _lonScale),
           (float)((p.lat - _lat0) * _latScale) };
}

bool Course::load(const char* path, void (*onProgress)(uint8_t)) {
  _error[0] = 0;
  File f = SD.open(path, FILE_READ);
  if (!f) { snprintf(_error, sizeof(_error), "cannot open file"); return false; }
  if (!alloc()) { f.close(); return false; }

  strncpy(_file, strrchr(path, '/') ? strrchr(path, '/') + 1 : path, sizeof(_file) - 1);

  Reader rd(f);
  char tag[192], text[64];
  uint8_t lastPct = 255;

  bool  inPt = false, inWpt = false;
  double ptLat = 0, ptLon = 0;
  int16_t ptEle = INT16_MIN;
  char  cueName[28] = {0};
  enum { WANT_NONE, WANT_ELE, WANT_NAME } want = WANT_NONE;

  float minSpacing = COURSE_MIN_SPACING_M;
  float lastX = 0, lastY = 0;

  auto commitPoint = [&]() {
    if (!inPt) return;
    inPt = false;
    if (_count == 0) {
      // First point anchors the local metric frame. Using the real
      // latitude-dependent scale keeps a 200 km course under ~0.1 % distortion.
      _lat0 = ptLat; _lon0 = ptLon;
      double phi = radians(ptLat);
      _latScale = (float)(111132.92 - 559.82 * cos(2 * phi) + 1.175 * cos(4 * phi));
      _lonScale = (float)(111412.84 * cos(phi) - 93.5 * cos(3 * phi));
    }
    MetresXY m = toXY({ptLat, ptLon});
    float x = m.x, y = m.y;
    if (_count > 0) {
      float dx = x - lastX, dy = y - lastY;
      if (dx * dx + dy * dy < minSpacing * minSpacing) return;   // too close, drop it
    }
    if (_count >= COURSE_MAX_POINTS) {
      // Halve the resolution in place and raise the spacing filter so we do not
      // immediately refill. A course longer than the buffer still loads.
      for (uint16_t i = 0; i < COURSE_MAX_POINTS / 2; i++) _pts[i] = _pts[i * 2];
      _count = COURSE_MAX_POINTS / 2;
      minSpacing *= 2.0f;
      lastX = _pts[_count - 1].xy.x; lastY = _pts[_count - 1].xy.y;
      float dx = x - lastX, dy = y - lastY;
      if (dx * dx + dy * dy < minSpacing * minSpacing) return;
    }
    CoursePoint& p = _pts[_count++];
    p.lat_e7 = (int32_t)llround(ptLat * 1e7);
    p.lon_e7 = (int32_t)llround(ptLon * 1e7);
    p.xy = m; p.dist = 0; p.ele = ptEle;
    lastX = x; lastY = y;
  };

  auto commitCue = [&]() {
    if (!inWpt) return;
    inWpt = false;
    if (_cueCount >= COURSE_MAX_CUES || !cueName[0]) return;
    CourseCue& c = _cues[_cueCount++];
    c.lat_e7 = (int32_t)llround(ptLat * 1e7);
    c.lon_e7 = (int32_t)llround(ptLon * 1e7);
    c.dist = 0;
    strncpy(c.name, cueName, sizeof(c.name) - 1);
    c.name[sizeof(c.name) - 1] = 0;
  };

  int ch;
  size_t ti = 0;
  while ((ch = rd.next()) >= 0) {
    if (onProgress) {
      uint8_t pct = rd.progress();
      if (pct != lastPct) { lastPct = pct; onProgress(pct); }
    }

    if (ch != '<') {
      if (want != WANT_NONE && ti < sizeof(text) - 1) text[ti++] = (char)ch;
      continue;
    }

    // Collect the tag body.
    size_t n = 0;
    while ((ch = rd.next()) >= 0 && ch != '>') {
      if (n < sizeof(tag) - 1) tag[n++] = (char)ch;
    }
    tag[n] = 0;
    if (ch < 0) break;

    // Close out any text we were collecting for the tag that just ended.
    if (want != WANT_NONE) {
      text[ti] = 0;
      if (tag[0] == '/') {
        if (want == WANT_ELE) {
          float e = atof(text);
          if (e > -500 && e < 9000) ptEle = (int16_t)lroundf(e);
        } else if (want == WANT_NAME) {
          if (inWpt) trimCopy(cueName, sizeof(cueName), text);
          else if (!_name[0]) trimCopy(_name, sizeof(_name), text);
        }
      }
      want = WANT_NONE;
      ti = 0;
    }

    bool selfClosing = n > 0 && tag[n - 1] == '/';

    if (!strncmp(tag, "trkpt", 5) || !strncmp(tag, "rtept", 5)) {
      inPt = true; ptEle = INT16_MIN;
      attr(tag, "lat", ptLat); attr(tag, "lon", ptLon);
      if (selfClosing) commitPoint();
    } else if (!strncmp(tag, "/trkpt", 6) || !strncmp(tag, "/rtept", 6)) {
      commitPoint();
    } else if (!strncmp(tag, "wpt", 3) && (tag[3] == ' ' || tag[3] == 0 || tag[3] == '/')) {
      inWpt = true; cueName[0] = 0;
      attr(tag, "lat", ptLat); attr(tag, "lon", ptLon);
      if (selfClosing) commitCue();
    } else if (!strncmp(tag, "/wpt", 4)) {
      commitCue();
    } else if (!strncmp(tag, "ele", 3) && !selfClosing) {
      want = WANT_ELE; ti = 0;
    } else if (!strncmp(tag, "name", 4) && !selfClosing) {
      want = WANT_NAME; ti = 0;
    }
  }
  f.close();

  if (_count < 2) {
    snprintf(_error, sizeof(_error), "no track points found");
    clear();
    return false;
  }
  if (!_name[0]) strncpy(_name, _file, sizeof(_name) - 1);
  finishLoad();
  if (onProgress) onProgress(100);
  return true;
}

void Course::finishLoad() {
  // Cumulative distance and total climb.
  _pts[0].dist = 0;
  float lastEle = _pts[0].ele == INT16_MIN ? NAN : _pts[0].ele;
  _totalAscent = 0;
  for (uint16_t i = 1; i < _count; i++) {
    float dx = _pts[i].xy.x - _pts[i - 1].xy.x, dy = _pts[i].xy.y - _pts[i - 1].xy.y;
    _pts[i].dist = _pts[i - 1].dist + sqrtf(dx * dx + dy * dy);
    if (_pts[i].ele != INT16_MIN) {
      float e = _pts[i].ele;
      if (isnan(lastEle)) lastEle = e;
      // 3 m deadband: GPX elevation from a phone is noisy enough that a 1 m
      // threshold invents hundreds of metres of climb on a flat road.
      if (e - lastEle > 3.0f) { _totalAscent += e - lastEle; lastEle = e; }
      else if (lastEle - e > 3.0f) { lastEle = e; }
    }
  }

  // Snap every cue onto the polyline so it has an along-course distance.
  for (uint16_t c = 0; c < _cueCount; c++) {
    MetresXY m = toXY(_cues[c].latLon());
    uint16_t idx; float along;
    snapWindow(m.x, m.y, 0, _count - 1, idx, along);
    _cues[c].dist = along;
  }
  // Insertion sort by distance - cue lists are short and often already ordered.
  for (uint16_t i = 1; i < _cueCount; i++) {
    CourseCue key = _cues[i];
    int j = i - 1;
    while (j >= 0 && _cues[j].dist > key.dist) { _cues[j + 1] = _cues[j]; j--; }
    _cues[j + 1] = key;
  }
}

// --------------------------------------------------------------------------
float Course::snapWindow(float px, float py, uint16_t from, uint16_t to,
                         uint16_t& bestIdx, float& bestAlong) const {
  float best = INFINITY;
  bestIdx = from;
  bestAlong = _pts[from].dist;
  for (uint16_t i = from; i < to; i++) {
    float ax = _pts[i].xy.x, ay = _pts[i].xy.y;
    float dx = _pts[i + 1].xy.x - ax, dy = _pts[i + 1].xy.y - ay;
    float len2 = dx * dx + dy * dy;
    float t = len2 > 0 ? ((px - ax) * dx + (py - ay) * dy) / len2 : 0;
    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    float cx = ax + t * dx, cy = ay + t * dy;
    float d2 = (px - cx) * (px - cx) + (py - cy) * (py - cy);
    if (d2 < best) {
      best = d2;
      bestIdx = i;
      bestAlong = _pts[i].dist + t * sqrtf(len2);
    }
  }
  return sqrtf(best);
}

// --------------------------------------------------------------------------
// Turn detection
// --------------------------------------------------------------------------
const char* Course::turnText(TurnType t) {
  switch (t) {
    case TurnType::SlightLeft:  return "Bear left";
    case TurnType::Left:        return "Turn left";
    case TurnType::SharpLeft:   return "Sharp left";
    case TurnType::SlightRight: return "Bear right";
    case TurnType::Right:       return "Turn right";
    case TurnType::SharpRight:  return "Sharp right";
    case TurnType::UTurn:       return "U-turn";
    case TurnType::Finish:      return "Finish";
    case TurnType::Straight:    return "Continue";
    default:                    return "Cue";
  }
}

TurnType Course::classifyAngle(float deg, float minDeg) {
  float a = fabsf(deg);
  bool left = deg < 0;
  if (a > 160.0f) return TurnType::UTurn;
  if (a > 120.0f) return left ? TurnType::SharpLeft   : TurnType::SharpRight;
  if (a >  55.0f) return left ? TurnType::Left        : TurnType::Right;
  if (a >= minDeg) return left ? TurnType::SlightLeft : TurnType::SlightRight;
  return TurnType::Straight;
}

// A waypoint that says "Turn left onto Polna" is more authoritative than any
// angle we can measure, so its text wins when it names a direction.
TurnType Course::typeFromText(const char* s) {
  char b[32];
  size_t i = 0;
  for (; s[i] && i < sizeof(b) - 1; i++) b[i] = tolower((unsigned char)s[i]);
  b[i] = 0;
  bool slight = strstr(b, "slight") || strstr(b, "bear") || strstr(b, "lekko");
  bool sharp  = strstr(b, "sharp")  || strstr(b, "ostro");
  if (strstr(b, "u-turn") || strstr(b, "uturn") || strstr(b, "zawr"))
    return TurnType::UTurn;
  if (strstr(b, "left")  || strstr(b, "lewo"))
    return slight ? TurnType::SlightLeft : sharp ? TurnType::SharpLeft : TurnType::Left;
  if (strstr(b, "right") || strstr(b, "prawo"))
    return slight ? TurnType::SlightRight : sharp ? TurnType::SharpRight : TurnType::Right;
  if (strstr(b, "straight") || strstr(b, "continue") || strstr(b, "prosto"))
    return TurnType::Straight;
  if (strstr(b, "finish") || strstr(b, "end") || strstr(b, "meta"))
    return TurnType::Finish;
  return TurnType::Generic;
}

uint16_t Course::indexAtDistance(float d) const {
  if (!loaded()) return 0;
  uint16_t lo = 0, hi = _count - 1;
  while (lo + 1 < hi) {
    uint16_t mid = (lo + hi) / 2;
    if (_pts[mid].dist <= d) lo = mid; else hi = mid;
  }
  return lo;
}

// Heading change across a fixed baseline rather than between adjacent points.
// Point-to-point angles are pure noise at 5 m spacing, and a long sweeping bend
// must not read as a turn just because it adds up.
float Course::turnAngleAt(uint16_t i) const {
  if (i == 0 || i + 1 >= _count) return 0;
  uint16_t a = indexAtDistance(_pts[i].dist - NAV_TURN_BASELINE_M);
  uint16_t b = indexAtDistance(_pts[i].dist + NAV_TURN_BASELINE_M);
  if (b + 1 < _count) b++;
  if (a >= i || b <= i) return 0;
  float in  = bearingXY(_pts[a].xy.x, _pts[a].xy.y, _pts[i].xy.x, _pts[i].xy.y);
  float out = bearingXY(_pts[i].xy.x, _pts[i].xy.y, _pts[b].xy.x, _pts[b].xy.y);
  return wrap180(out - in);
}

// Rebuild the short list of turns ahead. Called only when the rider has moved
// far enough to matter, so the cost never lands in the per-second budget twice.
void Course::scanTurns() {
  _upCount = 0;
  if (!loaded()) return;

  const uint8_t CAP = NAV_LOOKAHEAD_CUES * 2;
  NavCue tmp[CAP];
  uint8_t n = 0;

  float from = _along + 5.0f;                  // ignore a turn we are already in
  float to   = min(_along + NAV_LOOKAHEAD_M, totalDistance());

  for (uint16_t i = indexAtDistance(from); i < _count && n < CAP; i++) {
    if (_pts[i].dist < from) continue;
    if (_pts[i].dist > to) break;
    float ang = turnAngleAt(i);
    if (fabsf(ang) < NAV_TURN_MIN_DEG) continue;

    // Collapse a cluster of candidates around one corner into its sharpest point.
    if (n && _pts[i].dist - tmp[n - 1].dist < NAV_TURN_MERGE_M) {
      if (fabsf(ang) > fabsf((float)tmp[n - 1].angle)) {
        tmp[n - 1].dist  = _pts[i].dist;
        tmp[n - 1].angle = (int16_t)lroundf(ang);
        tmp[n - 1].type  = classifyAngle(ang);
        tmp[n - 1].lat_e7 = _pts[i].lat_e7;
        tmp[n - 1].lon_e7 = _pts[i].lon_e7;
      }
      continue;
    }
    NavCue& c = tmp[n++];
    c.dist   = _pts[i].dist;
    c.lat_e7 = _pts[i].lat_e7;
    c.lon_e7 = _pts[i].lon_e7;
    c.angle  = (int16_t)lroundf(ang);
    c.type   = classifyAngle(ang);
    c.named  = false;
    c.name[0] = 0;
  }

  // Fold in the GPX waypoints: they name the turn we already found, or they are
  // an instruction the geometry could not see (a junction you ride straight
  // through, a feed stop) and become a cue of their own.
  for (uint16_t k = 0; k < _cueCount; k++) {
    const CourseCue& w = _cues[k];
    if (w.dist < from || w.dist > to) continue;

    int match = -1;
    for (uint8_t j = 0; j < n; j++)
      if (fabsf(w.dist - tmp[j].dist) < NAV_CUE_MATCH_M) { match = j; break; }

    if (match >= 0) {
      strncpy(tmp[match].name, w.name, sizeof(tmp[match].name) - 1);
      tmp[match].name[sizeof(tmp[match].name) - 1] = 0;
      tmp[match].named = true;
      TurnType fromText = typeFromText(w.name);
      if (fromText != TurnType::Generic) tmp[match].type = fromText;
    } else if (n < CAP) {
      NavCue& c = tmp[n++];
      c.dist   = w.dist;
      c.lat_e7 = w.lat_e7;
      c.lon_e7 = w.lon_e7;
      c.angle  = 0;
      c.type   = typeFromText(w.name);
      c.named  = true;
      strncpy(c.name, w.name, sizeof(c.name) - 1);
      c.name[sizeof(c.name) - 1] = 0;

      // The waypoint text named no direction, so it would draw as a featureless
      // dot. Measure the course where it sits instead: whoever placed the cue
      // already decided something happens here, so a shallower angle than the
      // turn detector's own threshold is enough to earn an arrow.
      if (c.type == TurnType::Generic) {
        float ang = turnAngleAt(indexAtDistance(w.dist));
        TurnType geo = classifyAngle(ang, NAV_CUE_TURN_MIN_DEG);
        if (geo != TurnType::Straight) {
          c.type  = geo;
          c.angle = (int16_t)lroundf(ang);
        }
      }
    }
  }

  // The end of the route is a cue in its own right.
  if (n < CAP && totalDistance() <= to && totalDistance() > from) {
    NavCue& c = tmp[n++];
    c.dist   = totalDistance();
    c.lat_e7 = _pts[_count - 1].lat_e7;
    c.lon_e7 = _pts[_count - 1].lon_e7;
    c.angle  = 0;
    c.type   = TurnType::Finish;
    c.named  = false;
    strncpy(c.name, "Course end", sizeof(c.name) - 1);
  }

  for (uint8_t i = 1; i < n; i++) {          // insertion sort, n is tiny
    NavCue key = tmp[i];
    int j = i - 1;
    while (j >= 0 && tmp[j].dist > key.dist) { tmp[j + 1] = tmp[j]; j--; }
    tmp[j + 1] = key;
  }

  _upCount = min<uint8_t>(n, NAV_LOOKAHEAD_CUES);
  for (uint8_t i = 0; i < _upCount; i++) _up[i] = tmp[i];
}

float Course::relativeBearingToRoute() const {
  if (isnan(_bearingToRoute) || isnan(_heading)) return NAN;
  return wrap180(_bearingToRoute - _heading);
}

// --------------------------------------------------------------------------
void Course::update(LatLon pos, uint32_t dtMs, float headingDeg) {
  if (!loaded()) return;
  float dt = dtMs / 1000.0f;
  if (dt <= 0 || dt > 10) dt = 1.0f;

  MetresXY me = toXY(pos);
  float px = me.x, py = me.y;

  // Search a window around where we were last. This is not just an
  // optimisation: on an out-and-back or a lollipop course the two passes run
  // metres apart, and a global search would happily snap to the return leg and
  // report the ride as nearly finished.
  uint16_t lo = _nearIdx > 100 ? _nearIdx - 100 : 0;
  uint16_t hi = min<uint16_t>(_count - 1, _nearIdx + 500);
  uint16_t idx; float along;
  float d = snapWindow(px, py, lo, hi, idx, along);

  // Genuinely lost, or just started: fall back to a full search.
  if (d > OFF_COURSE_THRESHOLD_M * 3.0f) {
    uint16_t idx2; float along2;
    float d2 = snapWindow(px, py, 0, _count - 1, idx2, along2);
    if (d2 < d) { d = d2; idx = idx2; along = along2; }
  }

  _crossTrack = d;
  _nearIdx = idx;
  _along = along;
  _heading = headingDeg;

  // Where on the line we actually snapped, so guidance can point back at it.
  {
    float ax = _pts[idx].xy.x, ay = _pts[idx].xy.y;
    float ddx = _pts[idx + 1 < _count ? idx + 1 : idx].xy.x - ax;
    float ddy = _pts[idx + 1 < _count ? idx + 1 : idx].xy.y - ay;
    float len2 = ddx * ddx + ddy * ddy;
    float t = len2 > 0 ? ((px - ax) * ddx + (py - ay) * ddy) / len2 : 0;
    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    _snapX = ax + t * ddx;
    _snapY = ay + t * ddy;
    if (d > 1.0f) {
      // Normalised to a real compass bearing: atan2 hands back +/-180 at due
      // south, and callers are promised 0..360.
      float b = bearingXY(px, py, _snapX, _snapY);
      _bearingToRoute = b < 0 ? b + 360.0f : b;
    } else {
      _bearingToRoute = NAN;
    }
  }

  // Rescan turns when the rider has moved meaningfully, or after a jump.
  if (fabsf(_along - _lastScanAlong) > 50.0f) {
    _lastScanAlong = _along;
    scanTurns();
  } else if (_upCount && _up[0].dist < _along) {
    scanTurns();
  }

  // --- off-course state machine with hysteresis on both edges ---
  if (d > OFF_COURSE_THRESHOLD_M) { _offTimer += dt; _onTimer = 0; }
  else                            { _onTimer += dt; _offTimer = 0; }

  if (!_off && _offTimer >= OFF_COURSE_HOLD_S) {
    _off = true;
    _pending = CourseEvent::WentOffCourse;
  } else if (_off && _onTimer >= ON_COURSE_HOLD_S) {
    _off = false;
    _pending = CourseEvent::BackOnCourse;
  }

  if (!_finished && !_off && distanceRemaining() < COURSE_FINISH_RADIUS_M) {
    _finished = true;
    _pending = CourseEvent::Finished;
  }

  // --- turn announcements, in three stages ---
  const NavCue* nx = nextTurn();
  if (!nx || _off) {
    _stagedDist = NAN;
    _stage = 0;
  } else {
    if (isnan(_stagedDist) || fabsf(nx->dist - _stagedDist) > 5.0f) {
      _stagedDist = nx->dist;      // a different turn is now next
      _stage = 0;
    }
    float ahead = nx->dist - _along;
    // Jump straight to the closest stage that applies. Riding up to a turn from
    // 100 m out should say "turn ahead" once, not replay the 400 m call first.
    uint8_t want = ahead <= NAV_ANNOUNCE_NOW_M  ? 3
                 : ahead <= NAV_ANNOUNCE_NEAR_M ? 2
                 : ahead <= NAV_ANNOUNCE_FAR_M  ? 1 : 0;
    if (want > _stage) {
      _stage = want;
      if (_pending == CourseEvent::None)
        _pending = want == 3 ? CourseEvent::TurnNow
                 : want == 2 ? CourseEvent::TurnNear
                             : CourseEvent::TurnFar;
    }
  }
}

CourseEvent Course::takeEvent() {
  CourseEvent e = _pending;
  _pending = CourseEvent::None;
  return e;
}

uint8_t Course::progressPct() const {
  float t = totalDistance();
  if (t <= 0) return 0;
  return (uint8_t)constrain(_along / t * 100.0f, 0.0f, 100.0f);
}

float Course::ascentRemaining() const {
  if (!loaded()) return 0;
  float sum = 0, last = NAN;
  for (uint16_t i = _nearIdx; i < _count; i++) {
    if (_pts[i].ele == INT16_MIN) continue;
    float e = _pts[i].ele;
    if (isnan(last)) { last = e; continue; }
    if (e - last > 3.0f) { sum += e - last; last = e; }
    else if (last - e > 3.0f) { last = e; }
  }
  return sum;
}

float Course::courseElevationAt(float dist) const {
  if (!loaded()) return NAN;
  // Binary search the cumulative distance array.
  uint16_t lo = 0, hi = _count - 1;
  while (lo + 1 < hi) {
    uint16_t mid = (lo + hi) / 2;
    if (_pts[mid].dist <= dist) lo = mid; else hi = mid;
  }
  if (_pts[lo].ele == INT16_MIN || _pts[hi].ele == INT16_MIN) return NAN;
  float span = _pts[hi].dist - _pts[lo].dist;
  float t = span > 0 ? (dist - _pts[lo].dist) / span : 0;
  return _pts[lo].ele + t * (_pts[hi].ele - _pts[lo].ele);
}
