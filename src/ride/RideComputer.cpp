#include "ride/RideComputer.h"
#include "Settings.h"
#include <math.h>

void RideComputer::begin() { reset(); }

void RideComputer::reset() {
  _s = RideState();
  _lastLat = _lastLon = NAN;
  _lastAlt = _altFiltered = NAN;
  _gradeDistAcc = _gradeAltAcc = 0;
  _lapStartUnix = 0; _lapStartMs = 0;
  _lapStartLat = _lapStartLon = NAN;
  _autoLapMark = 0;
  _lapRecCount = 0;
  _lapsCompleted = 0;
  _lapEvent = false;
  _profile.clear();
  _trackCount = 0;
  _lastTrackLat = _lastTrackLon = NAN;
  memset(_pwrRing, 0, sizeof(_pwrRing));
  _pwrIdx = 0; _npSum = 0; _npCount = 0;
}

void RideComputer::restore(const RideState& s, const ElevationProfile& p) {
  reset();
  GpsFix keep = _s.fix;         // whatever the GPS has said since boot
  _s = s;
  _s.fix = keep;
  _profile = p;
  // The ride is not running until the rider says so, but it is recording:
  // the file is open and the totals are real.
  _s.status = RideStatus::Paused;
  _s.recording = true;
  _autoLapMark = _s.distance;
  _lapStartUnix = _s.startUnix;
}

void RideComputer::start() {
  if (_s.status == RideStatus::Idle || _s.status == RideStatus::Stopped) {
    _s.startUnix = _s.fix.timeValid ? _s.fix.unixTime : 0;
    _lapStartUnix = _s.startUnix;
    _lapStartMs = 0;
    _lapStartLat = _s.fix.valid ? _s.fix.lat : NAN;
    _lapStartLon = _s.fix.valid ? _s.fix.lon : NAN;
  }
  _s.status = RideStatus::Running;
  _s.recording = true;
}

void RideComputer::pause()  { if (_s.recording) _s.status = RideStatus::Paused; }
void RideComputer::resume() { if (_s.recording) _s.status = RideStatus::Running; }
void RideComputer::stop()   { _s.status = RideStatus::Stopped; _s.recording = false; }

LapRecord RideComputer::currentLap() const {
  LapRecord r;
  r.index      = _s.lapCount;
  r.distance   = (float)_s.lapDistance;
  r.movingMs   = _s.lapMovingMs;
  r.ascent     = _s.lapAscent;
  r.avgSpeed   = _s.lapAvgSpeed;
  r.maxSpeed   = _s.lapMaxSpeed;
  r.avgHr      = _s.lapSamples && _s.lapHrSum ? (uint8_t)(_s.lapHrSum / _s.lapSamples) : 0;
  r.maxHr      = (uint8_t)_s.lapMaxHr;
  r.avgPower   = _s.lapPwrSamples ? (uint16_t)(_s.lapPwrSum / _s.lapPwrSamples) : 0;
  r.maxPower   = _s.lapMaxPower;
  r.avgCadence = _s.lapSamples && _s.lapCadSum ? (uint8_t)(_s.lapCadSum / _s.lapSamples) : 0;
  r.calories   = _s.lapEnergyKj;
  r.startUnix  = _lapStartUnix;
  return r;
}

int8_t RideComputer::bestLapIndex() const {
  int8_t best = -1;
  float bestSpeed = 0;
  for (uint8_t i = 0; i < _lapRecCount; i++) {
    // A lap has to be long enough to mean something - pressing LAP twice at a
    // junction should not crown a two-metre lap as the fastest of the ride.
    if (_lapRec[i].distance < 200.0f) continue;
    if (_lapRec[i].avgSpeed > bestSpeed) { bestSpeed = _lapRec[i].avgSpeed; best = (int8_t)i; }
  }
  return best;
}

bool RideComputer::takeLapEvent(FitSummary& out) {
  if (!_lapEvent) return false;
  _lapEvent = false;
  out = _lastLapSummary;
  return true;
}

