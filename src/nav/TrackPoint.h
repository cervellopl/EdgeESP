#pragma once
#include "nav/Geo.h"

// A single breadcrumb. Shared between the ride recorder (which produces them)
// and the navigator (which can turn them back into a route home), so it lives
// here rather than in either one.
// Stored as floats: 2048 of them, and metre-ish resolution is plenty for a
// breadcrumb. latLon() is how it enters any code that does real geodesy.
struct TrackPoint {
  float lat, lon;
  LatLon latLon() const { return {(double)lat, (double)lon}; }
};
