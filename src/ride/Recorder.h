#pragma once
#include <Arduino.h>
#include <SD.h>
#include "ride/FitEncoder.h"
#include "ride/RideComputer.h"
#include "ride/RideCheckpoint.h"

// Owns the SD card and turns RideState into a .fit (+ .gpx) on disk.
class Recorder {
 public:
  bool begin();                       // mounts the shield's microSD slot
  bool mounted() const { return _mounted; }

  bool startRide(const RideState& s);
  // --- crash and flat-battery recovery ---
  // Look for a ride that was never finished. Does not touch anything.
  bool findCheckpoint(RideCheckpoint& out);
  // Reopen that ride's files and carry on appending to them.
  bool resumeRide(const RideCheckpoint& c);
  // Close it off properly instead, leaving a readable .fit behind.
  bool finishAbandoned(const RideCheckpoint& c, const RideComputer& rc);
  void clearCheckpoint();
  void tick(const RideState& s);      // call often; writes one record per REC_INTERVAL_MS
  // Takes the finished lap's summary rather than the live computer: by the time
  // an auto-lap is noticed, the counters it was built from have been reset.
  void markLap(const FitSummary& s);
  bool stopRide(const RideComputer& rc);
  void discard();

  bool active() const { return _active; }
  const char* fitPath() const { return _fitPath; }
  uint32_t recordCount() const { return _records; }
  uint64_t cardSizeMb() const { return _cardMb; }

 private:
  bool _mounted = false, _active = false;
  File _fit, _gpx;
  FitEncoder _enc;
  char _fitPath[40] = {0};
  char _gpxPath[40] = {0};
  uint32_t _records = 0, _lastRecMs = 0, _lastFlushMs = 0;
  uint16_t _laps = 0;
  uint64_t _cardMb = 0;

  char _rstPath[44] = {0};
  uint32_t _lastCkptMs = 0;
  void writeCheckpoint(const RideState& s);
  void writeGpxHeader();
  void writeGpxPoint(const RideState& s);
  void closeGpx();
};
