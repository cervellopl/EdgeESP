// Host-side exercise of the environment history ring and the pressure trend.
#include "sensors/EnvHistory.h"
#include <stdio.h>
#include <math.h>
#include <string.h>

unsigned long g_fakeMillis = 0;

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

static EnvHistory eh;
static const uint32_t MIN = EnvHistory::INTERVAL_MS;

// Feed n minutes of samples, temperature and pressure moving linearly.
static void feed(EnvHistory& e, uint32_t& t, int minutes,
                 float t0, float dT, float p0, float dP, float rh = 55.0f) {
  for (int i = 0; i < minutes; i++) {
    e.sample(t0 + dT * i, p0 + dP * i, rh, t);
    t += MIN;
  }
}

int main() {
  uint32_t now = 100000;

  head("sampling is rate limited");
  eh.clear();
  ok(eh.count() == 0, "starts empty");
  ok(eh.sample(20.0f, 1013.0f, 50.0f, now), "the first sample is taken immediately");
  ok(eh.count() == 1, "and stored");
  ok(!eh.sample(20.0f, 1013.0f, 50.0f, now + 1000), "a second later is too soon");
  ok(!eh.sample(20.0f, 1013.0f, 50.0f, now + MIN - 1), "so is a millisecond early");
  ok(eh.sample(21.0f, 1013.0f, 50.0f, now + MIN), "a minute later is taken");
  ok(eh.count() == 2, "two samples");

  head("readback");
  float t, p, rh;
  ok(eh.get(0, t, p, rh), "the oldest sample reads back");
  near(t, 20.0, 0.06, "temperature, to the tenth it is stored at");
  near(p, 1013.0, 0.06, "pressure");
  near(rh, 50.0, 0.5, "humidity");
  ok(eh.get(1, t, p, rh) && fabs(t - 21.0) < 0.06, "and so does the newest");
  ok(!eh.get(2, t, p, rh), "reading past the end fails rather than inventing data");

  head("min and max track the window");
  eh.clear();
  now = 0;
  feed(eh, now, 10, 15.0f, 1.0f, 1010.0f, 0.0f);   // 15..24 degrees
  near(eh.minTemp(), 15.0, 0.06, "lowest seen");
  near(eh.maxTemp(), 24.0, 0.06, "highest seen");

  head("absent readings are not zeros");
  eh.clear();
  now = 0;
  eh.sample(NAN, NAN, NAN, now); now += MIN;
  ok(eh.get(0, t, p, rh), "a sample with nothing in it is still a slot");
  ok(isnan(t) && isnan(p) && isnan(rh), "and reads back as NAN, not as zero");
  ok(isnan(eh.minTemp()) || eh.count() == 1, "a NAN temperature does not become the minimum");
  eh.sample(18.0f, 1005.0f, 60.0f, now); now += MIN;
  near(eh.minTemp(), 18.0, 0.06, "the first real reading sets the range");
  // Out-of-range values are sensor faults, not data.
  eh.sample(-300.0f, 5000.0f, 900.0f, now); now += MIN;
  ok(eh.get(2, t, p, rh) && isnan(t) && isnan(p) && isnan(rh),
     "impossible readings are discarded rather than stored");

  head("pressure trend");
  eh.clear();
  now = 0;
  float chg; uint16_t span;
  ok(!eh.pressureTrend(chg, span), "nothing to say with no history");
  // Two hours falling 1.5 hPa an hour.
  feed(eh, now, 121, 18.0f, 0.0f, 1015.0f, -0.025f);
  ok(eh.pressureTrend(chg, span), "a trend is available");
  printf("       %+.2f hPa over %u min -> \"%s\"\n", chg, span, eh.trendWord());
  near(span, 120, 0.5, "span is the window actually covered");
  near(chg, -3.0, 0.15, "pressure fell three hPa across it");
  // -3 hPa in 2 h is -4.5 per 3 h, which is the "very rapidly" band.
  ok(strstr(eh.trendWord(), "falling") != nullptr, "worded as falling");
  ok(strstr(eh.trendWord(), "very rapidly") != nullptr, "and at the fastest rate");

  head("a short window gives numbers but no wording");
  eh.clear();
  now = 0;
  feed(eh, now, 20, 18.0f, 0.0f, 1015.0f, -0.05f);
  ok(eh.pressureTrend(chg, span), "twenty minutes is enough for a figure");
  near(span, 19, 0.5, "over nineteen intervals");
  // Under an hour the slope is noise; extrapolating it to three hours would
  // turn a passing gust into a storm warning.
  ok(eh.trendWord()[0] == 0, "but not enough to put a word to it");

  head("steady pressure reads as steady");
  eh.clear();
  now = 0;
  feed(eh, now, 121, 18.0f, 0.0f, 1015.0f, 0.001f);   // +0.12 hPa in 2 h
  ok(!strcmp(eh.trendWord(), "steady"), "a tiny drift is steady, not rising");

  head("rising");
  eh.clear();
  now = 0;
  feed(eh, now, 181, 18.0f, 0.0f, 1000.0f, 0.01f);    // +1.8 hPa in 3 h
  printf("       \"%s\"\n", eh.trendWord());
  ok(strstr(eh.trendWord(), "rising") != nullptr, "worded as rising");
  ok(strstr(eh.trendWord(), "slowly") != nullptr, "at the slow rate");

  head("gaps in pressure do not corrupt the trend");
  eh.clear();
  now = 0;
  // A run with no pressure at either end: the trend must use the real readings,
  // not treat a missing sample as 0 hPa and report a 1000 hPa collapse.
  eh.sample(18.0f, NAN, 50.0f, now); now += MIN;
  feed(eh, now, 90, 18.0f, 0.0f, 1010.0f, -0.02f);
  eh.sample(18.0f, NAN, 50.0f, now); now += MIN;
  ok(eh.pressureTrend(chg, span), "a trend is still found");
  printf("       %+.2f hPa over %u min\n", chg, span);
  ok(fabs(chg) < 5.0f, "and it is a plausible number, not a phantom collapse");
  near(chg, -1.78, 0.15, "matching the real readings in between");

  head("the ring drops the oldest");
  eh.clear();
  now = 0;
  feed(eh, now, EnvHistory::SAMPLES + 60, 10.0f, 0.0f, 1000.0f, 0.0f);
  ok(eh.count() == EnvHistory::SAMPLES, "count stops at the buffer size");
  ok(eh.spanMinutes() == EnvHistory::SAMPLES - 1, "span is the window it holds");

  // The newest sample must be the newest, and the oldest must have moved on.
  eh.clear();
  now = 0;
  for (uint16_t i = 0; i < EnvHistory::SAMPLES + 5; i++) {
    eh.sample(10.0f + i * 0.1f, 1000.0f, 50.0f, now);
    now += MIN;
  }
  eh.get(eh.count() - 1, t, p, rh);
  near(t, 10.0f + (EnvHistory::SAMPLES + 4) * 0.1f, 0.06, "newest is the last one fed");
  eh.get(0, t, p, rh);
  near(t, 10.0f + 5 * 0.1f, 0.06, "and the first five were dropped");

  head("clear");
  eh.clear();
  ok(eh.count() == 0, "emptied");
  ok(isnan(eh.minTemp()), "range forgotten");
  ok(!eh.pressureTrend(chg, span), "trend forgotten");

  printf("\n%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
