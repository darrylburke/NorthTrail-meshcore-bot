#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <ArduinoJson.h>
#include "BotText.h"
#include "BotReplies.h"       // BotOut, botOutSnr
#include "BotWebRules.h"      // botJsonEscape
#include "BotJsonPath.h"      // botJsonStr
#include "BotJsonAlloc.h"
#include "BotMqttTopics.h"
#include "BotMqttWait.h"      // botMqttCleanDesc

// The JSON the bot exchanges with an MQTT broker (host-testable). Messages the bot sends are built
// by hand with the bounded writer and botJsonEscape, so the key order is exactly the documented one
// and nothing can overflow. Messages from the broker are untrusted: they are length-checked before
// parsing, parsed into a document that cannot take more than 2 KB, with at most 4 levels of nesting,
// and only typed reads are used. Every function NUL-terminates when cap > 0, never writes past cap,
// and treats a null string as "".

#define BOT_MQTT_REQUEST_MAX   1000
#define BOT_MQTT_REPLY_IN_MAX  512    // largest reply payload that is parsed at all
#define BOT_MQTT_REPLY_TEXT_MAX 200   // longest reply text after cleaning
#define BOT_MQTT_ANNOUNCE_MAX  256    // largest announcement payload that is parsed at all
#define BOT_MQTT_ANNOUNCE_DESC_MAX 48
// The parse limit is 2 KB on the device, where ArduinoJson's pool is 128 slots of 8 bytes (1 KB).
// Its bytes scale with the pool (slots are 2 pointers wide), so the 64-bit host, with 256 slots of
// 16 bytes (4 KB), gets 2 KB * 4 = 8 KB: the same "two pools" in both places.
#define BOT_MQTT_PARSE_HEAP_DEVICE 2048u
#define BOT_MQTT_POOL_DEVICE       1024u
#define BOT_MQTT_POOL_BYTES        (ARDUINOJSON_POOL_CAPACITY * 2u * sizeof(void*))
#define BOT_MQTT_PARSE_HEAP        (BOT_MQTT_PARSE_HEAP_DEVICE * BOT_MQTT_POOL_BYTES / BOT_MQTT_POOL_DEVICE)
#define BOT_MQTT_PARSE_DEPTH   4

struct BotMqttRequest {            // everything in the request table; strings are borrowed
  const char *id, *prefix, *cmd, *args, *sender, *sender_key, *channel, *path, *path_names, *time, *date, *tz, *bot, *scope, *uptime;
  double sender_lat, sender_lon, bot_lat, bot_lon; float snr; int rssi; int hops; bool dm; uint32_t unix; int max_len;
};

// ---- bounded appends: the written length n stays below cap, so dst[n] always exists ----

static inline bool botMqttPutN(char* dst, size_t cap, size_t& n, const char* s, size_t len) {
  if (n + len + 1 > cap) return false;
  memcpy(dst + n, s, len);
  n += len;
  dst[n] = '\0';
  return true;
}

static inline bool botMqttPut(char* dst, size_t cap, size_t& n, const char* s) { return botMqttPutN(dst, cap, n, s, strlen(s)); }

// Quoted and escaped.
static inline bool botMqttPutStr(char* dst, size_t cap, size_t& n, const char* s) {
  if (n + 1 >= cap) return false;
  size_t w = botJsonEscape(dst + n, cap - n, s);
  if (w == 0) return false;
  n += w;
  return true;
}

static inline bool botMqttPutKeyStr(char* dst, size_t cap, size_t& n, const char* key, const char* val) {
  return botMqttPut(dst, cap, n, key) && botMqttPutStr(dst, cap, n, val);
}

// The escaped text of s[0..len), without quotes, in chunks of 8 bytes (an escaped byte is at most
// 6, so a chunk needs at most 48 + 2 quotes + NUL) so no large scratch is needed. The result is what
// botJsonEscape gives for the whole text: it works one byte at a time.
static inline bool botMqttPutEscaped(char* dst, size_t cap, size_t& n, const char* s, size_t len) {
  char c[9], o[56];
  for (size_t off = 0; off < len; off += 8) {
    size_t k = len - off < 8 ? len - off : 8;
    memcpy(c, s + off, k);
    c[k] = '\0';
    size_t w = botJsonEscape(o, sizeof(o), c);          // "…"
    if (w < 2 || !botMqttPutN(dst, cap, n, o + 1, w - 2)) return false;
  }
  return true;
}

// An unsigned 32-bit value in decimal (through unsigned long, which is at least 32 bits; never a signed type).
static inline bool botMqttPutU32(char* dst, size_t cap, size_t& n, uint32_t v) {
  char t[16];
  snprintf(t, sizeof(t), "%lu", (unsigned long)v);
  return botMqttPut(dst, cap, n, t);
}

