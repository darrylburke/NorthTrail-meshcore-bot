#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <ArduinoJson.h>
#include "BotUrl.h"
#include "BotJsonPath.h"
#include "BotMqttTopics.h"

// Settings model for the chat bot: one struct, its defaults, and strict JSON load/save.
// Only ArduinoJson is used besides plain C; no heap of our own. BotConfig is ~9 KB, so
// callers own the storage and nothing in here puts one on the stack.
// The JSON can come from the device file or from an untrusted web POST: every top-level key
// is validated and applied on its own, so one bad field never blocks the others. The lists
// (channels, custom commands, watched nodes) are the exception on the web path
// (botConfigFromWebJsonStrict): each is taken whole or not at all, and the reason for a refusal
// is reported.

enum BotBuiltin : uint8_t { BB_PING, BB_TEST, BB_PATH, BB_HELP, BB_HEARD, BB_MESH, BB_DIST, BB_SUN, BB_ROLL, BB_8BALL, BB_WX, BB_AURORA, BB_ALERTS, BB_NODES, BB_COUNT };
struct BotBuiltinCfg { char name[17]; bool enabled; };
// A custom command may carry a live lookup: when url is set, the reply's {fetch} is the value at
// path in the JSON the url returns (kept for cache_s seconds). url/path are both set or both empty.
struct BotCustomCmd  { char trigger[17]; char reply[161]; char desc[49]; bool enabled; char url[161]; char path[49]; uint16_t cache_s; };
#define BOT_MAX_CUSTOM 16
// A watched observer (kind BW_OBSERVER, id a lower-case UUID) or node (BW_NODE, id 64 upper-case hex).
enum BotWatchKind : uint8_t { BW_OBSERVER = 0, BW_NODE = 1 };
struct BotWatchCfg { char label[25]; uint8_t kind; char id[65]; };
#define BOT_MAX_WATCH 4
#define BOT_MAX_CHANNELS 4
struct BotConfig {
  char channels[BOT_MAX_CHANNELS][32];   // {"#bot"}; each "#name" or "Public", unique ignoring ASCII case; unused rows zero
  uint8_t num_channels;    // 1   (1..BOT_MAX_CHANNELS after loading)
  char scope[31];          // ""
  char prefix;             // '!'
  uint16_t cooldown_s;     // 10   (0..3600)
  uint8_t  max_per_min;    // 6    (1..30)
  char ignore[96];         // "bot"
  double lat, lon;         // 0,0  (-90..90, -180..180)
  char tz[40];             // "America/Toronto"
  char help_link[64];      // ""
  uint16_t advert_zero_hop_min;  // 240  (0 = off, max 10080)
  uint16_t advert_flood_min;     // 1440 (0 = off, max 10080)
  BotBuiltinCfg builtin[BB_COUNT];   // default names: ping test path help heard mesh dist sun roll 8ball wx aurora alerts nodes, all enabled
  BotCustomCmd  custom[BOT_MAX_CUSTOM];
  uint8_t num_custom;
  BotWatchCfg watch[BOT_MAX_WATCH];
  uint8_t num_watch;
  uint16_t watch_online_min;  // 720  (5..10080)
  bool     mqtt_enabled;      // false
  char     mqtt_host[65];     // ""   (0..64 of letters, digits, '.', '-'; stored as given)
  uint16_t mqtt_port;         // 1883 (1..65535)
  char     mqtt_user[33];     // ""   (printable ASCII 0x21..0x7E, no space)
  char     mqtt_pass[65];     // ""   (same rule). A secret: stored in the file, never sent to the browser
  char     mqtt_prefix[65];   // ""   (empty = the default prefix, else botMqttPrefixValid)
  uint8_t  mqtt_wait_s;       // 5    (1..15)
  char web_salt[17];       // hex, "" until a password is set
  char web_hash[65];       // hex PBKDF2-HMAC-SHA256 of the password with web_salt, "" until set
};

static_assert(sizeof(((BotConfig*)0)->mqtt_prefix) == BOT_MQTT_PREFIX_MAX + 1, "mqtt_prefix holds exactly a valid prefix");

// Fixed built-in ids: the default names and the keys of the "builtin" JSON object.
static inline const char* botBuiltinId(int i) {
  static const char* const ids[BB_COUNT] = { "ping", "test", "path", "help", "heard", "mesh", "dist", "sun", "roll", "8ball", "wx", "aurora", "alerts", "nodes" };
  return ids[i];
}

// Zeroes the whole struct first so two defaulted structs compare equal with memcmp.
static inline void botConfigDefaults(BotConfig& c) {
  memset(&c, 0, sizeof(c));
  strcpy(c.channels[0], "#bot");
  c.num_channels = 1;
  c.prefix = '!';
  c.cooldown_s = 10;
  c.max_per_min = 6;
  strcpy(c.ignore, "bot");
  strcpy(c.tz, "America/Toronto");
  c.advert_zero_hop_min = 240;
  c.advert_flood_min = 1440;
  c.watch_online_min = 720;
  c.mqtt_port = 1883;
  c.mqtt_wait_s = 5;
  for (int i = 0; i < BB_COUNT; i++) { strcpy(c.builtin[i].name, botBuiltinId(i)); c.builtin[i].enabled = true; }
}

// Time zone name -> POSIX TZ string. Null/empty -> nullptr; table lookups are case-sensitive
// and return static strings; anything else containing a digit is taken as a POSIX string and
// the argument pointer itself is returned; otherwise nullptr.
static inline const char* botTzToPosix(const char* tz) {
  if (!tz || !*tz) return nullptr;
  static const char* const table[][2] = {
    { "America/St_Johns", "NST3:30NDT,M3.2.0,M11.1.0" },
    { "America/Halifax",  "AST4ADT,M3.2.0,M11.1.0" },
    { "America/Toronto",  "EST5EDT,M3.2.0,M11.1.0" },
    { "America/Winnipeg", "CST6CDT,M3.2.0,M11.1.0" },
    { "America/Regina",   "CST6" },
    { "America/Edmonton", "MST7MDT,M3.2.0,M11.1.0" },
    { "America/Vancouver","PST8PDT,M3.2.0,M11.1.0" },
    { "UTC",              "UTC0" },
  };
  for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++)
    if (strcmp(tz, table[i][0]) == 0) return table[i][1];
  for (const char* p = tz; *p; p++) if (*p >= '0' && *p <= '9') return tz;
  return nullptr;
}

// ---- validation helpers (internal) ----

// A JSON string that fits cap (with terminator) and has no control byte (< 0x20 or 0x7F;
// this also rejects an embedded NUL from \u0000). Empty only if allow_empty.
static inline bool botCfgGetStr(JsonVariantConst v, size_t cap, bool allow_empty, const char*& s, size_t& n) {
  if (!v.is<const char*>()) return false;
  JsonString js = v.as<JsonString>();
  s = js.c_str();
  n = js.size();
  if (!s || n + 1 > cap || (n == 0 && !allow_empty)) return false;
  for (size_t i = 0; i < n; i++) {
    uint8_t b = (uint8_t)s[i];
    if (b < 0x20 || b == 0x7F) return false;
  }
  return true;
}

