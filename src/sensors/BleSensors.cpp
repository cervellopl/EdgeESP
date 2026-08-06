#include "sensors/BleSensors.h"
#include "config.h"
#include "Settings.h"
#include <NimBLEDevice.h>
#include <Preferences.h>

BleSensors g_sensors;

static const NimBLEUUID SVC_HR("180d"),  CHR_HR("2a37");
static const NimBLEUUID SVC_CSC("1816"), CHR_CSC("2a5b");
static const NimBLEUUID SVC_PWR("1818"), CHR_PWR("2a63");
static const NimBLEUUID SVC_BATT("180f"), CHR_BATT("2a19");

static Preferences s_prefs;
static NimBLEClient* s_client[SLOT_COUNT] = {nullptr, nullptr, nullptr};
static volatile bool s_scanning = false;
static volatile bool s_scanRequest = false;
static uint32_t s_scanSeconds = 15;

// Devices the scanner found that we want to connect to, handed to the worker.
struct PendingConnect { NimBLEAddress addr; uint8_t slot; char name[24]; bool valid; };
static PendingConnect s_pending[SLOT_COUNT];
static SemaphoreHandle_t s_lock;

static const char* kSlotKey[SLOT_COUNT] = {"hr", "csc", "pwr"};

// --------------------------------------------------------------------------
static void notifyCb(NimBLERemoteCharacteristic* chr, uint8_t* data, size_t len, bool) {
  NimBLEUUID u = chr->getUUID();
  if (u.equals(CHR_HR))       g_sensors.onHrData(data, len);
  else if (u.equals(CHR_CSC)) g_sensors.onCscData(data, len);
  else if (u.equals(CHR_PWR)) g_sensors.onPowerData(data, len);
}

class ClientCb : public NimBLEClientCallbacks {
 public:
  explicit ClientCb(uint8_t slot) : _slot(slot) {}
  void onDisconnect(NimBLEClient* c) override {
    // Slot state is cleared here; the worker loop will try to reconnect.
    (void)c;
  }
 private:
  uint8_t _slot;
};

static int8_t slotForDevice(NimBLEAdvertisedDevice* d) {
  if (d->isAdvertisingService(SVC_PWR)) return SLOT_PWR;   // power meters win: they often also advertise CSC
  if (d->isAdvertisingService(SVC_HR))  return SLOT_HR;
  if (d->isAdvertisingService(SVC_CSC)) return SLOT_CSC;
  return -1;
}

class ScanCb : public NimBLEAdvertisedDeviceCallbacks {
  void onResult(NimBLEAdvertisedDevice* d) override {
    int8_t slot = slotForDevice(d);
    if (slot < 0) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (!s_pending[slot].valid) {
      s_pending[slot].addr  = d->getAddress();
      s_pending[slot].slot  = slot;
      s_pending[slot].valid = true;
      strncpy(s_pending[slot].name,
              d->haveName() ? d->getName().c_str() : "sensor", sizeof(s_pending[0].name) - 1);
    }
    xSemaphoreGive(s_lock);
  }
};
static ScanCb s_scanCb;

// --------------------------------------------------------------------------
static bool connectSlot(uint8_t slot, const NimBLEAddress& addr, const char* name) {
  NimBLEClient* c = s_client[slot];
  if (!c) {
    c = NimBLEDevice::createClient();
    c->setClientCallbacks(new ClientCb(slot), true);
    c->setConnectionParams(12, 24, 0, 400);
    c->setConnectTimeout(8);
    s_client[slot] = c;
  }
  if (!c->connect(addr, true)) return false;

  const NimBLEUUID& svcUuid = (slot == SLOT_HR) ? SVC_HR : (slot == SLOT_CSC) ? SVC_CSC : SVC_PWR;
  const NimBLEUUID& chrUuid = (slot == SLOT_HR) ? CHR_HR : (slot == SLOT_CSC) ? CHR_CSC : CHR_PWR;

  NimBLERemoteService* svc = c->getService(svcUuid);
  if (!svc) { c->disconnect(); return false; }
  NimBLERemoteCharacteristic* chr = svc->getCharacteristic(chrUuid);
  if (!chr || !chr->canNotify() || !chr->subscribe(true, notifyCb)) {
    c->disconnect();
    return false;
  }

  SensorInfo& in = const_cast<SensorInfo&>(g_sensors.info((SensorSlot)slot));
  in.connected = true;
  in.paired    = true;
  in.rssi      = c->getRssi();
  strncpy(in.name, name, sizeof(in.name) - 1);
  strncpy(in.addr, addr.toString().c_str(), sizeof(in.addr) - 1);

  NimBLERemoteService* bsvc = c->getService(SVC_BATT);
  if (bsvc) {
    NimBLERemoteCharacteristic* b = bsvc->getCharacteristic(CHR_BATT);
    if (b && b->canRead()) in.battery = b->readValue<uint8_t>();
  }

  s_prefs.putString(kSlotKey[slot], addr.toString().c_str());
  s_prefs.putString((String(kSlotKey[slot]) + "n").c_str(), name);
  return true;
}

