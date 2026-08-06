#include "ride/FitEncoder.h"

// --- FIT CRC-16 (the nibble-table variant from the SDK) -------------------
static const uint16_t kCrcTable[16] = {
  0x0000, 0xCC01, 0xD801, 0x1400, 0xF001, 0x3C00, 0x2800, 0xE401,
  0xA001, 0x6C00, 0x7800, 0xB401, 0x5000, 0x9C01, 0x8801, 0x4400};

uint16_t fitCrc16(uint16_t crc, uint8_t b) {
  uint16_t tmp = kCrcTable[crc & 0x0F];
  crc = (crc >> 4) & 0x0FFF;
  crc = crc ^ tmp ^ kCrcTable[b & 0x0F];
  tmp = kCrcTable[crc & 0x0F];
  crc = (crc >> 4) & 0x0FFF;
  crc = crc ^ tmp ^ kCrcTable[(b >> 4) & 0x0F];
  return crc;
}

// --- base types -----------------------------------------------------------
enum : uint8_t {
  T_ENUM = 0x00, T_SINT8 = 0x01, T_UINT8 = 0x02, T_SINT16 = 0x83, T_UINT16 = 0x84,
  T_SINT32 = 0x85, T_UINT32 = 0x86, T_UINT32Z = 0x8C
};
// local message numbers
enum : uint8_t { L_FILE_ID = 0, L_EVENT = 1, L_RECORD = 2, L_LAP = 3, L_SESSION = 4, L_ACTIVITY = 5 };

void FitEncoder::put(const void* p, size_t n) {
  _f->write((const uint8_t*)p, n);
  _dataSize += n;
}

void FitEncoder::writeHeader(uint32_t dataSize) {
  uint8_t h[14];
  h[0] = 14;                       // header size
  h[1] = 0x20;                     // protocol 2.0
  h[2] = 0x5C; h[3] = 0x08;        // profile 21.40 -> 2140
  memcpy(h + 4, &dataSize, 4);
  memcpy(h + 8, ".FIT", 4);
  uint16_t crc = 0;
  for (int i = 0; i < 12; i++) crc = fitCrc16(crc, h[i]);
  memcpy(h + 12, &crc, 2);
  _f->write(h, 14);
}

