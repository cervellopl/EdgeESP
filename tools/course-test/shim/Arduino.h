// Minimal Arduino shim so the course parser can be compiled and run on the host.
// Deliberately C-headers-only: this lets us build with -nostdinc++ and avoid
// libc++ entirely, which the parser does not use anyway.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

template <class T> inline T min(T a, T b) { return a < b ? a : b; }
template <class T> inline T max(T a, T b) { return a > b ? a : b; }

inline double radians(double d) { return d * M_PI / 180.0; }
inline double degrees(double r) { return r * 180.0 / M_PI; }
template <class T, class L, class H>
inline T constrain(T v, L lo, H hi) { return v < (T)lo ? (T)lo : (v > (T)hi ? (T)hi : v); }

inline bool  psramFound() { return false; }
inline void* ps_malloc(size_t n) { return malloc(n); }

// Tests drive the clock directly so staleness and rate limits are reachable.
extern unsigned long g_fakeMillis;
inline unsigned long millis() { return g_fakeMillis; }

// Only ever used as a reference or pointer in the headers under test.
class HardwareSerial;

// FreeRTOS critical sections are no-ops in a single-threaded host test.
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(m) ((void)0)
#define portEXIT_CRITICAL(m)  ((void)0)