// Validates into dst (cap bytes); on failure dst is untouched. The whole buffer is zeroed
// first so no stale bytes survive a shorter value (keeps memcmp round trips exact).
static inline bool botCfgSetStr(JsonVariantConst v, char* dst, size_t cap, bool allow_empty) {
  const char* s; size_t n;
  if (!botCfgGetStr(v, cap, allow_empty, s, n)) return false;
  memset(dst, 0, cap);
  memcpy(dst, s, n);
  return true;
}

// Integer in [lo, hi]. Floats (even 10.0), booleans, strings and nulls are rejected.
static inline bool botCfgGetInt(JsonVariantConst v, long long lo, long long hi, long long& out) {
  if (!v.is<long long>()) return false;
  long long x = v.as<long long>();
  if (x < lo || x > hi) return false;
  out = x;
  return true;
}

// A host name or IPv4 literal: 0..cap-1 bytes of letters, digits, '.' and '-'.
static inline bool botCfgMqttHostOk(JsonVariantConst v, char* dst, size_t cap) {
  const char* s; size_t n;
  if (!botCfgGetStr(v, cap, true, s, n)) return false;
  for (size_t i = 0; i < n; i++) {
    char ch = s[i];
    if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '.' || ch == '-')) return false;
  }
  memset(dst, 0, cap);
  memcpy(dst, s, n);
  return true;
}

// A broker user name or password: 0..cap-1 bytes of printable ASCII 0x21..0x7E (no space).
static inline bool botCfgMqttTokenOk(JsonVariantConst v, char* dst, size_t cap) {
  const char* s; size_t n;
  if (!botCfgGetStr(v, cap, true, s, n)) return false;
  for (size_t i = 0; i < n; i++) if ((uint8_t)s[i] < 0x21 || (uint8_t)s[i] > 0x7E) return false;
  memset(dst, 0, cap);
  memcpy(dst, s, n);
  return true;
}

// The topic prefix: empty (use the default) or botMqttPrefixValid.
static inline bool botCfgMqttPrefixOk(JsonVariantConst v, char* dst, size_t cap) {
  const char* s; size_t n;
  if (!botCfgGetStr(v, cap, true, s, n)) return false;
  if (n != 0 && !botMqttPrefixValid(s)) return false;
  memset(dst, 0, cap);
  memcpy(dst, s, n);
  return true;
}

// Whether the bot should try the broker at all: enabled and a host given.
static inline bool botConfigMqttUsable(const BotConfig& c) { return c.mqtt_enabled && c.mqtt_host[0] != '\0'; }

// Any JSON number, finite, within [lo, hi].
static inline bool botCfgGetDouble(JsonVariantConst v, double lo, double hi, double& out) {
  if (!v.is<double>()) return false;
  double x = v.as<double>();
  if (!isfinite(x) || x < lo || x > hi) return false;
  out = x;
  return true;
}

// Command name: 1..16 ASCII letters/digits, lower-cased into out[17] (zero-padded).
static inline bool botCfgGetName(JsonVariantConst v, char out[17]) {
  const char* s; size_t n;
  if (!botCfgGetStr(v, 17, false, s, n)) return false;
  for (size_t i = 0; i < n; i++) {
    char ch = s[i];
    bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9');
    if (!ok) return false;
  }
  memset(out, 0, 17);
  for (size_t i = 0; i < n; i++) out[i] = (s[i] >= 'A' && s[i] <= 'Z') ? (char)(s[i] + 32) : s[i];
  return true;
}

// Lower-case hex of exactly len characters (or empty when allow_empty), copied to dst.
static inline bool botCfgGetHex(JsonVariantConst v, size_t len, char* dst) {
  const char* s; size_t n;
  if (!botCfgGetStr(v, len + 1, true, s, n)) return false;
  if (n != 0 && n != len) return false;
  for (size_t i = 0; i < n; i++)
    if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f'))) return false;
  memset(dst, 0, len + 1);
  memcpy(dst, s, n);
  return true;
}

// Why parts of a posted change were not taken; filled by botConfigFromWebJsonStrict.
struct BotApplyReport {
  char custom[112];    // "" or "#<position> '<trigger>': <reason>" for a refused custom list
  char builtin[112];   // "" or the first refused built-in rename and why
  char watch[112];     // "" or "#<position> '<label>': <reason>" for a refused watch list
  char channels[112];  // "" or "empty list" or "#<position>[ '<name>']: <reason>" for a refused channel list
};

// A posted string as shown in a message: printable ASCII kept, other bytes as '?', at most
// cap - 1 bytes; "" when v is not a string.
static inline void botCfgShowStr(JsonVariantConst v, char* out, size_t cap) {
  size_t n = 0;
  if (v.is<const char*>()) {
    JsonString js = v.as<JsonString>();
    const char* s = js.c_str();
    for (size_t i = 0; s && i < js.size() && n + 1 < cap; i++) {
      uint8_t b = (uint8_t)s[i];
      out[n++] = (b >= 0x20 && b < 0x7F) ? (char)b : '?';
    }
  }
  out[n] = '\0';
}

// The posted trigger as shown in a message, at most 16 bytes.
static inline void botCfgShowTrigger(JsonVariantConst v, char out[17]) { botCfgShowStr(v, out, 17); }

// ---- channels ----

// A channel name: "#" plus at least one character, or exactly "Public"; at most 31 bytes and no
// control character. Valid: copied to out (zero-filled, so memcmp round trips hold) and true.
static inline bool botCfgChannelName(JsonVariantConst v, char out[32]) {
  const char* s; size_t n;
  if (!botCfgGetStr(v, 32, false, s, n)) return false;
  if (!((s[0] == '#' && n >= 2) || (n == 6 && memcmp(s, "Public", 6) == 0))) return false;
  memset(out, 0, 32);
  memcpy(out, s, n);
  return true;
}

// Equal when ASCII letters are folded to one case (other bytes compared as they are).
static inline bool botCfgSameNoCase(const char* a, const char* b) {
  for (;; a++, b++) {
    char x = (*a >= 'A' && *a <= 'Z') ? (char)(*a + 32) : *a;
    char y = (*b >= 'A' && *b <= 'Z') ? (char)(*b + 32) : *b;
    if (x != y) return false;
    if (!x) return true;
  }
}

// Whether a channel message on the node channel called name is for the bot: an exact,
// case-sensitive match with one of the listed channels.
static inline bool botConfigHasChannel(const BotConfig& c, const char* name) {
  if (!name || !name[0]) return false;
  int n = c.num_channels < BOT_MAX_CHANNELS ? c.num_channels : BOT_MAX_CHANNELS;
  for (int i = 0; i < n; i++)   // every stored row is terminated (at most 31 bytes, zero-filled)
    if (c.channels[i][0] && strcmp(c.channels[i], name) == 0) return true;
  return false;
}

