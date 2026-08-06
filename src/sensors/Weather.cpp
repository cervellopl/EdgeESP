#include "sensors/Weather.h"
#include <math.h>

Weather g_weather;

// Weather lines arrive on the NimBLE host task and are read by the render loop,
// so every commit and every read runs inside this.
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

static inline float wrap180(float d) {
  while (d > 180.0f) d -= 360.0f;
  while (d < -180.0f) d += 360.0f;
  return d;
}

// --------------------------------------------------------------------------
// key=value scanner. Deliberately tolerant: an unknown key is skipped, a
// missing key leaves the field NAN, and the companion can grow the format
// without a firmware change.
// Note the double: a unix timestamp does not survive a float. At 1.75e9 the
// spacing between representable floats is 128 seconds, so sr= and ss= would
// land minutes away from the real sunrise.
static bool findKey(const char* s, const char* key, double& out) {
  size_t kl = strlen(key);
  for (const char* p = s; *p; p++) {
    if ((p == s || p[-1] == ' ') && !strncmp(p, key, kl) && p[kl] == '=') {
      out = atof(p + kl + 1);
      return true;
    }
  }
  return false;
}

bool Weather::feedLine(const char* line) {
  if (!line) return false;

  // ---- WXH <slot> ... : one hour of forecast ----
  if (!strncmp(line, "WXH ", 4)) {
    const char* p = line + 4;
    int slot = atoi(p);
    if (slot < 0 || slot >= WEATHER_MAX_HOURS) return true;
    WeatherHour h;
    double v;
    if (findKey(p, "t",    v)) h.tempC = v;
    if (findKey(p, "ws",   v)) h.windMps = v;
    if (findKey(p, "wd",   v)) h.windFromDeg = (int16_t)v;
    if (findKey(p, "pr",   v)) h.precipMm = v;
    if (findKey(p, "pp",   v)) h.precipProb = v;
    if (findKey(p, "code", v)) h.code = (int16_t)v;
    h.valid = true;

    portENTER_CRITICAL(&s_mux);
    _hours[slot] = h;
    if (slot + 1 > _hourCount) _hourCount = slot + 1;
    portEXIT_CRITICAL(&s_mux);
    return true;
  }

  // ---- WX ... |description : current conditions ----
  if (!strncmp(line, "WX ", 3)) {
    const char* p = line + 3;
    WeatherNow n;
    double v;
    if (findKey(p, "t",    v)) n.tempC = v;
    if (findKey(p, "f",    v)) n.feelsC = v;
    if (findKey(p, "h",    v)) n.humidity = v;
    if (findKey(p, "p",    v)) n.pressureHpa = v;
    if (findKey(p, "ws",   v)) n.windMps = v;
    if (findKey(p, "wd",   v)) n.windFromDeg = (int16_t)v;
    if (findKey(p, "g",    v)) n.gustMps = v;
    if (findKey(p, "c",    v)) n.cloudsPct = v;
    if (findKey(p, "pr",   v)) n.precipMm = v;
    if (findKey(p, "uv",   v)) n.uv = v;
    if (findKey(p, "code", v)) n.code = (int16_t)v;
    if (findKey(p, "sr",   v)) n.sunriseUnix = (uint32_t)v;
    if (findKey(p, "ss",   v)) n.sunsetUnix = (uint32_t)v;

    const char* bar = strchr(p, '|');
    if (bar && bar[1]) {
      strncpy(n.desc, bar + 1, sizeof(n.desc) - 1);
      n.desc[sizeof(n.desc) - 1] = 0;
    } else if (n.code >= 0) {
      strncpy(n.desc, textFor(n.code), sizeof(n.desc) - 1);
    }

    n.updatedMs = millis();
    n.valid = !isnan(n.tempC) || !isnan(n.windMps);

    portENTER_CRITICAL(&s_mux);
    _now = n;
    portEXIT_CRITICAL(&s_mux);
    return true;
  }

  return false;
}

void Weather::clear() {
  portENTER_CRITICAL(&s_mux);
  _now = WeatherNow();
  for (auto& h : _hours) h = WeatherHour();
  _hourCount = 0;
  portEXIT_CRITICAL(&s_mux);
  _pending = WeatherEvent::None;
  _minsToRain = -1;
  _rainAnnounced = _wasRaining = false;
}

WeatherNow Weather::now() const {
  portENTER_CRITICAL(&s_mux);
  WeatherNow n = _now;
  portEXIT_CRITICAL(&s_mux);
  return n;
}

WeatherHour Weather::hour(uint8_t i) const {
  WeatherHour h;
  if (i >= WEATHER_MAX_HOURS) return h;
  portENTER_CRITICAL(&s_mux);
  h = _hours[i];
  portEXIT_CRITICAL(&s_mux);
  return h;
}