void RideComputer::lap() {
  // Snapshot the lap that just ended before the counters are cleared. The FIT
  // summary has to be taken here too: fillLapSummary() reads the live counters,
  // so anything asking for it after the reset would get an empty lap.
  LapRecord done = currentLap();
  fillLapSummary(_lastLapSummary);
  _lapEvent = true;
  _lapsCompleted++;
  if (_lapRecCount < MAX_LAP_RECORDS) {
    _lapRec[_lapRecCount++] = done;
  } else {
    // Full: drop the oldest. Mid-ride the recent laps are the ones being
    // compared, and the FIT file still has every one of them.
    memmove(&_lapRec[0], &_lapRec[1], sizeof(LapRecord) * (MAX_LAP_RECORDS - 1));
    _lapRec[MAX_LAP_RECORDS - 1] = done;
  }

  _s.lapCount++;
  _s.lapDistance = 0;
  _s.lapMovingMs = 0;
  _s.lapAscent = 0;
  _s.lapMaxSpeed = 0;
  _s.lapHrSum = _s.lapPwrSum = _s.lapSamples = _s.lapPwrSamples = 0;
  _s.lapCadSum = 0;
  _s.lapMaxHr = _s.lapMaxPower = _s.lapMaxCad = 0;
  _s.lapAvgSpeed = 0;
  _s.lapEnergyKj = 0;
  _lapStartUnix = _s.fix.timeValid ? _s.fix.unixTime : 0;
  _lapStartMs = _s.movingMs;
  _lapStartLat = _s.fix.valid ? _s.fix.lat : NAN;
  _lapStartLon = _s.fix.valid ? _s.fix.lon : NAN;
  _autoLapMark = _s.distance;
}

void RideComputer::setSensors(bool hasHr, uint8_t hr, bool hasCad, uint8_t cad,
                              bool hasPwr, uint16_t pwr) {
  _s.hasHr = hasHr;   if (hasHr)  _s.hr = hr;       else _s.hr = 0;
  _s.hasCad = hasCad; if (hasCad) _s.cadence = cad; else _s.cadence = 0;
  _s.hasPwr = hasPwr; if (hasPwr) _s.power = pwr;   else _s.power = 0;
}

void RideComputer::setAir(float headwindMps, float densityKgM3) {
  _headwind   = isnan(headwindMps) ? 0.0f : constrain(headwindMps, -25.0f, 25.0f);
  _airDensity = isnan(densityKgM3) ? 1.225f : densityKgM3;
}

void RideComputer::pushTrack(float lat, float lon) {
  // Only store a point once we have moved ~10 m, otherwise the buffer fills
  // with jitter while stopped at lights.
  if (!isnan(_lastTrackLat)) {
    if (haversine({_lastTrackLat, _lastTrackLon}, {lat, lon}) < 10.0) return;
  }
  if (_trackCount >= TRACK_BUFFER_POINTS) {
    // Halve the resolution in place and keep going - a long ride still fits.
    for (uint16_t i = 0; i < TRACK_BUFFER_POINTS / 2; i++) _track[i] = _track[i * 2];
    _trackCount = TRACK_BUFFER_POINTS / 2;
  }
  _track[_trackCount++] = {lat, lon};
  _lastTrackLat = lat; _lastTrackLon = lon;
}

void RideComputer::trackBounds(float& minLat, float& maxLat,
                               float& minLon, float& maxLon) const {
  minLat = minLon = 1e9f; maxLat = maxLon = -1e9f;
  for (uint16_t i = 0; i < _trackCount; i++) {
    minLat = min(minLat, _track[i].lat); maxLat = max(maxLat, _track[i].lat);
    minLon = min(minLon, _track[i].lon); maxLon = max(maxLon, _track[i].lon);
  }
}

