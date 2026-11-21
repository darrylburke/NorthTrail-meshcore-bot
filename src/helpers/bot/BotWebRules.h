#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <ArduinoJson.h>
#include "BotJsonPath.h"   // botJsonStr
#include "BotConfig.h"     // BotConfig field sizes, for the settings document's size bound

// Pure checks behind the bot's web page handlers (no Arduino, host-testable): the same-origin
// check, cookie parsing, the login lockout, field rules for the setup form and for a command
// tried from the page, the JSON string escaping used to write the recent-messages list, and
// the size bound of the settings document the page receives.

// ---- same-origin ----

// Host part ("host" or "host:port") of an http:// or https:// URL, copied to out.
// False for anything else ("null", other schemes, empty host, or out too small).
static inline bool botUrlHost(const char* url, char* out, size_t cap) {
  if (!url || !out || cap == 0) return false;
  const char* p;
  if (strncmp(url, "http://", 7) == 0) p = url + 7;
  else if (strncmp(url, "https://", 8) == 0) p = url + 8;
  else return false;
  size_t n = 0;
  while (p[n] && p[n] != '/' && p[n] != '?' && p[n] != '#') n++;
  if (n == 0 || n + 1 > cap) return false;
  memcpy(out, p, n);
  out[n] = '\0';
  return true;
}

static inline bool botAsciiEqualsNoCase(const char* a, const char* b) {
  for (;; a++, b++) {
    char x = *a, y = *b;
    if (x >= 'A' && x <= 'Z') x = (char)(x + 32);
    if (y >= 'A' && y <= 'Z') y = (char)(y + 32);
    if (x != y) return false;
    if (!x) return true;
  }
}

// True when the Origin header (or, only when Origin is absent or empty, the Referer header)
// names exactly the host in the request's Host header. Any null/empty Host fails.
static inline bool botSameOrigin(const char* origin, const char* referer, const char* host) {
  if (!host || !host[0]) return false;
  const char* src = (origin && origin[0]) ? origin : referer;
  if (!src || !src[0]) return false;
  char h[96];
  if (!botUrlHost(src, h, sizeof(h))) return false;
  return botAsciiEqualsNoCase(h, host);
}

// ---- cookie ----

// Value of cookie `name` from a Cookie header ("a=1; bot=xyz"). False if absent or too long.
static inline bool botCookieValue(const char* header, const char* name, char* out, size_t cap) {
  if (!header || !name || !out || cap == 0) return false;
  size_t nl = strlen(name);
  const char* p = header;
  while (*p) {
    while (*p == ' ' || *p == ';') p++;
    if (strncmp(p, name, nl) == 0 && p[nl] == '=') {
      const char* v = p + nl + 1;
      size_t n = 0;
      while (v[n] && v[n] != ';') n++;
      if (n + 1 > cap) return false;
      memcpy(out, v, n);
      out[n] = '\0';
      return true;
    }
    while (*p && *p != ';') p++;
  }
  return false;
}

// ---- login lockout ----

// Wrong-password limits, keyed by client IPv4 address (as a uint32_t): five wrong in a row
// lock that address for 60 s and a correct one resets it; 8 addresses are tracked, the one
// seen longest ago being replaced. On top, 20 wrong passwords from all addresses within any
// 60 s lock login for everyone until the oldest of them is a minute old, so one device cannot
// lock the owner out for long yet many addresses cannot guess quickly. Times are millis();
// differences are unsigned, so the 32-bit wrap is harmless.
class BotLoginLimiter {
public:
  static const int SLOTS = 8;
  static const int MAX_FAILS = 5;
  static const int GLOBAL_MAX = 20;
  static const uint32_t LOCK_MS = 60000u;
  static const uint32_t WINDOW_MS = 60000u;

  BotLoginLimiter() : _g_n(0), _g_head(0) { memset(_e, 0, sizeof(_e)); memset(_g, 0, sizeof(_g)); }