static inline bool botMqttPutInt(char* dst, size_t cap, size_t& n, long v) {
  char t[24];
  snprintf(t, sizeof(t), "%ld", v);
  return botMqttPut(dst, cap, n, t);
}

// A position with 5 decimals; NaN and infinities print as 0; never "-0.00000".
static inline bool botMqttPutPos(char* dst, size_t cap, size_t& n, double v) {
  if (!(v == v) || v > 1e6 || v < -1e6) v = 0;
  char t[24];
  snprintf(t, sizeof(t), "%.5f", v);
  if (strcmp(t, "-0.00000") == 0) strcpy(t, "0.00000");
  return botMqttPut(dst, cap, n, t);
}

// An id is exactly 8 lower-case hex characters.
static inline bool botMqttIdValid(const char* id) {
  if (!id || strlen(id) != 8) return false;
  for (int i = 0; i < 8; i++) if (!((id[i] >= '0' && id[i] <= '9') || (id[i] >= 'a' && id[i] <= 'f'))) return false;
  return true;
}

// One attempt at the whole request into dst[0..cap), with the first args_len bytes of args; false
// when anything did not fit. r.id and r.prefix are already checked.
static inline bool botMqttRequestTry(char* dst, size_t cap, const BotMqttRequest& r, const char* args, size_t args_len) {
  size_t n = 0;
  dst[0] = '\0';
  char t[24];
  bool ok = botMqttPut(dst, cap, n, "{\"id\":") && botMqttPutStr(dst, cap, n, r.id)
    && botMqttPut(dst, cap, n, ",\"reply_to\":\"") && botMqttPutEscaped(dst, cap, n, r.prefix, strlen(r.prefix))
    && botMqttPut(dst, cap, n, "/reply/") && botMqttPutEscaped(dst, cap, n, r.id, strlen(r.id)) && botMqttPut(dst, cap, n, "\"")
    && botMqttPutKeyStr(dst, cap, n, ",\"cmd\":", r.cmd)
    && botMqttPut(dst, cap, n, ",\"args\":\"") && botMqttPutEscaped(dst, cap, n, args, args_len) && botMqttPut(dst, cap, n, "\"")
    && botMqttPutKeyStr(dst, cap, n, ",\"sender\":", r.sender)
    && botMqttPutKeyStr(dst, cap, n, ",\"sender_key\":", r.sender_key)
    && botMqttPut(dst, cap, n, ",\"sender_lat\":") && botMqttPutPos(dst, cap, n, r.sender_lat)
    && botMqttPut(dst, cap, n, ",\"sender_lon\":") && botMqttPutPos(dst, cap, n, r.sender_lon)
    && botMqttPut(dst, cap, n, r.dm ? ",\"dm\":true" : ",\"dm\":false")
    && botMqttPutKeyStr(dst, cap, n, ",\"channel\":", r.channel)
    && botMqttPut(dst, cap, n, ",\"hops\":") && botMqttPutInt(dst, cap, n, r.hops)
    && botMqttPutKeyStr(dst, cap, n, ",\"path\":", r.path)
    && botMqttPutKeyStr(dst, cap, n, ",\"path_names\":", r.path_names);
  if (!ok) return false;
  BotOut o = botOutInit(t, sizeof(t));
  botOutSnr(o, r.snr);                      // up to 2 decimals, trailing zeros removed; NaN prints 0
  ok = botMqttPut(dst, cap, n, ",\"snr\":") && botMqttPut(dst, cap, n, t)
    && botMqttPut(dst, cap, n, ",\"rssi\":") && botMqttPutInt(dst, cap, n, r.rssi)
    && botMqttPutKeyStr(dst, cap, n, ",\"time\":", r.time)
    && botMqttPutKeyStr(dst, cap, n, ",\"date\":", r.date)
    && botMqttPutKeyStr(dst, cap, n, ",\"tz\":", r.tz)
    && botMqttPut(dst, cap, n, ",\"unix\":") && botMqttPutU32(dst, cap, n, r.unix)
    && botMqttPutKeyStr(dst, cap, n, ",\"bot\":", r.bot)
    && botMqttPutKeyStr(dst, cap, n, ",\"scope\":", r.scope)
    && botMqttPutKeyStr(dst, cap, n, ",\"uptime\":", r.uptime)
    && botMqttPut(dst, cap, n, ",\"bot_lat\":") && botMqttPutPos(dst, cap, n, r.bot_lat)
    && botMqttPut(dst, cap, n, ",\"bot_lon\":") && botMqttPutPos(dst, cap, n, r.bot_lon)
    && botMqttPut(dst, cap, n, ",\"max_len\":") && botMqttPutInt(dst, cap, n, r.max_len)
    && botMqttPut(dst, cap, n, "}");
  return ok;
}

