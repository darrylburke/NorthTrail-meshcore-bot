#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <ArduinoJson.h>

// An ArduinoJson allocator that refuses to hold more than a fixed number of bytes at once
// (host-testable). Parsing an internet response with it can never take more of the device's
// heap than the limit, whatever the response holds: deserializeJson then fails with NoMemory (or
// the document reports overflowed()). Each block carries its size in an 8-byte header, which
// keeps malloc's alignment and is not counted against the limit.

#define BOT_JSON_HEAP_LIMIT 24576u

class BotJsonBoundedAllocator : public ArduinoJson::Allocator {
public:
  explicit BotJsonBoundedAllocator(size_t limit = BOT_JSON_HEAP_LIMIT) : _limit(limit), _used(0), _peak(0) {}

  void* allocate(size_t n) override {
    if (n > _limit - _used) return nullptr;
    uint8_t* b = (uint8_t*)malloc(n + HDR);
    if (!b) return nullptr;
    *(size_t*)b = n;
    take(n);
    return b + HDR;
  }

  void deallocate(void* p) override {
    if (!p) return;
    uint8_t* b = (uint8_t*)p - HDR;
    _used -= *(size_t*)b;
    free(b);
  }

  void* reallocate(void* p, size_t n) override {
    if (!p) return allocate(n);
    uint8_t* b = (uint8_t*)p - HDR;
    size_t old = *(size_t*)b;
    if (n > old && n - old > _limit - _used) return nullptr;
    uint8_t* nb = (uint8_t*)realloc(b, n + HDR);
    if (!nb) return nullptr;
    *(size_t*)nb = n;
    _used -= old;
    take(n);
    return nb + HDR;
  }

  size_t used() const { return _used; }
  size_t peak() const { return _peak; }

private:
  static const size_t HDR = 8;
  void take(size_t n) { _used += n; if (_used > _peak) _peak = _used; }
  size_t _limit, _used, _peak;
};