void RideComputer::updatePower(uint16_t w) {
  _pwrRing[_pwrIdx] = w;
  _pwrIdx = (_pwrIdx + 1) % 30;

  uint32_t sum3 = 0;
  for (int i = 1; i <= 3; i++) sum3 += _pwrRing[(_pwrIdx + 30 - i) % 30];
  _s.power3s = sum3 / 3;

  uint32_t sum30 = 0;
  for (int i = 0; i < 30; i++) sum30 += _pwrRing[i];
  _s.power30s = sum30 / 30;

  // Normalized power: 4th power of the 30 s rolling average, averaged, 4th root.
  double a = _s.power30s;
  _npSum += a * a * a * a;
  _npCount++;
  if (_npCount) _s.normalizedPower = (uint16_t)pow(_npSum / _npCount, 0.25);
}

void RideComputer::accumulate(float dtSec) {
  _s.sampleCount++;

  // Time in zone uses instantaneous power, not the 3 s average: the smoothed
  // figure is for holding a target, the raw one is what the ride was made of.
  uint32_t dtMs = (uint32_t)lroundf(dtSec * 1000.0f);
  if (_s.hasPwr)
    _s.zoneMs[Zones::powerZoneFor(_s.power, g_settings.ftpWatts)] += dtMs;
  if (_s.hasHr && _s.hr)
    _s.hrZoneMs[Zones::hrZoneFor(_s.hr, g_settings.lthrBpm)] += dtMs;
  if (_s.hasCad) {
    if (_s.cadence) _s.cadZoneMs[Zones::cadenceZoneFor(_s.cadence)] += dtMs;
    else            _s.coastingMs += dtMs;
  }

  if (_s.hasHr && _s.hr) {
    _s.hrSum += _s.hr;
    _s.maxHr = max<uint16_t>(_s.maxHr, _s.hr);
    _s.lapHrSum += _s.hr; _s.lapMaxHr = max<uint16_t>(_s.lapMaxHr, _s.hr);
  }
  if (_s.hasCad) {
    _s.cadSum += _s.cadence;
    _s.maxCad = max<uint16_t>(_s.maxCad, _s.cadence);
    _s.lapCadSum += _s.cadence;
    _s.lapMaxCad = max<uint16_t>(_s.lapMaxCad, _s.cadence);
  }
  if (_s.hasPwr) {
    _s.pwrSum += _s.power; _s.pwrSamples++;
    _s.maxPower = max<uint16_t>(_s.maxPower, _s.power);
    _s.lapPwrSum += _s.power; _s.lapPwrSamples++;
    _s.lapMaxPower = max<uint16_t>(_s.lapMaxPower, _s.power);
    float dE = _s.power * dtSec / 1000.0f;
    _s.energyKj += dE;
    _s.lapEnergyKj += dE;
  }
  _s.lapSamples++;

  if (_s.hasPwr) {
    // ~24 % gross efficiency makes kJ and kcal numerically interchangeable.
    _s.calories = _s.energyKj;
  } else {
    // No meter: estimate from a rolling-resistance + aero + gravity model.
    // Aero work is done against the air you move through, not the ground, so
    // the drag term uses air speed squared times ground speed. A 5 m/s headwind
    // roughly doubles the aero cost at 25 km/h - ignoring it makes a windy ride
    // read far too cheap.
    float v = _s.speed;
    float vAir = max(0.0f, v + _headwind);
    float mass = g_settings.totalMassKg();
    float pRoll = RIDER_CRR * mass * 9.81f * v;
    float pAero = 0.5f * _airDensity * RIDER_CDA * vAir * vAir * v;
    float pGrav = mass * 9.81f * v * (_s.grade / 100.0f);
    float p = max(0.0f, pRoll + pAero + pGrav);
    float dE = p * dtSec / 1000.0f;
    _s.energyKj += dE;
    _s.lapEnergyKj += dE;
    _s.calories = _s.energyKj;
  }
}

