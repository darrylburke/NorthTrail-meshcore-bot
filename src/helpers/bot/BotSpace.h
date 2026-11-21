#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <ArduinoJson.h>
#include "BotText.h"
#include "BotReplies.h"
#include "BotJsonPath.h"
#include "BotTime.h"

// Geomagnetic (Kp) outlook for the chat bot (NOAA SWPC forecast table; pure C/C++ apart from
// ArduinoJson, no heap of its own, host-testable).

struct BotKp { float now; float max24; bool has_max24; };

static inline const char* botKpUrl() {
  return "https://services.swpc.noaa.gov/products/noaa-planetary-k-index-forecast.json";
}

// Keeps time_tag, kp and observed of every row.
static inline void botKpFilter(JsonDocument& f) {
  f.clear();
  f[0]["time_tag"] = true;
  f[0]["kp"] = true;
  f[0]["observed"] = true;
}

// root is an array of {"time_tag":"2026-11-21T00:00:00","kp":2.33,"observed":"observed"|"estimated"|"predicted"}.
// now = kp of the latest row at or before now_unix; max24 = largest kp of the rows after now_unix
// and within 24 hours. When now_unix is 0 (clock not set): now = the last "observed" or
// "estimated" row, no max24. Rows with a bad time_tag, a kp that is not a number from 0 to 9 are
// skipped. False when no usable row gives a "now" (including a table that starts in the future).
static inline bool botKpParse(JsonVariantConst root, uint32_t now_unix, BotKp& out) {
  if (!root.is<JsonArrayConst>()) return false;
  bool have_now = false, have_max = false;
  uint32_t now_t = 0;
  float now_kp = 0, max_kp = 0;
  for (JsonVariantConst row : root.as<JsonArrayConst>()) {
    if (!row.is<JsonObjectConst>()) continue;
    double kp;
    if (!botJsonNum(row["kp"], kp) || kp < 0 || kp > 9) continue;
    if (now_unix == 0) {
      const char* ob = botJsonStr(row["observed"]);
      if (ob && (strcmp(ob, "observed") == 0 || strcmp(ob, "estimated") == 0)) { have_now = true; now_kp = (float)kp; }
      continue;
    }
    const char* tag = botJsonStr(row["time_tag"]);
    uint32_t t;
    if (!tag || !botIsoToUnix(tag, t)) continue;
    if (t <= now_unix) {
      if (!have_now || t >= now_t) { have_now = true; now_t = t; now_kp = (float)kp; }
    } else if ((uint64_t)t <= (uint64_t)now_unix + 86400u) {
      if (!have_max || (float)kp > max_kp) { have_max = true; max_kp = (float)kp; }
    }
  }
  if (!have_now) return false;
  out.now = now_kp;
  out.has_max24 = have_max;
  out.max24 = have_max ? max_kp : 0;
  return true;
}

static inline const char* botKpWord(float kp) {
  if (kp >= 8) return "severe storm";
  if (kp >= 7) return "strong storm";
  if (kp >= 6) return "moderate storm";
  if (kp >= 5) return "minor storm";
  if (kp >= 4) return "active";
  if (kp >= 3) return "unsettled";
  return "quiet";   // also NaN
}

// Rough guide for eastern Canada only: the Kp usually needed for a sighting is 3 at latitude 50
// and above, 5 from 45 to below 50, 6 from 42 to below 45, 7 below 42. "unlikely" under needed-1,
// "possible" from needed-1 to below needed, "likely" at or above it.
static inline const char* botAuroraChance(float kp, double lat) {
  float need = lat >= 50 ? 3.0f : lat >= 45 ? 5.0f : lat >= 42 ? 6.0f : 7.0f;
  if (kp >= need) return "likely";
  if (kp >= need - 1) return "possible";
  return "unlikely";
}

static inline void botKpOne(BotOut& o, float kp) {
  char t[16];
  int k = snprintf(t, sizeof t, "%.1f", (double)kp);
  if (k > 0) botOutAppendN(o, t, (size_t)k < sizeof t ? (size_t)k : sizeof t - 1);
}

// "Kp 2.3 (quiet). Aurora unlikely here. Next 24h peak Kp 4.3" without the mention; lat exactly 0
// (not set) gives "Set a location for a local outlook." in place of the aurora sentence. Parts
// are dropped from the right to fit max_bytes (and cap-1); if even the first does not fit it is
// cut with botClamp.
static inline size_t botAuroraFormat(char* dst, size_t cap, const BotKp& k, double lat, size_t max_bytes) {
  if (!dst || cap == 0) return 0;
  dst[0] = '\0';
  size_t limit = max_bytes < cap - 1 ? max_bytes : cap - 1;
  size_t n = 0;
  char seg[64];
  BotOut o = botOutInit(seg, sizeof seg);
  botOutAppend(o, "Kp ");
  botKpOne(o, k.now);
  botOutAppend(o, " (");
  botOutAppend(o, botKpWord(k.now));
  botOutAppend(o, ")");
  if (!botAddSegment(dst, cap, n, limit, seg)) return n;
  o = botOutInit(seg, sizeof seg);
  if (lat == 0) {
    botOutAppend(o, ". Set a location for a local outlook");
  } else {
    botOutAppend(o, ". Aurora ");
    botOutAppend(o, botAuroraChance(k.now, lat));
    botOutAppend(o, " here");
  }
  if (!botAddSegment(dst, cap, n, limit, seg)) return n;
  if (k.has_max24) {
    o = botOutInit(seg, sizeof seg);
    botOutAppend(o, ". Next 24h peak Kp ");
    botKpOne(o, k.max24);
    botAddSegment(dst, cap, n, limit, seg);
  }
  return n;
}
