#pragma once
#include <stdint.h>
#include <string.h>

// In-memory stand-in for the ESP32 NVS wrapper, so save/load round trips and
// the clamp-on-load path can be exercised on the host.
class Preferences {
 public:
  bool begin(const char* ns, bool = false) { _ns = ns; return true; }
  void end() {}

  uint32_t getULong (const char* k, uint32_t d = 0) { return (uint32_t)get(k, d); }
  void     putULong (const char* k, uint32_t v)     { put(k, v); }
  // Blobs get their own slot list: they are too big for the numeric table.
  size_t getBytes(const char* k, void* buf, size_t n) {
    for (auto& e : s_blobs)
      if (e.used && !strcmp(e.ns, _ns) && !strcmp(e.k, k) && e.len == n) {
        memcpy(buf, e.data, n);
        return n;
      }
    return 0;
  }
  size_t putBytes(const char* k, const void* buf, size_t n) {
    if (n > sizeof(s_blobs[0].data)) return 0;
    for (auto& e : s_blobs)
      if (e.used && !strcmp(e.ns, _ns) && !strcmp(e.k, k)) {
        memcpy(e.data, buf, n); e.len = n; return n;
      }
    for (auto& e : s_blobs)
      if (!e.used) {
        e.used = true;
        strncpy(e.ns, _ns, sizeof(e.ns) - 1); e.ns[sizeof(e.ns) - 1] = 0;
        strncpy(e.k, k, sizeof(e.k) - 1);     e.k[sizeof(e.k) - 1] = 0;
        memcpy(e.data, buf, n); e.len = n;
        return n;
      }
    return 0;
  }

  uint8_t  getUChar (const char* k, uint8_t d = 0)  { return (uint8_t)get(k, d); }
  uint16_t getUShort(const char* k, uint16_t d = 0) { return (uint16_t)get(k, d); }
  float    getFloat (const char* k, float d = 0)    { return (float)get(k, d); }
  bool     getBool  (const char* k, bool d = false) { return get(k, d ? 1 : 0) != 0; }

  void putUChar (const char* k, uint8_t v)  { put(k, v); }
  void putUShort(const char* k, uint16_t v) { put(k, v); }
  void putFloat (const char* k, float v)    { put(k, v); }
  void putBool  (const char* k, bool v)     { put(k, v ? 1 : 0); }

  static void wipe() {
    for (auto& e : s_ents) e.used = false;
    for (auto& e : s_blobs) e.used = false;
  }
  // Tests use this to plant a corrupt value and check it is rejected on load.
  static void poke(const char* ns, const char* k, double v) {
    Preferences p; p.begin(ns); p.put(k, v);
  }

 private:
  struct Ent  { char ns[16]; char k[16]; double v; bool used; };
  struct Blob { char ns[16]; char k[16]; uint8_t data[256]; size_t len; bool used; };
  static Ent  s_ents[48];
  static Blob s_blobs[4];
  const char* _ns = "";

  double get(const char* k, double d) {
    for (auto& e : s_ents)
      if (e.used && !strcmp(e.ns, _ns) && !strcmp(e.k, k)) return e.v;
    return d;
  }
  void put(const char* k, double v) {
    for (auto& e : s_ents)
      if (e.used && !strcmp(e.ns, _ns) && !strcmp(e.k, k)) { e.v = v; return; }
    for (auto& e : s_ents)
      if (!e.used) {
        e.used = true;
        strncpy(e.ns, _ns, sizeof(e.ns) - 1); e.ns[sizeof(e.ns) - 1] = 0;
        strncpy(e.k, k, sizeof(e.k) - 1);     e.k[sizeof(e.k) - 1] = 0;
        e.v = v;
        return;
      }
  }
};
