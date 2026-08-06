// Host-side exercise of the real weather line parser and wind maths.
#include "sensors/Weather.h"
#include <stdio.h>
#include <math.h>

unsigned long g_fakeMillis = 100000;

static int failures = 0, checks = 0;
static void ok(bool cond, const char* what, const char* detail = "") {
  checks++;
  if (!cond) { failures++; printf("  FAIL  %s  %s\n", what, detail); }
  else       { printf("  ok    %s %s\n", what, detail); }
}
static void near(double got, double want, double tol, const char* what) {
  char d[128];
  snprintf(d, sizeof(d), "(got %.3f, want %.3f)", got, want);
  ok(fabs(got - want) <= tol, what, d);
}
static void head(const char* s) { printf("\n== %s ==\n", s); }

static Weather w;

int main() {
  head("WX line - every field");
  const char* line =
      "WX t=18.4 f=17.1 h=62 p=1013 ws=5.2 wd=270 g=9.1 c=40 pr=0.2 uv=3 "
      "code=61 sr=1754194800 ss=1754250000|Light rain";
  ok(w.feedLine(line), "line accepted");
  WeatherNow n = w.now();
  ok(w.valid(), "marked valid");
  near(n.tempC, 18.4, 0.01, "temperature");
  near(n.feelsC, 17.1, 0.01, "apparent temperature");
  near(n.humidity, 62, 0.01, "humidity");
  // The keys p, pp and pr all start with the same letter - a sloppy scanner
  // reads the wrong one, and pressure silently becomes a rainfall figure.
  near(n.pressureHpa, 1013, 0.01, "pressure, not confused with pr=");
  near(n.windMps, 5.2, 0.01, "wind speed");
  ok(n.windFromDeg == 270, "wind direction");
  near(n.gustMps, 9.1, 0.01, "gusts");
  near(n.cloudsPct, 40, 0.01, "cloud cover");
  near(n.precipMm, 0.2, 0.01, "precipitation");
  near(n.uv, 3, 0.01, "uv");
  ok(n.code == 61, "weather code");
  ok(n.sunriseUnix == 1754194800UL, "sunrise");
  ok(n.sunsetUnix == 1754250000UL, "sunset");
  ok(!strcmp(n.desc, "Light rain"), "description after the bar");

  head("partial and malformed lines");
  w.clear();
  ok(w.feedLine("WX t=9.0 ws=3.0 wd=45 code=3"), "sparse line accepted");
  n = w.now();
  near(n.tempC, 9.0, 0.01, "temperature read");
  ok(isnan(n.feelsC), "missing field stays NAN");
  ok(isnan(n.humidity), "another missing field stays NAN");
  ok(!strcmp(n.desc, "Overcast"), "description filled in from the code");
  ok(!w.feedLine("START"), "a command line is not claimed");
  ok(!w.feedLine("NOTIFY hi|there"), "a notification is not claimed");
  ok(!w.feedLine(""), "an empty line is not claimed");
  ok(w.feedLine("WX code=0"), "code-only line accepted");
  ok(!w.valid(), "...but a line with no temperature or wind is not valid data");

  head("wind resolved against heading");
  w.clear();
  w.feedLine("WX t=15 ws=10 wd=270");     // 10 m/s from the west
  near(w.relativeWindDeg(270), 0, 0.1, "heading into it: relative 0");
  near(w.headwind(270), 10.0, 0.05, "full headwind riding west");
  near(w.crosswind(270), 0.0, 0.05, "no crosswind riding west");
  near(w.headwind(90), -10.0, 0.05, "full tailwind riding east");
  // Facing south with a westerly, the wind is on your right shoulder.
  near(w.crosswind(180), 10.0, 0.05, "crosswind from the right riding south");
  near(w.headwind(180), 0.0, 0.05, "no headwind component riding south");
  near(w.crosswind(0), -10.0, 0.05, "crosswind from the left riding north");
  // 45 degrees off: components are wind / sqrt(2) each.
  near(w.headwind(225), 7.071, 0.02, "quartering headwind");
  near(w.crosswind(225), 7.071, 0.02, "quartering crosswind");
  ok(isnan(w.headwind(NAN)), "no headwind without a heading");
  w.clear();
  ok(isnan(w.headwind(90)), "no headwind without weather");

  head("air density");
  near(Weather::airDensity(1013.25f, 15.0f), 1.225, 0.002, "ISA sea level");
  near(Weather::airDensity(795.0f, 5.0f), 0.995, 0.01, "2000 m on a cold day");
  near(Weather::airDensity(NAN, 15.0f), 1.225, 0.001, "falls back when pressure is unknown");
  near(Weather::airDensity(3.0f, 15.0f), 1.225, 0.001, "falls back on a nonsense reading");

  head("hourly forecast");
  w.clear();
  w.feedLine("WX t=15 ws=3 wd=180 pr=0.0 code=2");
  for (int i = 0; i < 12; i++) {
    char b[96];
    // Dry for three hours, then a wet band from hour 3 to hour 5.
    snprintf(b, sizeof(b), "WXH %d t=%.1f ws=4.0 wd=200 pr=%.2f pp=%d code=%d",
             i, 15.0 + i * 0.5, (i >= 3 && i <= 5) ? 1.2 : 0.0,
             (i >= 3 && i <= 5) ? 80 : 5, (i >= 3 && i <= 5) ? 63 : 2);
    w.feedLine(b);
  }
  ok(w.hourCount() == 12, "twelve slots stored");
  near(w.hour(0).tempC, 15.0, 0.01, "first slot temperature");
  near(w.hour(11).tempC, 20.5, 0.01, "last slot temperature");
  near(w.hour(4).precipMm, 1.2, 0.01, "wet slot precipitation");
  ok(w.hour(4).code == 63, "wet slot code");
  ok(!w.hour(15).valid, "out-of-range slot is not valid");

  head("rain alerting");
  g_fakeMillis += 2000;
  w.tick();
  printf("       minutesToRain=%d\n", w.minutesToRain());
  ok(w.minutesToRain() == 180, "rain found three hours out");
  ok(w.takeEvent() == WeatherEvent::None, "three hours out is too far to warn");

  // Shift the band forward so it lands inside the alert window.
  for (int i = 0; i < 12; i++) {
    char b[96];
    snprintf(b, sizeof(b), "WXH %d t=16.0 ws=4.0 wd=200 pr=%.2f pp=%d code=%d",
             i, (i == 1) ? 1.2 : 0.0, (i == 1) ? 80 : 5, (i == 1) ? 63 : 2);
    w.feedLine(b);
  }
  g_fakeMillis += 2000;
  w.tick();
  ok(w.minutesToRain() == 60, "rain an hour out");
  ok(w.takeEvent() == WeatherEvent::RainSoon, "RainSoon raised");
  g_fakeMillis += 2000;
  w.tick();
  ok(w.takeEvent() == WeatherEvent::None, "and not repeated every second");

  head("staleness");
  w.clear();
  w.feedLine("WX t=12 ws=2 wd=90");
  ok(!w.stale(), "fresh after a push");
  ok(w.ageMinutes() == 0, "zero minutes old");
  g_fakeMillis += (WEATHER_STALE_MIN - 1) * 60000UL;
  ok(!w.stale(), "still fresh just inside the window");
  g_fakeMillis += 2 * 60000UL;
  ok(w.stale(), "stale once past it");
  printf("       age=%lu min\n", (unsigned long)w.ageMinutes());
  ok(w.ageMinutes() == WEATHER_STALE_MIN + 1, "age reported correctly");
  ok(w.valid(), "stale data is still valid data");

  head("weather code mapping");
  ok(Weather::iconFor(0)  == WxIcon::Clear,     "0 is clear");
  ok(Weather::iconFor(2)  == WxIcon::PartCloud, "2 is partly cloudy");
  ok(Weather::iconFor(48) == WxIcon::Fog,       "48 is fog");
  ok(Weather::iconFor(53) == WxIcon::Drizzle,   "53 is drizzle");
  ok(Weather::iconFor(81) == WxIcon::Rain,      "81 is a shower");
  ok(Weather::iconFor(75) == WxIcon::Snow,      "75 is snow");
  ok(Weather::iconFor(99) == WxIcon::Storm,     "99 is a storm");
  ok(Weather::iconFor(-1) == WxIcon::Cloud,     "unknown falls back to cloud");
  ok(!strcmp(Weather::textFor(65), "Heavy rain"), "65 reads as heavy rain");

  printf("\n%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
