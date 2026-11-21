#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// A minimal HTTP/1.0 GET for the bot's fetch worker (pure C/C++, no heap, host-testable): the
// URL split into host, port and path, the request text, and an incremental parser for the
// status line and headers that is fed one byte at a time and enforces its size limits while it
// reads, so a peer can never make it grow anything.

#define BOT_HTTP_HOST_MAX 100
#define BOT_HTTP_PATH_MAX 319
#define BOT_HTTP_HEAD_MAX 2048   // status line and headers together, line ends included
#define BOT_HTTP_LINE_MAX 256    // one line without its line end

struct BotHttpUrl {
  bool https;
  char host[BOT_HTTP_HOST_MAX + 1];
  uint16_t port;
  char path[BOT_HTTP_PATH_MAX + 1];   // starts with '/', includes the query
};

// Splits "http(s)://host[:port]/path?query" (as botFetchNormalizeUrl writes it). False for any
// other scheme; a host that is empty, over 100 bytes or has anything but letters, digits, '.' and
// '-'; a port that is not 1-65535; a missing '/' after the host; a path over 319 bytes or with a
// byte outside visible ASCII (so nothing can break the request line); any '@' before the path.
static inline bool botHttpParseUrl(const char* url, BotHttpUrl& out) {
  if (!url) return false;
  size_t p;
  bool https;
  if (strncmp(url, "https://", 8) == 0) { p = 8; https = true; }
  else if (strncmp(url, "http://", 7) == 0) { p = 7; https = false; }
  else return false;
  size_t h = p;
  while (url[h] && url[h] != ':' && url[h] != '/') {
    char c = url[h];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-')) return false;
    h++;
  }
  size_t hl = h - p;
  if (hl < 1 || hl > BOT_HTTP_HOST_MAX) return false;
  uint32_t port = https ? 443 : 80;
  size_t q = h;
  if (url[q] == ':') {
    q++;
    uint32_t v = 0;
    size_t digits = 0;
    while (url[q] >= '0' && url[q] <= '9') {
      v = v * 10 + (uint32_t)(url[q] - '0');
      if (++digits > 5) return false;
      q++;
    }
    if (digits == 0 || v < 1 || v > 65535) return false;
    port = v;
  }
  if (url[q] != '/') return false;
  size_t pl = strlen(url + q);
  if (pl > BOT_HTTP_PATH_MAX) return false;
  for (size_t i = 0; i < pl; i++) {
    unsigned char c = (unsigned char)url[q + i];
    if (c <= 0x20 || c > 0x7E) return false;
  }
  memcpy(out.host, url + p, hl);
  out.host[hl] = '\0';
  memcpy(out.path, url + q, pl + 1);
  out.port = (uint16_t)port;
  out.https = https;
  return true;
}

// The request text. Returns its length, or 0 (dst "") when it does not fit cap-1.
static inline size_t botHttpRequest(char* dst, size_t cap, const BotHttpUrl& u) {
  if (!dst || cap == 0) return 0;
  dst[0] = '\0';
  bool default_port = u.port == (u.https ? 443 : 80);
  char port[8] = "";
  if (!default_port) snprintf(port, sizeof port, ":%u", (unsigned)u.port);
  int k = snprintf(dst, cap,
                   "GET %s HTTP/1.0\r\nHost: %s%s\r\nUser-Agent: meshcore-bot\r\nAccept: application/json\r\n"
                   "Accept-Encoding: identity\r\nConnection: close\r\n\r\n",
                   u.path, u.host, port);
  if (k < 0 || (size_t)k >= cap) { dst[0] = '\0'; return 0; }
  return (size_t)k;
}

enum BotHttpState : uint8_t {
  BHS_MORE,       // keep feeding
  BHS_DONE,       // headers complete with status 200: the body follows
  BHS_STATUS,     // a well-formed status line other than 200 (status() has it)
  BHS_BAD,        // not HTTP, or a malformed line or Content-Length
  BHS_TOO_BIG,    // a line over BOT_HTTP_LINE_MAX or headers over BOT_HTTP_HEAD_MAX
  BHS_CHUNKED,    // a Transfer-Encoding other than identity
  BHS_ENCODING,   // a Content-Encoding other than identity
  BHS_LENGTH,     // Content-Length over the body limit
};

