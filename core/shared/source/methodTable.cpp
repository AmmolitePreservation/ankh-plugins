#include "methodTable.h"

void* methodOf(void* object, size_t slot) {
  return (*reinterpret_cast<void***>(object))[slot];
}
