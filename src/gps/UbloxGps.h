#pragma once
#include <Arduino.h>
#include "nav/Geo.h"

// Minimal UBX-only driver for u-blox NEO-M8/M9/M10.
// We turn NMEA off entirely and live on a single UBX-NAV-PVT message, which
// already carries time, fix quality, position, height and velocity.

struct GpsFix {
  bool     valid       = false;   // 3D fix with usable accuracy
  bool     timeValid   = false;
  uint8_t  fixType     = 0;       // 0 none, 2 = 2D, 3 = 3D
  uint8_t  numSV       = 0;
  double   lat         = 0;       // degrees
  double   lon         = 0;
  float    altMSL      = 0;       // metres above mean sea level
  float    speed       = 0;       // ground speed, m/s
  float    heading     = 0;       // degrees
  float    hAcc        = 0;       // horizontal accuracy, metres
  float    pDOP        = 0;
  uint32_t unixTime    = 0;       // UTC seconds
  uint16_t millisPart  = 0;
  uint32_t lastUpdateMs = 0;

  LatLon latLon() const { return {lat, lon}; }
};

class UbloxGps {
 public:
  bool begin(HardwareSerial& port, int rxPin, int txPin, uint32_t targetBaud);
  // Feed the parser. Returns true when a fresh NAV-PVT was decoded.
  bool update();

  const GpsFix& fix() const { return _fix; }
  uint32_t detectedBaud() const { return _baud; }
  bool     configured()   const { return _configured; }
  // Seconds since the last decoded message - used to flag a dead receiver.
  uint32_t staleSeconds() const {
    return _fix.lastUpdateMs ? (millis() - _fix.lastUpdateMs) / 1000 : 0xFFFF;
  }

 private:
  HardwareSerial* _ser = nullptr;
  GpsFix   _fix;
  uint32_t _baud = 0;
  bool     _configured = false;

  // UBX frame assembly
  uint8_t  _buf[128];
  uint16_t _len = 0, _payloadLen = 0;
  uint8_t  _state = 0, _cls = 0, _id = 0, _ckA = 0, _ckB = 0;

  void sendUbx(uint8_t cls, uint8_t id, const uint8_t* payload, uint16_t len);
  bool waitAck(uint8_t cls, uint8_t id, uint32_t timeoutMs);
  bool probeBaud(uint32_t baud);
  void configure(uint32_t targetBaud);
  void configureValset(uint32_t targetBaud);
  void configureLegacy(uint32_t targetBaud);
  void decodePvt(const uint8_t* p);
};
