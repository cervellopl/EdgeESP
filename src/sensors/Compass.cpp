#include "sensors/Compass.h"
#include <Wire.h>
#include <Preferences.h>
#include <math.h>

Compass g_compass;

static Preferences s_prefs;

static const uint8_t QMC_ADDR = 0x0D;
static const uint8_t HMC_ADDR = 0x1E;

// --------------------------------------------------------------------------
static bool wr(uint8_t addr, uint8_t reg, uint8_t val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

static bool rd(uint8_t addr, uint8_t reg, uint8_t* buf, uint8_t n) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(addr, n) != n) return false;
  for (uint8_t i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}

static bool ping(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

bool Compass::begin() {
#if !COMPASS_ENABLE
  return false;
#else
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, 400000);

  if (ping(QMC_ADDR)) {
    // QMC5883L: soft reset, then continuous / 200 Hz / 8 G / 512 OSR.
    // Register 0x0B must be 0x01 or the chip silently returns nothing.
    wr(QMC_ADDR, 0x0A, 0x80);
    delay(10);
    wr(QMC_ADDR, 0x0B, 0x01);
    if (wr(QMC_ADDR, 0x09, 0x1D)) {
      _chip = MagChip::QMC5883L;
      _addr = QMC_ADDR;
    }
  } else if (ping(HMC_ADDR)) {
    uint8_t id[3] = {0, 0, 0};
    rd(HMC_ADDR, 0x0A, id, 3);
    if (id[0] == 'H' && id[1] == '4' && id[2] == '3') {
      wr(HMC_ADDR, 0x00, 0x70);   // 8-sample average, 15 Hz
      wr(HMC_ADDR, 0x01, 0x20);   // gain +/-1.3 Ga
      wr(HMC_ADDR, 0x02, 0x00);   // continuous measurement
      _chip = MagChip::HMC5883L;
      _addr = HMC_ADDR;
    }
  }

  if (_chip != MagChip::None) loadCalibration();
  return present();
#endif
}

const char* Compass::chipName() const {
  switch (_chip) {
    case MagChip::QMC5883L: return "QMC5883L";
    case MagChip::HMC5883L: return "HMC5883L";
    default: return "none";
  }
}

void Compass::loadCalibration() {
  s_prefs.begin("compass", false);
  _calibrated = s_prefs.getBool("ok", false);
  if (_calibrated) {
    _minX = s_prefs.getShort("mnx", 0); _maxX = s_prefs.getShort("mxx", 0);
    _minY = s_prefs.getShort("mny", 0); _maxY = s_prefs.getShort("mxy", 0);
    _minZ = s_prefs.getShort("mnz", 0); _maxZ = s_prefs.getShort("mxz", 0);
    if (_maxX <= _minX || _maxY <= _minY) _calibrated = false;
  }
  s_prefs.end();
}

bool Compass::readRaw(int16_t& x, int16_t& y, int16_t& z) {
  uint8_t b[6];
  if (_chip == MagChip::QMC5883L) {
    if (!rd(_addr, 0x00, b, 6)) return false;
    x = (int16_t)(b[0] | (b[1] << 8));
    y = (int16_t)(b[2] | (b[3] << 8));
    z = (int16_t)(b[4] | (b[5] << 8));
    return true;
  }
  if (_chip == MagChip::HMC5883L) {
    if (!rd(_addr, 0x03, b, 6)) return false;
    // Note the axis order: this part returns X, Z, Y, not X, Y, Z.
    x = (int16_t)((b[0] << 8) | b[1]);
    z = (int16_t)((b[2] << 8) | b[3]);
    y = (int16_t)((b[4] << 8) | b[5]);
    return true;
  }
  return false;
}