// One definition per message type, all emitted up front. Local numbers then
// stay stable for the whole file so records cost only their payload.
void FitEncoder::writeDefinitions() {
  auto def = [&](uint8_t local, uint16_t global, uint8_t nFields, const uint8_t* fields) {
    put8(0x40 | local);
    put8(0);            // reserved
    put8(0);            // architecture: little endian
    put16(global);
    put8(nFields);
    put(fields, nFields * 3);
  };

  // file_id (0)
  const uint8_t fFileId[] = {
    0, 1, T_ENUM,      // type = 4 (activity)
    1, 2, T_UINT16,    // manufacturer
    2, 2, T_UINT16,    // product
    3, 4, T_UINT32Z,   // serial_number
    4, 4, T_UINT32,    // time_created
  };
  def(L_FILE_ID, 0, 5, fFileId);

  // event (21)
  const uint8_t fEvent[] = {
    253, 4, T_UINT32,  // timestamp
    0,   1, T_ENUM,    // event
    1,   1, T_ENUM,    // event_type
    3,   4, T_UINT32,  // data
  };
  def(L_EVENT, 21, 4, fEvent);

  // record (20)
  const uint8_t fRecord[] = {
    253, 4, T_UINT32,  // timestamp
    0,   4, T_SINT32,  // position_lat
    1,   4, T_SINT32,  // position_long
    5,   4, T_UINT32,  // distance
    2,   2, T_UINT16,  // altitude
    6,   2, T_UINT16,  // speed
    7,   2, T_UINT16,  // power
    9,   2, T_SINT16,  // grade
    3,   1, T_UINT8,   // heart_rate
    4,   1, T_UINT8,   // cadence
    13,  1, T_SINT8,   // temperature
  };
  def(L_RECORD, 20, 11, fRecord);

  // lap (19)
  const uint8_t fLap[] = {
    254, 2, T_UINT16,  // message_index
    253, 4, T_UINT32,  // timestamp
    2,   4, T_UINT32,  // start_time
    3,   4, T_SINT32,  // start_position_lat
    4,   4, T_SINT32,  // start_position_long
    7,   4, T_UINT32,  // total_elapsed_time
    8,   4, T_UINT32,  // total_timer_time
    9,   4, T_UINT32,  // total_distance
    11,  2, T_UINT16,  // total_calories
    13,  2, T_UINT16,  // avg_speed
    14,  2, T_UINT16,  // max_speed
    19,  2, T_UINT16,  // avg_power
    20,  2, T_UINT16,  // max_power
    21,  2, T_UINT16,  // total_ascent
    22,  2, T_UINT16,  // total_descent
    15,  1, T_UINT8,   // avg_heart_rate
    16,  1, T_UINT8,   // max_heart_rate
    17,  1, T_UINT8,   // avg_cadence
    18,  1, T_UINT8,   // max_cadence
    0,   1, T_ENUM,    // event
    1,   1, T_ENUM,    // event_type
    25,  1, T_ENUM,    // sport
  };
  def(L_LAP, 19, 22, fLap);

  // session (18)
  const uint8_t fSession[] = {
    254, 2, T_UINT16,  // message_index
    253, 4, T_UINT32,  // timestamp
    2,   4, T_UINT32,  // start_time
    3,   4, T_SINT32,  // start_position_lat
    4,   4, T_SINT32,  // start_position_long
    7,   4, T_UINT32,  // total_elapsed_time
    8,   4, T_UINT32,  // total_timer_time
    9,   4, T_UINT32,  // total_distance
    11,  2, T_UINT16,  // total_calories
    14,  2, T_UINT16,  // avg_speed
    15,  2, T_UINT16,  // max_speed
    20,  2, T_UINT16,  // avg_power
    21,  2, T_UINT16,  // max_power
    22,  2, T_UINT16,  // total_ascent
    23,  2, T_UINT16,  // total_descent
    26,  2, T_UINT16,  // num_laps
    16,  1, T_UINT8,   // avg_heart_rate
    17,  1, T_UINT8,   // max_heart_rate
    18,  1, T_UINT8,   // avg_cadence
    19,  1, T_UINT8,   // max_cadence
    0,   1, T_ENUM,    // event
    1,   1, T_ENUM,    // event_type
    5,   1, T_ENUM,    // sport
    6,   1, T_ENUM,    // sub_sport
  };
  def(L_SESSION, 18, 24, fSession);

  // activity (34)
  const uint8_t fActivity[] = {
    253, 4, T_UINT32,  // timestamp
    0,   4, T_UINT32,  // total_timer_time
    5,   4, T_UINT32,  // local_timestamp
    1,   2, T_UINT16,  // num_sessions
    2,   1, T_ENUM,    // type
    3,   1, T_ENUM,    // event
    4,   1, T_ENUM,    // event_type
  };
  def(L_ACTIVITY, 34, 7, fActivity);

  _defsWritten = true;
}

bool FitEncoder::begin(File& f, uint32_t startUnix) {
  _f = &f;
  _dataSize = 0;
  _defsWritten = false;

  writeHeader(0);                 // placeholder, patched in finalize()
  writeDefinitions();

  // file_id
  put8(L_FILE_ID);
  put8(4);                        // type = activity
  put16(255);                     // manufacturer = development
  put16(1);                       // product
  put32(0xE59E0001);              // serial
  put32(toFitTime(startUnix));

  writeEvent(toFitTime(startUnix), 0 /*timer*/, 0 /*start*/);
  return true;
}

bool FitEncoder::resume(File& f, uint32_t dataSize) {
  _f = &f;
  _dataSize = dataSize;
  _defsWritten = true;
  // Straight past the header and every record already written.
  return _f->seek(14 + dataSize);
}

