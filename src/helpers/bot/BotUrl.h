#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "BotText.h"

// Pure URL helpers for the chat bot (no Arduino dependencies; host-testable). The URL
// template comes from configuration; the arguments are text typed by strangers on the radio,
// so they are always percent-encoded and can never change the host, path or other parameters.

#define BOT_URL_MAX 160      // longest template
#define BOT_URL_ARG_MAX 60   // longest argument text used (bytes, before encoding)
#define BOT_URL_HOST_MAX 100

enum BotUrlErr : uint8_t { BU_OK, BU_SCHEME, BU_HOST, BU_CREDENTIALS, BU_ARGS_PLACE, BU_CHARS, BU_LENGTH };

static inline bool botUrlIsUnreserved(unsigned char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
         c == '-' || c == '.' || c == '_' || c == '~';
}

// Percent-encodes src (RFC 3986: unreserved bytes kept, every other byte %XX upper-case hex).
// Returns the bytes written excluding the NUL. Decisions: if the result plus NUL does not fit,
// dst is set to "" and 0 is returned; cap == 0 writes nothing; null src is "".
static inline size_t botUrlEncode(char* dst, size_t cap, const char* src) {
  if (!dst || cap == 0) return 0;
  dst[0] = '\0';
  if (!src) src = "";
  size_t need = 0;
  for (const char* p = src; *p; p++) need += botUrlIsUnreserved((unsigned char)*p) ? 1 : 3;
  if (need + 1 > cap) return 0;
  static const char hex[] = "0123456789ABCDEF";
  size_t n = 0;
  for (const char* p = src; *p; p++) {
    unsigned char c = (unsigned char)*p;
    if (botUrlIsUnreserved(c)) {
      dst[n++] = (char)c;
    } else {
      dst[n++] = '%'; dst[n++] = hex[c >> 4]; dst[n++] = hex[c & 15];
    }
  }
  dst[n] = '\0';
  return n;
}

// Checks a URL template and returns the first applicable error: BU_LENGTH, BU_SCHEME, BU_CHARS,
// BU_CREDENTIALS, BU_HOST, BU_ARGS_PLACE. Decisions: null is "" (BU_LENGTH); the '@' rule looks
// at everything before the first '/' after the scheme, so "http://h?x=a@b" is refused; a {args}
// inside the host part is reported as BU_ARGS_PLACE (not BU_HOST), the port is 1-5 digits whose
// value is 1 to 65535 (leading zeros allowed), else BU_HOST.
static inline BotUrlErr botUrlCheckTemplate(const char* tmpl) {
  if (!tmpl) tmpl = "";
  size_t len = strlen(tmpl);
  if (len == 0 || len > BOT_URL_MAX) return BU_LENGTH;
  size_t p;
  if (strncmp(tmpl, "https://", 8) == 0) p = 8;
  else if (strncmp(tmpl, "http://", 7) == 0) p = 7;
  else return BU_SCHEME;
  for (size_t i = 0; i < len; i++) {
    unsigned char c = (unsigned char)tmpl[i];
    if (c <= 0x20 || c > 0x7E) return BU_CHARS;
    if (c == '{') {
      if (strncmp(tmpl + i, "{args}", 6) != 0) return BU_CHARS;
      i += 5;
    } else if (c == '}') {
      return BU_CHARS;
    }
  }
  for (size_t i = p; tmpl[i] && tmpl[i] != '/'; i++)
    if (tmpl[i] == '@') return BU_CREDENTIALS;
  size_t e = p;
  while (tmpl[e] && tmpl[e] != '/' && tmpl[e] != '?') e++;
  bool argsInHost = false;
  for (size_t i = p; i < e; i++) if (tmpl[i] == '{') argsInHost = true;
  if (!argsInHost) {
    size_t hend = p;
    while (hend < e && tmpl[hend] != ':') hend++;
    size_t hlen = hend - p;
    if (hlen < 1 || hlen > BOT_URL_HOST_MAX) return BU_HOST;
    for (size_t i = p; i < hend; i++) {
      unsigned char c = (unsigned char)tmpl[i];
      if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-'))
        return BU_HOST;
    }
    if (hend < e) {   // a port: 1 to 65535, as the HTTP client (botHttpParseUrl) accepts
      size_t digits = e - hend - 1;
      if (digits < 1 || digits > 5) return BU_HOST;
      unsigned long port = 0;
      for (size_t i = hend + 1; i < e; i++) {
        if (tmpl[i] < '0' || tmpl[i] > '9') return BU_HOST;
        port = port * 10 + (unsigned long)(tmpl[i] - '0');
      }
      if (port < 1 || port > 65535) return BU_HOST;
    }
  }
  size_t q = 0;
  while (tmpl[q] && tmpl[q] != '?') q++;
  for (size_t i = 0; i < q; i++) if (tmpl[i] == '{') return BU_ARGS_PLACE;  // only {args} can be a '{' here
  return BU_OK;
}

// Replaces every {args} in tmpl with the percent-encoded args. Decisions: null args is "";
// args longer than 60 bytes are cut to 60 on a UTF-8 boundary first; returns false (dst "") if
// the result does not fit or cap == 0; the template is not re-checked here (use
// botUrlCheckTemplate when it is configured).
static inline bool botUrlBuild(char* dst, size_t cap, const char* tmpl, const char* args) {
  if (!dst || cap == 0) return false;
  dst[0] = '\0';
  if (!tmpl) tmpl = "";
  if (!args) args = "";
  char cut[BOT_URL_ARG_MAX + 1];
  size_t alen = strlen(args);
  if (alen > BOT_URL_ARG_MAX) {
    alen = BOT_URL_ARG_MAX;
    while (alen > 0 && botIsUtf8Cont(args[alen])) alen--;
  }
  memcpy(cut, args, alen);
  cut[alen] = '\0';
  char enc[BOT_URL_ARG_MAX * 3 + 1];
  size_t elen = botUrlEncode(enc, sizeof enc, cut);
  size_t n = 0;
  for (const char* p = tmpl; *p;) {
    if (strncmp(p, "{args}", 6) == 0) {
      if (n + elen + 1 > cap) { dst[0] = '\0'; return false; }
      memcpy(dst + n, enc, elen);
      n += elen;
      p += 6;
    } else {
      if (n + 2 > cap) { dst[0] = '\0'; return false; }
      dst[n++] = *p++;
    }
  }
  dst[n] = '\0';
  return true;
}

static inline bool botUrlIsHttps(const char* url) { return url && strncmp(url, "https://", 8) == 0; }

// Latitude/longitude as request-URL text with exactly 4 decimals (about 11 m). Returns false for
// a non-finite or out-of-range value (|v| over 181, to allow a bounding box past the date line)
// or when the text does not fit. Rounds to 4 decimals first so a tiny negative never prints "-0.0000".
static inline bool botUrlFmtCoord(char* dst, size_t cap, double v) {
  if (!dst || cap == 0) return false;
  dst[0] = '\0';
  if (!(v == v) || v > 181.0 || v < -181.0) return false;
  double r = (v < 0 ? -1.0 : 1.0) * (double)(long)(((v < 0 ? -v : v) * 10000.0) + 0.5) / 10000.0;
  if (r == 0) r = 0.0;
  int k = snprintf(dst, cap, "%.4f", r);
  if (k < 0 || (size_t)k >= cap) { dst[0] = '\0'; return false; }
  return true;
}

static inline bool botValidLatLon(double lat, double lon) {
  return lat == lat && lon == lon && lat >= -90.0 && lat <= 90.0 && lon >= -180.0 && lon <= 180.0;
}