void Compass::update() {
  if (!present()) return;
  int16_t x, y, z;
  if (!readRaw(x, y, z)) return;
  // -4096 is the saturation marker on the HMC part; a saturated axis gives a
  // heading that is confidently wrong, which is the worst kind.
  if (x == -4096 || y == -4096 || z == -4096) return;
  _x = x; _y = y; _z = z;

  if (_calibrating) {
    _minX = min(_minX, x); _maxX = max(_maxX, x);
    _minY = min(_minY, y); _maxY = max(_maxY, y);
    _minZ = min(_minZ, z); _maxZ = max(_maxZ, z);
    // Track which 30 degree sectors of the raw circle we have actually visited,
    // so "calibrated" means a real turn rather than a wiggle.
    float a = atan2f((float)y, (float)x);
    int sec = (int)((a + (float)M_PI) / (2.0f * (float)M_PI) * 12.0f) % 12;
    _sectors |= (uint16_t)(1u << sec);
  }

  float h = magneticHeading();
  if (isnan(h)) { _haveSmooth = false; return; }
  // Smooth as a vector, never as an angle: averaging 359 and 1 as numbers gives
  // 180, which would swing the needle to due south once per revolution.
  float s = sinf(radians(h)), c = cosf(radians(h));
  if (!_haveSmooth) { _smoothSin = s; _smoothCos = c; _haveSmooth = true; }
  else {
    _smoothSin += (s - _smoothSin) * 0.25f;
    _smoothCos += (c - _smoothCos) * 0.25f;
  }
}

float Compass::magneticHeading() const {
  if (!present() || !_calibrated) return NAN;
  if (_maxX <= _minX || _maxY <= _minY) return NAN;

  // Hard-iron: shift each axis to be centred on zero. Soft-iron: scale them to
  // a common span, which turns the ellipse the readings trace into a circle.
  float cx = (_maxX + _minX) * 0.5f, cy = (_maxY + _minY) * 0.5f;
  float sx = (_maxX - _minX) * 0.5f, sy = (_maxY - _minY) * 0.5f;
  if (sx < 1 || sy < 1) return NAN;
  float avg = (sx + sy) * 0.5f;
  float nx = ((float)_x - cx) * (avg / sx);
  float ny = ((float)_y - cy) * (avg / sy);

  float h = degrees(atan2f(ny, nx)) + COMPASS_MOUNT_OFFSET_DEG;
  while (h < 0) h += 360.0f;
  while (h >= 360.0f) h -= 360.0f;
  return h;
}

float Compass::trueHeading() const {
  if (!present() || !_calibrated) return NAN;
  float h;
  if (_haveSmooth) {
    h = degrees(atan2f(_smoothSin, _smoothCos));
  } else {
    h = magneticHeading();
    if (isnan(h)) return NAN;
  }
  h += MAG_DECLINATION_DEG;
  while (h < 0) h += 360.0f;
  while (h >= 360.0f) h -= 360.0f;
  return h;
}

// --------------------------------------------------------------------------
void Compass::startCalibration() {
  if (!present()) return;
  _calibrating = true;
  _sectors = 0;
  _minX = _minY = _minZ = 32767;
  _maxX = _maxY = _maxZ = -32768;
  _haveSmooth = false;
}

void Compass::cancelCalibration() {
  _calibrating = false;
  loadCalibration();          // put the previous figures back
}

uint8_t Compass::coverage() const {
  uint8_t n = 0;
  for (int i = 0; i < 12; i++) if (_sectors & (1u << i)) n++;
  return (uint8_t)(n * 100 / 12);
}

bool Compass::saveCalibration() {
  _calibrating = false;
  // Under three quarters of the circle and the ellipse fit is guesswork, so
  // refuse rather than store a calibration that will point the wrong way.
  if (coverage() < 75 || _maxX <= _minX || _maxY <= _minY) {
    loadCalibration();
    return false;
  }
  s_prefs.begin("compass", false);
  s_prefs.putShort("mnx", _minX); s_prefs.putShort("mxx", _maxX);
  s_prefs.putShort("mny", _minY); s_prefs.putShort("mxy", _maxY);
  s_prefs.putShort("mnz", _minZ); s_prefs.putShort("mxz", _maxZ);
  s_prefs.putBool("ok", true);
  s_prefs.end();
  _calibrated = true;
  _haveSmooth = false;
  return true;
}
