#pragma once
#include <Arduino.h>
#include "ride/RideComputer.h"

// Wi-Fi side of the phone link: browse and download recorded .fit / .gpx files,
// see live telemetry, and push firmware updates. Off by default - the radio is
// expensive and a bike computer spends its life away from any access point.
//
// Joins a known network if one is configured, otherwise raises its own AP
// (SSID "EdgeESP", password "edgeesp123") at 192.168.4.1.
class WebPortal {
 public:
  void begin(RideComputer* rc);
  void toggle();
  void stop();
  void handle();                    // call from the main loop while on

  bool running() const { return _running; }
  const char* ip() const { return _ip; }

  void setCredentials(const char* ssid, const char* pass);   // persisted in NVS

 private:
  bool _running = false;
  bool _apMode = false;
  char _ip[16] = {0};
  RideComputer* _rc = nullptr;
  uint32_t _startedMs = 0;

  void startServer();
  void routes();
};

extern WebPortal g_portal;