  // Seconds until this address may try again (0 = it may try now).
  uint32_t retryAfterS(uint32_t ip, uint32_t now_ms) {
    uint32_t left = 0;
    E* e = find(ip);
    if (e && e->locked) {
      uint32_t age = now_ms - e->lock_ms;
      if (age >= LOCK_MS) { e->locked = false; e->fails = 0; }
      else left = LOCK_MS - age;
    }
    if (_g_n == GLOBAL_MAX) {
      uint32_t age = now_ms - _g[_g_head];   // _g_head is the oldest of the last GLOBAL_MAX
      if (age < WINDOW_MS && WINDOW_MS - age > left) left = WINDOW_MS - age;
    }
    return (left + 999u) / 1000u;
  }

  // One wrong password. Ignored for an address that is locked (it should not have been tried).
  void fail(uint32_t ip, uint32_t now_ms) {
    E* e = find(ip);
    if (e && e->locked && (uint32_t)(now_ms - e->lock_ms) < LOCK_MS) return;
    if (!e) e = take(ip);
    if (e->locked) { e->locked = false; e->fails = 0; }
    e->last_ms = now_ms;
    if (++e->fails >= MAX_FAILS) { e->locked = true; e->lock_ms = now_ms; }
    _g[_g_head] = now_ms;
    _g_head = (_g_head + 1) % GLOBAL_MAX;
    if (_g_n < GLOBAL_MAX) _g_n++;
  }

  void success(uint32_t ip) {
    E* e = find(ip);
    if (e) memset(e, 0, sizeof(*e));
  }

private:
  struct E { uint32_t ip; uint32_t last_ms; uint32_t lock_ms; uint8_t fails; bool locked; bool used; };
  E _e[SLOTS];
  uint32_t _g[GLOBAL_MAX];   // times of the last GLOBAL_MAX wrong passwords (ring)
  int _g_n, _g_head;

  E* find(uint32_t ip) {
    for (int i = 0; i < SLOTS; i++) if (_e[i].used && _e[i].ip == ip) return &_e[i];
    return nullptr;
  }
  E* take(uint32_t ip) {
    int slot = -1;
    uint32_t newest = _e[0].last_ms;   // reference point for "seen longest ago"
    for (int i = 0; i < SLOTS; i++) {
      if (!_e[i].used) { slot = i; break; }
      if ((int32_t)(_e[i].last_ms - newest) > 0) newest = _e[i].last_ms;
    }
    if (slot < 0) {
      uint32_t oldest_age = 0;
      slot = 0;
      for (int i = 0; i < SLOTS; i++) {
        uint32_t age = newest - _e[i].last_ms;
        if (age >= oldest_age) { oldest_age = age; slot = i; }
      }
    }
    memset(&_e[slot], 0, sizeof(_e[slot]));
    _e[slot].used = true;
    _e[slot].ip = ip;
    return &_e[slot];
  }
};

// ---- field rules ----

static inline bool botNoControl(const char* s) {
  for (; *s; s++) { uint8_t b = (uint8_t)*s; if (b < 0x20 || b == 0x7F) return false; }
  return true;
}

// Web password: 8 to 63 bytes, no control characters.
static inline bool botWebPasswordOk(const char* s) {
  if (!s) return false;
  size_t n = strlen(s);
  return n >= 8 && n <= 63 && botNoControl(s);
}

// Node name: 1 to 31 bytes, no control characters, none of the characters the advert name
// rules refuse ([ ] \ : , ? *).
static inline bool botNodeNameOk(const char* s) {
  if (!s) return false;
  size_t n = strlen(s);
  if (n < 1 || n > 31 || !botNoControl(s)) return false;
  return strpbrk(s, "[]\\:,?*") == nullptr;
}

// Region scope: empty, or up to 30 lower-case letters, digits and hyphens.
static inline bool botScopeOk(const char* s) {
  if (!s) return false;
  size_t n = 0;
  for (; s[n]; n++) {
    char c = s[n];
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
  }
  return n <= 30;
}