static void sensorTask(void*) {
  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setAdvertisedDeviceCallbacks(&s_scanCb, false);
  scan->setActiveScan(true);
  scan->setInterval(100);
  scan->setWindow(80);

  uint32_t scanEnd = 0;
  uint32_t lastRetry = 0;

  // Reconnect to anything we paired with previously, without a scan.
  for (uint8_t i = 0; i < SLOT_COUNT; i++) {
    String a = s_prefs.getString(kSlotKey[i], "");
    if (a.length() >= 17) {
      String n = s_prefs.getString((String(kSlotKey[i]) + "n").c_str(), "sensor");
      SensorInfo& in = const_cast<SensorInfo&>(g_sensors.info((SensorSlot)i));
      in.paired = true;
      strncpy(in.name, n.c_str(), sizeof(in.name) - 1);
      strncpy(in.addr, a.c_str(), sizeof(in.addr) - 1);
    }
  }

  for (;;) {
    if (s_scanRequest) {
      s_scanRequest = false;
      s_scanning = true;
      scanEnd = millis() + s_scanSeconds * 1000;
      scan->start(s_scanSeconds, nullptr, false);
    }
    if (s_scanning && millis() > scanEnd) {
      s_scanning = false;
      scan->stop();
    }

    // Handle anything the scan callback queued.
    for (uint8_t i = 0; i < SLOT_COUNT; i++) {
      PendingConnect p;
      xSemaphoreTake(s_lock, portMAX_DELAY);
      p = s_pending[i];
      s_pending[i].valid = false;
      xSemaphoreGive(s_lock);
      if (p.valid && !(s_client[i] && s_client[i]->isConnected())) {
        if (s_scanning) { scan->stop(); }
        connectSlot(i, p.addr, p.name);
        if (s_scanning) scan->start(s_scanSeconds, nullptr, false);
      }
    }

    // Reconnect known sensors every 10 s while they are away.
    if (!s_scanning && millis() - lastRetry > 10000) {
      lastRetry = millis();
      for (uint8_t i = 0; i < SLOT_COUNT; i++) {
        SensorInfo& in = const_cast<SensorInfo&>(g_sensors.info((SensorSlot)i));
        bool live = s_client[i] && s_client[i]->isConnected();
        in.connected = live;
        if (!live && in.paired && strlen(in.addr) >= 17) {
          connectSlot(i, NimBLEAddress(std::string(in.addr)), in.name);
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

// --------------------------------------------------------------------------
void BleSensors::begin() {
  s_lock = xSemaphoreCreateMutex();
  s_prefs.begin("sensors", false);
  // NimBLEDevice::init() is done once in PhoneLink; call it here too in case
  // sensors come up first - the call is idempotent.
  NimBLEDevice::init(DEVICE_NAME);
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);
  xTaskCreatePinnedToCore(sensorTask, "ble_sensors", 6144, nullptr, 3, nullptr, 0);
}

void BleSensors::startScan(uint32_t seconds) { s_scanSeconds = seconds; s_scanRequest = true; }
bool BleSensors::scanning() const { return s_scanning; }

void BleSensors::forget(SensorSlot slot) {
  if (s_client[slot] && s_client[slot]->isConnected()) s_client[slot]->disconnect();
  s_prefs.remove(kSlotKey[slot]);
  s_prefs.remove((String(kSlotKey[slot]) + "n").c_str());
  _info[slot] = SensorInfo();
}

bool BleSensors::hasHr() const      { return millis() - _info[SLOT_HR].lastDataMs  < 5000; }
bool BleSensors::hasCadence() const { return _crankSeen && millis() - max(_info[SLOT_CSC].lastDataMs, _info[SLOT_PWR].lastDataMs) < 5000; }
bool BleSensors::hasPower() const   { return millis() - _info[SLOT_PWR].lastDataMs < 5000; }
bool BleSensors::hasWheelSpeed() const { return _wheelSeen && millis() - _info[SLOT_CSC].lastDataMs < 5000; }

void BleSensors::onHrData(const uint8_t* d, size_t n) {
  if (n < 2) return;
  _hr = (d[0] & 0x01) ? (uint8_t)(d[1] | (d[2] << 8)) : d[1];
  _info[SLOT_HR].lastDataMs = millis();
}

// Wheel/crank event times are in 1/1024 s and wrap at 16 bits (~64 s).
void BleSensors::computeWheel(uint32_t revs, uint16_t evtTime) {
  if (_wheelSeen) {
    uint16_t dt = evtTime - _lastWheelTime;          // wraps correctly
    uint32_t dr = revs - _lastWheelRev;
    if (dt > 0 && dr > 0 && dr < 1000) {
      float sec = dt / 1024.0f;
      _wheelSpeed = (dr * (g_settings.wheelMm / 1000.0f)) / sec;
    } else if (dt > 3072) {
      _wheelSpeed = 0;                                // 3 s with no rev = stopped
    }
  }
  _lastWheelRev = revs; _lastWheelTime = evtTime; _wheelSeen = true;
}

void BleSensors::computeCrank(uint16_t revs, uint16_t evtTime) {
  if (_crankSeen) {
    uint16_t dt = evtTime - _lastCrankTime;
    uint16_t dr = revs - _lastCrankRev;
    if (dt > 0 && dr > 0 && dr < 200) {
      _cadence = (uint8_t)min(250.0f, (dr * 60.0f) / (dt / 1024.0f));
    } else if (dt > 3072) {
      _cadence = 0;
    }
  }
  _lastCrankRev = revs; _lastCrankTime = evtTime; _crankSeen = true;
}

void BleSensors::onCscData(const uint8_t* d, size_t n) {
  if (n < 1) return;
  uint8_t flags = d[0];
  size_t i = 1;
  if (flags & 0x01) {                       // wheel revolution data
    if (n < i + 6) return;
    uint32_t revs = d[i] | (d[i+1] << 8) | ((uint32_t)d[i+2] << 16) | ((uint32_t)d[i+3] << 24);
    uint16_t t    = d[i+4] | (d[i+5] << 8);
    computeWheel(revs, t);
    i += 6;
  }
  if (flags & 0x02) {                       // crank revolution data
    if (n < i + 4) return;
    computeCrank(d[i] | (d[i+1] << 8), d[i+2] | (d[i+3] << 8));
  }
  _info[SLOT_CSC].lastDataMs = millis();
}

void BleSensors::onPowerData(const uint8_t* d, size_t n) {
  if (n < 4) return;
  uint16_t flags = d[0] | (d[1] << 8);
  _power = (uint16_t)max<int16_t>(0, (int16_t)(d[2] | (d[3] << 8)));

  // Walk the optional fields in spec order to find crank revolutions.
  size_t i = 4;
  if (flags & 0x0001) i += 1;    // pedal power balance
  if (flags & 0x0004) i += 2;    // accumulated torque
  if (flags & 0x0010) i += 6;    // wheel revolution data
  if (flags & 0x0020) {          // crank revolution data
    if (n >= i + 4) computeCrank(d[i] | (d[i+1] << 8), d[i+2] | (d[i+3] << 8));
  }
  _info[SLOT_PWR].lastDataMs = millis();
}
