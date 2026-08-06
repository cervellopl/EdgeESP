#include "link/PhoneLink.h"
#include "config.h"
#include "nav/Course.h"
#include "sensors/Weather.h"
#include <NimBLEDevice.h>
#include <time.h>

PhoneLink g_phone;

static NimBLECharacteristic* s_tx   = nullptr;
static NimBLECharacteristic* s_batt = nullptr;
static volatile bool s_connected = false;
static uint32_t s_rateMs = 1000;

static QueueHandle_t s_cmdQueue = nullptr;
static char s_notifTitle[24] = {0}, s_notifBody[64] = {0};
static volatile bool s_notifPending = false;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

static void pushCommand(PhoneCommand c) {
  if (s_cmdQueue) xQueueSend(s_cmdQueue, &c, 0);
}

class ServerCb : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* srv, ble_gap_conn_desc* desc) override {
    s_connected = true;
    // A phone that is only listening is happy with a slow, cheap connection.
    srv->updateConnParams(desc->conn_handle, 24, 40, 0, 400);
  }
  void onDisconnect(NimBLEServer* srv) override {
    s_connected = false;
    NimBLEDevice::startAdvertising();
  }
};

class RxCb : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* chr) override {
    std::string v = chr->getValue();
    if (v.empty()) return;
    while (!v.empty() && (v.back() == '\n' || v.back() == '\r')) v.pop_back();

    // Weather lines carry data rather than an action, so they are parsed here
    // rather than queued. Weather does its own locking for the hand-off.
    if (g_weather.feedLine(v.c_str())) return;

    if      (v == "START")   pushCommand(PhoneCommand::Start);
    else if (v == "STOP")    pushCommand(PhoneCommand::Stop);
    else if (v == "LAP")     pushCommand(PhoneCommand::Lap);
    else if (v == "SAVE")    pushCommand(PhoneCommand::Save);
    else if (v == "DISCARD") pushCommand(PhoneCommand::Discard);
    else if (v == "PAIR")    pushCommand(PhoneCommand::Pair);
    else if (v == "WIFI")    pushCommand(PhoneCommand::Wifi);
    else if (v == "SLEEP")   pushCommand(PhoneCommand::Sleep);
    else if (v.rfind("RATE ", 0) == 0) {
      s_rateMs = strtoul(v.c_str() + 5, nullptr, 10);
    } else if (v.rfind("TIME ", 0) == 0) {
      time_t t = (time_t)strtoul(v.c_str() + 5, nullptr, 10);
      struct timeval tv = {t, 0};
      settimeofday(&tv, nullptr);
    } else if (v.rfind("NOTIFY ", 0) == 0) {
      std::string rest = v.substr(7);
      size_t bar = rest.find('|');
      portENTER_CRITICAL(&s_mux);
      strncpy(s_notifTitle, rest.substr(0, bar).c_str(), sizeof(s_notifTitle) - 1);
      strncpy(s_notifBody, bar == std::string::npos ? "" : rest.substr(bar + 1).c_str(),
              sizeof(s_notifBody) - 1);
      s_notifPending = true;
      portEXIT_CRITICAL(&s_mux);
    }
  }
};
static ServerCb s_serverCb;
static RxCb     s_rxCb;

