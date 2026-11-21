#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ArduinoJson.h>

// Keeping secrets (the web and broker passwords) out of freed heap (pure C++, host-testable): a
// wipe the compiler cannot leave out, and an ArduinoJson allocator that wipes every block before
// freeing it. Used by the web layer (login, password change, setup, settings) and by BotStore
// (/bot.json holds both passwords); the device includes it through bot/BotWipe.h.

// Zeroes n bytes through a volatile pointer, so it is never dropped as a store before free().
static inline void botWipe(void* p, size_t n) {
  if (!p) return;
  volatile uint8_t* b = (volatile uint8_t*)p;
  while (n--) *b++ = 0;
}

// The heap the wiping allocator takes its blocks from (the host tests put guard bytes around them).
struct BotWipeHeap {
  static void* take(size_t n) { return malloc(n); }
  static void give(void* p) { free(p); }
};

// Zeroes every block before freeing it, exactly the bytes that were asked for and no more. Each
// block carries its requested size in an 8-byte header in front of the caller's bytes: the heap's
// own "allocated size" (heap_caps_get_allocated_size, malloc_usable_size) is rounded up past the
// request, and with heap poisoning on (the Arduino core's default) the bytes after the request hold
// the heap's check word, so wiping to the rounded size corrupts the heap. The header keeps the
// heap's alignment up to 8 bytes. A reallocation moves to a new block and wipes the old one; when
// the new block cannot be had, the old one is left as it was (realloc's rule, which ArduinoJson
// relies on). allocate(0) gives a valid block of no bytes; deallocate(nullptr) does nothing.
template <class Heap>
class BotWipingAllocatorOn : public ArduinoJson::Allocator {
public:
  static const size_t HEAD = 8;
  static_assert(sizeof(size_t) <= HEAD, "the header holds a size_t");

  void* allocate(size_t n) override {
    if (n > SIZE_MAX - HEAD) return nullptr;
    uint8_t* b = (uint8_t*)Heap::take(HEAD + n);
    if (!b) return nullptr;
    memset(b, 0, HEAD);
    memcpy(b, &n, sizeof(n));
    return b + HEAD;
  }
  void deallocate(void* p) override {
    if (!p) return;
    uint8_t* b = (uint8_t*)p - HEAD;
    botWipe(p, requested(p));
    botWipe(b, HEAD);
    Heap::give(b);
  }
  void* reallocate(void* p, size_t n) override {
    void* q = allocate(n);
    if (!q) return nullptr;
    if (p) {
      size_t old = requested(p);
      memcpy(q, p, old < n ? old : n);
      deallocate(p);
    }
    return q;
  }
  // The size asked for when p was allocated (p from this allocator, not null).
  static size_t requested(const void* p) {
    size_t n;
    memcpy(&n, (const uint8_t*)p - HEAD, sizeof(n));
    return n;
  }
  static BotWipingAllocatorOn* instance() { static BotWipingAllocatorOn a; return &a; }
};

typedef BotWipingAllocatorOn<BotWipeHeap> BotWipingAllocator;