// A command tried from the page ({"text":"..."} on POST /api/try): 1 to 120 bytes, no control
// characters. What it means is the bot's business, exactly as for a direct message.
#define BOT_TRY_TEXT_MAX 120
#define BOT_TRY_BODY_CAP 200   // the posted JSON: {"text":"<120 bytes, some escaped>"}
static inline bool botTryTextOk(const char* s) {
  if (!s) return false;
  size_t n = strlen(s);
  return n >= 1 && n <= BOT_TRY_TEXT_MAX && botNoControl(s);
}

// ---- setup form ----

struct BotSetupData {
  char ssid[33];
  char wifi_password[64];
  char web_password[64];
  char name[32];
  double lat, lon;
  char scope[31];
};

// Validates the setup form into out. Returns nullptr when every field is acceptable, else a
// short message for the page. lat/lon/scope may be omitted (0, 0, ""). When a web password
// already exists (has_password), web_password may be omitted or empty: keep the current one.
// (The current password itself, "current", is checked by the caller, not here.)
static inline const char* botParseSetup(JsonVariantConst v, BotSetupData& out, bool has_password = false) {
  memset(&out, 0, sizeof(out));
  if (!v.is<JsonObjectConst>()) return "bad request";
  JsonObjectConst o = v.as<JsonObjectConst>();

  const char* ssid = botJsonStr(o["ssid"]);
  if (!ssid || !ssid[0] || strlen(ssid) > 32 || !botNoControl(ssid)) return "WiFi name must be 1 to 32 characters";
  const char* wpw = o["wifi_password"].isNull() ? "" : botJsonStr(o["wifi_password"]);
  if (!wpw) return "bad WiFi password";
  size_t wn = strlen(wpw);
  if (wn != 0 && (wn < 8 || wn > 63 || !botNoControl(wpw))) return "WiFi password must be empty or 8 to 63 characters";
  const char* pw = (has_password && o["web_password"].isNull()) ? "" : botJsonStr(o["web_password"]);
  if (!(has_password && pw && !pw[0]) && !botWebPasswordOk(pw)) return "page password must be 8 to 63 characters";
  const char* name = botJsonStr(o["name"]);
  if (!botNodeNameOk(name)) return "name must be 1 to 31 characters, without [ ] \\ : , ? *";
  double lat = 0, lon = 0;
  if (!o["lat"].isNull()) {
    if (!o["lat"].is<double>()) return "latitude must be a number";
    lat = o["lat"].as<double>();
    if (!(lat >= -90.0 && lat <= 90.0)) return "latitude must be -90 to 90";
  }
  if (!o["lon"].isNull()) {
    if (!o["lon"].is<double>()) return "longitude must be a number";
    lon = o["lon"].as<double>();
    if (!(lon >= -180.0 && lon <= 180.0)) return "longitude must be -180 to 180";
  }
  const char* scope = o["scope"].isNull() ? "" : botJsonStr(o["scope"]);
  if (!botScopeOk(scope)) return "scope: up to 30 lower-case letters, digits or hyphens";

  strcpy(out.ssid, ssid);
  strcpy(out.wifi_password, wpw);
  strcpy(out.web_password, pw);
  strcpy(out.name, name);
  out.lat = lat;
  out.lon = lon;
  strcpy(out.scope, scope);
  return nullptr;
}

// ---- JSON string escaping ----

