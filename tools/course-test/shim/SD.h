#pragma once
#include <FS.h>

// File itself lives in FS.h so headers that only need the type (FitEncoder)
// do not have to drag SD in.
class SDClass {
 public:
  File open(const char* path, const char* mode = FILE_READ) { return File(fopen(path, mode)); }
};
static SDClass SD;
