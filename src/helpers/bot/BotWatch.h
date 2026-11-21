#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <ArduinoJson.h>
#include "BotText.h"
#include "BotReplies.h"
#include "BotJsonPath.h"
#include "BotConfig.h"   // BotWatchKind

// Watched observers and nodes for the chat bot (MeshCore Canada analyzer; pure C/C++ apart from
// ArduinoJson, no heap of its own, host-testable).

// BotWatchKind (BW_OBSERVER, BW_NODE) is defined once, in BotConfig.h.

struct BotWatchState { bool known; bool online; uint32_t last_seen_unix; };   // last_seen_unix 0 = unknown

// The id is inserted as given, so it must be non-empty and only hex digits and '-' (it is
// validated when stored; this is the last line of defence against a changed URL). False when it
// does not fit.
static inline bool botWatchUrl(char* dst, size_t cap, BotWatchKind kind, const char* id) {
  if (!dst || cap == 0) return false;
  dst[0] = '\0';
  if (!id || !*id) return false;
  for (const char* p = id; *p; p++) {
    char c = *p;
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F') || c == '-')) return false;
  }
  int k = snprintf(dst, cap, kind == BW_NODE ? "https://live.meshcore.ca/api/v1/nodes?pubkey=%s"
                                             : "https://live.meshcore.ca/api/v1/observers/%s", id);
  if (k < 0 || (size_t)k >= cap) { dst[0] = '\0'; return false; }
  return true;
}

static inline void botWatchFilter(JsonDocument& f, BotWatchKind kind) {
  f.clear();
  if (kind == BW_NODE) {
    f["items"][0]["lastSeen"] = true;
  } else {
    f["status"] = true;
    f["lastSeen"] = true;
  }
}

// lastSeen is unix milliseconds. Missing means unknown (0); a value that is not a number is a
// parse failure; zero, negative or beyond 32-bit seconds counts as unknown.
static inline bool botWatchLastSeen(JsonVariantConst v, uint32_t& out) {
  out = 0;
  if (v.isNull()) return true;
  double ms;
  if (!botJsonNum(v, ms)) return false;
  if (ms >= 1000.0 && ms / 1000.0 <= 4294967295.0) out = (uint32_t)(ms / 1000.0);
  return true;
}

// Observer: root is an object whose status is a string; online when it is "online". Node: root is
// an object whose items is an array; empty means known = false; otherwise items[0] (an object)
// gives lastSeen and the node is online when now_unix - lastSeen <= online_within_s (a last-seen
// in the future counts as now; when now_unix is 0 any known lastSeen counts as online; an unknown
// lastSeen is offline).
static inline bool botWatchParse(JsonVariantConst root, BotWatchKind kind, uint32_t now_unix,
                                 uint32_t online_within_s, BotWatchState& out) {
  if (!root.is<JsonObjectConst>()) return false;
  BotWatchState s = {true, false, 0};
  if (kind == BW_OBSERVER) {
    const char* st = botJsonStr(root["status"]);
    if (!st || !botWatchLastSeen(root["lastSeen"], s.last_seen_unix)) return false;
    s.online = botEqNoCase(st, "online");
  } else {
    JsonVariantConst items = root["items"];
    if (!items.is<JsonArrayConst>()) return false;
    if (items.as<JsonArrayConst>().size() == 0) {
      s.known = false;
    } else {
      JsonVariantConst it = items[0];
      if (!it.is<JsonObjectConst>() || !botWatchLastSeen(it["lastSeen"], s.last_seen_unix)) return false;
      if (s.last_seen_unix != 0)
        s.online = now_unix == 0 || s.last_seen_unix >= now_unix || now_unix - s.last_seen_unix <= online_within_s;
    }
  }
  out = s;
  return true;
}

// "Label: online", "Label: offline 2d" (<n>m under an hour, <n>h under two days, else <n>d; plain
// "offline" when the last-seen time or the clock is unknown) or "Label: unknown". Null label is "".
static inline size_t botWatchFormatOne(char* dst, size_t cap, const char* label, const BotWatchState& s, uint32_t now_unix) {
  BotOut o = botOutInit(dst, cap);
  botOutAppend(o, label);
  botOutAppend(o, ": ");
  if (!s.known) {
    botOutAppend(o, "unknown");
  } else if (s.online) {
    botOutAppend(o, "online");
  } else {
    botOutAppend(o, "offline");
    if (s.last_seen_unix != 0 && now_unix != 0) {
      uint32_t age = now_unix > s.last_seen_unix ? now_unix - s.last_seen_unix : 0;
      botOutAppend(o, " ");
      if (age < 3600) { botOutInt(o, (long)(age / 60)); botOutAppend(o, "m"); }
      else if (age < 2 * 86400u) { botOutInt(o, (long)(age / 3600)); botOutAppend(o, "h"); }
      else { botOutInt(o, (long)(age / 86400)); botOutAppend(o, "d"); }
    }
  }
  return o.n;
}
