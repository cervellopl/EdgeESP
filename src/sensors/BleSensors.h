#pragma once
#include <Arduino.h>

// BLE central for the three standard cycling sensor profiles:
//   0x180D Heart Rate            -> 0x2A37 Heart Rate Measurement
//   0x1816 Cycling Speed/Cadence -> 0x2A5B CSC Measurement
//   0x1818 Cycling Power         -> 0x2A63 Cycling Power Measurement
//
// Runs its own task: scanning and connecting must never stall the render loop.

enum SensorSlot : uint8_t { SLOT_HR = 0, SLOT_CSC = 1, SLOT_PWR = 2, SLOT_COUNT = 3 };

struct SensorInfo {
  bool     connected = false;
  bool     paired    = false;
  char     name[24]  = {0};
  char     addr[18]  = {0};
  int8_t   rssi      = 0;
  uint32_t lastDataMs = 0;
  uint16_t battery   = 0xFFFF;   // percent, 0xFFFF unknown
};

class BleSensors {
 public:
  void begin();                      // starts the worker task
  void startScan(uint32_t seconds);  // pairing mode
  bool scanning() const;
  void forget(SensorSlot slot);

  // Live values. hasX goes false once the sensor has been silent for 5 s.
  bool     hasHr()      const;
  uint8_t  hr()         const { return _hr; }
  bool     hasCadence() const;
  uint8_t  cadence()    const { return _cadence; }
  bool     hasPower()   const;
  uint16_t power()      const { return _power; }
  bool     hasWheelSpeed() const;
  float    wheelSpeed() const { return _wheelSpeed; }   // m/s

  const SensorInfo& info(SensorSlot s) const { return _info[s]; }

  // Called from the NimBLE notify callbacks.
  void onHrData(const uint8_t* d, size_t n);
  void onCscData(const uint8_t* d, size_t n);
  void onPowerData(const uint8_t* d, size_t n);

 private:
  SensorInfo _info[SLOT_COUNT];
  volatile uint8_t  _hr = 0, _cadence = 0;
  volatile uint16_t _power = 0;
  volatile float    _wheelSpeed = 0;

  // CSC / power revolution deltas
  uint32_t _lastWheelRev = 0; uint16_t _lastWheelTime = 0; bool _wheelSeen = false;
  uint16_t _lastCrankRev = 0; uint16_t _lastCrankTime = 0; bool _crankSeen = false;

  void computeWheel(uint32_t revs, uint16_t evtTime);
  void computeCrank(uint16_t revs, uint16_t evtTime);
};

extern BleSensors g_sensors;
