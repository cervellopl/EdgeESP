#pragma once
#include <Arduino.h>
#include "config.h"
#include "nav/TrackPoint.h"

// GPX course following and turn-by-turn guidance: load a route from SD, snap the
// rider onto it, work out where the turns are, and shout at the right moments.
//
// The course is held in a flat array projected into local metres, so the
// cross-track maths is plain float arithmetic with no trig in the inner loop.

struct CoursePoint {
  int32_t  lat_e7, lon_e7;  // 1e-7 degrees
  MetresXY xy;              // position in the course's local metric frame
  float    dist;            // cumulative distance along the course, metres
  int16_t  ele;             // metres, INT16_MIN when the GPX had no elevation
  LatLon latLon() const { return {lat_e7 * 1e-7, lon_e7 * 1e-7}; }
};

// Raw <wpt> waypoint straight out of the file.
struct CourseCue {
  int32_t lat_e7, lon_e7;
  float   dist;             // where it snaps onto the course
  char    name[28];
  LatLon latLon() const { return {lat_e7 * 1e-7, lon_e7 * 1e-7}; }
};

enum class TurnType : uint8_t {
  Straight, SlightLeft, Left, SharpLeft, SlightRight, Right, SharpRight, UTurn,
  Finish, Generic
};

// A turn the rider is about to reach. Either detected from the course geometry
// or supplied by a GPX waypoint - usually both, with the waypoint contributing
// the street name and the geometry contributing the angle.
struct NavCue {
  float    dist;            // along-course position
  int32_t  lat_e7, lon_e7;
  TurnType type;
  int16_t  angle;           // signed degrees, negative = left
  bool     named;           // came from a GPX waypoint
  char     name[28];
  LatLon latLon() const { return {lat_e7 * 1e-7, lon_e7 * 1e-7}; }
};

// Things worth interrupting the rider about.
enum class CourseEvent : uint8_t {
  None, WentOffCourse, BackOnCourse, TurnFar, TurnNear, TurnNow, Finished
};

class Course {
 public:
  // path is a full path such as "/courses/tatry.gpx".
  // onProgress receives 0..100 while parsing so the UI can draw a bar.
  bool load(const char* path, void (*onProgress)(uint8_t) = nullptr);
  // Turn the ride's own breadcrumb into a course home, newest point first.
  bool buildFromTrack(const TrackPoint* pts, uint16_t n, const char* name);
  void clear();

  bool loaded() const { return _count >= 2; }
  const char* name() const { return _name; }
  const char* fileName() const { return _file; }
  uint16_t pointCount() const { return _count; }
  uint16_t cueCount() const { return _cueCount; }
  float totalDistance() const { return _count ? _pts[_count - 1].dist : 0; }
  float totalAscent() const { return _totalAscent; }
  const char* lastError() const { return _error; }

  // Call about once a second with the current fix and heading.
  void update(LatLon pos, uint32_t dtMs, float headingDeg = NAN);

  // --- snapped state ---
  float    crossTrack() const { return _crossTrack; }     // metres off the line
  float    alongDistance() const { return _along; }       // metres along the course
  float    distanceRemaining() const { return max(0.0f, totalDistance() - _along); }
  float    ascentRemaining() const;
  float    courseElevationAt(float dist) const;           // interpolated, NAN if none
  uint8_t  progressPct() const;
  bool     offCourse() const { return _off; }
  bool     finished() const { return _finished; }
  uint16_t nearestIndex() const { return _nearIdx; }

  // --- guidance ---
  uint8_t        upcomingCount() const { return _upCount; }
  const NavCue*  upcoming(uint8_t i) const { return i < _upCount ? &_up[i] : nullptr; }
  const NavCue*  nextTurn() const { return _upCount ? &_up[0] : nullptr; }
  float          distanceToNextTurn() const { return _upCount ? _up[0].dist - _along : NAN; }
  // Compass bearing from the rider back to the route, 0..360 degrees.
  float          bearingToRoute() const { return _bearingToRoute; }
  // Same thing relative to where the rider is pointing; NAN without a heading.
  float          relativeBearingToRoute() const;
  const CourseCue* cues() const { return _cues; }

  // Pop the next event for the UI. Returns None when there is nothing to say.
  CourseEvent takeEvent();

  const CoursePoint* points() const { return _pts; }

  // Project into the same local metric frame the points are stored in.
  MetresXY toXY(LatLon p) const;

  static const char* turnText(TurnType t);
  // minDeg is the angle below which this counts as going straight. Cue points
  // use a lower bar than bare geometry - see scanTurns().
  static TurnType    classifyAngle(float deg, float minDeg = NAV_TURN_MIN_DEG);

 private:
  CoursePoint* _pts = nullptr;
  CourseCue*   _cues = nullptr;
  uint16_t _count = 0, _cueCount = 0;
  char _name[40] = {0};
  char _file[40] = {0};
  char _error[48] = {0};

  double _lat0 = 0, _lon0 = 0;
  float  _latScale = 111132.0f;     // metres per degree of latitude at _lat0
  float  _lonScale = 71000.0f;      // metres per degree of longitude at _lat0
  float  _totalAscent = 0;

  // snap state
  uint16_t _nearIdx = 0;
  float    _crossTrack = 0, _along = 0;
  float    _snapX = 0, _snapY = 0;
  float    _bearingToRoute = NAN, _heading = NAN;
  bool     _off = false, _finished = false;
  float    _offTimer = 0, _onTimer = 0;
  CourseEvent _pending = CourseEvent::None;

  // guidance state
  NavCue   _up[NAV_LOOKAHEAD_CUES];
  uint8_t  _upCount = 0;
  float    _lastScanAlong = -1e9f;
  float    _stagedDist = NAN;
  uint8_t  _stage = 0;

  bool  alloc();
  void  finishLoad();
  float snapWindow(float px, float py, uint16_t from, uint16_t to,
                   uint16_t& bestIdx, float& bestAlong) const;
  uint16_t indexAtDistance(float d) const;
  float turnAngleAt(uint16_t i) const;
  void  scanTurns();
  static TurnType typeFromText(const char* s);
};

extern Course g_course;