// Writes src as a quoted JSON string to dst (with NUL). Escapes " and \, control characters
// (\n, \r, \t or \u00XX) and '<' (as <, so the text can never form a tag even if the
// JSON is pasted into HTML). Other bytes, including UTF-8, are copied. Returns the length
// written, or 0 (dst set to "" when cap > 0) if it does not fit. Null src writes "".
static inline size_t botJsonEscape(char* dst, size_t cap, const char* src) {
  if (!dst || cap == 0) return 0;
  static const char hex[] = "0123456789abcdef";
  size_t o = 0;
  auto put = [&](char c) { if (o + 1 < cap) { dst[o++] = c; return true; } return false; };
  bool ok = put('"');
  for (const char* p = src ? src : ""; ok && *p; p++) {
    uint8_t b = (uint8_t)*p;
    if (b == '"' || b == '\\') ok = put('\\') && put((char)b);
    else if (b == '\n') ok = put('\\') && put('n');
    else if (b == '\r') ok = put('\\') && put('r');
    else if (b == '\t') ok = put('\\') && put('t');
    else if (b < 0x20 || b == '<') ok = put('\\') && put('u') && put('0') && put('0') && put(hex[b >> 4]) && put(hex[b & 15]);
    else ok = put((char)b);
  }
  ok = ok && put('"');
  if (!ok) { dst[0] = '\0'; return 0; }
  dst[o] = '\0';
  return o;
}

// ---- request routing and the early guard ----

// Content-Type is application/json, optionally with parameters (case-insensitive).
static inline bool botIsJsonType(const char* ct) {
  if (!ct) return false;
  static const char want[] = "application/json";
  for (size_t i = 0; i < sizeof(want) - 1; i++) {
    char c = ct[i];
    if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
    if (c != want[i]) return false;
  }
  char next = ct[sizeof(want) - 1];
  return next == 0 || next == ';' || next == ' ';
}

enum BotRoute : uint8_t { BR_UNKNOWN = 0, BR_PAGE, BR_STATE, BR_RESULT, BR_SETUP, BR_LOGIN, BR_LOGOUT, BR_CONFIG, BR_PASSWORD, BR_ADVERT, BR_TRY };

static inline BotRoute botRouteFor(const char* path) {
  if (!path) return BR_UNKNOWN;
  static const struct { const char* p; BotRoute r; } t[] = {
    { "/", BR_PAGE }, { "/api/state", BR_STATE }, { "/api/result", BR_RESULT }, { "/api/setup", BR_SETUP },
    { "/api/login", BR_LOGIN }, { "/api/logout", BR_LOGOUT }, { "/api/config", BR_CONFIG },
    { "/api/password", BR_PASSWORD }, { "/api/advert", BR_ADVERT }, { "/api/try", BR_TRY },
  };
  for (size_t i = 0; i < sizeof(t) / sizeof(t[0]); i++) if (strcmp(path, t[i].p) == 0) return t[i].r;
  return BR_UNKNOWN;
}

// A posted settings change. The page sends one section at a time; the largest is a full custom
// list, 16 commands with every field at its longest and escaped: at most
// sizeof("{\"custom\":[]}") - 1 + 16 * BOT_CFG_JSON_CUSTOM_MAX = 14,205 bytes (static_assert
// below the size bound), which no longer fits the 8 KB the other routes allow.
#define BOT_CONFIG_BODY_CAP 16384

// Largest body accepted per POST route. Login and setup are reachable without a session, so
// their bodies stay small (setup carries up to three passwords, hence 1 KB). A try carries one
// short line of text.
static inline size_t botRouteBodyCap(BotRoute r) {
  if (r == BR_LOGIN) return 512;
  if (r == BR_SETUP) return 1024;
  if (r == BR_TRY) return BOT_TRY_BODY_CAP;
  if (r == BR_CONFIG) return BOT_CONFIG_BODY_CAP;
  return 8192;
}

// Routes that answer only with a session (for /api/config and /api/try: both GET and POST).
static inline bool botRouteNeedsSession(BotRoute r) {
  return r == BR_RESULT || r == BR_LOGOUT || r == BR_CONFIG || r == BR_PASSWORD || r == BR_ADVERT || r == BR_TRY;
}

