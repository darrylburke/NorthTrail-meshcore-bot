#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "BotText.h"
#include "BotReplies.h"

// In-memory "who was heard" tables for the chat bot (pure C/C++, no heap, host-testable).
// Times are unsigned differences (now - then), so they survive the 32-bit wrap. now_s is unix
// seconds, or seconds since boot when the clock is not set; only differences are compared.

// Last 64 distinct nodes heard, keyed by the first 4 public-key bytes. When full, the entry
// seen longest ago is replaced.
class BotHeardTable {
public:
  static const int SLOTS = 64;
  static const size_t NAME_CAP = 24;  // including the terminator
  static const uint32_t WINDOW_S = 86400;

  BotHeardTable() { for (int i = 0; i < SLOTS; i++) _e[i].used = false; }

  // pubkey points at >= 4 bytes. A repeat sighting updates name, time, hops and SNR. A null
  // name is stored as "" and a long one is cut on a UTF-8 boundary to fit NAME_CAP.
  void note(const uint8_t* pubkey, const char* name, uint32_t now_s, uint8_t hops, float snr) {
    if (!pubkey) return;
    int slot = -1, free_slot = -1, oldest = 0;
    uint32_t oldest_age = 0;
    for (int i = 0; i < SLOTS; i++) {
      if (!_e[i].used) { if (free_slot < 0) free_slot = i; continue; }
      if (memcmp(_e[i].key, pubkey, 4) == 0) { slot = i; break; }
      uint32_t age = now_s - _e[i].last_seen;
      if (age >= oldest_age) { oldest_age = age; oldest = i; }
    }
    if (slot < 0) slot = free_slot >= 0 ? free_slot : oldest;
    Entry& e = _e[slot];
    e.used = true;
    memcpy(e.key, pubkey, 4);
    size_t n = name ? strlen(name) : 0;
    if (n > NAME_CAP - 1) {  // plain cut (no ellipsis) at a UTF-8 character start
      n = NAME_CAP - 1;
      while (n > 0 && botIsUtf8Cont(name[n])) n--;
    }
    if (n) memcpy(e.name, name, n);
    e.name[n] = '\0';
    e.last_seen = now_s;
    e.hops = hops;
    e.snr = snr;
  }

  // Entries with now_s - last_seen <= window_s.
  int count(uint32_t now_s, uint32_t window_s) const {
    int n = 0;
    for (int i = 0; i < SLOTS; i++) if (_e[i].used && now_s - _e[i].last_seen <= window_s) n++;
    return n;
  }

  // "Heard <n> in 24h." plus, when any counted node was heard with zero hops,
  // " Direct: <name> <snr>, ..." most recent first, adding names only while the whole result
  // stays within max_bytes (and cap - 1). Nameless nodes show their first two key bytes as
  // upper-case hex. Returns bytes written excluding the NUL; NUL-terminates when cap > 0.
  size_t format(char* dst, size_t cap, uint32_t now_s, size_t max_bytes) const {
    if (!dst || cap == 0) return 0;
    size_t limit = max_bytes < cap - 1 ? max_bytes : cap - 1;
    BotOut o = botOutInit(dst, limit + 1);
    botOutAppend(o, "Heard ");
    botOutInt(o, count(now_s, WINDOW_S));
    botOutAppend(o, " in 24h.");
    bool picked[SLOTS];
    for (int i = 0; i < SLOTS; i++) picked[i] = false;
    bool first = true;
    for (;;) {
      int best = -1;
      uint32_t best_age = 0;
      for (int i = 0; i < SLOTS; i++) {
        if (!_e[i].used || picked[i] || _e[i].hops != 0) continue;
        uint32_t age = now_s - _e[i].last_seen;
        if (age > WINDOW_S) continue;
        if (best < 0 || age < best_age) { best = i; best_age = age; }
      }
      if (best < 0 || o.full) break;
      picked[best] = true;
      char item[64];
      BotOut it = botOutInit(item, sizeof(item));
      botOutAppend(it, first ? " Direct: " : ", ");
      if (_e[best].name[0]) {
        botOutAppend(it, _e[best].name);
      } else {
        char hex[8];
        snprintf(hex, sizeof(hex), "%02X%02X", _e[best].key[0], _e[best].key[1]);
        botOutAppend(it, hex);
      }
      botOutAppend(it, " ");
      botOutSnr(it, _e[best].snr);
      if (it.full || o.n + it.n > limit) break;  // whole name or nothing; no trailing comma
      botOutAppendN(o, item, it.n);
      first = false;
    }
    return o.n;
  }

private:
  struct Entry {
    bool used;
    uint8_t key[4];
    char name[NAME_CAP];
    uint32_t last_seen;
    uint8_t hops;
    float snr;
  };
  Entry _e[SLOTS];
};

// Packet counts and hop totals in 60 buckets, each opened by the first packet after the
// previous bucket is >= 1 minute old. Ages are unsigned differences, so the 32-bit
// millisecond wrap is safe and buckets older than an hour are ignored. (After ~49 days of
// silence an old bucket could look recent again; harmless.) Counters saturate.
class BotMeshStats {
public:
  static const int BUCKETS = 60;
  static const uint32_t BUCKET_MS = 60000u;
  static const uint32_t HOUR_MS = 3600000u;

  BotMeshStats() : _head(0), _any(false) {
    for (int i = 0; i < BUCKETS; i++) { _start[i] = 0; _count[i] = 0; _hops[i] = 0; }
  }

  void notePacket(uint32_t now_ms, uint8_t hops) {
    if (!_any) {
      _any = true;
      _start[_head] = now_ms;
    } else if (now_ms - _start[_head] >= BUCKET_MS) {
      _head = (_head + 1) % BUCKETS;  // reuse the oldest slot
      _start[_head] = now_ms;
      _count[_head] = 0;
      _hops[_head] = 0;
    }
    if (_count[_head] < 0xFFFFu) _count[_head]++;
    if (_hops[_head] > 0xFFFFFFFFu - hops) _hops[_head] = 0xFFFFFFFFu; else _hops[_head] += hops;
  }

  uint16_t packetsLastHour(uint32_t now_ms) const {
    uint32_t n, h;
    sum(now_ms, n, h);
    return n > 0xFFFFu ? 0xFFFFu : (uint16_t)n;
  }

  // Mean hops per packet over the last hour; 0 when there were none.
  float avgHops(uint32_t now_ms) const {
    uint32_t n, h;
    sum(now_ms, n, h);
    return n ? (float)((double)h / (double)n) : 0.0f;
  }

private:
  void sum(uint32_t now_ms, uint32_t& n, uint32_t& h) const {
    n = 0; h = 0;
    for (int i = 0; i < BUCKETS; i++) {
      if (_count[i] == 0 || now_ms - _start[i] >= HOUR_MS) continue;
      n += _count[i];
      h = (h > 0xFFFFFFFFu - _hops[i]) ? 0xFFFFFFFFu : h + _hops[i];
    }
  }
  uint32_t _start[BUCKETS];
  uint16_t _count[BUCKETS];
  uint32_t _hops[BUCKETS];
  int _head;
  bool _any;
};