// --------------------------------------------------------------------------
float Weather::relativeWindDeg(float headingDeg) const {
  if (!_now.valid || _now.windFromDeg < 0 || isnan(headingDeg)) return NAN;
  return wrap180((float)_now.windFromDeg - headingDeg);
}

float Weather::headwind(float headingDeg) const {
  float rel = relativeWindDeg(headingDeg);
  if (isnan(rel) || isnan(_now.windMps)) return NAN;
  return _now.windMps * cosf(radians(rel));
}

float Weather::crosswind(float headingDeg) const {
  float rel = relativeWindDeg(headingDeg);
  if (isnan(rel) || isnan(_now.windMps)) return NAN;
  return _now.windMps * sinf(radians(rel));
}

// Ideal gas law on dry air. Humidity shifts this by well under a percent, which
// is far smaller than the error in any CdA you can guess for yourself.
float Weather::airDensity(float pressureHpa, float tempC) {
  if (isnan(pressureHpa) || isnan(tempC)) return 1.225f;
  float rho = (pressureHpa * 100.0f) / (287.05f * (tempC + 273.15f));
  return (rho > 0.7f && rho < 1.6f) ? rho : 1.225f;
}

// --------------------------------------------------------------------------
void Weather::tick() {
  uint32_t nowMs = millis();
  if (nowMs - _lastTickMs < 1000) return;
  _lastTickMs = nowMs;
  if (!_hourCount) { _minsToRain = -1; return; }

  // Slot 0 is the hour we are in, so its remaining minutes are unknown without
  // a wall clock. Treating each slot as a whole hour ahead is the conservative
  // reading - it never promises rain sooner than it might arrive.
  int16_t mins = -1;
  for (uint8_t i = 0; i < _hourCount; i++) {
    WeatherHour h = hour(i);
    if (!h.valid || isnan(h.precipMm)) continue;
    if (h.precipMm >= WEATHER_RAIN_ALERT_MM) { mins = (int16_t)(i * 60); break; }
  }
  _minsToRain = mins;

  bool rainingNow = _now.valid && !isnan(_now.precipMm) &&
                    _now.precipMm >= WEATHER_RAIN_ALERT_MM;

  if (mins >= 0 && mins <= WEATHER_RAIN_ALERT_MIN && !rainingNow && !_rainAnnounced) {
    _rainAnnounced = true;
    _pending = WeatherEvent::RainSoon;
  }
  // Re-arm once the wet window has passed, so a second front still warns.
  if (mins < 0 || mins > WEATHER_RAIN_ALERT_MIN + 30) _rainAnnounced = false;

  if (_wasRaining && !rainingNow && _pending == WeatherEvent::None)
    _pending = WeatherEvent::RainStopping;
  _wasRaining = rainingNow;
}

WeatherEvent Weather::takeEvent() {
  WeatherEvent e = _pending;
  _pending = WeatherEvent::None;
  return e;
}

// --------------------------------------------------------------------------
// WMO weather codes, as used by Open-Meteo and most European services.
WxIcon Weather::iconFor(int16_t c) {
  if (c < 0) return WxIcon::Cloud;
  if (c == 0) return WxIcon::Clear;
  if (c == 1 || c == 2) return WxIcon::PartCloud;
  if (c == 3) return WxIcon::Cloud;
  if (c == 45 || c == 48) return WxIcon::Fog;
  if (c >= 51 && c <= 57) return WxIcon::Drizzle;
  if ((c >= 61 && c <= 67) || (c >= 80 && c <= 82)) return WxIcon::Rain;
  if ((c >= 71 && c <= 77) || c == 85 || c == 86) return WxIcon::Snow;
  if (c >= 95) return WxIcon::Storm;
  return WxIcon::Cloud;
}

const char* Weather::textFor(int16_t c) {
  switch (c) {
    case 0:  return "Clear";
    case 1:  return "Mainly clear";
    case 2:  return "Partly cloudy";
    case 3:  return "Overcast";
    case 45: case 48: return "Fog";
    case 51: case 53: case 55: return "Drizzle";
    case 56: case 57: return "Freezing drizzle";
    case 61: return "Light rain";
    case 63: return "Rain";
    case 65: return "Heavy rain";
    case 66: case 67: return "Freezing rain";
    case 71: return "Light snow";
    case 73: return "Snow";
    case 75: return "Heavy snow";
    case 77: return "Snow grains";
    case 80: return "Light showers";
    case 81: return "Showers";
    case 82: return "Violent showers";
    case 85: case 86: return "Snow showers";
    case 95: return "Thunderstorm";
    case 96: case 99: return "Storm with hail";
    default: return "";
  }
}