// Host header name part (port and one trailing dot removed, case-insensitive) is the station
// IP, the access point IP, or "<mdns_name>.local". Null/empty allowlist entries never match;
// IPv6 literals are refused.
static inline bool botHostAllowed(const char* host, const char* sta_ip, const char* ap_ip, const char* mdns_name) {
  if (!host || !host[0] || host[0] == '[') return false;
  char name[96];
  size_t n = 0;
  while (host[n] && host[n] != ':') { if (n + 1 >= sizeof(name)) return false; name[n] = host[n]; n++; }
  name[n] = 0;
  if (host[n] == ':') {
    const char* p = host + n + 1;
    if (!*p) return false;
    for (; *p; p++) if (*p < '0' || *p > '9') return false;
  }
  if (n && name[n - 1] == '.') name[--n] = 0;
  if (n == 0) return false;
  if (sta_ip && sta_ip[0] && botAsciiEqualsNoCase(name, sta_ip)) return true;
  if (ap_ip && ap_ip[0] && botAsciiEqualsNoCase(name, ap_ip)) return true;
  if (mdns_name && mdns_name[0]) {
    char full[96];
    size_t m = strlen(mdns_name);
    if (m + 7 <= sizeof(full)) {
      memcpy(full, mdns_name, m);
      memcpy(full + m, ".local", 7);
      if (botAsciiEqualsNoCase(name, full)) return true;
    }
  }
  return false;
}

enum BotWebMode : uint8_t { BWM_NORMAL = 0, BWM_SETUP, BWM_NEEDS_PASSWORD };
enum BotMethod : uint8_t { BOT_M_GET = 0, BOT_M_POST, BOT_M_OTHER };
#define BOT_GUARD_REDIRECT 302   // answer with a redirect to the access point's page

// What the server knows once the headers are in, before any body byte is read. Every body
// field must be the value the server's own parser will act on (ESPAsyncWebServer 3.10.3,
// AsyncWebServerRequest::_parseReqHeader), not a separate header lookup:
//  - length: request->contentLength(), which the parser sets from Content-Length OR
//    X-Expected-Entity-Length (last one wins, atoi, so "-1" becomes SIZE_MAX). The body
//    phase runs whenever it is non-zero, for any method.
//  - multipart: request->multipart(), set (and never cleared) by ANY Content-Type header
//    starting "multipart/", even when a later Content-Type replaces the type string.
//  - has_content_type / json: request->contentType(), the LAST Content-Type value up to ';'.
//  - chunked: any Transfer-Encoding header at all (the parser goes chunked when one of them
//    lists "chunked"; refusing every one is the safe superset).
struct BotReqFacts {
  BotMethod method;
  BotRoute route;
  size_t length;          // request->contentLength()
  bool chunked;           // a Transfer-Encoding header was sent
  bool multipart;         // request->multipart()
  bool has_content_type;  // request->contentType() is not empty
  bool json;              // request->contentType() is application/json
  bool host_ok;       // botHostAllowed
  bool origin_ok;     // botSameOrigin (meaningful for POST)
  bool session;       // a valid session cookie (checked without refreshing it)
  bool from_ap;       // arrived on the setup access point
  BotWebMode mode;
};