void RideComputer::update(const GpsFix& fix, float baroAlt, float temperature, uint32_t dtMs) {
  _s.fix = fix;
  if (!isnan(temperature)) _s.temperature = temperature;

  float dt = dtMs / 1000.0f;
  if (dt <= 0 || dt > 10) dt = 0;

  // Speed: prefer the wheel sensor if we have one, GPS otherwise. A wheel
  // sensor keeps working in tunnels and under trees.
  _s.speed = fix.valid ? fix.speed : 0.0f;

  // Altitude: the barometer is the reference, slowly leashed to GPS MSL so it
  // does not drift with the weather over a long ride.
  float alt = !isnan(baroAlt) ? baroAlt : (fix.valid ? fix.altMSL : NAN);
  if (!isnan(alt)) {
    if (isnan(_altFiltered)) _altFiltered = alt;
    _altFiltered += (alt - _altFiltered) * 0.15f;   // ~1 s time constant at 5 Hz
    _s.altitude = _altFiltered;
  }

  bool running = (_s.status == RideStatus::Running);

  if (running && _s.recording && dt > 0) {
    bool moving = _s.speed >= MOVING_SPEED_MPS;

    // Auto-pause mirrors Edge behaviour: stop the timer when you stop rolling.
    if (g_settings.autoPause) {
      if (moving) _s.status = RideStatus::Running;
      else if (_s.speed < AUTOPAUSE_SPEED_MPS) _s.status = RideStatus::AutoPaused;
    }

    if (moving) {
      _s.movingMs += dtMs;
      _s.lapMovingMs += dtMs;

      double step = 0;
      if (fix.valid && !isnan(_lastLat)) {
        double h = haversine({_lastLat, _lastLon}, fix.latLon());
        // Reject teleports; trust integrated speed when the jump is implausible.
        double byspeed = _s.speed * dt;
        step = (h > byspeed * 3 + 5) ? byspeed : h;
      } else {
        step = _s.speed * dt;
      }
      _s.distance += step;
      _s.lapDistance += step;

      // Ascent / descent with a 1 m deadband to kill barometric noise.
      if (!isnan(_s.altitude)) {
        if (isnan(_lastAlt)) _lastAlt = _s.altitude;
        float d = _s.altitude - _lastAlt;
        if (d > 1.0f)  { _s.ascent += d;  _s.lapAscent += d;  _lastAlt = _s.altitude; }
        if (d < -1.0f) { _s.descent += -d;                    _lastAlt = _s.altitude; }
      }

      // Grade over a 30 m window - short windows are pure noise.
      _gradeDistAcc += step;
      if (!isnan(_s.altitude)) _gradeAltAcc = _s.altitude - _lastAlt;
      if (_gradeDistAcc >= 30.0f) {
        float g = (_gradeAltAcc / _gradeDistAcc) * 100.0f;
        _s.grade += (constrain(g, -30.0f, 30.0f) - _s.grade) * 0.4f;
        _gradeDistAcc = 0;
      }

      _s.maxSpeed = max(_s.maxSpeed, _s.speed);
      _s.lapMaxSpeed = max(_s.lapMaxSpeed, _s.speed);

      if (fix.valid) pushTrack((float)fix.lat, (float)fix.lon);
      // Indexed by distance, so it only advances when the wheels do.
      _profile.sample(_s.distance, _s.altitude);
    }

    _s.elapsedMs += dtMs;
    accumulate(dt);

    if (g_settings.autoLapM && (_s.distance - _autoLapMark) >= g_settings.autoLapM) lap();
  } else if (_s.recording && dt > 0) {
    _s.elapsedMs += dtMs;   // elapsed keeps running through pauses
  }

  if (fix.valid) { _lastLat = fix.lat; _lastLon = fix.lon; }

  _s.avgSpeed    = _s.movingMs    ? (float)(_s.distance    / (_s.movingMs / 1000.0)) : 0;
  _s.lapAvgSpeed = _s.lapMovingMs ? (float)(_s.lapDistance / (_s.lapMovingMs / 1000.0)) : 0;

  static uint32_t lastPwrTick = 0;
  if (millis() - lastPwrTick >= 1000) {
    lastPwrTick = millis();
    if (running) updatePower(_s.hasPwr ? _s.power : 0);
  }
}

