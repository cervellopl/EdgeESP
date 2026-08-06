#include "sensors/Baro.h"
#include "config.h"
#include <Wire.h>
#include <Adafruit_BME280.h>

static Adafruit_BME280 s_bme;

bool Baro::begin() {
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, 400000);
  _ok = s_bme.begin(BME280_ADDR, &Wire) || s_bme.begin(0x77, &Wire);
  if (_ok) {
    // Weather-station-ish sampling with heavy IIR: we want a stable altitude,
    // not a fast one. Vibration on a bike is brutal on raw pressure readings.
    s_bme.setSampling(Adafruit_BME280::MODE_NORMAL,
                      Adafruit_BME280::SAMPLING_X1,    // temperature
                      Adafruit_BME280::SAMPLING_X16,   // pressure
                      Adafruit_BME280::SAMPLING_X1,    // humidity
                      Adafruit_BME280::FILTER_X16,
                      Adafruit_BME280::STANDBY_MS_125);
  }
  return _ok;
}

void Baro::update() {
  if (!_ok) return;
  uint32_t now = millis();
  if (now - _lastMs < 200) return;
  _lastMs = now;

  _press = s_bme.readPressure() / 100.0f;
  _temp  = s_bme.readTemperature();
  _hum   = s_bme.readHumidity();
  if (_press > 300 && _press < 1200) {
    _alt = 44330.0f * (1.0f - powf(_press / _seaLevel, 0.1903f));
  }
}

void Baro::calibrateToGps(float gpsAltMsl) {
  if (!_ok || isnan(gpsAltMsl) || _press < 300) return;
  // Sea-level pressure that would put us exactly at the GPS altitude.
  float implied = _press / powf(1.0f - gpsAltMsl / 44330.0f, 5.255f);
  if (implied < 900 || implied > 1100) return;
  // 0.2 % per call: with a 1 Hz caller that is a ~10 minute time constant, slow
  // enough that a climb is never mistaken for a weather change.
  _seaLevel += (implied - _seaLevel) * 0.002f;
}