// The status to answer at once (the body, if any, is then never parsed or buffered), or 0 to
// hand the request to its route. BOT_GUARD_REDIRECT means redirect to the access point page.
static inline int botGuard(const BotReqFacts& f) {
  if (f.method == BOT_M_OTHER) return 405;
  if (!f.host_ok) return (f.mode == BWM_SETUP && f.from_ap && f.method == BOT_M_GET) ? BOT_GUARD_REDIRECT : 421;
  if (f.mode == BWM_SETUP && !f.from_ap) return 404;
  if (f.chunked) return 411;
  if (f.method == BOT_M_GET) {
    // No body of any kind on a GET: with a zero length and no chunking the server never
    // enters its body phase, and a body type is refused too for good measure.
    if (f.length > 0 || f.multipart || f.has_content_type) return 400;
    if (f.route == BR_PAGE || f.route == BR_STATE) return 0;
    if (f.mode == BWM_SETUP && f.route == BR_UNKNOWN) return BOT_GUARD_REDIRECT;   // captive-portal probes
    // Signed-in reads: the save result, the settings and the last try's reply.
    if (f.mode == BWM_NORMAL && (f.route == BR_RESULT || f.route == BR_CONFIG || f.route == BR_TRY)) return f.session ? 0 : 401;
    return 404;
  }
  bool available = f.mode == BWM_SETUP ? f.route == BR_SETUP
                 : f.mode == BWM_NORMAL ? (f.route == BR_LOGIN || f.route == BR_LOGOUT || f.route == BR_CONFIG ||
                                           f.route == BR_PASSWORD || f.route == BR_ADVERT || f.route == BR_TRY)
                 : false;
  if (!available) return 404;
  if (!f.origin_ok) return 403;
  if (f.length == 0) return 411;
  if (f.multipart || !f.json) return 415;   // so the server never runs its form or multipart parser
  if (f.length > botRouteBodyCap(f.route)) return 413;
  if (botRouteNeedsSession(f.route) && !f.session) return 401;
  return 0;
}

// ---- size of the settings document (GET /api/config) ----

// botConfigToJson(c, doc, false) written by serializeJson is at most BOT_CONFIG_JSON_MAX bytes.
// The arithmetic, member by member, from the field sizes and from the literal key text (never a
// hand count; the host test StateSize.WorstSettingsFitTheBound checks it against the worst real
// document and that it is not loose enough to hide a new key):
//  - a stored string of a char[cap] field is at most cap - 1 bytes. Nothing stored holds a
//    control character (every loader refuses them), so ArduinoJson writes each byte as itself
//    or, for '"' and '\', as two bytes: at most 2 * (cap - 1) + 2 with the quotes
//    (BOT_JS_ESC). It does not escape '<' or UTF-8. Fields whose rules allow only letters,
//    digits, '_', '-', '.', '[', ']' (command names, triggers, JSON paths, watch ids) are
//    never escaped: cap - 1 + 2 (BOT_JS_PLAIN).
//  - a double (lat, lon) at most 24 bytes ("-1.7976931348623157e+308" is 24), a uint16_t 5,
//    a uint8_t 3, a bool 5 ("false").
//  - the broker settings: mqtt_enabled and mqtt_pass_set are booleans, mqtt_port a uint16_t,
//    mqtt_wait_s a uint8_t; mqtt_host (letters, digits, '.', '-') and mqtt_prefix (letters,
//    digits, '_', '-', '/') are never escaped, mqtt_user may hold '"' or '\\'. The password
//    itself is not in the web document, only mqtt_pass_set.
//  - the punctuation and keys of each object are the literal text below with the values
//    removed (sizeof - 1), plus one comma per array element / object member.
// With BotConfig as it stands (4 channels, 16 custom commands, 8 text rules, 4 watch entries,
// 14 built-ins) this comes to 19,724 bytes (each built-in and custom command also says which forms it answers
// to, "pfx" and "bare", 25 bytes a command), of which the bare switch and the text rules are 1,976
// (keys 19, the switch 5, per rule 244 = text 98, run 98, kind 7, enabled 5, keys and comma 36).
// Without those: per channel 65 (the name 64, comma 1), per custom command 887
// (trigger 18, reply 322, desc 98, url 322, path 50, two small values 10, keys and comma 67), per
// watch entry 151, per built-in 63, the rest 1,060 (742 before the broker settings, which add
// 318: their keys and commas 102, values 216 = enabled 5, host 66, port 5, user 66, prefix 66,
// wait 3, pass_set 5).
#define BOT_FSZ(T, m) sizeof(((T*)0)->m)
#define BOT_JS_ESC(cap) (2 * ((cap) - 1) + 2)
#define BOT_JS_PLAIN(cap) (((cap) - 1) + 2)
#define BOT_JN_DBL 24
#define BOT_JN_U16 5
#define BOT_JN_U8 3
#define BOT_JN_BOOL 5
static const size_t BOT_CFG_JSON_BUILTIN_MAX =   // "<id>":{"name":"..","enabled":false},
    BOT_JS_PLAIN(BOT_FSZ(BotBuiltinCfg, name)) + (sizeof(":{\"name\":,\"enabled\":,\"pfx\":,\"bare\":}") - 1) +
    BOT_JS_PLAIN(BOT_FSZ(BotBuiltinCfg, name)) + 3 * BOT_JN_BOOL + 1;