void RideComputer::fillLapSummary(FitSummary& o) const {
  memset(&o, 0, sizeof(o));
  o.start_time  = FitEncoder::toFitTime(_lapStartUnix ? _lapStartUnix : _s.startUnix);
  o.timestamp   = FitEncoder::toFitTime(_s.fix.timeValid ? _s.fix.unixTime : _s.startUnix);
  o.elapsed_ms  = _s.lapMovingMs;
  o.timer_ms    = _s.lapMovingMs;
  o.distance_cm = (uint32_t)(_s.lapDistance * 100.0);
  o.ascent_m    = (uint16_t)_s.lapAscent;
  o.descent_m   = 0;
  o.avg_speed_mms = (uint16_t)(_s.lapAvgSpeed * 1000.0f);
  o.max_speed_mms = (uint16_t)(_s.lapMaxSpeed * 1000.0f);
  o.avg_hr  = _s.lapSamples && _s.lapHrSum ? (uint8_t)(_s.lapHrSum / _s.lapSamples) : 0xFF;
  o.max_hr  = _s.lapMaxHr ? (uint8_t)_s.lapMaxHr : 0xFF;
  o.avg_cad = 0xFF;
  o.max_cad = 0xFF;
  o.avg_power = _s.lapPwrSamples ? (uint16_t)(_s.lapPwrSum / _s.lapPwrSamples) : 0xFFFF;
  o.max_power = _s.lapMaxPower ? _s.lapMaxPower : 0xFFFF;
  o.calories  = (uint16_t)_s.calories;
  o.start_lat_semi = isnan(_lapStartLat) ? INT32_MAX : FitEncoder::toSemicircles(_lapStartLat);
  o.start_lon_semi = isnan(_lapStartLon) ? INT32_MAX : FitEncoder::toSemicircles(_lapStartLon);
}

void RideComputer::fillSessionSummary(FitSummary& o) const {
  memset(&o, 0, sizeof(o));
  o.start_time  = FitEncoder::toFitTime(_s.startUnix);
  o.timestamp   = FitEncoder::toFitTime(_s.fix.timeValid ? _s.fix.unixTime : _s.startUnix);
  o.elapsed_ms  = _s.elapsedMs;
  o.timer_ms    = _s.movingMs;
  o.distance_cm = (uint32_t)(_s.distance * 100.0);
  o.ascent_m    = (uint16_t)_s.ascent;
  o.descent_m   = (uint16_t)_s.descent;
  o.avg_speed_mms = (uint16_t)(_s.avgSpeed * 1000.0f);
  o.max_speed_mms = (uint16_t)(_s.maxSpeed * 1000.0f);
  o.avg_hr  = _s.sampleCount && _s.hrSum ? (uint8_t)(_s.hrSum / _s.sampleCount) : 0xFF;
  o.max_hr  = _s.maxHr ? (uint8_t)_s.maxHr : 0xFF;
  o.avg_cad = _s.sampleCount && _s.cadSum ? (uint8_t)(_s.cadSum / _s.sampleCount) : 0xFF;
  o.max_cad = _s.maxCad ? (uint8_t)_s.maxCad : 0xFF;
  o.avg_power = _s.pwrSamples ? (uint16_t)(_s.pwrSum / _s.pwrSamples) : 0xFFFF;
  o.max_power = _s.maxPower ? _s.maxPower : 0xFFFF;
  o.calories  = (uint16_t)_s.calories;
  o.start_lat_semi = _trackCount ? FitEncoder::toSemicircles(_track[0].lat) : INT32_MAX;
  o.start_lon_semi = _trackCount ? FitEncoder::toSemicircles(_track[0].lon) : INT32_MAX;
}
