#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "BotWebRules.h"     // botJsonEscape, BOT_JS_PLAIN
#include "BotMqttLink.h"     // BotMqttState, BOT_MQTT_DETAIL_CAP
#include "BotMqttTopics.h"   // botMqttPrefixValid, BOT_MQTT_PREFIX_MAX
#include "BotMqttWait.h"     // BOT_MQTT_CMD_SLOTS, BOT_MQTT_CMD_NAME_MAX, BOT_MQTT_CMD_DESC_MAX
#include "BotMqttAsk.h"      // BotAskHidden

// The "mqtt" member of GET /api/state (pure, host-testable): the broker connection's state and
// words, the prefix in use and the default one, the counters, and the commands announced on the
// broker with what hides each. Nothing in it comes from the password or the user name, and the host
// never appears (the words are botMqttShow's). Everything is written with botJsonEscape, so text
// from the broker can never form markup even if pasted into HTML; the page inserts it as text
// anyway. The descriptions are stored already safe (botMqttCleanDesc: nothing in them needs
// escaping), and one that is not is written "".
//
//   "mqtt":{"state":"connected","detail":"connected","prefix":"meshcore/MeshBot",
//           "default_prefix":"meshcore/MeshBot","connects":1,"published":3,"received":5,"dropped":0,
//           "answered":2,"timed_out":1,"capped":0,
//           "commands":[{"name":"tide","desc":"tide times","hidden":""},{"name":"ping","desc":"..","hidden":"builtin"}]}

// The state in words: off, waiting for WiFi, connecting, connected, login refused, error.
static inline const char* botMqttStateWord(uint8_t s) {
  switch (s) {
    case BMS_OFF: return "off";
    case BMS_WAIT_WIFI: return "waiting for WiFi";
    case BMS_CONNECTING: return "connecting";
    case BMS_CONNECTED: return "connected";
    case BMS_REFUSED: return "login refused";
    default: return "error";
  }
}

// What hides an announced command: "" (nothing), "builtin" or "custom".
static inline const char* botAskHiddenWord(uint8_t h) { return h == BAH_BUILTIN ? "builtin" : h == BAH_CUSTOM ? "custom" : ""; }

struct BotWebMqttView {
  uint8_t state;                 // BotMqttState
  const char* detail;            // botMqttShow's words; one of BOT_MQTT_DETAIL_CAP bytes or more is written ""
  const char* prefix;            // in use; one that is not botMqttPrefixValid is written ""
  const char* default_prefix;    // the one derived from the node name (the page's placeholder); same rule
  uint32_t connects, published, received, dropped, answered, timed_out, capped;
  int count;                     // announced commands (at most BOT_MQTT_CMD_SLOTS are written)
  const char* names[BOT_MQTT_CMD_SLOTS];   // 1 to 16 of a-z 0-9, else the command is left out
  const char* descs[BOT_MQTT_CMD_SLOTS];   // at most 48 bytes and botMqttDescSafe, else written ""
  uint8_t hidden[BOT_MQTT_CMD_SLOTS];      // BotAskHidden
};

// The longest words above, with their terminator, and the most digits of a uint32_t.
#define BOT_WEB_MQTT_STATE_WORD_CAP sizeof("waiting for WiFi")
#define BOT_WEB_MQTT_HIDDEN_WORD_CAP sizeof("builtin")
#define BOT_JN_U32 10
// botJsonEscape writes a byte as at most 6 ("<" for '<'), plus the two quotes.
#define BOT_JE6(cap) (6 * ((cap) - 1) + 2)

// The written member is at most BOT_WEB_MQTT_JSON_MAX bytes: the literal keys and punctuation
// (sizeof - 1) plus each value at its longest. The state and hidden words, the prefixes (letters,
// digits, '_', '-', '/'), the command names (a-z 0-9) and the descriptions (botMqttDescSafe, or
// written "") are never escaped; the detail is counted at 6 bytes a byte. One comma per command
// (the last has none). The host test MqttState.WorstCaseIsTheBound writes the worst case and
// finds exactly this less that comma.
static const size_t BOT_WEB_MQTT_CMD_JSON_MAX =
    (sizeof("{\"name\":,\"desc\":,\"hidden\":}") - 1) + BOT_JS_PLAIN(BOT_MQTT_CMD_NAME_MAX + 1) +
    BOT_JS_PLAIN(BOT_MQTT_CMD_DESC_MAX + 1) + BOT_JS_PLAIN(BOT_WEB_MQTT_HIDDEN_WORD_CAP) + 1;