void FitEncoder::writeEvent(uint32_t fitTime, uint8_t event, uint8_t eventType) {
  put8(L_EVENT);
  put32(fitTime);
  put8(event);
  put8(eventType);
  put32(0);
}

void FitEncoder::writeRecord(const FitRecord& r) {
  put8(L_RECORD);
  put32(r.timestamp);
  put32((uint32_t)r.lat_semi);
  put32((uint32_t)r.lon_semi);
  put32(r.distance_cm);
  put16(r.altitude_raw);
  put16(r.speed_mms);
  put16(r.power);
  put16((uint16_t)r.grade_x100);
  put8(r.heart_rate);
  put8(r.cadence);
  put8((uint8_t)r.temperature);
}

void FitEncoder::writeLap(const FitSummary& s, uint16_t index) {
  put8(L_LAP);
  put16(index);
  put32(s.timestamp);
  put32(s.start_time);
  put32((uint32_t)s.start_lat_semi);
  put32((uint32_t)s.start_lon_semi);
  put32(s.elapsed_ms);
  put32(s.timer_ms);
  put32(s.distance_cm);
  put16(s.calories);
  put16(s.avg_speed_mms);
  put16(s.max_speed_mms);
  put16(s.avg_power);
  put16(s.max_power);
  put16(s.ascent_m);
  put16(s.descent_m);
  put8(s.avg_hr);
  put8(s.max_hr);
  put8(s.avg_cad);
  put8(s.max_cad);
  put8(9);    // event = lap
  put8(1);    // event_type = stop
  put8(2);    // sport = cycling
}

void FitEncoder::writeSession(const FitSummary& s, uint16_t numLaps) {
  put8(L_SESSION);
  put16(0);
  put32(s.timestamp);
  put32(s.start_time);
  put32((uint32_t)s.start_lat_semi);
  put32((uint32_t)s.start_lon_semi);
  put32(s.elapsed_ms);
  put32(s.timer_ms);
  put32(s.distance_cm);
  put16(s.calories);
  put16(s.avg_speed_mms);
  put16(s.max_speed_mms);
  put16(s.avg_power);
  put16(s.max_power);
  put16(s.ascent_m);
  put16(s.descent_m);
  put16(numLaps);
  put8(s.avg_hr);
  put8(s.max_hr);
  put8(s.avg_cad);
  put8(s.max_cad);
  put8(8);    // event = session
  put8(1);    // event_type = stop
  put8(2);    // sport = cycling
  put8(0);    // sub_sport = generic
}

bool FitEncoder::finalize(const FitSummary& s, uint16_t numLaps) {
  writeEvent(s.timestamp, 0 /*timer*/, 4 /*stop_all*/);
  writeSession(s, numLaps);

  put8(L_ACTIVITY);
  put32(s.timestamp);
  put32(s.timer_ms);
  put32(s.timestamp);      // local_timestamp; we run in UTC
  put16(1);                // num_sessions
  put8(0);                 // type = manual
  put8(26);                // event = activity
  put8(1);                 // event_type = stop

  uint32_t dataSize = _dataSize;

  // Rewrite the header now that dataSize is known.
  if (!_f->seek(0)) return false;
  writeHeader(dataSize);
  _f->flush();

  // CRC covers header + data, so stream the whole file back through it.
  if (!_f->seek(0)) return false;
  uint16_t crc = 0;
  uint8_t chunk[512];
  uint32_t remaining = 14 + dataSize;
  while (remaining) {
    size_t n = _f->read(chunk, remaining < sizeof(chunk) ? remaining : sizeof(chunk));
    if (!n) return false;
    for (size_t i = 0; i < n; i++) crc = fitCrc16(crc, chunk[i]);
    remaining -= n;
  }
  if (!_f->seek(14 + dataSize)) return false;
  _f->write((uint8_t*)&crc, 2);
  _f->flush();
  return true;
}
