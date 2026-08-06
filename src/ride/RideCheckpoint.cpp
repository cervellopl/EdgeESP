#include "ride/RideCheckpoint.h"
#include "ride/FitEncoder.h"
#include <stddef.h>   // offsetof

// Everything up to but not including the crc field itself. offsetof rather
// than sizeof-minus-two: the struct may carry trailing padding after crc, and
// hashing indeterminate bytes would make the checksum unrepeatable.
static size_t bodyBytes() {
  return offsetof(RideCheckpoint, crc);
}

static uint16_t bodyCrc(const RideCheckpoint& c) {
  const uint8_t* p = (const uint8_t*)&c;
  uint16_t crc = 0;
  size_t n = bodyBytes();
  for (size_t i = 0; i < n; i++) crc = fitCrc16(crc, p[i]);
  return crc;
}

void checkpointSeal(RideCheckpoint& c) {
  c.magic = RIDE_CKPT_MAGIC;
  c.version = RIDE_CKPT_VERSION;
  c.stateSize = (uint16_t)sizeof(RideState);
  c.crc = bodyCrc(c);
}

bool checkpointValid(const RideCheckpoint& c) {
  if (c.magic != RIDE_CKPT_MAGIC) return false;
  if (c.version != RIDE_CKPT_VERSION) return false;
  // A firmware update that changes RideState changes the meaning of every byte
  // after this point. Refusing is the only safe answer.
  if (c.stateSize != sizeof(RideState)) return false;
  if (c.fitPath[0] == 0) return false;
  // A crash halfway through writing leaves a plausible-looking file; the CRC is
  // what tells it apart from a complete one.
  return c.crc == bodyCrc(c);
}
