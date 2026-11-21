#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "BotLimits.h"   // BotRateCap
#include "BotText.h"     // botCopyClean

// Requests the bot has published and is waiting to hear about, the publish rate limit, request ids
// and the commands announced over MQTT (pure C/C++, no heap, host-testable). All times are millis()
// values compared by unsigned subtraction, so they survive the 32-bit wrap.

#define BOT_MQTT_WAIT_SLOTS   4
#define BOT_MQTT_RATE_PER_MIN 30
#define BOT_MQTT_CMD_SLOTS    16
#define BOT_MQTT_CMD_NAME_MAX 16
#define BOT_MQTT_CMD_DESC_MAX 48

struct BotMqttWaiting { bool used; char id[9]; uint32_t sent_ms; uint32_t generation; uint8_t user; uint32_t wait_ms; };   // user = caller's slot index for its own data

// Four slots. Contract:
//  - A reply is accepted once: match() frees the slot, and expire() frees it too, so neither a
//    repeated reply nor a late one can be matched again.
//  - A reply is late when now_ms - sent_ms >= wait_ms (unsigned, wrap-safe). A late reply never
//    matches, even before expire() has run; the slot stays until expire() reports it as timed out.
//  - A reply for another generation (the settings changed after the request was sent) never
//    matches and does not free the slot; it simply expires.
//  - expire() must be called regularly (at least every few seconds): ages are 32-bit differences, so
//    an entry that is not expired within about 49.7 days looks young again.
class BotMqttWaitTable {
public:
  static const int SLOTS = BOT_MQTT_WAIT_SLOTS;
  BotMqttWaitTable() { clear(); }

  // Lowest free slot, or -1 when full, when id is null, empty or longer than 8 bytes, or when the id is already waiting.
  // wait_ms is kept for matchOwn()/expireOwn(); match() and expire() take theirs from the caller.
  int add(const char id[9], uint32_t now_ms, uint32_t generation, uint32_t wait_ms = 0) {
    if (!id) return -1;
    size_t n = strnlen(id, 9);
    if (n == 0 || n > 8) return -1;
    int free_slot = -1;
    for (int i = 0; i < SLOTS; i++) {
      if (!_s[i].used) { if (free_slot < 0) free_slot = i; continue; }
      if (strcmp(_s[i].id, id) == 0) return -1;
    }
    if (free_slot < 0) return -1;
    BotMqttWaiting& w = _s[free_slot];
    w.used = true; memcpy(w.id, id, n); w.id[n] = '\0'; w.sent_ms = now_ms; w.generation = generation; w.user = 0;
    w.wait_ms = wait_ms;
    return free_slot;
  }

  // The slot waiting for exactly this id and generation whose wait has not run out, freed at once; otherwise -1.
  int match(const char* id, uint32_t generation, uint32_t now_ms, uint32_t wait_ms) { return matchWith(id, generation, now_ms, &wait_ms, nullptr); }

  // The slot that has waited longest, if it has waited wait_ms or more; freed. One per call, -1 when none.
  int expire(uint32_t now_ms, uint32_t wait_ms) { return expireWith(now_ms, &wait_ms, nullptr); }

  // As match() and expire(), with each slot's own wait (given to add(), so a later change of the
  // wait time applies only to requests made after it) and the slot's user handed out before the
  // slot is freed. *user is 0 when -1 is returned. Of several slots past their wait, expireOwn
  // frees the one furthest past it (with one common wait, that is the oldest, as expire()).
  int matchOwn(const char* id, uint32_t generation, uint32_t now_ms, uint8_t* user) { return matchWith(id, generation, now_ms, nullptr, user); }
  int expireOwn(uint32_t now_ms, uint8_t* user) { return expireWith(now_ms, nullptr, user); }

  // Frees any one slot in use (lowest first) and hands out its user: for emptying the table when
  // the connection is lost, each freed request being reported as timed out. -1 when empty.
  int takeAny(uint8_t* user) {
    if (user) *user = 0;
    for (int i = 0; i < SLOTS; i++)
      if (_s[i].used) { if (user) *user = _s[i].user; _s[i].used = false; return i; }
    return -1;
  }
  // Frees slot i (a request that could not be sent after all).
  void release(int i) { if (i >= 0 && i < SLOTS) _s[i].used = false; }

  int count() const { int n = 0; for (int i = 0; i < SLOTS; i++) if (_s[i].used) n++; return n; }
  void clear() { for (int i = 0; i < SLOTS; i++) { _s[i].used = false; _s[i].id[0] = '\0'; _s[i].sent_ms = 0; _s[i].generation = 0; _s[i].user = 0; _s[i].wait_ms = 0; } }

  // The caller's own storage for a waiting slot; ignored for a slot out of range or free.
  void setUser(int slot, uint8_t user) { if (slot >= 0 && slot < SLOTS && _s[slot].used) _s[slot].user = user; }
  uint8_t userOf(int slot) const { return (slot >= 0 && slot < SLOTS && _s[slot].used) ? _s[slot].user : 0; }

private:
  uint32_t waitOf(int i, const uint32_t* wait_ms) const { return wait_ms ? *wait_ms : _s[i].wait_ms; }
  int matchWith(const char* id, uint32_t generation, uint32_t now_ms, const uint32_t* wait_ms, uint8_t* user) {
    if (user) *user = 0;
    if (!id) return -1;
    for (int i = 0; i < SLOTS; i++) {
      if (!_s[i].used || strcmp(_s[i].id, id) != 0) continue;
      if (_s[i].generation != generation || (uint32_t)(now_ms - _s[i].sent_ms) >= waitOf(i, wait_ms)) return -1;
      if (user) *user = _s[i].user;
      _s[i].used = false;
      return i;
    }
    return -1;
  }
  int expireWith(uint32_t now_ms, const uint32_t* wait_ms, uint8_t* user) {
    if (user) *user = 0;
    int best = -1;
    uint32_t best_over = 0;
    for (int i = 0; i < SLOTS; i++) {
      if (!_s[i].used) continue;
      uint32_t age = now_ms - _s[i].sent_ms, w = waitOf(i, wait_ms);
      if (age < w) continue;
      if (best < 0 || age - w > best_over) { best = i; best_over = age - w; }
    }
    if (best >= 0) { if (user) *user = _s[best].user; _s[best].used = false; }
    return best;
  }
  BotMqttWaiting _s[SLOTS];
};

