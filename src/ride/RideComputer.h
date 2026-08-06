#pragma once
#include <Arduino.h>
#include "config.h"
#include "gps/UbloxGps.h"
#include "ride/FitEncoder.h"
#include "nav/TrackPoint.h"
#include "nav/Geo.h"
#include "ride/Zones.h"
#include "ride/ElevationProfile.h"

enum class RideStatus : uint8_t { Idle, Running, AutoPaused, Paused, Stopped };

// A completed lap, kept so the lap page has something to show. The FIT file
// gets every lap regardless; this is only what fits in RAM for display.
struct LapRecord {
  uint16_t index = 0;          // 1-based, as the rider counts them
  float    distance = 0;       // m
  uint32_t movingMs = 0;
  float    ascent = 0;
  float    avgSpeed = 0;       // m/s
  float    maxSpeed = 0;
  uint8_t  avgHr = 0, maxHr = 0;
  uint16_t avgPower = 0, maxPower = 0;
  uint8_t  avgCadence = 0;
  float    calories = 0;       // kcal
  uint32_t startUnix = 0;
};

// Everything the UI, the recorder and the phone link read from.
struct RideState {
  RideStatus status = RideStatus::Idle;

  // live
  float    speed        = 0;      // m/s
  float    altitude     = 0;      // m (baro-corrected when available)
  float    grade        = 0;      // %
  float    temperature  = NAN;    // degC
  uint8_t  hr           = 0;      // 0 = no sensor
  uint8_t  cadence      = 0;
  uint16_t power        = 0;
  bool     hasHr = false, hasCad = false, hasPwr = false;

  // totals
  double   distance     = 0;      // m
  uint32_t elapsedMs    = 0;      // wall clock since start
  uint32_t movingMs     = 0;      // timer time (excludes pauses)
  float    ascent       = 0;      // m
  float    descent      = 0;
  float    maxSpeed     = 0;
  float    avgSpeed     = 0;      // distance / movingTime
  uint16_t maxHr = 0, maxCad = 0, maxPower = 0;
  uint32_t hrSum = 0, cadSum = 0, pwrSum = 0, sampleCount = 0;
  uint32_t pwrSamples = 0;
  float    calories     = 0;      // kcal
  float    energyKj     = 0;

  // Time in each training zone, milliseconds. Only ticks while the timer runs.
  uint32_t zoneMs[Zones::POWER_COUNT] = {0};
  uint32_t hrZoneMs[Zones::HR_COUNT]  = {0};
  uint32_t cadZoneMs[Zones::CAD_COUNT] = {0};
  // Freewheeling. Kept out of the cadence zones so a descent is not filed as
  // an hour of grinding.
  uint32_t coastingMs = 0;

  // 3s / 30s rolling power, like a head unit shows
  uint16_t power3s = 0, power30s = 0;
  uint16_t normalizedPower = 0;

  // lap
  uint16_t lapCount     = 1;
  double   lapDistance  = 0;
  uint32_t lapMovingMs  = 0;
  float    lapAscent    = 0;
  float    lapMaxSpeed  = 0;
  uint32_t lapHrSum = 0, lapPwrSum = 0, lapSamples = 0, lapPwrSamples = 0;
  uint32_t lapCadSum    = 0;
  uint16_t lapMaxHr = 0, lapMaxPower = 0, lapMaxCad = 0;
  float    lapAvgSpeed  = 0;
  float    lapEnergyKj  = 0;

  // gps/system
  GpsFix   fix;
  bool     recording    = false;
  uint16_t batteryMv    = 0;
  uint8_t  batteryPct   = 0;
  bool     charging     = false;
  uint32_t startUnix    = 0;
};

// Breadcrumb points for the map page come from nav/TrackPoint.h - the
// navigator needs the same type to build a route home out of them.

class RideComputer {
 public:
  void begin();
  // Call at the GPS solution rate. dtMs is time since the previous call.
  void update(const GpsFix& fix, float baroAlt, float temperature, uint32_t dtMs);
  void setSensors(bool hasHr, uint8_t hr, bool hasCad, uint8_t cad, bool hasPwr, uint16_t pwr);
  // Air the rider is actually pushing through. headwind is m/s (negative for a
  // tailwind), density kg/m3. Both fall back to still, standard air when NAN.
  void setAir(float headwindMps, float densityKgM3);

  void start();
  void pause();
  void resume();
  void stop();
  void reset();
  void lap();
  // Put back the totals from a checkpoint after a power loss. The fix is
  // not restored - the old position is stale and the GPS will supply a new
  // one - and neither are the breadcrumb or the lap list, which are display
  // data the .fit file already holds properly.
  void restore(const RideState& s, const ElevationProfile& p);

  RideState& state() { return _s; }
  const RideState& state() const { return _s; }

  // Completed laps, oldest first. On a very long ride only the most recent
  // MAX_LAP_RECORDS are kept for display; lapsCompleted() is the true total.
  static const uint8_t MAX_LAP_RECORDS = 64;
  uint8_t  lapRecordCount() const { return _lapRecCount; }
  uint16_t lapsCompleted() const { return _lapsCompleted; }
  const LapRecord& lapRecord(uint8_t i) const { return _lapRec[i]; }
  // The lap currently being ridden, built from the live counters.
  LapRecord currentLap() const;
  // True once after any lap completes, manual or automatic, with the finished
  // lap ready for the FIT writer. Auto-laps happen inside update(), so this is
  // the only way the recorder hears about them.
  bool takeLapEvent(FitSummary& out);
  // Index into lapRecord() of the fastest completed lap, or -1.
  int8_t bestLapIndex() const;

  // The ride's elevation against distance. Lives here so it is cleared with
  // the ride and fed from the same distance the ride computer already tracks.
  const ElevationProfile& profile() const { return _profile; }

  const TrackPoint* track() const { return _track; }
  uint16_t trackCount() const { return _trackCount; }
  void trackBounds(float& minLat, float& maxLat, float& minLon, float& maxLon) const;

  // Snapshot ready for the FIT writer.
  void fillLapSummary(FitSummary& out) const;
  void fillSessionSummary(FitSummary& out) const;

 private:
  RideState _s;
  double _lastLat = NAN, _lastLon = NAN;
  float  _lastAlt = NAN, _altFiltered = NAN;
  float  _gradeDistAcc = 0, _gradeAltAcc = 0;
  float  _headwind = 0, _airDensity = 1.225f;
  uint32_t _lapStartUnix = 0;
  uint32_t _lapStartMs = 0;
  float    _lapStartLat = NAN, _lapStartLon = NAN;
  double   _autoLapMark = 0;

  ElevationProfile _profile;

  LapRecord _lapRec[MAX_LAP_RECORDS];
  uint8_t   _lapRecCount = 0;
  uint16_t  _lapsCompleted = 0;
  bool      _lapEvent = false;
  FitSummary _lastLapSummary{};

  TrackPoint _track[TRACK_BUFFER_POINTS];
  uint16_t   _trackCount = 0;
  float      _lastTrackLat = NAN, _lastTrackLon = NAN;

  // rolling power windows
  uint16_t _pwrRing[30] = {0};
  uint8_t  _pwrIdx = 0;
  double   _npSum = 0;          // sum of (30s avg)^4 for normalized power
  uint32_t _npCount = 0;

  void pushTrack(float lat, float lon);
  void updatePower(uint16_t w);
  void accumulate(float dtSec);
};

// haversine() / bearingDeg() live in nav/Geo.h, included above.
