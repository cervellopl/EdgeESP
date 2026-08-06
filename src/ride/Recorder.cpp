#include "ride/Recorder.h"
#include "config.h"
#include <SPI.h>
#include <time.h>
#include <new>

static SPIClass sdSpi(HSPI);

bool Recorder::begin() {
  sdSpi.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  // The shield's slot has no level shifting on some revisions, so keep the
  // clock modest; 20 MHz is reliable on the ribbon lengths involved here.
  _mounted = SD.begin(SD_CS_PIN, sdSpi, 20000000);
  if (_mounted) {
    _cardMb = SD.cardSize() / (1024ULL * 1024ULL);
    if (!SD.exists("/rides")) SD.mkdir("/rides");
  }
  return _mounted;
}

// The recovery file sits beside the ride it describes, same name, .rst.
static void checkpointPathFor(const char* fitPath, char* out, size_t n) {
  snprintf(out, n, "%s", fitPath);
  char* dot = strrchr(out, '.');
  if (dot) *dot = 0;
  strncat(out, ".rst", n - strlen(out) - 1);
}

// Names the file from GPS UTC when we have it, from an incrementing counter
// when we do not - a ride must never fail to start because the sky is cloudy.
static void makeName(char* out, size_t n, uint32_t unixTime, const char* ext) {
  if (unixTime > 1600000000UL) {
    time_t t = (time_t)unixTime;
    struct tm tmv;
    gmtime_r(&t, &tmv);
    snprintf(out, n, "/rides/%04d%02d%02d_%02d%02d%02d.%s",
             tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
             tmv.tm_hour, tmv.tm_min, tmv.tm_sec, ext);
  } else {
    for (int i = 1; i < 1000; i++) {
      snprintf(out, n, "/rides/ride_%03d.%s", i, ext);
      if (!SD.exists(out)) return;
    }
  }
}

bool Recorder::startRide(const RideState& s) {
  if (!_mounted || _active) return false;

  uint32_t t = s.fix.timeValid ? s.fix.unixTime : 0;
  makeName(_fitPath, sizeof(_fitPath), t, "fit");
  makeName(_gpxPath, sizeof(_gpxPath), t, "gpx");

  _fit = SD.open(_fitPath, FILE_WRITE);
  if (!_fit) return false;
  _enc.begin(_fit, t ? t : 946684800UL);   // fall back to 2000-01-01

  _gpx = SD.open(_gpxPath, FILE_WRITE);
  if (_gpx) writeGpxHeader();

  checkpointPathFor(_fitPath, _rstPath, sizeof(_rstPath));
  _records = 0; _laps = 0; _lastRecMs = 0; _lastCkptMs = 0;
  _active = true;
  return true;
}

void Recorder::writeGpxHeader() {
  _gpx.print(F("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
               "<gpx version=\"1.1\" creator=\"EdgeESP\" "
               "xmlns=\"http://www.topografix.com/GPX/1/1\" "
               "xmlns:gpxtpx=\"http://www.garmin.com/xmlschemas/TrackPointExtension/v1\">\n"
               "<trk><name>EdgeESP ride</name><trkseg>\n"));
}

