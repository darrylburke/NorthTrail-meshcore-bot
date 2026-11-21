#pragma once

#include <stddef.h>
#include <stdint.h>

// Rate limiting and ignore rules for the chat bot (no Arduino dependencies; host-testable).
// All time arithmetic is unsigned subtraction (now - then) so it survives the 32-bit wrap.

static inline char botLowerAscii(char c) {
  return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;  // bytes >= 0x80 untouched
}

// FNV-1a over the ASCII-lower-cased name; null is treated as "". Two different names that
// collide on this hash share a cooldown slot (accepted: 32-bit hash, 16 slots).
static inline uint32_t botNameHash(const char* name) {
  uint32_t h = 2166136261u;
  if (!name) name = "";
  for (; *name; name++) { h ^= (uint8_t)botLowerAscii(*name); h *= 16777619u; }
  return h;
}

// Per-sender cooldown: 16 slots keyed by name hash; when full the oldest slot is reused.
// A blocked attempt does not restart the cooldown. cooldown_ms == 0 always allows.
// Entries older than ~49 days (one wrap of the 32-bit clock) may look recent again; harmless.
class BotCooldown {
public:
  static const int SLOTS = 16;
  BotCooldown() { for (int i = 0; i < SLOTS; i++) { _used[i] = false; _hash[i] = 0; _last[i] = 0; } }

  bool allow(const char* sender, uint32_t now_ms, uint32_t cooldown_ms) {
    if (cooldown_ms == 0) return true;
    uint32_t h = botNameHash(sender);
    int free_slot = -1, oldest = 0;
    uint32_t oldest_age = 0;
    for (int i = 0; i < SLOTS; i++) {
      if (!_used[i]) { if (free_slot < 0) free_slot = i; continue; }
      uint32_t age = now_ms - _last[i];
      if (_hash[i] == h) {
        if (age < cooldown_ms) return false;
        _last[i] = now_ms;
        return true;
      }
      if (age > oldest_age) { oldest_age = age; oldest = i; }
    }
    int slot = free_slot >= 0 ? free_slot : oldest;
    _used[slot] = true; _hash[slot] = h; _last[slot] = now_ms;
    return true;
  }

private:
  bool _used[SLOTS];
  uint32_t _hash[SLOTS];
  uint32_t _last[SLOTS];
};

// Sliding 60-second window of at most 30 sends. max_per_min == 0 always refuses; values
// above 30 are capped at 30. Refused attempts are not recorded.
class BotRateCap {
public:
  static const int MAX_TS = 30;
  static const uint32_t WINDOW_MS = 60000u;
  BotRateCap() : _n(0) { for (int i = 0; i < MAX_TS; i++) _ts[i] = 0; }

  // What allow() would answer now, without recording anything.
  bool wouldAllow(uint32_t now_ms, uint8_t max_per_min) const {
    if (max_per_min == 0) return false;
    int live = 0;
    for (int i = 0; i < _n; i++) if ((uint32_t)(now_ms - _ts[i]) < WINDOW_MS) live++;
    int limit = max_per_min < MAX_TS ? max_per_min : MAX_TS;
    return live < limit;
  }

  bool allow(uint32_t now_ms, uint8_t max_per_min) {
    if (max_per_min == 0) return false;
    // Drop expired entries (oldest first, so they are always at the front).
    int drop = 0;
    while (drop < _n && (uint32_t)(now_ms - _ts[drop]) >= WINDOW_MS) drop++;
    if (drop > 0) {
      for (int i = drop; i < _n; i++) _ts[i - drop] = _ts[i];
      _n -= drop;
    }
    int limit = max_per_min < MAX_TS ? max_per_min : MAX_TS;
    if (_n >= limit) return false;
    _ts[_n++] = now_ms;
    return true;
  }

private:
  uint32_t _ts[MAX_TS];
  int _n;
};

// True if text contains pat[0..pat_len) ignoring ASCII case. pat_len > 0 is required by callers.
static inline bool botContainsFold(const char* text, const char* pat, size_t pat_len) {
  for (; *text; text++) {
    size_t i = 0;
    while (i < pat_len && text[i] && botLowerAscii(text[i]) == botLowerAscii(pat[i])) i++;
    if (i == pat_len) return true;
    if (!text[i]) return false;  // ran out of text: no later start can fit either
  }
  return false;
}

// True if sender equals own_name (case-insensitive, ASCII folding) or contains any pattern
// from the comma-separated list. Patterns are trimmed of spaces; empty patterns are skipped
// (never match everything). Null is treated as ""; an empty own_name never matches.
static inline bool botIsIgnored(const char* sender, const char* own_name, const char* patterns_csv) {
  if (!sender) sender = "";
  if (own_name && *own_name) {
    const char* a = sender; const char* b = own_name;
    while (*a && *b && botLowerAscii(*a) == botLowerAscii(*b)) { a++; b++; }
    if (!*a && !*b) return true;
  }
  if (!patterns_csv) return false;
  const char* p = patterns_csv;
  while (*p) {
    const char* end = p;
    while (*end && *end != ',') end++;
    const char* s = p; const char* e = end;
    while (s < e && *s == ' ') s++;
    while (e > s && e[-1] == ' ') e--;
    if (e > s && botContainsFold(sender, s, (size_t)(e - s))) return true;
    p = *end ? end + 1 : end;
  }
  return false;
}