// At most 30 requests per sliding 60 s; refused attempts are not recorded (BotRateCap).
class BotMqttRate {
public:
  bool allow(uint32_t now_ms) { return _cap.allow(now_ms, BOT_MQTT_RATE_PER_MIN); }
  bool wouldAllow(uint32_t now_ms) const { return _cap.wouldAllow(now_ms, BOT_MQTT_RATE_PER_MIN); }   // records nothing
private:
  BotRateCap _cap;
};

// 8 lower-case hex characters of `random`.
static inline void botMqttMakeId(char id[9], uint32_t random) {
  if (!id) return;
  static const char hex[] = "0123456789abcdef";
  for (int i = 0; i < 8; i++) id[i] = hex[(random >> (28 - 4 * i)) & 15];
  id[8] = '\0';
}

// Whether s needs no JSON escaping at all: no control byte, '"', '\\' or '<' (the bytes
// botJsonEscape escapes). Null is "" (safe).
static inline bool botMqttDescSafe(const char* s) {
  for (const char* p = s ? s : ""; *p; p++) {
    uint8_t b = (uint8_t)*p;
    if (b < 0x20 || b == '"' || b == '\\' || b == '<') return false;
  }
  return true;
}

// An announced description as the bot keeps it, shows it on the page and in "help <name>", and
// publishes it in the command list: botCopyClean (control characters, 0x7F included, become spaces;
// cut to cap - 1 on a UTF-8 boundary), then '"' becomes '\'', '\\' becomes '/' and '<' becomes '(',
// so no byte of it needs JSON escaping (botMqttDescSafe) and the page's "mqtt" member can count it
// at one byte per byte. Returns the length.
static inline size_t botMqttCleanDesc(char* dst, size_t cap, const char* src) {
  size_t n = botCopyClean(dst, cap, src);
  for (size_t i = 0; i < n; i++) {
    if (dst[i] == '"') dst[i] = '\'';
    else if (dst[i] == '\\') dst[i] = '/';
    else if (dst[i] == '<') dst[i] = '(';
  }
  return n;
}

// Commands announced by handlers over MQTT: 16 entries in announcement order (removing one closes
// the gap, so indexes 0..count()-1 are always in use). Names are 1 to 16 characters of a-z 0-9;
// descriptions are stored as botMqttCleanDesc leaves them (at most 48 bytes, cut on a UTF-8
// boundary, nothing in them needing JSON escaping): set() is the one way in.
class BotMqttCommands {
public:
  BotMqttCommands() : _n(0) { clear(); }

  // Adds or replaces. False for an invalid name or a full table (replacing never needs room).
  bool set(const char* name, const char* desc) {
    if (!validName(name)) return false;
    int i = indexOf(name);
    if (i < 0) {
      if (_n >= BOT_MQTT_CMD_SLOTS) return false;
      i = _n++;
      strcpy(_name[i], name);
    }
    copyDesc(_desc[i], desc);
    return true;
  }

  bool remove(const char* name) {
    int i = indexOf(name);
    if (i < 0) return false;
    for (int j = i + 1; j < _n; j++) { memcpy(_name[j - 1], _name[j], sizeof(_name[0])); memcpy(_desc[j - 1], _desc[j], sizeof(_desc[0])); }
    _n--;
    _name[_n][0] = '\0'; _desc[_n][0] = '\0';
    return true;
  }

  int count() const { return _n; }
  const char* nameAt(int i) const { return (i >= 0 && i < _n) ? _name[i] : ""; }
  const char* descAt(int i) const { return (i >= 0 && i < _n) ? _desc[i] : ""; }
  const char* find(const char* name) const { int i = indexOf(name); return i < 0 ? nullptr : _desc[i]; }
  void clear() { _n = 0; for (int i = 0; i < BOT_MQTT_CMD_SLOTS; i++) { _name[i][0] = '\0'; _desc[i][0] = '\0'; } }

private:
  static bool validName(const char* name) {
    if (!name) return false;
    size_t n = 0;
    for (; name[n]; n++) if (n >= BOT_MQTT_CMD_NAME_MAX || !((name[n] >= 'a' && name[n] <= 'z') || (name[n] >= '0' && name[n] <= '9'))) return false;
    return n > 0;
  }
  int indexOf(const char* name) const {
    if (!name) return -1;
    for (int i = 0; i < _n; i++) if (strcmp(_name[i], name) == 0) return i;
    return -1;
  }
  static void copyDesc(char* dst, const char* src) { botMqttCleanDesc(dst, BOT_MQTT_CMD_DESC_MAX + 1, src); }

  int _n;
  char _name[BOT_MQTT_CMD_SLOTS][BOT_MQTT_CMD_NAME_MAX + 1];
  char _desc[BOT_MQTT_CMD_SLOTS][BOT_MQTT_CMD_DESC_MAX + 1];
};
