#pragma once
#include <Arduino.h>
#include "config.h"

// Non-blocking piezo patterns. A bike computer must never stall its render loop
// to make a noise, so patterns are played from tick() as a small state machine.
class Beeper {
 public:
  struct Note { uint16_t freq, ms; };   // freq 0 = silence

  void begin();
  void tick();

  void chirp();        // lap, button confirm
  void alert();        // off course - three urgent notes
  void cue();          // turn coming up - two rising notes
  void done();         // course finished / ride saved

  void mute(bool m) { _muted = m; }

 private:
  const Note* _seq = nullptr;
  uint8_t  _len = 0, _idx = 0;
  uint32_t _nextMs = 0;
  bool     _muted = false;

  void play(const Note* seq, uint8_t len);
};

extern Beeper g_beeper;