// The request JSON (keys in the documented order) within BOT_MQTT_REQUEST_MAX bytes and cap-1.
// When it is too long, path_names is emptied, then path, then args is cut (on a UTF-8 boundary, by
// as little as possible). Returns the bytes written, or 0 ("" written) when it still does not fit,
// when id is not exactly 8 lower-case hex characters, or when prefix is missing or makes a reply_to
// topic over 96 bytes. The largest local buffer here is 56 bytes (the escape chunk).
static inline size_t botMqttBuildRequest(char* dst, size_t cap, const BotMqttRequest& r) {
  if (!dst || cap == 0) return 0;
  dst[0] = '\0';
  if (!botMqttIdValid(r.id) || !r.prefix || !r.prefix[0] || strlen(r.prefix) + 1 + 5 + 1 + 8 > BOT_MQTT_TOPIC_MAX) return 0;
  size_t room = cap < (size_t)BOT_MQTT_REQUEST_MAX + 1 ? cap : (size_t)BOT_MQTT_REQUEST_MAX + 1;
  BotMqttRequest q = r;
  const char* args = r.args ? r.args : "";
  size_t alen = strlen(args);
  if (botMqttRequestTry(dst, room, q, args, alen)) return strlen(dst);
  q.path_names = "";
  if (botMqttRequestTry(dst, room, q, args, alen)) return strlen(dst);
  q.path = "";
  if (botMqttRequestTry(dst, room, q, args, alen)) return strlen(dst);
  // Cut args: the longest slice (ending on a character boundary) that fits; the size only grows with the slice.
  auto snap = [&](size_t want) { while (want > 0 && want < alen && botIsUtf8Cont(args[want])) want--; return want; };
  if (!botMqttRequestTry(dst, room, q, args, 0)) { dst[0] = '\0'; return 0; }
  size_t lo = 0, hi = alen;                // lo always fits
  while (lo < hi) {
    size_t want = lo + (hi - lo + 1) / 2, mid = snap(want);
    if (mid > lo && botMqttRequestTry(dst, room, q, args, mid)) lo = mid;
    else hi = want - 1;                    // no boundary between mid and want fits or exists
  }
  if (!botMqttRequestTry(dst, room, q, args, lo)) { dst[0] = '\0'; return 0; }
  return strlen(dst);
}

// {"state":"online","version":"…","bot":"…","uptime_s":n}, or {"state":"offline"} (the other
// arguments ignored). 0 and "" when it does not fit.
static inline size_t botMqttBuildStatus(char* dst, size_t cap, bool online, const char* version, const char* bot, uint32_t uptime_s) {
  if (!dst || cap == 0) return 0;
  dst[0] = '\0';
  size_t n = 0;
  bool ok;
  if (!online) {
    ok = botMqttPut(dst, cap, n, "{\"state\":\"offline\"}");
  } else {
    ok = botMqttPut(dst, cap, n, "{\"state\":\"online\",\"version\":") && botMqttPutStr(dst, cap, n, version)
      && botMqttPutKeyStr(dst, cap, n, ",\"bot\":", bot)
      && botMqttPut(dst, cap, n, ",\"uptime_s\":") && botMqttPutU32(dst, cap, n, uptime_s)
      && botMqttPut(dst, cap, n, "}");
  }
  if (!ok) { dst[0] = '\0'; return 0; }
  return n;
}

// Parses an untrusted payload into doc; false when it is not a JSON object within the limits.
static inline bool botMqttParseObject(const char* payload, size_t len, JsonDocument& doc) {
  DeserializationError e = deserializeJson(doc, payload, len, DeserializationOption::NestingLimit(BOT_MQTT_PARSE_DEPTH));
  return !e && !doc.overflowed() && doc.is<JsonObject>();
}

#define BOT_MQTT_REPLY_PARTS_MAX 4   // messages one reply may carry: "text" and up to 3 in "more"

