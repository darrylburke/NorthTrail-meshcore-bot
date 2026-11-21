#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Pure text helpers for the chat bot (no Arduino dependencies; host-testable).

static inline bool botIsUtf8Cont(char c) { return ((unsigned char)c & 0xC0) == 0x80; }

// Copies at most max_bytes bytes of text into dst (never more than dst_cap-1), always
// NUL-terminated when dst_cap > 0, and returns the bytes written excluding the NUL.
// If text is cut, the result ends with U+2026 (3 bytes) and still fits the limit; the cut
// never splits a UTF-8 sequence. Decisions: dst_cap == 0 writes nothing and returns 0;
// null text is treated as ""; when the limit is under 3 bytes there is no room for the
// ellipsis, so the text is cut at a character boundary with no ellipsis (possibly empty).
static inline size_t botClamp(char* dst, size_t dst_cap, const char* text, size_t max_bytes) {
  if (!dst || dst_cap == 0) return 0;
  if (!text) text = "";
  size_t limit = max_bytes < dst_cap - 1 ? max_bytes : dst_cap - 1;
  size_t len = strlen(text);
  size_t cut, n;
  bool ellipsis = false;
  if (len <= limit) {
    cut = len;
  } else if (limit >= 3) {
    cut = limit - 3;
    ellipsis = true;
  } else {
    cut = limit;
  }
  if (cut < len) {
    while (cut > 0 && botIsUtf8Cont(text[cut])) cut--;  // back off to a character start
  }
  memcpy(dst, text, cut);
  n = cut;
  if (ellipsis) {
    dst[n++] = (char)0xE2; dst[n++] = (char)0x80; dst[n++] = (char)0xA6;
  }
  dst[n] = '\0';
  return n;
}

// Splits "Name: body" at the first ": ". Copies the name into `name` and sets *body to the
// text after the separator (may be ""). Returns false if there is no separator or the name
// is empty. Decisions: a name longer than name_cap-1 is truncated at a UTF-8 character
// boundary (still returns true); name_cap == 0 or null name skips the copy; null body skips
// that output.
static inline bool botSplitSender(const char* text, char* name, size_t name_cap, const char** body) {
  if (!text) return false;
  const char* sep = strstr(text, ": ");
  if (!sep || sep == text) return false;
  if (name && name_cap > 0) {
    size_t n = (size_t)(sep - text);
    if (n > name_cap - 1) {
      n = name_cap - 1;
      while (n > 0 && botIsUtf8Cont(text[n])) n--;
    }
    memcpy(name, text, n);
    name[n] = '\0';
  }
  if (body) *body = sep + 2;
  return true;
}

// Copies src into dst for display: at most cap-1 bytes, cut on a UTF-8 character boundary (no
// ellipsis), control characters (below 0x20 and 0x7F) replaced by spaces. Always NUL-terminated
// when cap > 0; returns the bytes written. Null src is "". Used for text read from the internet.
static inline size_t botCopyClean(char* dst, size_t cap, const char* src) {
  if (!dst || cap == 0) return 0;
  if (!src) src = "";
  size_t n = strlen(src);
  if (n > cap - 1) {
    n = cap - 1;
    while (n > 0 && botIsUtf8Cont(src[n])) n--;
  }
  for (size_t i = 0; i < n; i++) {
    unsigned char c = (unsigned char)src[i];
    dst[i] = (c < 0x20 || c == 0x7F) ? ' ' : (char)c;
  }
  dst[n] = '\0';
  return n;
}

// ASCII case-insensitive equality; null is "".
static inline bool botEqNoCase(const char* a, const char* b) {
  if (!a) a = "";
  if (!b) b = "";
  for (; *a && *b; a++, b++) {
    char x = *a, y = *b;
    if (x >= 'A' && x <= 'Z') x = (char)(x + 32);
    if (y >= 'A' && y <= 'Z') y = (char)(y + 32);
    if (x != y) return false;
  }
  return *a == *b;
}

// Appends one whole segment to dst (length *n so far) when the total stays within limit bytes.
// Returns true when it fitted. When it did not, nothing is appended, except that if nothing has
// been written yet (the first segment) the segment is cut with botClamp so there is always a
// reply. Callers add segments in order and stop at the first false, so whole trailing segments
// are dropped and none is ever cut in the middle.
static inline bool botAddSegment(char* dst, size_t cap, size_t& n, size_t limit, const char* seg) {
  size_t len = seg ? strlen(seg) : 0;
  if (n + len <= limit) {
    if (len) memcpy(dst + n, seg, len);
    n += len;
    dst[n] = '\0';
    return true;
  }
  if (n == 0) n = botClamp(dst, cap, seg, limit);
  return false;
}
