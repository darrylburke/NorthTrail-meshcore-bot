#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <ArduinoJson.h>

// Reads one scalar out of a parsed JSON document by a short path such as
// "current.temperature_2m" or "results[0].name" (host-testable).

#define BOT_JSON_PATH_MAX 48
#define BOT_JSON_KEY_MAX 32
#define BOT_JSON_PATH_STEPS 10   // keys plus indices in one path
#define BOT_JSON_NESTING 12      // nesting limit responses are parsed with (above the steps, so a path can be reached)

static inline bool botJsonKeyChar(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
}

// Parses the path (iteratively, so depth costs no stack) and, when walk is set, follows it from
// root into *out. Returns false on bad syntax, and when walking, on a missing key or index.
static inline bool botJsonPathResolve(const char* path, bool walk, JsonVariantConst root, JsonVariantConst* out) {
  size_t len = path ? strlen(path) : 0;
  if (len == 0 || len > BOT_JSON_PATH_MAX) return false;
  JsonVariantConst cur = root;
  size_t i = 0, steps = 0;
  while (i < len) {
    if (++steps > BOT_JSON_PATH_STEPS) return false;
    if (path[i] == '[') {
      size_t digits = 0, n = 0;
      for (i++; i < len && path[i] >= '0' && path[i] <= '9'; i++) { n = n * 10 + (size_t)(path[i] - '0'); digits++; }
      if (digits < 1 || digits > 3 || i >= len || path[i] != ']') return false;
      i++;
      if (walk) {
        if (!cur.is<JsonArrayConst>()) return false;
        cur = cur.as<JsonArrayConst>()[n];
      }
    } else {
      size_t start = i;
      while (i < len && botJsonKeyChar(path[i])) i++;
      size_t kl = i - start;
      if (kl < 1 || kl > BOT_JSON_KEY_MAX) return false;
      if (walk) {
        char key[BOT_JSON_KEY_MAX + 1];
        memcpy(key, path + start, kl);
        key[kl] = '\0';
        if (!cur.is<JsonObjectConst>()) return false;
        cur = cur.as<JsonObjectConst>()[key];
      }
    }
    if (i >= len) break;
    if (path[i] == '[') continue;
    if (path[i] != '.') return false;
    i++;
    if (i >= len || path[i] == '[') return false;  // a key must follow '.'
  }
  if (walk && out) *out = cur;
  return true;
}

// Syntax only: keys (1-32 of letters, digits, _ and -) separated by '.', each followed by any
// number of [n] (1-3 digits, 0-999, leading zeros allowed); may start with [n]; length 1-48; at
// most BOT_JSON_PATH_STEPS keys and indices in all.
static inline bool botJsonPathCheck(const char* path) {
  return botJsonPathResolve(path, false, JsonVariantConst(), nullptr);
}

// Builds an ArduinoJson filter that keeps only the branch path leads to (use with
// DeserializationOption::Filter). An index keeps that branch in every element of the array (a
// filter array applies its first element to all elements), so the value is still found. False
// (filter cleared) for bad syntax.
static inline bool botJsonPathFilter(const char* path, JsonDocument& filter) {
  filter.clear();
  if (!botJsonPathResolve(path, false, JsonVariantConst(), nullptr)) return false;
  // At most 48 path bytes: every key adds 5 bytes of JSON around it and every index 2, so a
  // filter text of 4 x 48 + 8 bytes always fits.
  char text[4 * BOT_JSON_PATH_MAX + 8];
  char close[BOT_JSON_PATH_MAX + 1];
  size_t n = 0, c = 0;
  for (size_t i = 0; path[i];) {
    if (path[i] == '[') {
      while (path[i] && path[i] != ']') i++;
      if (path[i]) i++;
      text[n++] = '[';
      close[c++] = ']';
    } else if (path[i] == '.') {
      i++;
    } else {
      text[n++] = '{';
      text[n++] = '"';
      while (path[i] && botJsonKeyChar(path[i])) text[n++] = path[i++];
      text[n++] = '"';
      text[n++] = ':';
      close[c++] = '}';
    }
  }
  memcpy(text + n, "true", 4);
  n += 4;
  while (c > 0) text[n++] = close[--c];
  text[n] = '\0';
  if (deserializeJson(filter, text, n, DeserializationOption::NestingLimit(BOT_JSON_PATH_MAX)) != DeserializationError::Ok) {
    filter.clear();
    return false;
  }
  return true;
}

// Writes the scalar at path into out. Strings as they are with control characters (below 0x20
// and 0x7F) replaced by spaces; integers without decimals; other numbers with up to 2 decimals
// and trailing zeros removed ("-0" is written "0"); booleans true/false. Returns false (out "")
// for bad syntax, a missing value, null/object/array, a non-finite or over-63-character number,
// or a result longer than cap-1 bytes (never truncated).
static inline bool botJsonPathGet(JsonVariantConst root, const char* path, char* out, size_t cap) {
  if (!out || cap == 0) return false;
  out[0] = '\0';
  JsonVariantConst v;
  if (!botJsonPathResolve(path, true, root, &v)) return false;
  char num[64];
  const char* text = nullptr;
  size_t n = 0;
  if (v.is<bool>()) {
    text = v.as<bool>() ? "true" : "false";
    n = strlen(text);
  } else if (v.is<const char*>()) {
    text = v.as<const char*>();
    if (!text) return false;
    n = strlen(text);
    if (n > cap - 1) return false;
    for (size_t i = 0; i < n; i++) {
      unsigned char c = (unsigned char)text[i];
      out[i] = (c < 0x20 || c == 0x7F) ? ' ' : (char)c;
    }
    out[n] = '\0';
    return true;
  } else if (v.is<int64_t>()) {
    n = (size_t)snprintf(num, sizeof num, "%lld", (long long)v.as<int64_t>());
    text = num;
  } else if (v.is<double>()) {
    double d = v.as<double>();
    if (!isfinite(d)) return false;
    int w = snprintf(num, sizeof num, "%.2f", d);
    if (w < 0 || (size_t)w >= sizeof num) return false;
    n = (size_t)w;
    while (n > 0 && num[n - 1] == '0') n--;
    if (n > 0 && num[n - 1] == '.') n--;
    num[n] = '\0';
    if (strcmp(num, "-0") == 0) { num[0] = '0'; num[1] = '\0'; n = 1; }
    text = num;
  } else {
    return false;
  }
  if (n > cap - 1) return false;
  memcpy(out, text, n);
  out[n] = '\0';
  return true;
}

// Typed reads for the parsers that use fixed keys. A number must be a JSON number (a numeric
// string is the wrong type) and finite; a string must be a JSON string. Null and missing
// variants are neither.
static inline bool botJsonNum(JsonVariantConst v, double& out) {
  if (!v.is<double>()) return false;
  double d = v.as<double>();
  if (!(d == d) || d > 1e300 || d < -1e300) return false;
  out = d;
  return true;
}

// A JSON string (no embedded NUL) or nullptr. The one definition for every bot header.
static inline const char* botJsonStr(JsonVariantConst v) {
  if (!v.is<const char*>()) return nullptr;
  JsonString s = v.as<JsonString>();
  if (!s.c_str() || strlen(s.c_str()) != s.size()) return nullptr;
  return s.c_str();
}
