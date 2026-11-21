#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "BotText.h"
#include "BotOutgoing.h"     // BotReplyDest
#include "BotReplies.h"      // BotMsgInfo, BotNameLookup, botPathUnknown
#include "BotTemplate.h"     // BotVars
#include "BotNetLogic.h"     // botParkSameCommand
#include "BotMqttWait.h"     // BotMqttCommands
#include "BotConfig.h"

// Unknown commands put to the MQTT broker, on the bot's side (pure C/C++, no heap, host-testable):
// the four slots that keep what is needed to finish a reply that comes later, the request's path
// fields, and merging announced commands with the bot's own for help and the command list.

#define BOT_ASK_SLOTS 4
// A slot still in use this long after it was taken is released by the bot itself. The client's own
// wait (at most 15 s) always ends first; this only guards against a slot that was never reported.
#define BOT_ASK_BACKSTOP_MS 60000u

// Everything kept for one request: where the reply goes, the log line, and the placeholder values
// from the original message (copied, so {sender}, {hops} and the rest render as they were).
struct BotAskSlot {
  bool used;
  uint8_t seq;                  // changes each time the slot is taken; part of its token
  uint32_t taken_ms;
  BotReplyDest dest;
  char cmd[17];
  char sender[32];
  char request[48];
  char args[48];                // {args}, {path}: a reply is at most one message, so longer is never used
  char path[40];
  char time[8];
  char date[12];
  char uptime[16];
  int16_t hops;
  int16_t rssi;
  float snr;
  uint16_t budget;              // reply bytes after the mention
};

// Four slots. A slot is named to the client by a token (index and a 6-bit sequence number), so a
// report that arrives for a slot already released and taken again never finishes the new request.
class BotAskTable {
public:
  BotAskTable() : _next_seq(1) { memset(_s, 0, sizeof(_s)); }

  // A free slot, now in use (its fields zeroed), or -1.
  int take(uint32_t now_ms) {
    for (int i = 0; i < BOT_ASK_SLOTS; i++) {
      if (_s[i].used) continue;
      memset(&_s[i], 0, sizeof(_s[i]));
      _s[i].used = true;
      _s[i].seq = _next_seq;
      _next_seq = (uint8_t)((_next_seq + 1) & 0x3F);
      if (_next_seq == 0) _next_seq = 1;
      _s[i].taken_ms = now_ms;
      return i;
    }
    return -1;
  }

  void release(int i) { if (valid(i)) memset(&_s[i], 0, sizeof(_s[i])); }
  bool used(int i) const { return valid(i) && _s[i].used; }
  int count() const { int n = 0; for (int i = 0; i < BOT_ASK_SLOTS; i++) if (_s[i].used) n++; return n; }
  BotAskSlot& at(int i) { return _s[valid(i) ? i : 0]; }
  const BotAskSlot& at(int i) const { return _s[valid(i) ? i : 0]; }

  uint8_t token(int i) const { return valid(i) ? (uint8_t)((_s[i].seq << 2) | (uint8_t)i) : 0; }
  // The slot a token names, when that slot is still the same request; else -1.
  int fromToken(uint8_t tok) const {
    int i = tok & 3;
    return (_s[i].used && _s[i].seq == (tok >> 2) && _s[i].seq != 0) ? i : -1;
  }

  // Fills slot i (in use) from the parsed command and the message's placeholder values. Text is
  // cut on a UTF-8 boundary to each field; null is "".
  void fill(int i, const BotReplyDest& dest, const char* cmd, const char* sender, const char* request, const BotVars& v,
            size_t budget) {
    if (!used(i)) return;
    BotAskSlot& s = _s[i];
    s.dest = dest;
    copy(s.cmd, sizeof(s.cmd), cmd);
    copy(s.sender, sizeof(s.sender), sender);
    copy(s.request, sizeof(s.request), request);
    copy(s.args, sizeof(s.args), v.args);
    copy(s.path, sizeof(s.path), v.path);
    copy(s.time, sizeof(s.time), v.time);
    copy(s.date, sizeof(s.date), v.date);
    copy(s.uptime, sizeof(s.uptime), v.uptime);
    s.hops = (int16_t)v.hops;
    s.rssi = (int16_t)v.rssi;
    s.snr = v.snr;
    s.budget = (uint16_t)(budget > 0xFFFF ? 0xFFFF : budget);
  }

