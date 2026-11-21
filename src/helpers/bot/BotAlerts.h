#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <ArduinoJson.h>
#include "BotText.h"
#include "BotUrl.h"
#include "BotReplies.h"
#include "BotJsonPath.h"
#include "BotTime.h"   // botIsoToUnix, botWeekdayHm

// Public weather alerts for the chat bot (Environment and Climate Change Canada GeoJSON; pure
// C/C++ apart from ArduinoJson, no heap of its own, host-testable).

struct BotAlert { char name[48]; char area[40]; uint32_t ends_unix; };   // ends_unix 0 = unknown

// A box of 0.05 degrees around the point, with exactly 4 decimals; only the properties the parser
// reads are asked for (a five-alert answer is then about 3 KB instead of 13 KB, well under the
// 16 KB body cap), up to 10 features. False for coordinates out of range or a URL that does not fit.
static inline bool botAlertsUrl(char* dst, size_t cap, double lat, double lon) {
  if (!dst || cap == 0) return false;
  dst[0] = '\0';
  char a[4][24];
  if (!botValidLatLon(lat, lon) || !botUrlFmtCoord(a[0], sizeof a[0], lon - 0.05) ||
      !botUrlFmtCoord(a[1], sizeof a[1], lat - 0.05) || !botUrlFmtCoord(a[2], sizeof a[2], lon + 0.05) ||
      !botUrlFmtCoord(a[3], sizeof a[3], lat + 0.05))
    return false;
  int k = snprintf(dst, cap,
      "https://api.weather.gc.ca/collections/weather-alerts/items?f=json&skipGeometry=true&limit=10"
      "&properties=alert_short_name_en,alert_name_en,status_en,event_end_datetime,expiration_datetime,feature_name_en"
      "&bbox=%s,%s,%s,%s",
      a[0], a[1], a[2], a[3]);
  if (k < 0 || (size_t)k >= cap) { dst[0] = '\0'; return false; }
  return true;
}

static inline void botAlertsFilter(JsonDocument& f) {
  f.clear();
  JsonObject p = f["features"][0]["properties"].to<JsonObject>();
  p["alert_name_en"] = true;
  p["alert_short_name_en"] = true;
  p["status_en"] = true;
  p["event_end_datetime"] = true;
  p["expiration_datetime"] = true;
  p["feature_name_en"] = true;
}

// Reads the features of a GeoJSON collection. Stores up to max alerts in out and returns how many
// were stored (0..max); *total (when not null) gets the number of distinct active alerts in the
// whole document, which can exceed max. -1 (and *total 0) when root is not an object with a
// "features" array. An alert is active when its status_en is not "ended" (any case) and its end
// time (event_end_datetime, else expiration_datetime) is missing or after now_unix (nothing is
// expired when now_unix is 0). name is alert_short_name_en when it is a non-empty string, else
// alert_name_en (a feature with neither is skipped); area is feature_name_en. Features with the
// same name and area are merged (the later known end wins). Alerts beyond max are counted but not
// stored, so two duplicates of each other among them are counted twice (an over-count, accepted);
// a duplicate of a stored alert is never counted again. Text is control-cleaned and cut on a
// UTF-8 boundary to fit.
static inline int botAlertsParse(JsonVariantConst root, uint32_t now_unix, BotAlert* out, int max, int* total) {
  if (total) *total = 0;
  if (!root.is<JsonObjectConst>()) return -1;
  JsonVariantConst feats = root["features"];
  if (!feats.is<JsonArrayConst>()) return -1;
  if (!out || max < 0) max = 0;
  int n = 0, count = 0;
  for (JsonVariantConst f : feats.as<JsonArrayConst>()) {
    JsonVariantConst p = f["properties"];
    if (!p.is<JsonObjectConst>()) continue;
    if (botEqNoCase(botJsonStr(p["status_en"]), "ended")) continue;
    uint32_t end = 0;
    const char* es = botJsonStr(p["event_end_datetime"]);
    if (!es || !botIsoToUnix(es, end)) {
      end = 0;
      es = botJsonStr(p["expiration_datetime"]);
      if (!es || !botIsoToUnix(es, end)) end = 0;
    }
    if (now_unix != 0 && end != 0 && end <= now_unix) continue;
    const char* nm = botJsonStr(p["alert_short_name_en"]);
    if (!nm || !*nm) nm = botJsonStr(p["alert_name_en"]);
    if (!nm || !*nm) continue;
    BotAlert a;
    botCopyClean(a.name, sizeof a.name, nm);
    botCopyClean(a.area, sizeof a.area, botJsonStr(p["feature_name_en"]));
    a.ends_unix = end;
    bool dup = false;
    for (int i = 0; i < n; i++) {
      if (strcmp(out[i].name, a.name) == 0 && strcmp(out[i].area, a.area) == 0) {
        if (a.ends_unix > out[i].ends_unix) out[i].ends_unix = a.ends_unix;
        dup = true;
        break;
      }
    }
    if (dup) continue;
    if (n < max) out[n++] = a;
    if (count < 1000) count++;
  }
  if (total) *total = count;
  return n;
}

static inline void botAlertText(BotOut& o, const BotAlert& a, bool with_area, int32_t utc_offset_s) {
  botOutAppend(o, a.name);
  if (with_area && a.area[0]) { botOutAppend(o, " ("); botOutAppend(o, a.area); botOutAppend(o, ")"); }
  if (a.ends_unix != 0) {
    char t[16];
    botWeekdayHm(a.ends_unix, utc_offset_s, t, sizeof t);
    botOutAppend(o, " until ");
    botOutAppend(o, t);
  }
}

// "No active alerts here", "1 alert: Snowfall warning (Toronto) until Sat 14:00" or
// "2 alerts: Snowfall warning until Sat 14:00; Wind warning", without the mention. Reads only
// a[0..stored-1] (negative stored is 0); the count at the front is total (stored when total is
// smaller). With nothing stored but a positive total only the count is printed ("5 alerts").
// The area is shown only when the total is 1 and it fits; the "until" part is left out when the
// end is unknown; alerts are dropped from the right to fit max_bytes (and cap-1) while the count
// stays the total; if even the first part does not fit it is cut with botClamp.
static inline size_t botAlertsFormat(char* dst, size_t cap, const BotAlert* a, int stored, int total,
                                     int32_t utc_offset_s, size_t max_bytes) {
  if (!dst || cap == 0) return 0;
  dst[0] = '\0';
  size_t limit = max_bytes < cap - 1 ? max_bytes : cap - 1;
  size_t len = 0;
  if (!a || stored < 0) stored = 0;
  if (total < stored) total = stored;
  if (total <= 0) {
    botAddSegment(dst, cap, len, limit, "No active alerts here");
    return len;
  }
  char seg[160];
  for (int with_area = (total == 1 && stored == 1) ? 1 : 0; with_area >= 0; with_area--) {   // a single alert: try with the area first
    BotOut o = botOutInit(seg, sizeof seg);
    botOutInt(o, total);
    botOutAppend(o, total == 1 ? " alert" : " alerts");
    if (stored > 0) {
      botOutAppend(o, ": ");
      botAlertText(o, a[0], with_area != 0, utc_offset_s);
    }
    if (strlen(seg) <= limit || with_area == 0) break;
  }
  if (!botAddSegment(dst, cap, len, limit, seg)) return len;
  for (int i = 1; i < stored; i++) {
    BotOut o = botOutInit(seg, sizeof seg);
    botOutAppend(o, "; ");
    botAlertText(o, a[i], false, utc_offset_s);
    if (!botAddSegment(dst, cap, len, limit, seg)) break;
  }
  return len;
}
