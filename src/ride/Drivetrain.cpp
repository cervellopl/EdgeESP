#include "ride/Drivetrain.h"
#include <math.h>

Drivetrain g_drive;

// A short list covering what most people actually ride. Editing the table is a
// rebuild; picking from it is a setting.
static const DrivetrainSpec kPresets[] = {
  {"50/34 x 11-28",  {50, 34, 0}, 2, {11,12,13,14,15,16,17,19,21,24,28}, 11},
  {"50/34 x 11-32",  {50, 34, 0}, 2, {11,12,13,14,16,18,20,22,25,28,32}, 11},
  {"52/36 x 11-30",  {52, 36, 0}, 2, {11,12,13,14,15,17,19,21,24,27,30}, 11},
  {"48/32 x 11-34",  {48, 32, 0}, 2, {11,13,15,17,19,21,23,25,27,30,34}, 11},
  {"46/30 x 11-36",  {46, 30, 0}, 2, {11,13,15,17,19,21,24,27,30,33,36}, 11},
  {"1x 40 x 10-42",  {40,  0, 0}, 1, {10,12,14,16,18,21,24,28,32,36,42}, 11},
  {"1x 42 x 11-46",  {42,  0, 0}, 1, {11,13,15,17,19,21,24,28,32,37,46}, 11},
  {"1x 38 x 10-52",  {38,  0, 0}, 1, {10,12,14,16,18,21,24,28,33,39,45,52}, 12},
};
static const uint8_t kPresetCount = sizeof(kPresets) / sizeof(kPresets[0]);

// Settings validates against the constant so it need not link this file.
static_assert(sizeof(kPresets) / sizeof(kPresets[0]) == DRIVETRAIN_PRESET_COUNT,
              "DRIVETRAIN_PRESET_COUNT in config.h is out of step with the table");

uint8_t Drivetrain::presetCount() { return kPresetCount; }

const DrivetrainSpec& Drivetrain::preset(uint8_t i) {
  return kPresets[i < kPresetCount ? i : 0];
}

void Drivetrain::setPreset(uint8_t i) {
  if (i >= kPresetCount) i = 0;
  if (i != _preset) { _preset = i; reset(); }
}

void Drivetrain::reset() {
  _valid = _ambiguous = false;
  _ratio = _development = _gearInches = NAN;
  _ring = _sprocket = -1;
  _candRing = _candSprocket = -1;
  _candMs = 0;
}

uint8_t Drivetrain::ringTeeth() const {
  return _ring >= 0 ? spec().rings[_ring] : 0;
}
uint8_t Drivetrain::sprocketTeeth() const {
  return _sprocket >= 0 ? spec().sprockets[_sprocket] : 0;
}

float Drivetrain::gearInchesOf(uint8_t ring, uint8_t sprocket, uint16_t wheelMm) {
  // Gear inches is the ratio times the wheel diameter in inches, and the
  // diameter comes from the circumference we already know.
  float d = (wheelMm / 1000.0f) / (float)M_PI / 0.0254f;
  return ratioOf(ring, sprocket) * d;
}

bool Drivetrain::crossChained() const {
  const DrivetrainSpec& s = spec();
  if (s.ringCount < 2 || _ring < 0 || _sprocket < 0) return false;  // 1x cannot
  bool bigRing = (_ring == 0);
  if (bigRing && _sprocket >= s.sprocketCount - 2) return true;     // big-big
  if (!bigRing && _sprocket <= 1) return true;                      // small-small
  return false;
}

void Drivetrain::update(float speedMps, uint8_t cadence, uint16_t wheelMm, uint32_t dtMs) {
  const DrivetrainSpec& s = spec();

  // Below these the ratio is a division by noise. Freewheeling reports a
  // cadence of zero, which would be an infinite gear.
  if (cadence < 25 || speedMps < 2.0f || !wheelMm) {
    _valid = false;
    _ratio = _development = _gearInches = NAN;
    _candRing = _candSprocket = -1;
    return;
  }

  float circumference = wheelMm / 1000.0f;
  float wheelRps = speedMps / circumference;
  float crankRps = cadence / 60.0f;
  float ratio = wheelRps / crankRps;
  if (!(ratio > 0.3f && ratio < 8.0f)) { _valid = false; return; }

  _ratio = ratio;
  _development = ratio * circumference;
  _gearInches = ratio * circumference / (float)M_PI / 0.0254f;

  // Find the closest combination, and the runner-up, so we can tell whether the
  // answer was clear-cut.
  int8_t bestR = -1, bestS = -1;
  float bestErr = 1e9f, secondErr = 1e9f;
  int8_t secondR = -1;
  for (uint8_t r = 0; r < s.ringCount; r++) {
    for (uint8_t c = 0; c < s.sprocketCount; c++) {
      float err = fabsf(ratioOf(s.rings[r], s.sprockets[c]) - ratio) / ratio;
      // Staying on the same chainring is far more likely than jumping to
      // another that happens to give the same ratio, so bias toward it. This
      // resolves most of the overlap on a 2x without pretending it is certain.
      if (_ring >= 0 && r == (uint8_t)_ring) err *= 0.75f;
      if (err < bestErr) {
        secondErr = bestErr; secondR = bestR;
        bestErr = err; bestR = (int8_t)r; bestS = (int8_t)c;
      } else if (err < secondErr) {
        secondErr = err; secondR = (int8_t)r;
      }
    }
  }
  if (bestR < 0) { _valid = false; return; }

  // A measured ratio further than this from any real gear means the wheel size
  // or the cassette is wrong, and naming a gear would be fiction.
  if (bestErr > 0.08f) {
    _valid = false;
    _candRing = _candSprocket = -1;
    return;
  }

  // Hold a new candidate briefly before showing it.
  if (bestR != _candRing || bestS != _candSprocket) {
    _candRing = bestR;
    _candSprocket = bestS;
    _candMs = 0;
  } else {
    _candMs += dtMs;
  }
  if (_candMs >= 600 || !_valid) {
    _ring = _candRing;
    _sprocket = _candSprocket;
    _valid = true;
  }

  _ambiguous = (secondR >= 0 && secondR != bestR && secondErr < bestErr * 1.25f);
}
