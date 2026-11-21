#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Login sessions for the bot's web page (pure C++, no Arduino, host-testable).
// Four slots; a session expires after 20 minutes without a successful check(). The caller
// supplies 16 random bytes per session (the ESP32 hardware RNG on the device) and the time in
// milliseconds; times are compared as unsigned differences, so they survive the 32-bit wrap.
// Not thread-safe: the caller serialises access (BotWeb holds its mutex).
class BotSessions {
public:
  static const int SLOTS = 4;
  static const uint32_t IDLE_MS = 20u * 60u * 1000u;
  static const size_t TOKEN_LEN = 32;   // lower-case hex of 16 bytes

  BotSessions() { clear(); }

  void clear() { memset(_s, 0, sizeof(_s)); }

  // Writes the new token (32 hex + NUL) to token_hex. Takes a free or expired slot, else
  // replaces the session used least recently.
  void create(const uint8_t random16[16], uint32_t now_ms, char token_hex[33]) {
    static const char digits[] = "0123456789abcdef";
    for (int i = 0; i < 16; i++) {
      token_hex[2 * i] = digits[random16[i] >> 4];
      token_hex[2 * i + 1] = digits[random16[i] & 15];
    }
    token_hex[32] = '\0';
    int slot = -1, lru = 0;
    uint32_t lru_age = 0;
    for (int i = 0; i < SLOTS; i++) {
      if (!_s[i].used || expired(_s[i], now_ms)) { slot = i; break; }
      uint32_t age = now_ms - _s[i].last_ms;
      if (age >= lru_age) { lru_age = age; lru = i; }
    }
    if (slot < 0) slot = lru;
    memcpy(_s[slot].token, token_hex, 33);
    _s[slot].last_ms = now_ms;
    _s[slot].used = true;
  }

  // True for a live session with exactly this token. With touch (the default) it also
  // refreshes the idle timer; the page's background polls pass touch=false so an open tab
  // alone never keeps a session alive. Every slot is compared in full, in constant time.
  bool check(const char* token_hex, uint32_t now_ms, bool touch = true) {
    int slot = find(token_hex);
    if (slot < 0) return false;
    if (expired(_s[slot], now_ms)) { memset(&_s[slot], 0, sizeof(_s[slot])); return false; }
    if (touch) _s[slot].last_ms = now_ms;
    return true;
  }

  void revoke(const char* token_hex) {
    int slot = find(token_hex);
    if (slot >= 0) memset(&_s[slot], 0, sizeof(_s[slot]));
  }

  // Ends every session but keep (which may be null or unknown: then all are ended).
  void revokeAllExcept(const char* keep) {
    int slot = find(keep);
    for (int i = 0; i < SLOTS; i++)
      if (i != slot) memset(&_s[i], 0, sizeof(_s[i]));
  }

private:
  struct Slot { char token[33]; uint32_t last_ms; bool used; };
  Slot _s[SLOTS];

  static bool expired(const Slot& s, uint32_t now_ms) { return (uint32_t)(now_ms - s.last_ms) > IDLE_MS; }

  // Index of the used slot holding token_hex, or -1. Length is checked first (it is not
  // secret); the byte comparison then always covers all 32 characters of every slot.
  int find(const char* token_hex) const {
    if (!token_hex) return -1;
    size_t n = 0;
    while (n <= TOKEN_LEN && token_hex[n]) n++;
    if (n != TOKEN_LEN) return -1;
    int found = -1;
    for (int i = 0; i < SLOTS; i++) {
      uint8_t diff = _s[i].used ? 0 : 1;
      for (size_t k = 0; k < TOKEN_LEN; k++) diff |= (uint8_t)(_s[i].token[k] ^ token_hex[k]);
      if (diff == 0) found = i;
    }
    return found;
  }
};