  // The saved values for rendering the reply ({fetch} is ""; {channel} is the channel of a channel
  // destination, else "").
  void vars(int i, BotVars& v) const {
    const BotAskSlot& s = at(i);
    v.sender = s.sender;
    v.args = s.args;
    v.path = s.path;
    v.channel = s.dest.kind == BD_CHANNEL ? s.dest.channel : "";
    v.time = s.time;
    v.date = s.date;
    v.uptime = s.uptime;
    v.hops = s.hops;
    v.snr = s.snr;
    v.rssi = s.rssi;
    v.fetch = "";
  }

  // A slot already waiting for the same command and arguments (ASCII case ignored, arguments as
  // they would be stored) for the same destination, else -1. Such a repeat is not asked again.
  int findSame(const char* cmd, const char* args, const BotReplyDest& dest) {
    copy(_cmp, sizeof(_cmp), args);
    for (int i = 0; i < BOT_ASK_SLOTS; i++)
      if (_s[i].used && botParkSameCommand(0, _s[i].cmd, _s[i].args, _s[i].dest, 0, 0, cmd, _cmp, dest, 0)) return i;
    return -1;
  }

  // A slot in use for backstop_ms or more (wrap-safe), else -1.
  int overdue(uint32_t now_ms, uint32_t backstop_ms = BOT_ASK_BACKSTOP_MS) const {
    for (int i = 0; i < BOT_ASK_SLOTS; i++)
      if (_s[i].used && (uint32_t)(now_ms - _s[i].taken_ms) >= backstop_ms) return i;
    return -1;
  }

private:
  static bool valid(int i) { return i >= 0 && i < BOT_ASK_SLOTS; }
  static void copy(char* dst, size_t cap, const char* src) {
    if (!src) src = "";
    size_t n = strlen(src);
    if (n > cap - 1) { n = cap - 1; while (n > 0 && botIsUtf8Cont(src[n])) n--; }
    memcpy(dst, src, n);
    dst[n] = '\0';
  }
  BotAskSlot _s[BOT_ASK_SLOTS];
  char _cmp[sizeof(((BotAskSlot*)0)->args)];
  uint8_t _next_seq;
};

// ---- the request's path fields ----

// Appends item after ',' (none before the first) when the whole still fits cap-1; false (nothing
// written) otherwise, so only whole entries are kept.
static inline bool botAskJoin(char* dst, size_t cap, size_t& n, const char* item) {
  size_t l = item ? strlen(item) : 0;
  size_t sep = n ? 1 : 0;
  if (n + sep + l + 1 > cap) return false;
  if (sep) dst[n++] = ',';
  if (l) memcpy(dst + n, item, l);
  n += l;
  dst[n] = '\0';
  return true;
}

// The repeaters' hashes as lower-case hex joined by ','; "" when the path is not known (direct, no
// hops). Only whole hashes: what does not fit cap-1 is left out from the end.
static inline size_t botAskPathHex(char* dst, size_t cap, const BotMsgInfo& m) {
  if (!dst || cap == 0) return 0;
  dst[0] = '\0';
  size_t n = 0;
  if (botPathUnknown(m)) return 0;
  static const char hex[] = "0123456789abcdef";
  for (uint8_t i = 0; i < m.hops; i++) {
    char t[7];
    const uint8_t* h = m.path + (size_t)i * m.hash_size;
    for (uint8_t k = 0; k < m.hash_size; k++) { t[2 * k] = hex[h[k] >> 4]; t[2 * k + 1] = hex[h[k] & 15]; }
    t[2 * m.hash_size] = '\0';
    if (!botAskJoin(dst, cap, n, t)) break;
  }
  return n;
}

// The same hops by name where lookup knows one (control characters removed, cut to 32 bytes), "?"
// otherwise, joined by ','; "" when the path is not known. Only whole entries are kept.
static inline size_t botAskPathNames(char* dst, size_t cap, const BotMsgInfo& m, BotNameLookup lookup, void* ctx) {
  if (!dst || cap == 0) return 0;
  dst[0] = '\0';
  size_t n = 0;
  if (botPathUnknown(m)) return 0;
  for (uint8_t i = 0; i < m.hops; i++) {
    const char* name = lookup ? lookup(m.path + (size_t)i * m.hash_size, m.hash_size, ctx) : nullptr;
    char t[33];
    if (name && name[0]) botStripControls(t, sizeof(t), name);
    else t[0] = '\0';
    if (!t[0]) { t[0] = '?'; t[1] = '\0'; }
    if (!botAskJoin(dst, cap, n, t)) break;
  }
  return n;
}

// ---- announced commands beside the bot's own ----

