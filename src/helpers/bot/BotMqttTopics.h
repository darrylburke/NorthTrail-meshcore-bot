#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// MQTT topic rules for the bot (pure C/C++, no heap, host-testable). The prefix is the one the
// owner configured (or the default below); topics from the broker are untrusted, so classifying one
// reads at most topic_len bytes and accepts only the two exact shapes the bot subscribes to.

#define BOT_MQTT_PREFIX_MAX 64
#define BOT_MQTT_TOPIC_MAX  96
#define BOT_MQTT_TAIL_MAX   16   // longest id or command name in a topic

static inline bool botMqttIsLowerAlnum(char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); }

// 1 to 64 bytes of A-Z a-z 0-9 _ - / ; no leading or trailing '/', no empty level, so never
// '#' or '+'. Null is not valid.
static inline bool botMqttPrefixValid(const char* prefix) {
  if (!prefix) return false;
  size_t n = 0;
  for (; prefix[n]; n++) {
    char c = prefix[n];
    bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '/';
    if (!ok || n >= BOT_MQTT_PREFIX_MAX) return false;
    if (c == '/' && (n == 0 || prefix[n - 1] == '/')) return false;
  }
  return n > 0 && prefix[n - 1] != '/';
}

// "meshcore/<node name>" with every character outside A-Z a-z 0-9 _ - removed ("meshcore/bot"
// when nothing is left), cut to 64 bytes. Returns the bytes written. Decisions: a null name is "";
// a buffer under 11 bytes (no room for one name character) gets "" and 0; a buffer that is too small
// for the whole result gets it cut (ASCII only, so no character is split).
static inline size_t botMqttDefaultPrefix(char* dst, size_t cap, const char* node_name) {
  if (!dst || cap == 0) return 0;
  dst[0] = '\0';
  static const char head[] = "meshcore/";
  const size_t hl = sizeof(head) - 1;
  if (cap < hl + 2) return 0;
  size_t limit = cap - 1 < BOT_MQTT_PREFIX_MAX ? cap - 1 : BOT_MQTT_PREFIX_MAX;
  memcpy(dst, head, hl);
  size_t n = hl;
  for (const char* p = node_name ? node_name : ""; *p && n < limit; p++) {
    char c = *p;
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-') dst[n++] = c;
  }
  if (n == hl) for (const char* p = "bot"; *p && n < limit; p++) dst[n++] = *p;
  dst[n] = '\0';
  return n;
}

// The prefix in use: the configured one when it is valid, else the default from the node name
// (an invalid configured prefix is never stored by the loaders; it falls back as an empty one does).
static inline size_t botMqttEffectivePrefix(char* dst, size_t cap, const char* configured, const char* node_name) {
  if (!dst || cap == 0) return 0;
  if (configured && configured[0] && botMqttPrefixValid(configured)) {
    size_t n = strlen(configured);
    if (n + 1 > cap) { dst[0] = '\0'; return 0; }
    memcpy(dst, configured, n + 1);
    return n;
  }
  return botMqttDefaultPrefix(dst, cap, node_name);
}

// "<prefix>/<leaf>" or "<prefix>/<leaf>/<tail>" (a null or empty tail adds no level). False, and
// "" written, when prefix or leaf is null or empty, or the result exceeds 96 bytes or does not fit cap.
static inline bool botMqttTopic(char* dst, size_t cap, const char* prefix, const char* leaf, const char* tail) {
  if (!dst || cap == 0) return false;
  dst[0] = '\0';
  size_t pl = prefix ? strlen(prefix) : 0, ll = leaf ? strlen(leaf) : 0, tl = tail ? strlen(tail) : 0;
  if (pl == 0 || ll == 0) return false;
  size_t total = pl + 1 + ll + (tl ? 1 + tl : 0);
  if (total > BOT_MQTT_TOPIC_MAX || total + 1 > cap) return false;
  size_t n = 0;
  memcpy(dst, prefix, pl); n = pl;
  dst[n++] = '/';
  memcpy(dst + n, leaf, ll); n += ll;
  if (tl) { dst[n++] = '/'; memcpy(dst + n, tail, tl); n += tl; }
  dst[n] = '\0';
  return true;
}

enum BotMqttIn : uint8_t { BMI_NONE, BMI_REPLY, BMI_COMMAND };

// Whether topic (topic_len bytes, not NUL-terminated; only those bytes are read) is exactly
// "<prefix>/reply/<tail>" or "<prefix>/commands/<tail>", the tail being 1 to 16 characters of
// a-z 0-9 (so a single level, no NUL). prefix is NUL-terminated and already validated. The tail is
// copied to `tail`; when it does not fit tail_cap (or tail is null) the result is BMI_NONE.
// `tail` is "" whenever BMI_NONE is returned.
static inline BotMqttIn botMqttClassify(const char* topic, size_t topic_len, const char* prefix, char* tail, size_t tail_cap) {
  if (tail && tail_cap > 0) tail[0] = '\0';
  if (!topic || !prefix || !tail || tail_cap == 0) return BMI_NONE;
  size_t pl = strlen(prefix);
  if (pl == 0 || topic_len <= pl + 1 || memcmp(topic, prefix, pl) != 0 || topic[pl] != '/') return BMI_NONE;
  const char* rest = topic + pl + 1;
  size_t rl = topic_len - pl - 1;
  BotMqttIn kind;
  size_t lead;
  if (rl > 6 && memcmp(rest, "reply/", 6) == 0) { kind = BMI_REPLY; lead = 6; }
  else if (rl > 9 && memcmp(rest, "commands/", 9) == 0) { kind = BMI_COMMAND; lead = 9; }
  else return BMI_NONE;
  size_t tl = rl - lead;
  if (tl < 1 || tl > BOT_MQTT_TAIL_MAX || tl + 1 > tail_cap) return BMI_NONE;
  for (size_t i = 0; i < tl; i++) if (!botMqttIsLowerAlnum(rest[lead + i])) return BMI_NONE;
  memcpy(tail, rest + lead, tl);
  tail[tl] = '\0';
  return kind;
}