void PhoneLink::begin() {
  s_cmdQueue = xQueueCreate(8, sizeof(PhoneCommand));

  NimBLEDevice::init(DEVICE_NAME);          // idempotent if sensors got here first
  NimBLEDevice::setMTU(185);

  NimBLEServer* srv = NimBLEDevice::createServer();
  srv->setCallbacks(&s_serverCb);

  NimBLEService* nus = srv->createService(NUS_SERVICE_UUID);
  s_tx = nus->createCharacteristic(NUS_TX_UUID, NIMBLE_PROPERTY::NOTIFY);
  NimBLECharacteristic* rx = nus->createCharacteristic(
      NUS_RX_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  rx->setCallbacks(&s_rxCb);
  nus->start();

  NimBLEService* batt = srv->createService("180F");
  s_batt = batt->createCharacteristic(
      "2A19", NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  s_batt->setValue((uint8_t)100);
  batt->start();

  NimBLEService* dis = srv->createService("180A");
  dis->createCharacteristic("2A24", NIMBLE_PROPERTY::READ)->setValue(DEVICE_NAME);
  dis->createCharacteristic("2A26", NIMBLE_PROPERTY::READ)->setValue(FIRMWARE_VERSION);
  dis->createCharacteristic("2A29", NIMBLE_PROPERTY::READ)->setValue("EdgeESP project");
  dis->start();

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(NUS_SERVICE_UUID);
  adv->setScanResponse(true);
  adv->setName(DEVICE_NAME);
  adv->start();
}

bool PhoneLink::connected() const { return s_connected; }

void PhoneLink::sendLine(const char* s) {
  if (!s_connected || !s_tx) return;
  s_tx->setValue((uint8_t*)s, strlen(s));
  s_tx->notify();
}

void PhoneLink::setBattery(uint8_t pct) {
  if (!s_batt) return;
  static uint8_t last = 255;
  if (pct == last) return;
  last = pct;
  s_batt->setValue(&pct, 1);
  if (s_connected) s_batt->notify();
}

PhoneCommand PhoneLink::takeCommand() {
  PhoneCommand c = PhoneCommand::None;
  if (s_cmdQueue) xQueueReceive(s_cmdQueue, &c, 0);
  return c;
}

bool PhoneLink::takeNotification(char* title, size_t tn, char* body, size_t bn) {
  if (!s_notifPending) return false;
  portENTER_CRITICAL(&s_mux);
  strncpy(title, s_notifTitle, tn - 1); title[tn - 1] = 0;
  strncpy(body,  s_notifBody,  bn - 1); body[bn - 1] = 0;
  s_notifPending = false;
  portEXIT_CRITICAL(&s_mux);
  return true;
}

void PhoneLink::update(const RideState& s) {
  if (!s_connected || !s_rateMs) return;
  if (millis() - _lastTelemetryMs < s_rateMs) return;
  _lastTelemetryMs = millis();

  // Compact JSON so it fits one MTU and any client can parse it.
  char buf[192];
  int n = snprintf(buf, sizeof(buf),
      "{\"st\":%u,\"v\":%.2f,\"d\":%.1f,\"t\":%lu,\"alt\":%.0f,\"g\":%.1f,"
      "\"hr\":%u,\"cad\":%u,\"pw\":%u,\"asc\":%.0f,\"kcal\":%.0f,"
      "\"lat\":%.6f,\"lon\":%.6f,\"sat\":%u,\"bat\":%u,\"lap\":%u}\n",
      (unsigned)s.status, s.speed * 3.6f, s.distance, (unsigned long)(s.movingMs / 1000),
      isnan(s.altitude) ? 0 : s.altitude, s.grade,
      s.hasHr ? s.hr : 0, s.hasCad ? s.cadence : 0, s.hasPwr ? s.power : 0,
      s.ascent, s.calories,
      s.fix.valid ? s.fix.lat : 0.0, s.fix.valid ? s.fix.lon : 0.0,
      s.fix.numSV, s.batteryPct, s.lapCount);
  if (n > 0) sendLine(buf);

  // Course data goes in its own frame. A single combined line would overrun the
  // negotiated MTU on phones that only grant 185 bytes, and a truncated
  // notification is worse than a missing one.
  if (g_course.loaded()) {
    snprintf(buf, sizeof(buf),
        "{\"cname\":\"%.20s\",\"cpct\":%u,\"crem\":%.0f,\"cclimb\":%.0f,"
        "\"coff\":%u,\"cx\":%.0f}\n",
        g_course.name(), g_course.progressPct(), g_course.distanceRemaining(),
        g_course.ascentRemaining(), g_course.offCourse() ? 1 : 0,
        g_course.crossTrack());
    sendLine(buf);
  }
  setBattery(s.batteryPct);
}
