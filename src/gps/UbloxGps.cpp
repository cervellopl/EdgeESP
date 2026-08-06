#include "gps/UbloxGps.h"
#include "config.h"

static const uint8_t SYNC1 = 0xB5, SYNC2 = 0x62;

// --- little helpers over the UBX payload ----------------------------------
static inline uint16_t rdU2(const uint8_t* p) { return p[0] | (p[1] << 8); }
static inline uint32_t rdU4(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static inline int32_t  rdI4(const uint8_t* p) { return (int32_t)rdU4(p); }

// Days-from-civil (Howard Hinnant's algorithm) so we can build a UTC epoch
// without pulling in <time.h> mktime, which is timezone-sensitive.
static uint32_t toUnix(uint16_t y, uint8_t mo, uint8_t d, uint8_t h, uint8_t mi, uint8_t s) {
  int yy = y - (mo <= 2);
  int era = (yy >= 0 ? yy : yy - 399) / 400;
  unsigned yoe = (unsigned)(yy - era * 400);
  unsigned doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  int32_t days = (int32_t)(era * 146097 + (int)doe) - 719468;
  return (uint32_t)days * 86400UL + h * 3600UL + mi * 60UL + s;
}

void UbloxGps::sendUbx(uint8_t cls, uint8_t id, const uint8_t* payload, uint16_t len) {
  uint8_t hdr[6] = {SYNC1, SYNC2, cls, id, (uint8_t)(len & 0xFF), (uint8_t)(len >> 8)};
  uint8_t a = 0, b = 0;
  for (int i = 2; i < 6; i++) { a += hdr[i]; b += a; }
  for (uint16_t i = 0; i < len; i++) { a += payload[i]; b += a; }
  _ser->write(hdr, 6);
  if (len) _ser->write(payload, len);
  uint8_t ck[2] = {a, b};
  _ser->write(ck, 2);
  _ser->flush();
}

bool UbloxGps::waitAck(uint8_t cls, uint8_t id, uint32_t timeoutMs) {
  uint32_t t0 = millis();
  uint8_t st = 0, c = 0, i = 0, plen = 0, p[2] = {0, 0}, n = 0;
  while (millis() - t0 < timeoutMs) {
    while (_ser->available()) {
      uint8_t ch = _ser->read();
      switch (st) {
        case 0: st = (ch == SYNC1) ? 1 : 0; break;
        case 1: st = (ch == SYNC2) ? 2 : 0; break;
        case 2: c = ch; st = 3; break;
        case 3: i = ch; st = 4; break;
        case 4: plen = ch; st = 5; break;
        case 5: st = 6; n = 0; break;
        case 6:
          if (n < 2) p[n] = ch;
          if (++n >= plen) {
            if (c == 0x05 && p[0] == cls && p[1] == id) return i == 0x01;  // 0x01 ACK, 0x00 NAK
            st = 0;
          }
          break;
      }
    }
    delay(1);
  }
  return false;
}

// Ask for UBX-MON-VER; anything that answers is talking UBX at this baud.
bool UbloxGps::probeBaud(uint32_t baud) {
  _ser->updateBaudRate(baud);
  delay(60);
  while (_ser->available()) _ser->read();
  sendUbx(0x0A, 0x04, nullptr, 0);   // MON-VER (poll)
  uint32_t t0 = millis();
  uint8_t st = 0;
  while (millis() - t0 < 350) {
    while (_ser->available()) {
      uint8_t ch = _ser->read();
      if (st == 0) st = (ch == SYNC1) ? 1 : 0;
      else if (st == 1) { if (ch == SYNC2) return true; st = 0; }
    }
    delay(1);
  }
  return false;
}

bool UbloxGps::begin(HardwareSerial& port, int rxPin, int txPin, uint32_t targetBaud) {
  _ser = &port;
  _ser->setRxBufferSize(1024);
  _ser->begin(9600, SERIAL_8N1, rxPin, txPin);

  // Receivers ship at 9600 but a previously configured one may already be fast.
  const uint32_t candidates[] = {targetBaud, 9600, 38400, 57600, 115200, 230400};
  for (uint32_t b : candidates) {
    if (probeBaud(b)) { _baud = b; break; }
  }
  if (!_baud) {
    // No UBX reply anywhere. Sit at 9600 and try to configure blind - a factory
    // receiver spewing NMEA will still accept CFG frames.
    _ser->updateBaudRate(9600);
    _baud = 9600;
  }

  configure(targetBaud);
  return _configured;
}

void UbloxGps::configure(uint32_t targetBaud) {
  // M9/M10 speak CFG-VALSET; M8 only knows the legacy CFG-* messages. Try the
  // modern path first and fall back on NAK/timeout.
  configureValset(targetBaud);
  if (!_configured) configureLegacy(targetBaud);
}

void UbloxGps::configureValset(uint32_t targetBaud) {
  // CFG-VALSET, layer 1 (RAM). Each item: 4-byte key + value.
  auto valset = [&](const uint8_t* items, uint16_t n) {
    uint8_t pl[64] = {0x00, 0x01, 0x00, 0x00};   // version, layers=RAM, reserved
    memcpy(pl + 4, items, n);
    sendUbx(0x06, 0x8A, pl, 4 + n);
    return waitAck(0x06, 0x8A, 400);
  };

  // CFG-UART1-BAUDRATE = targetBaud  (key 0x40520001)
  uint8_t baudItem[8] = {0x01, 0x00, 0x52, 0x40,
                         (uint8_t)(targetBaud), (uint8_t)(targetBaud >> 8),
                         (uint8_t)(targetBaud >> 16), (uint8_t)(targetBaud >> 24)};
  bool ok = valset(baudItem, 8);
  if (!ok) return;                       // not a VALSET-capable receiver
  _ser->updateBaudRate(targetBaud);
  _baud = targetBaud;
  delay(60);

  // Silence NMEA on UART1, enable UBX in+out.
  uint8_t proto[] = {
    0x01, 0x00, 0x74, 0x10, 0x01,   // CFG-UART1INPROT-UBX  = 1
    0x02, 0x00, 0x74, 0x10, 0x00,   // CFG-UART1INPROT-NMEA = 0
    0x01, 0x00, 0x75, 0x10, 0x01,   // CFG-UART1OUTPROT-UBX = 1
    0x02, 0x00, 0x75, 0x10, 0x00,   // CFG-UART1OUTPROT-NMEA= 0
  };
  valset(proto, sizeof(proto));

  // Nav rate + NAV-PVT on UART1 + dynamic model 4 (automotive; the closest
  // stock model to cycling - it keeps the filter honest at low speed).
  uint16_t measMs = 1000 / GPS_NAV_RATE_HZ;
  uint8_t rest[] = {
    0x01, 0x00, 0x21, 0x30, (uint8_t)(measMs), (uint8_t)(measMs >> 8),  // CFG-RATE-MEAS
    0x02, 0x00, 0x21, 0x30, 0x01, 0x00,                                 // CFG-RATE-NAV = 1
    0x07, 0x00, 0x91, 0x20, 0x01,                                       // MSGOUT-NAV_PVT_UART1
    0x21, 0x00, 0x11, 0x20, 0x04,                                       // CFG-NAVSPG-DYNMODEL
  };
  valset(rest, sizeof(rest));
  _configured = true;
}

void UbloxGps::configureLegacy(uint32_t targetBaud) {
  // CFG-PRT for UART1: UBX in/out only, at targetBaud.
  uint8_t prt[20] = {0};
  prt[0]  = 0x01;                       // portID = UART1
  prt[4]  = 0xD0; prt[5] = 0x08;        // mode: 8N1
  prt[8]  = (uint8_t)(targetBaud);
  prt[9]  = (uint8_t)(targetBaud >> 8);
  prt[10] = (uint8_t)(targetBaud >> 16);
  prt[11] = (uint8_t)(targetBaud >> 24);
  prt[12] = 0x01;                       // inProtoMask  = UBX
  prt[14] = 0x01;                       // outProtoMask = UBX
  sendUbx(0x06, 0x00, prt, 20);
  // The receiver switches baud mid-reply, so the ACK is usually lost. Just move.
  delay(120);
  _ser->updateBaudRate(targetBaud);
  _baud = targetBaud;
  delay(120);
  while (_ser->available()) _ser->read();

  // CFG-RATE
  uint16_t measMs = 1000 / GPS_NAV_RATE_HZ;
  uint8_t rate[6] = {(uint8_t)(measMs), (uint8_t)(measMs >> 8), 0x01, 0x00, 0x01, 0x00};
  sendUbx(0x06, 0x08, rate, 6);
  waitAck(0x06, 0x08, 400);

  // CFG-NAV5: dynamic model 4 (automotive), fix mode 3 (auto 2D/3D)
  uint8_t nav5[36] = {0};
  nav5[0] = 0x05; nav5[1] = 0x00;       // mask: dyn + fixMode
  nav5[2] = 0x04;                        // dynModel
  nav5[3] = 0x03;                        // fixMode
  sendUbx(0x06, 0x24, nav5, 36);
  waitAck(0x06, 0x24, 400);

  // CFG-MSG: NAV-PVT (0x01,0x07) once per nav solution on every port.
  uint8_t msg[8] = {0x01, 0x07, 1, 1, 1, 1, 1, 0};
  sendUbx(0x06, 0x01, msg, 8);
  _configured = waitAck(0x06, 0x01, 500);
}

void UbloxGps::decodePvt(const uint8_t* p) {
  uint8_t valid = p[11];
  _fix.fixType = p[20];
  uint8_t flags = p[21];
  _fix.numSV   = p[23];

  _fix.lon    = rdI4(p + 24) * 1e-7;
  _fix.lat    = rdI4(p + 28) * 1e-7;
  _fix.altMSL = rdI4(p + 36) * 1e-3f;
  _fix.hAcc   = rdU4(p + 40) * 1e-3f;
  _fix.speed  = rdI4(p + 60) * 1e-3f;      // gSpeed, mm/s -> m/s
  _fix.heading= rdI4(p + 64) * 1e-5f;
  _fix.pDOP   = rdU2(p + 76) * 0.01f;

  _fix.timeValid = (valid & 0x07) == 0x07; // validDate | validTime | fullyResolved
  if (_fix.timeValid) {
    _fix.unixTime = toUnix(rdU2(p + 4), p[6], p[7], p[8], p[9], p[10]);
    int32_t nano = rdI4(p + 16);
    _fix.millisPart = (uint16_t)((nano / 1000000 + 1000) % 1000);
  }

  // gnssFixOK plus a sanity gate on accuracy - a 3D fix with 60 m hAcc is not
  // something we want integrating into the distance total.
  _fix.valid = (flags & 0x01) && _fix.fixType >= 3 && _fix.hAcc < 50.0f;
  _fix.lastUpdateMs = millis();
}

bool UbloxGps::update() {
  bool got = false;
  while (_ser->available()) {
    uint8_t c = _ser->read();
    switch (_state) {
      case 0: if (c == SYNC1) _state = 1; break;
      case 1: _state = (c == SYNC2) ? 2 : 0; break;
      case 2: _cls = c; _ckA = c; _ckB = c; _state = 3; break;
      case 3: _id = c;  _ckA += c; _ckB += _ckA; _state = 4; break;
      case 4: _payloadLen = c; _ckA += c; _ckB += _ckA; _state = 5; break;
      case 5:
        _payloadLen |= (uint16_t)c << 8; _ckA += c; _ckB += _ckA;
        _len = 0;
        _state = (_payloadLen > sizeof(_buf)) ? 0 : (_payloadLen ? 6 : 7);
        break;
      case 6:
        _buf[_len++] = c; _ckA += c; _ckB += _ckA;
        if (_len >= _payloadLen) _state = 7;
        break;
      case 7: _state = (c == _ckA) ? 8 : 0; break;
      case 8:
        if (c == _ckB && _cls == 0x01 && _id == 0x07 && _payloadLen >= 92) {
          decodePvt(_buf);
          got = true;
        }
        _state = 0;
        break;
    }
  }
  return got;
}
