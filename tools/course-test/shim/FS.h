#pragma once
#include <stdio.h>
#include <stdint.h>

#define FILE_READ  "rb"
#define FILE_WRITE "wb+"

// Minimal stand-in for the Arduino FS File, enough for the headers that take
// one by reference and for the GPX reader to stream a real file.
class File {
 public:
  File() {}
  explicit File(FILE* f) : _f(f) {}
  explicit operator bool() const { return _f != nullptr; }
  int read(uint8_t* buf, size_t n) { return _f ? (int)fread(buf, 1, n, _f) : -1; }
  size_t write(const uint8_t* buf, size_t n) { return _f ? fwrite(buf, 1, n, _f) : 0; }
  bool seek(uint32_t pos) { return _f && fseek(_f, (long)pos, SEEK_SET) == 0; }
  void flush() { if (_f) fflush(_f); }
  uint32_t size() {
    if (!_f) return 0;
    long cur = ftell(_f);
    fseek(_f, 0, SEEK_END);
    long e = ftell(_f);
    fseek(_f, cur, SEEK_SET);
    return (uint32_t)e;
  }
  void close() { if (_f) { fclose(_f); _f = nullptr; } }
 private:
  FILE* _f = nullptr;
};
