#pragma once
#include <Arduino.h>
#include "config.h"

// Training Stress Score and the fitness/fatigue model built on it.
//
// TSS is Coggan's: an hour at threshold is 100. The load curves are the usual
// exponentially weighted averages of daily TSS - 42 days for fitness, 7 for
// fatigue - and form is the difference between them.
//
// Days are UTC days. The whole device runs on UTC (there is no timezone
// setting), so a ride finishing after midnight UTC lands on the next day.

class TrainingLoad {
 public:
  static const uint16_t DAYS = 90;      // history kept, index DAYS-1 is today

  void begin();                         // load history from NVS
  void save();

  // Roll the ring forward if the date has changed. Safe to call often.
  void setNow(uint32_t unixTime);
  bool haveDate() const { return _newestDay != 0; }

  // Add a finished ride's score to today.
  void addTss(float tss);

  float dayTss(uint16_t daysAgo) const;         // 0 = today
  float weekTss() const;                        // last 7 days including today
  float peak(uint16_t days) const;              // largest daily TSS in a window

  // extraToday folds an unsaved ride into the curves, which is what makes the
  // form figure answer "what will I be like tomorrow if I finish this".
  float ctl(float extraToday = 0) const;        // fitness, 42-day
  float atl(float extraToday = 0) const;        // fatigue, 7-day
  float tsb(float extraToday = 0) const { return ctl(extraToday) - atl(extraToday); }

  void clearHistory();

  // --- scoring a ride ---
  // Power is the real thing: normalised power against FTP.
  static float powerTss(uint32_t movingMs, uint16_t np, uint16_t ftp);
  // Heart rate is the fallback, and is explicitly a different number.
  static float hrTss(uint32_t movingMs, uint8_t avgHr, uint8_t lthr);
  static float intensityFactor(uint16_t np, uint16_t ftp) {
    return ftp ? (float)np / (float)ftp : 0.0f;
  }

 private:
  uint16_t _tss10[DAYS] = {0};          // TSS x10, so 0.1 resolution in 2 bytes
  uint32_t _newestDay = 0;              // days since the unix epoch
};

extern TrainingLoad g_load;