static const size_t BOT_WEB_MQTT_JSON_MAX =
    (sizeof("\"mqtt\":{\"state\":,\"detail\":,\"prefix\":,\"default_prefix\":,\"connects\":,\"published\":,"
            "\"received\":,\"dropped\":,\"answered\":,\"timed_out\":,\"capped\":,\"commands\":[]}") - 1) +
    BOT_JS_PLAIN(BOT_WEB_MQTT_STATE_WORD_CAP) + BOT_JE6(BOT_MQTT_DETAIL_CAP) + 2 * BOT_JS_PLAIN(BOT_MQTT_PREFIX_MAX + 1) +
    7 * BOT_JN_U32 + BOT_MQTT_CMD_SLOTS * BOT_WEB_MQTT_CMD_JSON_MAX;

static inline bool botWebMqttNameOk(const char* n) {
  if (!n) return false;
  size_t i = 0;
  for (; n[i]; i++) if (i >= BOT_MQTT_CMD_NAME_MAX || !botMqttIsLowerAlnum(n[i])) return false;
  return i > 0;
}

// Writes the member (without a leading or trailing comma) into dst; returns its length, or 0 with
// dst set to "" when it does not fit cap (cap > BOT_WEB_MQTT_JSON_MAX always fits).
static inline size_t botWebMqttJson(char* dst, size_t cap, const BotWebMqttView& v) {
  if (!dst || cap == 0) return 0;
  struct W {
    char* b; size_t cap, n; bool ok;
    void raw(const char* s) {
      size_t l = strlen(s);
      if (!ok || n + l + 1 > cap) { ok = false; return; }
      memcpy(b + n, s, l + 1);
      n += l;
    }
    void str(const char* s) {
      if (!ok) return;
      size_t w = botJsonEscape(b + n, cap - n, s);
      if (w == 0) { ok = false; return; }
      n += w;
    }
    void num(const char* k, uint32_t x) {
      char t[16];
      snprintf(t, sizeof(t), "%lu", (unsigned long)x);
      raw(",\""); raw(k); raw("\":"); raw(t);
    }
  } w = { dst, cap, 0, true };
  dst[0] = '\0';
  auto bounded = [](const char* s, size_t max_len) { return s && strlen(s) <= max_len ? s : ""; };
  auto prefix = [](const char* s) { return s && botMqttPrefixValid(s) ? s : ""; };
  w.raw("\"mqtt\":{\"state\":");
  w.str(botMqttStateWord(v.state));
  w.raw(",\"detail\":");
  w.str(bounded(v.detail, BOT_MQTT_DETAIL_CAP - 1));
  w.raw(",\"prefix\":");
  w.str(prefix(v.prefix));
  w.raw(",\"default_prefix\":");
  w.str(prefix(v.default_prefix));
  w.num("connects", v.connects);
  w.num("published", v.published);
  w.num("received", v.received);
  w.num("dropped", v.dropped);
  w.num("answered", v.answered);
  w.num("timed_out", v.timed_out);
  w.num("capped", v.capped);
  w.raw(",\"commands\":[");
  bool first = true;
  for (int i = 0; i < v.count && i < BOT_MQTT_CMD_SLOTS; i++) {
    if (!botWebMqttNameOk(v.names[i])) continue;
    w.raw(first ? "{\"name\":" : ",{\"name\":");
    first = false;
    w.str(v.names[i]);
    w.raw(",\"desc\":");
    const char* d = bounded(v.descs[i], BOT_MQTT_CMD_DESC_MAX);
    w.str(botMqttDescSafe(d) ? d : "");
    w.raw(",\"hidden\":");
    w.str(botAskHiddenWord(v.hidden[i]));
    w.raw("}");
  }
  w.raw("]}");
  if (!w.ok) { dst[0] = '\0'; return 0; }
  return w.n;
}