// Copies s into text as a reply part: control characters made spaces, the ends trimmed. False (text
// untouched) unless 1 to 200 bytes are left and they fit text_cap; a null text only checks.
static inline bool botMqttReplyPartOk(const char* s, char* text, size_t text_cap) {
  if (!s) return false;
  size_t b = 0, e = strlen(s);
  auto blank = [](char c) { unsigned char u = (unsigned char)c; return u <= 0x20 || u == 0x7F; };   // a space once cleaned
  while (b < e && blank(s[b])) b++;
  while (e > b && blank(s[e - 1])) e--;
  size_t n = e - b;
  if (n == 0 || n > BOT_MQTT_REPLY_TEXT_MAX) return false;
  if (!text) return true;
  if (n + 1 > text_cap) return false;
  for (size_t i = 0; i < n; i++) { unsigned char c = (unsigned char)s[b + i]; text[i] = (c < 0x20 || c == 0x7F) ? ' ' : (char)c; }
  text[n] = '\0';
  return true;
}

// A reply is a JSON object whose "text" is a string of 1 to 200 bytes once control characters are
// made spaces and the ends trimmed (a longer text is refused, not cut). It may also carry "more":
// an array of 1 to 3 further strings under the same rule, each sent as a message of its own after
// the first. Anything else under "more" (another type, an empty or longer array, a bad element)
// refuses the whole reply, as does a payload over 512 bytes (not parsed at all).
// Copies part number `part` (0 is "text") into text and sets *parts to how many the reply has. False
// with text "" when the reply is refused, the part does not exist, or it does not fit text_cap.
static inline bool botMqttParseReplyPartLimited(const char* payload, size_t len, uint8_t part, char* text, size_t text_cap,
                                                size_t heap_limit, uint8_t* parts = nullptr, size_t* peak = nullptr) {
  if (peak) *peak = 0;
  if (parts) *parts = 0;
  if (text && text_cap > 0) text[0] = '\0';
  if (!payload || !text || text_cap == 0 || len == 0 || len > BOT_MQTT_REPLY_IN_MAX) return false;
  BotJsonBoundedAllocator alloc(heap_limit);
  bool ok = false;
  {
    JsonDocument doc(&alloc);
    if (botMqttParseObject(payload, len, doc)) {
      const char* first = botJsonStr(doc["text"]);
      uint8_t count = 0;
      bool valid = botMqttReplyPartOk(first, nullptr, 0);
      const char* want = part == 0 ? first : nullptr;
      if (valid) {
        count = 1;
        JsonVariantConst more = doc["more"];
        if (!more.isNull()) {
          JsonArrayConst arr = more.as<JsonArrayConst>();
          if (arr.isNull() || arr.size() == 0 || arr.size() > BOT_MQTT_REPLY_PARTS_MAX - 1) valid = false;
          else for (JsonVariantConst v : arr) {
            const char* s = botJsonStr(v);
            if (!botMqttReplyPartOk(s, nullptr, 0)) { valid = false; break; }
            if (count == part) want = s;
            count++;
          }
        }
      }
      if (valid) {
        if (parts) *parts = count;
        ok = want && botMqttReplyPartOk(want, text, text_cap);
      }
    }
  }
  if (!ok) text[0] = '\0';
  if (peak) *peak = alloc.peak();
  return ok;
}

// The first message of a reply (see botMqttParseReplyPartLimited).
static inline bool botMqttParseReplyLimited(const char* payload, size_t len, char* text, size_t text_cap, size_t heap_limit, size_t* peak = nullptr) {
  return botMqttParseReplyPartLimited(payload, len, 0, text, text_cap, heap_limit, nullptr, peak);
}

static inline bool botMqttParseReply(const char* payload, size_t len, char* text, size_t text_cap) {
  return botMqttParseReplyLimited(payload, len, text, text_cap, BOT_MQTT_PARSE_HEAP);
}

// A command announcement. An empty payload (0 bytes) removes the command: true with *remove set.
// Otherwise a JSON object of at most 256 bytes; its optional "desc" (a string) is cleaned with
// botMqttCleanDesc (control characters made spaces, '"' '\\' '<' replaced by ' / ( ) and cut to
// 48 bytes (and to desc_cap-1) on a UTF-8 boundary. desc is "" and *remove false on false.
static inline bool botMqttParseAnnounceLimited(const char* payload, size_t len, char* desc, size_t desc_cap, bool* remove, size_t heap_limit, size_t* peak = nullptr) {
  if (peak) *peak = 0;
  if (remove) *remove = false;
  if (desc && desc_cap > 0) desc[0] = '\0';
  if (len == 0) { if (remove) *remove = true; return true; }
  if (!payload || len > BOT_MQTT_ANNOUNCE_MAX) return false;
  BotJsonBoundedAllocator alloc(heap_limit);
  bool ok = false;
  {
    JsonDocument doc(&alloc);
    if (botMqttParseObject(payload, len, doc)) {
      const char* s = "";
      ok = true;
      for (JsonPairConst kv : doc.as<JsonObjectConst>())     // present but not a string (null included) is invalid
        if (kv.key().size() == 4 && memcmp(kv.key().c_str(), "desc", 4) == 0) {   // length too: a key "desc\u0000x" is another key
          s = botJsonStr(kv.value());
          if (!s) ok = false;
          break;
        }
      if (ok && desc && desc_cap > 0) botMqttCleanDesc(desc, desc_cap < BOT_MQTT_ANNOUNCE_DESC_MAX + 1 ? desc_cap : BOT_MQTT_ANNOUNCE_DESC_MAX + 1, s);
    }
  }
  if (peak) *peak = alloc.peak();
  return ok;
}

