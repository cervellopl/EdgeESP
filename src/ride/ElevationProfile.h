#pragma once
#include <Arduino.h>

// The ride's elevation against **distance**, which is what an elevation profile
// is. Sampling against time instead makes ten minutes at a cafe into a flat
// line occupying a sixth of the graph.
//
// The sample interval starts fine and doubles whenever the buffer fills, so a
// 5 km spin and a 300 km audax both draw a full-width profile without needing
// a buffer sized for the worst case.

class ElevationProfile {
 public:
  static const uint16_t SAMPLES = 512;
  static constexpr float BASE_INTERVAL_M = 20.0f;

  void clear();
  // Call with cumulative ride distance and current altitude. Stores a point
  // each time another interval has been covered. Returns true if it stored.
  bool sample(double distanceM, float altitudeM);

  uint16_t count() const { return _count; }
  float intervalM() const { return _interval; }
  float distanceAt(uint16_t i) const { return i * _interval; }
  float elevationAt(uint16_t i) const;                 // NAN when absent
  float coveredM() const { return _count ? (_count - 1) * _interval : 0; }

  float minElevation() const { return _count ? _minE : NAN; }
  float maxElevation() const { return _count ? _maxE : NAN; }

  // Gradient at sample i, measured across at least windowM so the 0.5 m
  // storage resolution does not turn into a 2.5 % quantisation step.
  float gradeAt(uint16_t i, float windowM = 100.0f) const;

 private:
  // Half-metre units: 0.5 m resolution and a range that still covers the
  // Himalaya, which decimetres would not.
  int16_t  _buf[SAMPLES];
  uint16_t _count = 0;
  float    _interval = BASE_INTERVAL_M;
  double   _nextDist = 0;
  float    _minE = NAN, _maxE = NAN;

  void decimate();
  void recomputeRange();
};
