#pragma once
#include <Arduino.h>
#include <math.h>

// Pure geodesy and bearing formatting. No hardware, no state - which is what
// makes it testable on the host, and these are exactly the functions where a
// swapped term or a missing wrap goes unnoticed until you are lost.

// A position on the earth, and a position in a local metric frame. They are
// separate types on purpose: both are a pair of numbers, both get passed to
// projection helpers, and mixing them up compiles perfectly happily when
// they are bare floats. Asking the compiler to tell them apart costs
// nothing and catches the mistake at the call site.
struct LatLon {
  double lat = NAN, lon = NAN;
  bool valid() const { return !isnan(lat) && !isnan(lon); }
};

struct MetresXY {
  float x = 0, y = 0;
};

// Great-circle distance in metres.
double haversine(LatLon a, LatLon b);

// Initial great-circle bearing, degrees true, 0..360.
double bearingDeg(LatLon from, LatLon to);

// Signed difference b - a, wrapped to -180..180.
float relativeBearing(float bearing, float heading);

// 16-point cardinal name for a bearing: "N", "WSW", ... "--" if NAN.
const char* cardinalName(float deg);