static inline void botCfgChannelMsg(char* err, size_t cap, int pos, JsonVariantConst e, const char* why) {
  char shown[32];
  botCfgShowStr(e, shown, sizeof(shown));
  if (shown[0]) snprintf(err, cap, "#%d '%s': %s", pos, shown, why);
  else snprintf(err, cap, "#%d: %s", pos, why);
}

// Checks a posted channel list on its own: 1 to BOT_MAX_CHANNELS elements, each a channel name,
// no two the same ignoring ASCII case. True when it passes; else the first problem goes to err.
static inline bool botChannelListCheck(JsonArrayConst arr, char* err, size_t cap) {
  if (arr.size() == 0) { snprintf(err, cap, "empty list"); return false; }
  int pos = 0;
  for (JsonVariantConst e : arr) {
    pos++;
    if (pos > BOT_MAX_CHANNELS) { snprintf(err, cap, "#%d: too many", pos); return false; }
    char nm[32];
    if (!botCfgChannelName(e, nm)) { botCfgChannelMsg(err, cap, pos, e, "not a channel name"); return false; }
    int k = 0;
    for (JsonVariantConst f : arr) {
      if (++k >= pos) break;
      char other[32];
      if (botCfgChannelName(f, other) && botCfgSameNoCase(other, nm)) {
        botCfgChannelMsg(err, cap, pos, e, "duplicate");
        return false;
      }
    }
  }
  return true;
}

// What a BotUrlErr means, in a few words.
static inline const char* botUrlErrText(BotUrlErr e) {
  switch (e) {
    case BU_SCHEME: return "url: must start with http:// or https://";
    case BU_HOST: return "url: bad host or port";
    case BU_CREDENTIALS: return "url: must not contain a user name or password";
    case BU_ARGS_PLACE: return "url: {args} is only allowed after the ?";
    case BU_CHARS: return "url: only visible ASCII and {args} are allowed";
    case BU_LENGTH: return "url: must be 1 to 160 bytes";
    default: return "url";
  }
}

// One posted custom command: nullptr when valid (its lower-cased trigger in trig), else why not.
// Also the rule set the lenient file loader uses, so the two paths cannot drift apart.
static inline const char* botCustomElemError(JsonVariantConst e, char trig[17]) {
  trig[0] = '\0';
  if (!e.is<JsonObjectConst>()) return "not a command";
  if (!botCfgGetName(e["trigger"], trig)) return "trigger must be 1 to 16 letters or digits";
  const char* s; size_t n;
  if (!botCfgGetStr(e["reply"], sizeof(((BotCustomCmd*)0)->reply), false, s, n))
    return "reply must be 1 to 160 bytes, without line breaks or tabs";
  bool has_fetch = strstr(s, "{fetch}") != nullptr;
  if (!e["desc"].isNull() && !botCfgGetStr(e["desc"], sizeof(((BotCustomCmd*)0)->desc), true, s, n))
    return "description must be at most 48 bytes, without line breaks or tabs";
  if (!e["enabled"].isNull() && !e["enabled"].is<bool>()) return "enabled must be true or false";
  bool url_set = false, path_set = false;
  if (!e["url"].isNull()) {
    if (!botCfgGetStr(e["url"], sizeof(((BotCustomCmd*)0)->url), true, s, n)) return "url: must be at most 160 bytes of text";
    if (n) {
      BotUrlErr ue = botUrlCheckTemplate(s);
      if (ue != BU_OK) return botUrlErrText(ue);
      url_set = true;
    }
  }
  if (!e["path"].isNull()) {
    if (!botCfgGetStr(e["path"], sizeof(((BotCustomCmd*)0)->path), true, s, n)) return "path: must be at most 48 bytes of text";
    if (n) {
      if (!botJsonPathCheck(s)) return "path: not a valid JSON path";
      path_set = true;
    }
  }
  if (url_set && !path_set) return "url needs a path";
  if (path_set && !url_set) return "path needs a url";
  if (!e["cache_s"].isNull()) {
    long long iv;
    if (!botCfgGetInt(e["cache_s"], 0, 3600, iv)) return "cache_s must be a whole number from 0 to 3600";
  }
  if (has_fetch && !url_set) return "{fetch} needs a url";
  return nullptr;
}