void Recorder::writeGpxPoint(const RideState& s) {
  if (!_gpx || !s.fix.valid) return;
  char buf[256];
  time_t t = (time_t)s.fix.unixTime;
  struct tm tmv; gmtime_r(&t, &tmv);
  int n = snprintf(buf, sizeof(buf),
      "<trkpt lat=\"%.7f\" lon=\"%.7f\"><ele>%.1f</ele>"
      "<time>%04d-%02d-%02dT%02d:%02d:%02dZ</time>",
      s.fix.lat, s.fix.lon, s.altitude,
      tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
      tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
  _gpx.write((const uint8_t*)buf, n);

  if (s.hasHr || s.hasCad || s.hasPwr) {
    _gpx.print(F("<extensions>"));
    if (s.hasPwr) { n = snprintf(buf, sizeof(buf), "<power>%u</power>", s.power);
                    _gpx.write((const uint8_t*)buf, n); }
    _gpx.print(F("<gpxtpx:TrackPointExtension>"));
    if (s.hasHr)  { n = snprintf(buf, sizeof(buf), "<gpxtpx:hr>%u</gpxtpx:hr>", s.hr);
                    _gpx.write((const uint8_t*)buf, n); }
    if (s.hasCad) { n = snprintf(buf, sizeof(buf), "<gpxtpx:cad>%u</gpxtpx:cad>", s.cadence);
                    _gpx.write((const uint8_t*)buf, n); }
    if (!isnan(s.temperature)) {
      n = snprintf(buf, sizeof(buf), "<gpxtpx:atemp>%.1f</gpxtpx:atemp>", s.temperature);
      _gpx.write((const uint8_t*)buf, n);
    }
    _gpx.print(F("</gpxtpx:TrackPointExtension></extensions>"));
  }
  _gpx.print(F("</trkpt>\n"));
}

void Recorder::closeGpx() {
  if (!_gpx) return;
  _gpx.print(F("</trkseg></trk></gpx>\n"));
  _gpx.close();
}

void Recorder::tick(const RideState& s) {
  if (!_active) return;
  uint32_t now = millis();
  if (now - _lastRecMs < REC_INTERVAL_MS) return;
  _lastRecMs = now;

  // Only the running state produces samples, so a paused ride does not fill the
  // file with a thousand identical points at a traffic light.
  if (s.status != RideStatus::Running) return;

  FitRecord r{};
  r.timestamp    = FitEncoder::toFitTime(s.fix.timeValid ? s.fix.unixTime
                                                         : s.startUnix + s.elapsedMs / 1000);
  r.lat_semi     = s.fix.valid ? FitEncoder::toSemicircles(s.fix.lat) : INT32_MAX;
  r.lon_semi     = s.fix.valid ? FitEncoder::toSemicircles(s.fix.lon) : INT32_MAX;
  r.distance_cm  = (uint32_t)(s.distance * 100.0);
  r.altitude_raw = isnan(s.altitude) ? 0xFFFF : FitEncoder::toAltitudeRaw(s.altitude);
  r.speed_mms    = (uint16_t)(s.speed * 1000.0f);
  r.heart_rate   = s.hasHr  ? s.hr      : 0xFF;
  r.cadence      = s.hasCad ? s.cadence : 0xFF;
  r.power        = s.hasPwr ? s.power   : 0xFFFF;
  r.temperature  = isnan(s.temperature) ? 0x7F : (int8_t)lroundf(s.temperature);
  r.grade_x100   = (int16_t)lroundf(s.grade * 100.0f);

  _enc.writeRecord(r);
  writeGpxPoint(s);
  _records++;

  // Flush every 15 s: often enough that a crash or a flat battery costs almost
  // nothing, rare enough not to thrash the card.
  if (now - _lastFlushMs > 15000) {
    _lastFlushMs = now;
    _fit.flush();
    if (_gpx) _gpx.flush();
    // The totals go down with the same beat. Without them the .fit file
    // survives a power cut but has no valid header, and nothing knows what
    // the ride had added up to.
    writeCheckpoint(s);
  }
}

void Recorder::markLap(const FitSummary& s) {
  if (!_active) return;
  _enc.writeLap(s, _laps++);
}

// --------------------------------------------------------------------------
// Crash recovery. A .rst file sits beside each ride while it is being written
// and is deleted when the ride is closed properly, so finding one at boot means
// the last ride ended with the power going away.
void Recorder::writeCheckpoint(const RideState& s) {
  if (!_active || !_rstPath[0]) return;
  // Allocated rather than stacked: RideCheckpoint carries a RideState and an
  // elevation profile, which is far too much for a task stack.
  RideCheckpoint* c = (RideCheckpoint*)malloc(sizeof(RideCheckpoint));
  if (!c) return;
  // Zeroed first so the padding between members is deterministic - it is
  // inside the region the checksum covers.
  memset(c, 0, sizeof(RideCheckpoint));
  new (c) RideCheckpoint();

  c->fitDataSize = _enc.dataBytes();
  c->writtenUnix = s.fix.timeValid ? s.fix.unixTime : 0;
  strncpy(c->fitPath, _fitPath, sizeof(c->fitPath) - 1);
  strncpy(c->gpxPath, _gpxPath, sizeof(c->gpxPath) - 1);
  c->state = s;
  checkpointSeal(*c);

  File f = SD.open(_rstPath, FILE_WRITE);
  if (f) {
    f.write((const uint8_t*)c, sizeof(RideCheckpoint));
    f.close();
  }
  c->~RideCheckpoint();
  free(c);
}

bool Recorder::findCheckpoint(RideCheckpoint& out) {
  if (!_mounted) return false;
  File dir = SD.open("/rides");
  if (!dir) return false;
  bool found = false;
  for (File f = dir.openNextFile(); f && !found; f = dir.openNextFile()) {
    if (f.isDirectory()) continue;
    const char* n = f.name();
    size_t l = strlen(n);
    if (l < 5 || strcasecmp(n + l - 4, ".rst") != 0) continue;
    if (f.size() != sizeof(RideCheckpoint)) continue;
    if (f.read((uint8_t*)&out, sizeof(RideCheckpoint)) != (int)sizeof(RideCheckpoint)) continue;
    if (checkpointValid(out)) found = true;
  }
  dir.close();
  return found;
}

bool Recorder::resumeRide(const RideCheckpoint& c) {
  if (!_mounted || _active) return false;
  // Opened read/write so the header can be rewritten when the ride is finally
  // closed; append mode would not allow the seek back to byte zero.
  _fit = SD.open(c.fitPath, "r+");
  if (!_fit) return false;
  if (!_enc.resume(_fit, c.fitDataSize)) { _fit.close(); return false; }

  strncpy(_fitPath, c.fitPath, sizeof(_fitPath) - 1);
  strncpy(_gpxPath, c.gpxPath, sizeof(_gpxPath) - 1);
  checkpointPathFor(_fitPath, _rstPath, sizeof(_rstPath));

  if (_gpxPath[0]) _gpx = SD.open(_gpxPath, FILE_APPEND);
  _records = 0;
  _laps = 0;
  _lastRecMs = 0;
  _active = true;
  return true;
}

bool Recorder::finishAbandoned(const RideCheckpoint& c, const RideComputer& rc) {
  if (!resumeRide(c)) return false;
  bool ok = stopRide(rc);
  clearCheckpoint();
  return ok;
}

void Recorder::clearCheckpoint() {
  if (_rstPath[0]) {
    SD.remove(_rstPath);
    _rstPath[0] = 0;
  }
}

bool Recorder::stopRide(const RideComputer& rc) {
  if (!_active) return false;

  FitSummary lap;
  rc.fillLapSummary(lap);
  _enc.writeLap(lap, _laps++);

  FitSummary sess;
  rc.fillSessionSummary(sess);
  bool ok = _enc.finalize(sess, _laps);

  _fit.close();
  closeGpx();
  // The ride is complete, so the recovery file has done its job.
  clearCheckpoint();
  _active = false;
  return ok;
}

void Recorder::discard() {
  if (!_active) return;
  _fit.close();
  closeGpx();
  SD.remove(_fitPath);
  SD.remove(_gpxPath);
  clearCheckpoint();
  // Forget the names too: the summary page shows the last saved file, and a
  // path to something just deleted would be a lie.
  _fitPath[0] = _gpxPath[0] = 0;
  _active = false;
}