static inline bool botMqttParseAnnounce(const char* payload, size_t len, char* desc, size_t desc_cap, bool* remove) {
  return botMqttParseAnnounceLimited(payload, len, desc, desc_cap, remove, BOT_MQTT_PARSE_HEAP);
}

// Bytes botJsonEscape writes for s, quotes included (null is ""), as it escapes: \" \\ \n \r \t
// take 2, other control bytes and '<' take 6.
static inline size_t botJsonEscapedLen(const char* s) {
  size_t n = 2;
  for (const char* p = s ? s : ""; *p; p++) {
    uint8_t b = (uint8_t)*p;
    if (b == '"' || b == '\\' || b == '\n' || b == '\r' || b == '\t') n += 2;
    else if (b < 0x20 || b == '<') n += 6;
    else n += 1;
  }
  return n;
}

// The exact length botMqttBuildCommandList gives when nothing has to be dropped (without the NUL),
// so a buffer of this plus 1 holds the whole list. Same skipping rules.
static inline size_t botMqttCommandListLen(const char* const* names, const char* const* descs, const char* const* sources, int count) {
  size_t n = sizeof("{\"commands\":[") - 1 + sizeof("]}") - 1;
  bool first = true;
  for (int i = 0; names && i < count; i++) {
    if (!names[i] || !names[i][0]) continue;
    n += (first ? 0 : 1) + (sizeof("{\"name\":,\"desc\":,\"source\":}") - 1) + botJsonEscapedLen(names[i]) +
         botJsonEscapedLen(descs ? descs[i] : nullptr) + botJsonEscapedLen(sources ? sources[i] : nullptr);
    first = false;
  }
  return n;
}

// {"commands":[{"name":"…","desc":"…","source":"…"},…]} from parallel arrays. Entries that do not
// fit are dropped from the end and the list then ends ],"more":true} (when that still fits, else
// plain ]}), so the result is always valid JSON and says when it is incomplete; "more" is absent
// when nothing was dropped. 0 and "" only when even {"commands":[]} does not fit. A null desc or
// source is ""; an entry with a null or empty name is skipped.
static inline size_t botMqttBuildCommandList(char* dst, size_t cap, const char* const* names, const char* const* descs, const char* const* sources, int count) {
  if (!dst || cap == 0) return 0;
  dst[0] = '\0';
  static const char head[] = "{\"commands\":[";
  static const char tailStr[] = "]}";
  static const char moreStr[] = "],\"more\":true}";
  // Puts entries while `reserve` bytes stay free; true when every named entry went in.
  auto entries = [&](size_t reserve, size_t& n) {
    n = 0;
    botMqttPut(dst, cap, n, head);
    bool first = true;
    for (int i = 0; names && i < count; i++) {
      if (!names[i] || !names[i][0]) continue;
      size_t keep = n;
      bool ok = (first || botMqttPut(dst, cap - reserve, n, ","))
        && botMqttPut(dst, cap - reserve, n, "{\"name\":") && botMqttPutStr(dst, cap - reserve, n, names[i])
        && botMqttPutKeyStr(dst, cap - reserve, n, ",\"desc\":", descs ? descs[i] : nullptr)
        && botMqttPutKeyStr(dst, cap - reserve, n, ",\"source\":", sources ? sources[i] : nullptr)
        && botMqttPut(dst, cap - reserve, n, "}");
      if (!ok) { n = keep; dst[n] = '\0'; return false; }
      first = false;
    }
    return true;
  };
  size_t n = 0;
  if (cap < sizeof(head) - 1 + sizeof(tailStr)) return 0;
  if (entries(sizeof(tailStr) - 1, n)) { botMqttPut(dst, cap, n, tailStr); return n; }
  if (cap >= sizeof(head) - 1 + sizeof(moreStr)) {
    entries(sizeof(moreStr) - 1, n);
    botMqttPut(dst, cap, n, moreStr);
    return n;
  }
  botMqttPut(dst, cap, n, tailStr);   // too small to say "more": still valid
  return n;
}