// One posted watch entry: nullptr when valid (normalised copy in out), else why not. The id is
// stored lower-case for an observer UUID (8-4-4-4-12 hex) and upper-case for a node (64 hex).
static inline const char* botWatchElemError(JsonVariantConst e, BotWatchCfg& out) {
  memset(&out, 0, sizeof(out));
  if (!e.is<JsonObjectConst>()) return "not a watch entry";
  const char* s; size_t n;
  if (!botCfgGetStr(e["label"], sizeof(out.label), false, s, n))
    return "label must be 1 to 24 bytes, without line breaks or tabs";
  memcpy(out.label, s, n);
  if (!botCfgGetStr(e["kind"], 9, false, s, n)) return "kind must be \"observer\" or \"node\"";
  if (strcmp(s, "observer") == 0) out.kind = BW_OBSERVER;
  else if (strcmp(s, "node") == 0) out.kind = BW_NODE;
  else return "kind must be \"observer\" or \"node\"";
  bool obs = out.kind == BW_OBSERVER;
  size_t want = obs ? 36 : 64;
  if (!botCfgGetStr(e["id"], sizeof(out.id), false, s, n) || n != want)
    return obs ? "id must be a UUID like 0a1b2c3d-4e5f-6a7b-8c9d-0e1f2a3b4c5d" : "id must be 64 hex characters";
  for (size_t i = 0; i < n; i++) {
    char ch = s[i];
    bool dash = obs && (i == 8 || i == 13 || i == 18 || i == 23);
    bool hex = (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
    if (dash ? ch != '-' : !hex)
      return obs ? "id must be a UUID like 0a1b2c3d-4e5f-6a7b-8c9d-0e1f2a3b4c5d" : "id must be 64 hex characters";
    if (ch >= 'A' && ch <= 'F' && obs) ch = (char)(ch + 32);
    else if (ch >= 'a' && ch <= 'f' && !obs) ch = (char)(ch - 32);
    out.id[i] = ch;
  }
  return nullptr;
}

static inline void botCfgElemMsg(char* err, size_t cap, int pos, JsonVariantConst e, const char* why, const char* key = "trigger") {
  char shown[17];
  botCfgShowTrigger(e.is<JsonObjectConst>() ? e[key] : JsonVariantConst(), shown);
  if (shown[0]) snprintf(err, cap, "#%d '%s': %s", pos, shown, why);
  else snprintf(err, cap, "#%d: %s", pos, why);
}

// Checks a posted custom list on its own: at most BOT_MAX_CUSTOM elements, each valid, no
// trigger twice. True when it passes; else the first problem goes to err.
static inline bool botCustomListCheck(JsonArrayConst arr, char* err, size_t cap) {
  int pos = 0;
  for (JsonVariantConst e : arr) {
    pos++;
    if (pos > BOT_MAX_CUSTOM) { snprintf(err, cap, "#%d: at most %d custom commands", pos, BOT_MAX_CUSTOM); return false; }
    char trig[17];
    const char* why = botCustomElemError(e, trig);
    if (why) { botCfgElemMsg(err, cap, pos, e, why); return false; }
    int k = 0;
    for (JsonVariantConst f : arr) {
      if (++k >= pos) break;
      char t2[17];
      if (botCfgGetName(f["trigger"], t2) && strcmp(t2, trig) == 0) {
        snprintf(err, cap, "#%d '%s': same trigger as #%d", pos, trig, k);
        return false;
      }
    }
  }
  return true;
}

// Checks a (shape-checked) custom list against the built-in names it will live beside.
static inline bool botCustomListVsBuiltins(JsonArrayConst arr, const char names[BB_COUNT][17], char* err, size_t cap) {
  int pos = 0;
  for (JsonVariantConst e : arr) {
    pos++;
    char trig[17];
    if (!botCfgGetName(e["trigger"], trig)) continue;
    for (int j = 0; j < BB_COUNT; j++) {
      if (strcmp(trig, names[j]) == 0) {
        snprintf(err, cap, "#%d '%s': same name as built-in '%s'", pos, trig, names[j]);
        return false;
      }
    }
  }
  return true;
}

// Checks a posted watch list on its own: at most BOT_MAX_WATCH elements, each valid. True when
// it passes; else the first problem goes to err.
static inline bool botWatchListCheck(JsonArrayConst arr, char* err, size_t cap) {
  int pos = 0;
  for (JsonVariantConst e : arr) {
    pos++;
    if (pos > BOT_MAX_WATCH) { snprintf(err, cap, "#%d: too many watched entries, at most %d", pos, BOT_MAX_WATCH); return false; }
    BotWatchCfg scratch;
    const char* why = botWatchElemError(e, scratch);
    if (why) { botCfgElemMsg(err, cap, pos, e, why, "label"); return false; }
  }
  return true;
}

// Resolves proposed built-in renames (want/want_changed) into prop/changed. A rename that
// would share a name with another built-in's final name, or (check_custom) a stored custom
// trigger, is reverted; the later built-in loses a clash between two new names, and a swap
// (ping<->test) is fine. The first refusal is written to msg when msg is non-null and empty.
static inline void botCfgResolveRenames(const BotConfig& c, const char want[BB_COUNT][17], const bool want_changed[BB_COUNT],
                                        bool check_custom, char prop[BB_COUNT][17], bool changed[BB_COUNT],
                                        char* msg, size_t cap) {
  for (int i = 0; i < BB_COUNT; i++) {
    changed[i] = want_changed[i];
    memcpy(prop[i], changed[i] ? want[i] : c.builtin[i].name, 17);
  }
  bool again = true;
  while (again) {
    again = false;
    for (int i = BB_COUNT - 1; i >= 0; i--) {
      if (!changed[i]) continue;
      const char* kind = nullptr;
      const char* other = nullptr;
      for (int j = 0; j < BB_COUNT && !kind; j++)
        if (j != i && strcmp(prop[i], prop[j]) == 0) { kind = "built-in"; other = prop[j]; }
      if (check_custom)
        for (int j = 0; j < c.num_custom && j < BOT_MAX_CUSTOM && !kind; j++)
          if (strcmp(prop[i], c.custom[j].trigger) == 0) { kind = "custom command"; other = c.custom[j].trigger; }
      if (kind) {
        if (msg && cap && !msg[0])
          snprintf(msg, cap, "built-in '%s' not renamed to '%s': name used by %s '%s'", botBuiltinId(i), prop[i], kind, other);
        memcpy(prop[i], c.builtin[i].name, 17);
        changed[i] = false;
        again = true;
      }
    }
  }
}

// Shared loader. with_secrets=false never touches web_salt/web_hash. rep non-null selects the
// strict web rules for the lists (see botConfigFromWebJsonStrict) and receives the reasons.
static inline void botConfigApply(JsonVariantConst src, BotConfig& c, bool with_secrets, BotApplyReport* rep = nullptr) {
  if (rep) { rep->custom[0] = '\0'; rep->builtin[0] = '\0'; rep->watch[0] = '\0'; rep->channels[0] = '\0'; }
  if (!src.is<JsonObjectConst>()) return;
  JsonObjectConst o = src.as<JsonObjectConst>();
  long long iv; double dv;

  // channels: a list of channel names (botCfgChannelName). Strict: taken only when
  // botChannelListCheck passes, else rep.channels says why. Lenient: invalid elements and
  // duplicates (ignoring ASCII case) are skipped, the first BOT_MAX_CHANNELS valid ones kept, and
  // the default list used when none is left. Not an array: nothing changes. The old key
  // "channel" (one name) counts only when "channels" is absent, as a one-element list; on the
  // strict path a refused one is reported as element #1.
  {
    JsonVariantConst cv = o["channels"];
    if (cv.is<JsonArrayConst>()) {
      JsonArrayConst arr = cv.as<JsonArrayConst>();
      if (!rep || botChannelListCheck(arr, rep->channels, sizeof(rep->channels))) {
        memset(c.channels, 0, sizeof(c.channels));
        int n = 0;
        for (JsonVariantConst e : arr) {
          if (n >= BOT_MAX_CHANNELS) break;
          bool ok = botCfgChannelName(e, c.channels[n]);
          for (int j = 0; ok && j < n; j++) if (botCfgSameNoCase(c.channels[j], c.channels[n])) ok = false;
          if (ok) n++; else memset(c.channels[n], 0, sizeof(c.channels[n]));
        }
        if (n == 0) { strcpy(c.channels[0], "#bot"); n = 1; }
        c.num_channels = (uint8_t)n;
      }
    } else if (cv.isNull() && !o["channel"].isNull()) {
      char nm[32];
      if (botCfgChannelName(o["channel"], nm)) {
        memset(c.channels, 0, sizeof(c.channels));
        memcpy(c.channels[0], nm, sizeof(nm));
        c.num_channels = 1;
      } else if (rep) {
        botCfgChannelMsg(rep->channels, sizeof(rep->channels), 1, o["channel"], "not a channel name");
      }
    }
  }
  botCfgSetStr(o["scope"], c.scope, sizeof(c.scope), true);
  // prefix: one printable, non-alphanumeric ASCII character (space excluded).
  {
    const char* s; size_t n;
    if (botCfgGetStr(o["prefix"], 2, false, s, n) && n == 1) {
      char ch = s[0];
      bool alnum = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9');
      if (ch >= 0x21 && ch <= 0x7E && !alnum) c.prefix = ch;
    }
  }
  if (botCfgGetInt(o["cooldown_s"], 0, 3600, iv)) c.cooldown_s = (uint16_t)iv;
  if (botCfgGetInt(o["max_per_min"], 1, 30, iv)) c.max_per_min = (uint8_t)iv;
  botCfgSetStr(o["ignore"], c.ignore, sizeof(c.ignore), true);
  if (botCfgGetDouble(o["lat"], -90.0, 90.0, dv)) c.lat = dv;
  if (botCfgGetDouble(o["lon"], -180.0, 180.0, dv)) c.lon = dv;
  // tz: empty (UTC for callers) or something botTzToPosix understands.
  {
    const char* s; size_t n;
    if (botCfgGetStr(o["tz"], sizeof(c.tz), true, s, n) && (n == 0 || botTzToPosix(s) != nullptr))
      botCfgSetStr(o["tz"], c.tz, sizeof(c.tz), true);
  }
  botCfgSetStr(o["help_link"], c.help_link, sizeof(c.help_link), true);
  if (botCfgGetInt(o["advert_zero_hop_min"], 0, 10080, iv)) c.advert_zero_hop_min = (uint16_t)iv;
  if (botCfgGetInt(o["advert_flood_min"], 0, 10080, iv)) c.advert_flood_min = (uint16_t)iv;
  if (botCfgGetInt(o["watch_online_min"], 5, 10080, iv)) c.watch_online_min = (uint16_t)iv;

  // Broker settings, each on its own. mqtt_pass is taken (by every loader) only when the key is a
  // string that passes; absent, null, a non-string or an invalid one leave the stored password.
  // "mqtt_pass_set" is output only and never read.
  if (o["mqtt_enabled"].is<bool>()) c.mqtt_enabled = o["mqtt_enabled"].as<bool>();
  botCfgMqttHostOk(o["mqtt_host"], c.mqtt_host, sizeof(c.mqtt_host));
  if (botCfgGetInt(o["mqtt_port"], 1, 65535, iv)) c.mqtt_port = (uint16_t)iv;
  botCfgMqttTokenOk(o["mqtt_user"], c.mqtt_user, sizeof(c.mqtt_user));
  botCfgMqttTokenOk(o["mqtt_pass"], c.mqtt_pass, sizeof(c.mqtt_pass));
  botCfgMqttPrefixOk(o["mqtt_prefix"], c.mqtt_prefix, sizeof(c.mqtt_prefix));
  if (botCfgGetInt(o["mqtt_wait_s"], 1, 15, iv)) c.mqtt_wait_s = (uint8_t)iv;

  // builtin (always before custom). Renames are judged on the resulting set of names, so a
  // swap (ping<->test) is accepted; a rename that would duplicate another built-in's final
  // name, or an existing custom trigger that is not about to be replaced, is rejected (the
  // later built-in loses a clash between two new names).
  JsonVariantConst bv = o["builtin"];
  bool list_given = o["custom"].is<JsonArrayConst>();
  JsonArrayConst list = o["custom"].as<JsonArrayConst>();
  // Strict: the posted list replaces the stored one only when every element is valid.
  bool list_ok = list_given && (!rep || botCustomListCheck(list, rep->custom, sizeof(rep->custom)));
  char want[BB_COUNT][17];
  bool want_changed[BB_COUNT];
  for (int i = 0; i < BB_COUNT; i++) { memcpy(want[i], c.builtin[i].name, 17); want_changed[i] = false; }
  if (bv.is<JsonObjectConst>()) {
    JsonObjectConst bo = bv.as<JsonObjectConst>();
    for (int i = 0; i < BB_COUNT; i++) {
      JsonVariantConst e = bo[botBuiltinId(i)];
      if (!e.is<JsonObjectConst>()) continue;
      JsonVariantConst en = e["enabled"];
      if (en.is<bool>()) c.builtin[i].enabled = en.as<bool>();
      char nm[17];
      if (botCfgGetName(e["name"], nm)) {
        if (strcmp(nm, c.builtin[i].name) != 0) { memcpy(want[i], nm, 17); want_changed[i] = true; }
      } else if (rep && !e["name"].isNull() && !rep->builtin[0]) {
        snprintf(rep->builtin, sizeof(rep->builtin), "built-in '%s' not renamed: name must be 1 to 16 letters or digits",
                 botBuiltinId(i));
      }
    }
  }
  char prop[BB_COUNT][17];
  bool changed[BB_COUNT];
  bool name_msg = rep && rep->builtin[0];
  char* bmsg = rep ? rep->builtin : nullptr;
  botCfgResolveRenames(c, want, want_changed, !list_ok, prop, changed, bmsg, rep ? sizeof(rep->builtin) : 0);
  if (rep && list_ok && !botCustomListVsBuiltins(list, prop, rep->custom, sizeof(rep->custom))) {
    // The stored list stays, so judge the renames against it instead.
    list_ok = false;
    if (!name_msg) rep->builtin[0] = '\0';
    botCfgResolveRenames(c, want, want_changed, true, prop, changed, bmsg, sizeof(rep->builtin));
  }
  for (int i = 0; i < BB_COUNT; i++) if (changed[i]) memcpy(c.builtin[i].name, prop[i], 17);

  // custom: replaces the list when it is an array (strict: only when list_ok). Built in place;
  // on the lenient path an invalid element (bad trigger, empty/bad reply, bad optional member,
  // duplicate name) is skipped.
  if (list_ok) {
    memset(c.custom, 0, sizeof(c.custom));
    c.num_custom = 0;
    int n = 0;
    for (JsonVariantConst e : list) {
      if (n >= BOT_MAX_CUSTOM) break;
      if (!e.is<JsonObjectConst>()) continue;
      BotCustomCmd& cc = c.custom[n];
      char trig[17];
      bool ok = botCustomElemError(e, trig) == nullptr;   // every rule, shared with the strict path
      if (ok) {
        memcpy(cc.trigger, trig, sizeof(trig));
        botCfgSetStr(e["reply"], cc.reply, sizeof(cc.reply), false);
        if (!e["desc"].isNull()) botCfgSetStr(e["desc"], cc.desc, sizeof(cc.desc), true);
        cc.enabled = e["enabled"].isNull() ? true : e["enabled"].as<bool>();
        if (!e["url"].isNull()) botCfgSetStr(e["url"], cc.url, sizeof(cc.url), true);
        if (!e["path"].isNull()) botCfgSetStr(e["path"], cc.path, sizeof(cc.path), true);
        long long cv;
        if (botCfgGetInt(e["cache_s"], 0, 3600, cv)) cc.cache_s = (uint16_t)cv;
      }
      for (int j = 0; ok && j < BB_COUNT; j++) if (strcmp(cc.trigger, c.builtin[j].name) == 0) ok = false;
      for (int j = 0; ok && j < n; j++) if (strcmp(cc.trigger, c.custom[j].trigger) == 0) ok = false;
      if (ok) n++; else memset(&cc, 0, sizeof(cc));
    }
    c.num_custom = (uint8_t)n;
  }

  // watch: replaces the list when it is an array (strict: only when every element is valid, at
  // most BOT_MAX_WATCH). Lenient: invalid elements are skipped and the first four valid ones kept.
  if (o["watch"].is<JsonArrayConst>()) {
    JsonArrayConst wl = o["watch"].as<JsonArrayConst>();
    if (!rep || botWatchListCheck(wl, rep->watch, sizeof(rep->watch))) {
      memset(c.watch, 0, sizeof(c.watch));
      c.num_watch = 0;
      int n = 0;
      for (JsonVariantConst e : wl) {
        if (n >= BOT_MAX_WATCH) break;
        BotWatchCfg w;
        if (botWatchElemError(e, w) == nullptr) c.watch[n++] = w;
      }
      c.num_watch = (uint8_t)n;
    }
  }

  // web_salt / web_hash: applied only as a consistent pair (both empty, or 16 + 64 hex);
  // a key given alone is combined with the stored other half before that check.
  if (with_secrets && (!o["web_salt"].isNull() || !o["web_hash"].isNull())) {
    char salt[17], hash[65];
    memcpy(salt, c.web_salt, sizeof(salt));
    memcpy(hash, c.web_hash, sizeof(hash));
    bool ok = true;
    if (!o["web_salt"].isNull()) ok = botCfgGetHex(o["web_salt"], 16, salt);
    if (ok && !o["web_hash"].isNull()) ok = botCfgGetHex(o["web_hash"], 64, hash);
    bool both_empty = !salt[0] && !hash[0];
    bool both_full = strlen(salt) == 16 && strlen(hash) == 64;
    if (ok && (both_empty || both_full)) {
      memcpy(c.web_salt, salt, sizeof(salt));
      memcpy(c.web_hash, hash, sizeof(hash));
    }
  }
}

// Starts from c as given and applies only valid fields; unknown keys are ignored; null or
// non-object src changes nothing. This is the trusted (device file) entry point.
static inline void botConfigFromJson(JsonVariantConst src, BotConfig& c) { botConfigApply(src, c, true); }

// Same, but never changes web_salt/web_hash.
static inline void botConfigFromWebJson(JsonVariantConst src, BotConfig& c) { botConfigApply(src, c, false); }

// The entry point for documents posted from the web page. As botConfigFromWebJson, except:
// a posted custom list replaces the stored one only when every element is valid (at most 16,
// each trigger 1 to 16 letters or digits, unique and not a built-in's name, reply 1 to 160
// bytes and description up to 48 bytes, both without control characters, enabled a boolean);
// otherwise the stored list is kept and rep.custom says which element failed and why (a
// custom command may also carry url, path and cache_s; {fetch} needs a url). The watch list
// (at most 4 entries of label, kind, id) is taken or refused the same way, reported in
// rep.watch, and so is the channel list (1 to 4 channel names, unique ignoring ASCII case; also
// the old single "channel" key when "channels" is absent), reported in rep.channels. A
// built-in rename that is refused is reported in rep.builtin. Other fields as before.
static inline void botConfigFromWebJsonStrict(JsonVariantConst src, BotConfig& c, BotApplyReport& rep) {
  botConfigApply(src, c, false, &rep);
}

// Writes every field; web_salt/web_hash and mqtt_pass only when for_storage (the web gets the
// boolean mqtt_pass_set instead of mqtt_pass). Caveat: ArduinoJson parses
// short decimals from text as 32-bit floats, so lat/lon read back from a file can differ
// from what was written by a few metres at most in the 7th digit (about 0.5 m).
static inline void botConfigToJson(const BotConfig& c, JsonObject dst, bool for_storage) {
  JsonArray ch = dst["channels"].to<JsonArray>();
  for (int i = 0; i < c.num_channels && i < BOT_MAX_CHANNELS; i++) ch.add((const char*)c.channels[i]);
  dst["scope"] = (const char*)c.scope;
  char pf[2] = { c.prefix, 0 };
  dst["prefix"] = (const char*)pf;
  dst["cooldown_s"] = c.cooldown_s;
  dst["max_per_min"] = c.max_per_min;
  dst["ignore"] = (const char*)c.ignore;
  dst["lat"] = c.lat;
  dst["lon"] = c.lon;
  dst["tz"] = (const char*)c.tz;
  dst["help_link"] = (const char*)c.help_link;
  dst["advert_zero_hop_min"] = c.advert_zero_hop_min;
  dst["advert_flood_min"] = c.advert_flood_min;
  JsonObject b = dst["builtin"].to<JsonObject>();
  for (int i = 0; i < BB_COUNT; i++) {
    JsonObject e = b[botBuiltinId(i)].to<JsonObject>();
    e["name"] = (const char*)c.builtin[i].name;
    e["enabled"] = c.builtin[i].enabled;
  }
  JsonArray arr = dst["custom"].to<JsonArray>();
  for (int i = 0; i < c.num_custom && i < BOT_MAX_CUSTOM; i++) {
    JsonObject e = arr.add<JsonObject>();
    e["trigger"] = (const char*)c.custom[i].trigger;
    e["reply"] = (const char*)c.custom[i].reply;
    e["desc"] = (const char*)c.custom[i].desc;
    e["enabled"] = c.custom[i].enabled;
    e["url"] = (const char*)c.custom[i].url;
    e["path"] = (const char*)c.custom[i].path;
    e["cache_s"] = c.custom[i].cache_s;
  }
  JsonArray wa = dst["watch"].to<JsonArray>();
  for (int i = 0; i < c.num_watch && i < BOT_MAX_WATCH; i++) {
    JsonObject e = wa.add<JsonObject>();
    e["label"] = (const char*)c.watch[i].label;
    e["kind"] = c.watch[i].kind == BW_NODE ? "node" : "observer";
    e["id"] = (const char*)c.watch[i].id;
  }
  dst["watch_online_min"] = c.watch_online_min;
  dst["mqtt_enabled"] = c.mqtt_enabled;
  dst["mqtt_host"] = (const char*)c.mqtt_host;
  dst["mqtt_port"] = c.mqtt_port;
  dst["mqtt_user"] = (const char*)c.mqtt_user;
  dst["mqtt_prefix"] = (const char*)c.mqtt_prefix;
  dst["mqtt_wait_s"] = c.mqtt_wait_s;
  if (!for_storage) dst["mqtt_pass_set"] = c.mqtt_pass[0] != '\0';   // the password itself never leaves the device
  if (for_storage) {
    dst["mqtt_pass"] = (const char*)c.mqtt_pass;
    dst["web_salt"] = (const char*)c.web_salt;
    dst["web_hash"] = (const char*)c.web_hash;
  }
}

// botConfigToJson into the whole of doc; false when the document ran out of memory, in which
// case ArduinoJson has silently left members out and doc must not be used.
static inline bool botConfigToJsonChecked(const BotConfig& c, JsonDocument& doc, bool for_storage) {
  doc.clear();
  botConfigToJson(c, doc.to<JsonObject>(), for_storage);
  return !doc.overflowed();
}

// ---- which posted settings were not taken ----
//
// A posted setting was not accepted when its stored value did not change although the posted
// value differs from it. (Comparing with the stored value, not the posted one, keeps values the
// loader normalises, such as rounded coordinates, from being reported.) This is decided key by
// key from the two structs, without building a settings document for either: the result is the
// one the web layer used to get by comparing posted[k], before[k] and after[k] of two whole
// botConfigToJson(..., false) documents (JSON equality, as ArduinoJson's operator== has it), and
// the host test RefusedKeys.MatchesWholeDocumentComparison checks exactly that. The one key not in
// those documents, the old "channel", is judged as the one-element "channels" it stands for, and
// not at all when the posted document also carries "channels" (which then wins).

// The settings keys botConfigToJson writes for the web (without secrets), plus the old "channel"
// and "mqtt_pass" (a posted key whose stored value is compared key by key but never written to a
// document; "mqtt_pass_set" is output only and not a posted setting).
static inline bool botCfgWebKey(const char* k) {
  static const char* const KEYS[] = {"channels", "channel", "scope", "prefix", "cooldown_s", "max_per_min", "ignore", "lat", "lon",
                                     "tz", "help_link", "advert_zero_hop_min", "advert_flood_min", "builtin", "custom",
                                     "watch", "watch_online_min", "mqtt_enabled", "mqtt_host", "mqtt_port", "mqtt_user",
                                     "mqtt_pass", "mqtt_prefix", "mqtt_wait_s"};
  if (!k) return false;
  for (size_t i = 0; i < sizeof(KEYS) / sizeof(KEYS[0]); i++) if (strcmp(k, KEYS[i]) == 0) return true;
  return false;
}

// Whether a and b hold the same value for key k, as botConfigToJson would write it.
static inline bool botCfgKeySame(const char* k, const BotConfig& a, const BotConfig& b) {
  if (!k) return true;
  if (strcmp(k, "channels") == 0 || strcmp(k, "channel") == 0) {
    int na = a.num_channels < BOT_MAX_CHANNELS ? a.num_channels : BOT_MAX_CHANNELS;
    int nb = b.num_channels < BOT_MAX_CHANNELS ? b.num_channels : BOT_MAX_CHANNELS;
    if (na != nb) return false;
    for (int i = 0; i < na; i++) if (strncmp(a.channels[i], b.channels[i], sizeof(a.channels[i])) != 0) return false;
    return true;
  }
  if (strcmp(k, "scope") == 0) return strcmp(a.scope, b.scope) == 0;
  if (strcmp(k, "prefix") == 0) return a.prefix == b.prefix;
  if (strcmp(k, "cooldown_s") == 0) return a.cooldown_s == b.cooldown_s;
  if (strcmp(k, "max_per_min") == 0) return a.max_per_min == b.max_per_min;
  if (strcmp(k, "ignore") == 0) return strcmp(a.ignore, b.ignore) == 0;
  if (strcmp(k, "lat") == 0) return a.lat == b.lat;
  if (strcmp(k, "lon") == 0) return a.lon == b.lon;
  if (strcmp(k, "tz") == 0) return strcmp(a.tz, b.tz) == 0;
  if (strcmp(k, "help_link") == 0) return strcmp(a.help_link, b.help_link) == 0;
  if (strcmp(k, "advert_zero_hop_min") == 0) return a.advert_zero_hop_min == b.advert_zero_hop_min;
  if (strcmp(k, "advert_flood_min") == 0) return a.advert_flood_min == b.advert_flood_min;
  if (strcmp(k, "watch_online_min") == 0) return a.watch_online_min == b.watch_online_min;
  if (strcmp(k, "mqtt_enabled") == 0) return a.mqtt_enabled == b.mqtt_enabled;
  if (strcmp(k, "mqtt_host") == 0) return strcmp(a.mqtt_host, b.mqtt_host) == 0;
  if (strcmp(k, "mqtt_port") == 0) return a.mqtt_port == b.mqtt_port;
  if (strcmp(k, "mqtt_user") == 0) return strcmp(a.mqtt_user, b.mqtt_user) == 0;
  if (strcmp(k, "mqtt_pass") == 0) return strcmp(a.mqtt_pass, b.mqtt_pass) == 0;
  if (strcmp(k, "mqtt_prefix") == 0) return strcmp(a.mqtt_prefix, b.mqtt_prefix) == 0;
  if (strcmp(k, "mqtt_wait_s") == 0) return a.mqtt_wait_s == b.mqtt_wait_s;
  if (strcmp(k, "builtin") == 0) {
    for (int i = 0; i < BB_COUNT; i++)
      if (strcmp(a.builtin[i].name, b.builtin[i].name) != 0 || a.builtin[i].enabled != b.builtin[i].enabled) return false;
    return true;
  }
  if (strcmp(k, "custom") == 0) {
    int na = a.num_custom < BOT_MAX_CUSTOM ? a.num_custom : BOT_MAX_CUSTOM;
    int nb = b.num_custom < BOT_MAX_CUSTOM ? b.num_custom : BOT_MAX_CUSTOM;
    if (na != nb) return false;
    for (int i = 0; i < na; i++) {
      const BotCustomCmd &x = a.custom[i], &y = b.custom[i];
      if (strcmp(x.trigger, y.trigger) != 0 || strcmp(x.reply, y.reply) != 0 || strcmp(x.desc, y.desc) != 0 ||
          x.enabled != y.enabled || strcmp(x.url, y.url) != 0 || strcmp(x.path, y.path) != 0 || x.cache_s != y.cache_s)
        return false;
    }
    return true;
  }
  if (strcmp(k, "watch") == 0) {
    int na = a.num_watch < BOT_MAX_WATCH ? a.num_watch : BOT_MAX_WATCH;
    int nb = b.num_watch < BOT_MAX_WATCH ? b.num_watch : BOT_MAX_WATCH;
    if (na != nb) return false;
    for (int i = 0; i < na; i++) {
      const BotWatchCfg &x = a.watch[i], &y = b.watch[i];
      if (strcmp(x.label, y.label) != 0 || (x.kind == BW_NODE) != (y.kind == BW_NODE) || strcmp(x.id, y.id) != 0) return false;
    }
    return true;
  }
  return true;   // not a settings key: never reported
}

// Whether the posted value v equals what botConfigToJson writes for key k of c.
static inline bool botCfgPostedSame(const char* k, JsonVariantConst v, const BotConfig& c) {
  if (!k) return true;
  if (strcmp(k, "channels") == 0) {
    if (!v.is<JsonArrayConst>()) return false;
    JsonArrayConst arr = v.as<JsonArrayConst>();
    int n = c.num_channels < BOT_MAX_CHANNELS ? c.num_channels : BOT_MAX_CHANNELS;
    if (arr.size() != (size_t)n) return false;
    int i = 0;
    for (JsonVariantConst e : arr) if (!(e == (const char*)c.channels[i++])) return false;
    return true;
  }
  if (strcmp(k, "channel") == 0) {   // the old key: one name for a one-element list
    int n = c.num_channels < BOT_MAX_CHANNELS ? c.num_channels : BOT_MAX_CHANNELS;
    return n == 1 && v == (const char*)c.channels[0];
  }
  if (strcmp(k, "scope") == 0) return v == (const char*)c.scope;
  if (strcmp(k, "prefix") == 0) { char pf[2] = {c.prefix, 0}; return v == (const char*)pf; }
  if (strcmp(k, "cooldown_s") == 0) return v == c.cooldown_s;
  if (strcmp(k, "max_per_min") == 0) return v == c.max_per_min;
  if (strcmp(k, "ignore") == 0) return v == (const char*)c.ignore;
  if (strcmp(k, "lat") == 0) return v == c.lat;
  if (strcmp(k, "lon") == 0) return v == c.lon;
  if (strcmp(k, "tz") == 0) return v == (const char*)c.tz;
  if (strcmp(k, "help_link") == 0) return v == (const char*)c.help_link;
  if (strcmp(k, "advert_zero_hop_min") == 0) return v == c.advert_zero_hop_min;
  if (strcmp(k, "advert_flood_min") == 0) return v == c.advert_flood_min;
  if (strcmp(k, "watch_online_min") == 0) return v == c.watch_online_min;
  if (strcmp(k, "mqtt_enabled") == 0) return v == c.mqtt_enabled;
  if (strcmp(k, "mqtt_host") == 0) return v == (const char*)c.mqtt_host;
  if (strcmp(k, "mqtt_port") == 0) return v == c.mqtt_port;
  if (strcmp(k, "mqtt_user") == 0) return v == (const char*)c.mqtt_user;
  if (strcmp(k, "mqtt_prefix") == 0) return v == (const char*)c.mqtt_prefix;
  if (strcmp(k, "mqtt_wait_s") == 0) return v == c.mqtt_wait_s;
  // The password: compared here against the stored one and never copied into a document. A null
  // means "keep the stored one", so nothing was asked and nothing is refused.
  if (strcmp(k, "mqtt_pass") == 0) return v.isNull() || v == (const char*)c.mqtt_pass;
  if (strcmp(k, "builtin") == 0) {
    if (!v.is<JsonObjectConst>()) return false;
    JsonObjectConst o = v.as<JsonObjectConst>();
    if (o.size() != (size_t)BB_COUNT) return false;
    for (int i = 0; i < BB_COUNT; i++) {
      JsonVariantConst e = o[botBuiltinId(i)];
      if (!e.is<JsonObjectConst>() || e.size() != 2) return false;
      if (!(e["name"] == (const char*)c.builtin[i].name) || !(e["enabled"] == c.builtin[i].enabled)) return false;
    }
    return true;
  }
  if (strcmp(k, "custom") == 0) {
    if (!v.is<JsonArrayConst>()) return false;
    JsonArrayConst arr = v.as<JsonArrayConst>();
    int n = c.num_custom < BOT_MAX_CUSTOM ? c.num_custom : BOT_MAX_CUSTOM;
    if (arr.size() != (size_t)n) return false;
    int i = 0;
    for (JsonVariantConst e : arr) {
      const BotCustomCmd& x = c.custom[i++];
      if (!e.is<JsonObjectConst>() || e.size() != 7) return false;
      if (!(e["trigger"] == (const char*)x.trigger) || !(e["reply"] == (const char*)x.reply) ||
          !(e["desc"] == (const char*)x.desc) || !(e["enabled"] == x.enabled) || !(e["url"] == (const char*)x.url) ||
          !(e["path"] == (const char*)x.path) || !(e["cache_s"] == x.cache_s))
        return false;
    }
    return true;
  }
  if (strcmp(k, "watch") == 0) {
    if (!v.is<JsonArrayConst>()) return false;
    JsonArrayConst arr = v.as<JsonArrayConst>();
    int n = c.num_watch < BOT_MAX_WATCH ? c.num_watch : BOT_MAX_WATCH;
    if (arr.size() != (size_t)n) return false;
    int i = 0;
    for (JsonVariantConst e : arr) {
      const BotWatchCfg& x = c.watch[i++];
      if (!e.is<JsonObjectConst>() || e.size() != 3) return false;
      if (!(e["label"] == (const char*)x.label) || !(e["kind"] == (x.kind == BW_NODE ? "node" : "observer")) ||
          !(e["id"] == (const char*)x.id))
        return false;
    }
    return true;
  }
  return true;
}

// The posted keys that were not taken, joined with ", " into out (cap bytes, cut when full), in
// the order they were posted. A list or built-in key whose refusal rep already explains is left
// out (rep gives the more precise reason; rep.channels covers both "channels" and the old
// "channel"); "channel" beside "channels" is ignored, not refused; keys that are not settings
// (node_name) never appear.
static inline void botCfgRefusedKeys(JsonObjectConst posted, const BotConfig& before, const BotConfig& after,
                                     const BotApplyReport& rep, char* out, size_t cap) {
  if (!out || cap == 0) return;
  out[0] = '\0';
  for (JsonPairConst kv : posted) {
    const char* k = kv.key().c_str();
    bool legacy = strcmp(k, "channel") == 0;
    if (legacy && !posted["channels"].isNull()) continue;
    if (!botCfgWebKey(k) || !botCfgKeySame(k, before, after) || botCfgPostedSame(k, kv.value(), before)) continue;
    if ((rep.custom[0] && strcmp(k, "custom") == 0) || (rep.builtin[0] && strcmp(k, "builtin") == 0) ||
        (rep.watch[0] && strcmp(k, "watch") == 0) || (rep.channels[0] && (legacy || strcmp(k, "channels") == 0)))
      continue;
    size_t n = strlen(out);
    snprintf(out + n, cap - n, "%s%s", n ? ", " : "", k);
  }
}

// ---- moving the broker clears its password ----

#define BOT_MQTT_PASS_CLEARED_MSG "broker changed: password cleared, enter it again"

// Whether a web save must clear the stored broker password: the save moved the broker (the host,
// compared exactly as stored, or the port differs between before and after) and the same request
// did not set a password (posted_pass is not a string, or a string the rules refused, so that after
// does not hold it). The old password is then never offered to the new broker. A posted "" counts as
// set: the password is cleared either way.
static inline bool botCfgBrokerMoveClearsPass(const BotConfig& before, const BotConfig& after, JsonVariantConst posted_pass) {
  bool moved = strcmp(before.mqtt_host, after.mqtt_host) != 0 || before.mqtt_port != after.mqtt_port;
  if (!moved) return false;
  bool set = posted_pass.is<const char*>() && strcmp(posted_pass.as<const char*>(), after.mqtt_pass) == 0;
  return !set && after.mqtt_pass[0] != '\0';
}
