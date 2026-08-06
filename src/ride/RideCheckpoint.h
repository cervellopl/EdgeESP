#pragma once
#include <Arduino.h>
#include "ride/RideComputer.h"

// A ride's totals, written beside the .fit file every so often so a flat
// battery or a crash costs minutes rather than the whole ride.
//
// The .fit file already holds every record, but it has no valid header or CRC
// until the ride is closed. This is what lets the firmware pick that file up
// again and either carry on writing to it or finish it off properly.
//
// The serialisation is deliberately separable from the SD code so it can be
// exercised on the host: a checkpoint that unpacks wrong is a ride lost.

static const uint32_t RIDE_CKPT_MAGIC   = 0x45444745UL;   // 'EDGE'
static const uint16_t RIDE_CKPT_VERSION = 1;

struct RideCheckpoint {
  uint32_t magic = 0;
  uint16_t version = 0;
  uint16_t stateSize = 0;        // sizeof(RideState) when written
  uint32_t fitDataSize = 0;      // FIT bytes after the 14-byte header
  uint32_t writtenUnix = 0;
  char     fitPath[40] = {0};
  char     gpxPath[40] = {0};
  RideState state;
  ElevationProfile profile;
  uint16_t crc = 0;              // over everything above; must stay last
};

// Stamp the header fields and compute the CRC. Call immediately before writing.
void checkpointSeal(RideCheckpoint& c);

// True when the blob is one of ours, of a version we understand, describing a
// RideState the same size as ours, and undamaged.
bool checkpointValid(const RideCheckpoint& c);
