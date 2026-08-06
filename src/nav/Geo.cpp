#include "nav/Geo.h"
#include <math.h>

double haversine(LatLon a, LatLon b) {
  const double R = 6371008.8;
  double dLat = radians(b.lat - a.lat), dLon = radians(b.lon - a.lon);
  double h = sin(dLat / 2) * sin(dLat / 2) +
             cos(radians(a.lat)) * cos(radians(b.lat)) * sin(dLon / 2) * sin(dLon / 2);
  return 2 * R * atan2(sqrt(h), sqrt(1 - h));
}

double bearingDeg(LatLon from, LatLon to) {
  double p1 = radians(from.lat), p2 = radians(to.lat), dl = radians(to.lon - from.lon);
  double y = sin(dl) * cos(p2);
  double x = cos(p1) * sin(p2) - sin(p1) * cos(p2) * cos(dl);
  double b = degrees(atan2(y, x));
  return b < 0 ? b + 360.0 : b;
}

float relativeBearing(float bearing, float heading) {
  if (isnan(bearing) || isnan(heading)) return NAN;
  float d = bearing - heading;
  while (d > 180.0f) d -= 360.0f;
  while (d < -180.0f) d += 360.0f;
  return d;
}

const char* cardinalName(float deg) {
  static const char* k[16] = {"N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
                              "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"};
  if (isnan(deg)) return "--";
  // Each name owns 22.5 degrees centred on its bearing, so the boundary sits
  // half a sector before it - 348.75 must already read as N, not NNW.
  while (deg < 0) deg += 360.0f;
  while (deg >= 360.0f) deg -= 360.0f;
  return k[(int)((deg + 11.25f) / 22.5f) % 16];
}