// Appends to names[count..max) the announced commands whose name is not already in
// names[0..count) (the enabled built-in and custom commands, which shadow them), in announcement
// order, with their description and source "mqtt" in the parallel arrays when those are given.
// Returns the new count.
static inline int botAskMergeAnnounced(const char** names, const char** descs, const char** sources, int count, int max,
                                       const BotMqttCommands& ann) {
  if (!names || count < 0) return count < 0 ? 0 : count;
  int own = count;
  for (int a = 0; a < ann.count() && count < max; a++) {
    const char* n = ann.nameAt(a);
    bool shadowed = false;
    for (int i = 0; i < own && !shadowed; i++) shadowed = names[i] && strcmp(names[i], n) == 0;
    if (shadowed) continue;
    names[count] = n;
    if (descs) descs[count] = ann.descAt(a);
    if (sources) sources[count] = "mqtt";
    count++;
  }
  return count;
}

// The published command list in this order: the custom commands, then the announced commands
// they and the built-ins do not shadow, then the built-ins (so what a handler author most needs,
// the names the owner and other handlers took, comes first). out_* get at most max entries; the
// inputs are parallel arrays of the enabled commands only. Returns the count.
static inline int botAskListEntries(const char** out_n, const char** out_d, const char** out_s, int max,
                                    const char* const* bn, const char* const* bd, int bcount,
                                    const char* const* cn, const char* const* cd, int ccount, const BotMqttCommands& ann) {
  int n = 0;
  for (int i = 0; i < ccount && n < max; i++) { out_n[n] = cn[i]; out_d[n] = cd[i]; out_s[n++] = "custom"; }
  for (int a = 0; a < ann.count() && n < max; a++) {
    const char* nm = ann.nameAt(a);
    bool shadowed = false;
    for (int i = 0; i < bcount && !shadowed; i++) shadowed = bn[i] && strcmp(bn[i], nm) == 0;
    for (int i = 0; i < ccount && !shadowed; i++) shadowed = cn[i] && strcmp(cn[i], nm) == 0;
    if (shadowed) continue;
    out_n[n] = nm; out_d[n] = ann.descAt(a); out_s[n++] = "mqtt";
  }
  for (int i = 0; i < bcount && n < max; i++) { out_n[n] = bn[i]; out_d[n] = bd[i]; out_s[n++] = "builtin"; }
  return n;
}

// ---- what may leave the device ----

// Whether cmd names a built-in (by its configured name) or a custom command (by its trigger) that
// exists but is switched off. Such a command stays silent: it is never put to the broker.
static inline bool botAskKnownDisabled(const BotConfig& c, const char* cmd) {
  if (!cmd) return false;
  for (int i = 0; i < BB_COUNT; i++) if (!c.builtin[i].enabled && strcmp(c.builtin[i].name, cmd) == 0) return true;
  for (int i = 0; i < c.num_custom && i < BOT_MAX_CUSTOM; i++)
    if (!c.custom[i].enabled && strcmp(c.custom[i].trigger, cmd) == 0) return true;
  return false;
}

// What hides an announced command of this name from the broker: a built-in (by its configured name)
// or a custom command (by its trigger), switched on or off. A switched-on one answers it locally;
// a switched-off one keeps it silent (botAskKnownDisabled). Built-ins are looked at first, as the
// bot does when it answers.
enum BotAskHidden : uint8_t { BAH_NONE = 0, BAH_BUILTIN, BAH_CUSTOM };
static inline uint8_t botAskHiddenBy(const BotConfig& c, const char* name) {
  if (!name || !name[0]) return BAH_NONE;
  for (int i = 0; i < BB_COUNT; i++) if (strcmp(c.builtin[i].name, name) == 0) return BAH_BUILTIN;
  for (int i = 0; i < c.num_custom && i < BOT_MAX_CUSTOM; i++) if (strcmp(c.custom[i].trigger, name) == 0) return BAH_CUSTOM;
  return BAH_NONE;
}

// Whether a command no enabled built-in or custom command answered may be offered to the broker:
// the client is ready, the command prefix was written (a direct message or a web try without it
// never leaves the device, whatever handlers announced; a channel message always has it), the name
// is not a switched-off command, and the cap would let the reply out (botBrokerMayAsk; nothing is
// counted).
static inline bool botAskMayOffer(uint8_t dest_kind, bool prefixed, bool known_disabled, bool ready, const BotRateCap& cap,
                                  uint32_t now_ms, uint8_t max_per_min) {
  return prefixed && !known_disabled && botBrokerMayAsk(dest_kind, ready, cap, now_ms, max_per_min);
}
