#include "ui/Beeper.h"

Beeper g_beeper;

static const uint8_t LEDC_CH = 6;   // channel 7 is the backlight

// A zero frequency means silence for that slot.
static const Beeper::Note kChirp[] = {{2400, 40}};
static const Beeper::Note kAlert[] = {{2200, 120}, {0, 60}, {2200, 120}, {0, 60}, {1800, 220}};
static const Beeper::Note kCue[]   = {{1800, 90}, {2600, 130}};
static const Beeper::Note kDone[]  = {{1800, 90}, {2200, 90}, {2900, 200}};

void Beeper::begin() {
#if BUZZER_PIN >= 0
  ledcSetup(LEDC_CH, 2000, 10);
  ledcAttachPin(BUZZER_PIN, LEDC_CH);
  ledcWrite(LEDC_CH, 0);
#endif
}

void Beeper::play(const Note* seq, uint8_t len) {
#if BUZZER_PIN >= 0
  if (_muted) return;
  _seq = seq; _len = len; _idx = 0; _nextMs = 0;
#else
  (void)seq; (void)len;
#endif
}

void Beeper::chirp() { play(kChirp, sizeof(kChirp) / sizeof(Note)); }
void Beeper::alert() { play(kAlert, sizeof(kAlert) / sizeof(Note)); }
void Beeper::cue()   { play(kCue,   sizeof(kCue)   / sizeof(Note)); }
void Beeper::done()  { play(kDone,  sizeof(kDone)  / sizeof(Note)); }

void Beeper::tick() {
#if BUZZER_PIN >= 0
  if (!_seq) return;
  uint32_t now = millis();
  if (_nextMs && now < _nextMs) return;

  if (_idx >= _len) {
    ledcWrite(LEDC_CH, 0);
    _seq = nullptr;
    return;
  }
  const Note& n = _seq[_idx++];
  if (n.freq) {
    ledcWriteTone(LEDC_CH, n.freq);
    ledcWrite(LEDC_CH, 512);      // 50 % duty is loudest on a passive piezo
  } else {
    ledcWrite(LEDC_CH, 0);
  }
  _nextMs = now + n.ms;
#endif
}