static const size_t BOT_CFG_JSON_CUSTOM_MAX =
    (sizeof("{\"trigger\":,\"reply\":,\"desc\":,\"enabled\":,\"url\":,\"path\":,\"cache_s\":,\"pfx\":,\"bare\":}") - 1) +
    BOT_JS_PLAIN(BOT_FSZ(BotCustomCmd, trigger)) + BOT_JS_ESC(BOT_FSZ(BotCustomCmd, reply)) +
    BOT_JS_ESC(BOT_FSZ(BotCustomCmd, desc)) + BOT_JN_BOOL + BOT_JS_ESC(BOT_FSZ(BotCustomCmd, url)) +
    BOT_JS_PLAIN(BOT_FSZ(BotCustomCmd, path)) + BOT_JN_U16 + 2 * BOT_JN_BOOL + 1;
static const size_t BOT_CFG_JSON_WATCH_MAX =
    (sizeof("{\"label\":,\"kind\":,\"id\":}") - 1) + BOT_JS_ESC(BOT_FSZ(BotWatchCfg, label)) +
    (sizeof("\"observer\"") - 1) + BOT_JS_PLAIN(BOT_FSZ(BotWatchCfg, id)) + 1;
static const size_t BOT_CFG_JSON_MATCH_MAX =
    (sizeof("{\"kind\":,\"text\":,\"run\":,\"enabled\":}") - 1) + (sizeof("\"regex\"") - 1) +
    BOT_JS_ESC(BOT_FSZ(BotMatchRule, text)) + BOT_JS_ESC(BOT_FSZ(BotMatchRule, run)) + BOT_JN_BOOL + 1;
static const size_t BOT_CFG_JSON_CHANNEL_MAX =   // "..",
    BOT_JS_ESC(BOT_FSZ(BotConfig, channels[0])) + 1;
static const size_t BOT_CONFIG_JSON_MAX =
    (sizeof("{\"channels\":[],\"scope\":,\"prefix\":,\"cooldown_s\":,\"max_per_min\":,\"ignore\":,\"lat\":,\"lon\":,"
            "\"tz\":,\"help_link\":,\"advert_zero_hop_min\":,\"advert_flood_min\":,\"builtin\":{},\"custom\":[],\"bare\":,\"match\":[],"
            "\"watch\":[],\"watch_online_min\":,\"mqtt_enabled\":,\"mqtt_host\":,\"mqtt_port\":,\"mqtt_user\":,"
            "\"mqtt_prefix\":,\"mqtt_wait_s\":,\"mqtt_pass_set\":}") - 1) +
    BOT_MAX_CHANNELS * BOT_CFG_JSON_CHANNEL_MAX + BOT_JS_ESC(BOT_FSZ(BotConfig, scope)) + BOT_JS_ESC(2) +
    BOT_JN_U16 + BOT_JN_U8 + BOT_JS_ESC(BOT_FSZ(BotConfig, ignore)) + 2 * BOT_JN_DBL +
    BOT_JS_ESC(BOT_FSZ(BotConfig, tz)) + BOT_JS_ESC(BOT_FSZ(BotConfig, help_link)) + 2 * BOT_JN_U16 +
    BB_COUNT * BOT_CFG_JSON_BUILTIN_MAX + BOT_MAX_CUSTOM * BOT_CFG_JSON_CUSTOM_MAX +
    BOT_JN_BOOL + BOT_MAX_MATCH * BOT_CFG_JSON_MATCH_MAX +
    BOT_MAX_WATCH * BOT_CFG_JSON_WATCH_MAX + BOT_JN_U16 +
    BOT_JN_BOOL + BOT_JS_PLAIN(BOT_FSZ(BotConfig, mqtt_host)) + BOT_JN_U16 + BOT_JS_ESC(BOT_FSZ(BotConfig, mqtt_user)) +
    BOT_JS_PLAIN(BOT_FSZ(BotConfig, mqtt_prefix)) + BOT_JN_U8 + BOT_JN_BOOL;

