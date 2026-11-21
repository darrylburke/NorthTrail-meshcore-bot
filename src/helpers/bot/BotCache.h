#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Small reply cache for the chat bot (no Arduino dependencies; host-testable): 8 entries,
// case-sensitive keys, oldest entry replaced when full. Times are caller-supplied millis().

#define BOT_CACHE_ENTRIES 8
#define BOT_CACHE_KEY_MAX 47
#define BOT_CACHE_TEXT_MAX 127

class BotCache {
public:
  BotCache() { for (int i = 0; i < BOT_CACHE_ENTRIES; i++) e_[i].used = false; }

  // Forgets every entry.
  void clear() { for (int i = 0; i < BOT_CACHE_ENTRIES; i++) e_[i].used = false; }

  // Copies the live entry for key into out. Decisions: null/empty key, expired entry (age >=
  // ttl, by unsigned subtraction so it survives rollover) or a text that does not fit in cap-1
  // bytes returns false; out is "" on false when cap > 0. Reading does not refresh the entry.
  bool get(const char* key, uint32_t now_ms, char* out, size_t cap) const {
    if (out && cap > 0) out[0] = '\0';
    if (!key || !key[0] || !out || cap == 0) return false;
    for (int i = 0; i < BOT_CACHE_ENTRIES; i++) {
      const Entry& x = e_[i];
      if (!x.used || strcmp(x.key, key) != 0) continue;
      if ((uint32_t)(now_ms - x.stamp) >= x.ttl) return false;
      size_t n = strlen(x.text);
      if (n > cap - 1) return false;
      memcpy(out, x.text, n + 1);
      return true;
    }
    return false;
  }

  // Stores text under key. Decisions: null/empty key, key over 47 bytes, null text, text over
  // 127 bytes or ttl_ms == 0 store nothing (an older entry for the key is left alone); an
  // existing key is overwritten in place; otherwise a free slot, else the oldest entry.
  void put(const char* key, const char* text, uint32_t now_ms, uint32_t ttl_ms) {
    if (!key || !key[0] || !text || ttl_ms == 0) return;
    size_t kl = strlen(key), tl = strlen(text);
    if (kl > BOT_CACHE_KEY_MAX || tl > BOT_CACHE_TEXT_MAX) return;
    int slot = -1;
    for (int i = 0; i < BOT_CACHE_ENTRIES && slot < 0; i++)
      if (e_[i].used && strcmp(e_[i].key, key) == 0) slot = i;
    for (int i = 0; i < BOT_CACHE_ENTRIES && slot < 0; i++)
      if (!e_[i].used) slot = i;
    if (slot < 0) {
      slot = 0;
      uint32_t best = (uint32_t)(now_ms - e_[0].stamp);
      for (int i = 1; i < BOT_CACHE_ENTRIES; i++) {
        uint32_t age = (uint32_t)(now_ms - e_[i].stamp);
        if (age > best) { best = age; slot = i; }
      }
    }
    Entry& x = e_[slot];
    memcpy(x.key, key, kl + 1);
    memcpy(x.text, text, tl + 1);
    x.stamp = now_ms;
    x.ttl = ttl_ms;
    x.used = true;
  }

private:
  struct Entry {
    char key[BOT_CACHE_KEY_MAX + 1];
    char text[BOT_CACHE_TEXT_MAX + 1];
    uint32_t stamp;
    uint32_t ttl;
    bool used;
  };
  Entry e_[BOT_CACHE_ENTRIES];
};