static inline bool botHttpEqNoCase(const char* a, size_t an, const char* b) {
  size_t bn = strlen(b);
  if (an != bn) return false;
  for (size_t i = 0; i < an; i++) {
    char x = a[i], y = b[i];
    if (x >= 'A' && x <= 'Z') x = (char)(x + 32);
    if (y >= 'A' && y <= 'Z') y = (char)(y + 32);
    if (x != y) return false;
  }
  return true;
}

// Fed one byte at a time; lines end with "\r\n" or a bare "\n". The first line must be
// "HTTP/1.<digit> <3 digits>" followed by a space or the line end; then header lines
// "Name: value" until an empty line. Content-Length (digits only, at most 9, every copy equal),
// Transfer-Encoding and Content-Encoding are read; other headers are skipped. Once a final state
// is returned, feed() keeps returning it.
class BotHttpHead {
public:
  explicit BotHttpHead(long max_body = 16384) { reset(max_body); }

  void reset(long max_body) {
    _max_body = max_body;
    _state = BHS_MORE;
    _status = 0;
    _length = -1;
    _total = 0;
    _n = 0;
    _first = true;
  }

  int status() const { return _status; }
  long contentLength() const { return _length; }   // -1 when not given
  uint8_t state() const { return _state; }

  uint8_t feed(char c) {
    if (_state != BHS_MORE) return _state;
    if (++_total > BOT_HTTP_HEAD_MAX) return _state = BHS_TOO_BIG;
    if (c == '\n') {
      if (_n > 0 && _line[_n - 1] == '\r') _n--;
      _line[_n] = '\0';
      size_t n = _n;
      _n = 0;
      return _state = line(n);
    }
    if (c == '\0') return _state = BHS_BAD;
    if (_n >= BOT_HTTP_LINE_MAX + 1) return _state = BHS_TOO_BIG;   // +1 leaves room for the '\r'
    _line[_n++] = c;
    return BHS_MORE;
  }

private:
  uint8_t line(size_t n) {
    if (n > BOT_HTTP_LINE_MAX) return BHS_TOO_BIG;
    if (_first) {
      _first = false;
      const char* s = _line;
      if (n < 12 || strncmp(s, "HTTP/1.", 7) != 0 || s[7] < '0' || s[7] > '9' || s[8] != ' ') return BHS_BAD;
      for (int i = 9; i < 12; i++) if (s[i] < '0' || s[i] > '9') return BHS_BAD;
      if (n > 12 && s[12] != ' ') return BHS_BAD;
      _status = (s[9] - '0') * 100 + (s[10] - '0') * 10 + (s[11] - '0');
      return _status == 200 ? BHS_MORE : BHS_STATUS;
    }
    if (n == 0) {
      if (_length > _max_body) return BHS_LENGTH;
      return BHS_DONE;
    }
    const char* colon = (const char*)memchr(_line, ':', n);
    if (!colon || colon == _line || _line[0] == ' ' || _line[0] == '\t') return BHS_BAD;
    size_t kn = (size_t)(colon - _line);
    const char* v = colon + 1;
    const char* e = _line + n;
    while (v < e && (*v == ' ' || *v == '\t')) v++;
    while (e > v && (e[-1] == ' ' || e[-1] == '\t')) e--;
    size_t vn = (size_t)(e - v);
    if (botHttpEqNoCase(_line, kn, "Content-Length")) {
      if (vn < 1 || vn > 9) return BHS_BAD;
      long len = 0;
      for (size_t i = 0; i < vn; i++) {
        if (v[i] < '0' || v[i] > '9') return BHS_BAD;
        len = len * 10 + (v[i] - '0');
      }
      if (_length >= 0 && _length != len) return BHS_BAD;
      _length = len;
    } else if (botHttpEqNoCase(_line, kn, "Transfer-Encoding")) {
      if (!botHttpEqNoCase(v, vn, "identity")) return BHS_CHUNKED;
    } else if (botHttpEqNoCase(_line, kn, "Content-Encoding")) {
      if (!botHttpEqNoCase(v, vn, "identity")) return BHS_ENCODING;
    }
    return BHS_MORE;
  }

  long _max_body;
  uint8_t _state;
  int _status;
  long _length;
  size_t _total;
  size_t _n;
  bool _first;
  char _line[BOT_HTTP_LINE_MAX + 2];
};