// The one buffer behind GET /api/state and GET /api/config (allocated once, see BotWeb.h). It
// holds either the settings document (at most BOT_CONFIG_JSON_MAX, so it always fits) or the
// state (fixed parts, bounded in BotWeb.cpp with the "mqtt" member's BOT_WEB_MQTT_JSON_MAX from
// BotWebMqtt.h, plus as many recent lines as fit, oldest dropped). Its size is the smallest that
// holds both: the settings document with its NUL, or the state's fixed parts at their longest
// with at least BOT_WEB_STATE_RECENT_MIN bytes left for recent lines, whichever is larger.
#define BOT_WEB_STATE_RECENT_MIN 4096u
static constexpr size_t botWebStateCap(size_t state_fixed_max) {
  return BOT_CONFIG_JSON_MAX + 1 > state_fixed_max + BOT_WEB_STATE_RECENT_MIN ? BOT_CONFIG_JSON_MAX + 1
                                                                              : state_fixed_max + BOT_WEB_STATE_RECENT_MIN;
}
static_assert(sizeof("{\"custom\":[]}") - 1 + BOT_MAX_CUSTOM * BOT_CFG_JSON_CUSTOM_MAX <= BOT_CONFIG_BODY_CAP,
              "a full custom list must fit the settings body cap");

// ---- setup mode helpers ----

// The setup access point's idle timeout. Signed difference: a last_ms stamped by another task
// just after now_ms was read (so slightly ahead) is "not idle", and the 32-bit wrap is fine.
static inline bool botApIdleExpired(uint32_t now_ms, uint32_t last_ms, uint32_t idle_ms) {
  return (int32_t)(now_ms - last_ms) >= (int32_t)idle_ms;
}

// Setup re-run for the network already saved with an empty WiFi password field: keep the
// stored password (the form cannot show it). A different SSID with an empty password means
// an open network.
static inline bool botSetupKeepsWifiPassword(const char* new_ssid, const char* new_pwd, const char* saved_ssid) {
  return new_ssid && new_pwd && saved_ssid && saved_ssid[0] && !new_pwd[0] && strcmp(new_ssid, saved_ssid) == 0;
}

enum BotWifiPwdRule : uint8_t { BOT_WIFI_USE_NEW = 0, BOT_WIFI_KEEP_STORED, BOT_WIFI_REFUSE };

// The setup form's WiFi password field. An empty field for the network already saved, which
// has a password stored, keeps that password, but only for a caller who proved the current
// web password (authed); anyone else is refused, so a visitor to the open access point cannot
// re-enable the saved network without knowing its password. Otherwise the field is used as
// given (empty: an open network).
static inline BotWifiPwdRule botSetupWifiPasswordRule(const char* new_ssid, const char* new_pwd, const char* saved_ssid,
                                                      bool saved_has_pwd, bool authed) {
  if (!botSetupKeepsWifiPassword(new_ssid, new_pwd, saved_ssid) || !saved_has_pwd) return BOT_WIFI_USE_NEW;
  return authed ? BOT_WIFI_KEEP_STORED : BOT_WIFI_REFUSE;
}
