#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// A small regular-expression matcher for the bot's text rules (pure C/C++, no heap,
// host-testable). The pattern is the owner's; the text is whatever a stranger sent, so the work
// and the stack are both bounded: a search that takes more than BOT_RX_STEP_LIMIT steps from one
// starting point, or nests deeper than BOT_RX_DEPTH_LIMIT calls, simply does not match.
//
// Supported: literals, '.', '^', '$', '\b', the classes \d \w \s (and \D \W \S), sets such as
// [a-z0-9_] and [^,], groups ( ) that capture (at most BOT_RX_MAX_GROUPS) and (?: ) that do not,
// alternatives a|b, and the greedy repeats * + ? on a character, set or group. ASCII letters
// match in either case. Not supported (botRxCheck refuses them): {n,m} repeats, lazy repeats
// such as *?, look-around, back-references.

#define BOT_RX_MAX_GROUPS 9
#define BOT_RX_STEP_LIMIT 6000u
#define BOT_RX_DEPTH_LIMIT 16
#define BOT_RX_PATTERN_MAX 48

struct BotRxMatch {
  const char* s[BOT_RX_MAX_GROUPS + 1];   // [0] the whole match; null for a group that took no part
  const char* e[BOT_RX_MAX_GROUPS + 1];
};

static inline char botRxLower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }
static inline bool botRxWord(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}
static inline bool botRxShort(char k, char c, bool& known) {
  known = true;
  switch (k) {
    case 'd': return c >= '0' && c <= '9';
    case 'D': return !(c >= '0' && c <= '9');
    case 'w': return botRxWord(c);
    case 'W': return !botRxWord(c);
    case 's': return c == ' ' || c == '\t' || c == '\n' || c == '\r';
    case 'S': return !(c == ' ' || c == '\t' || c == '\n' || c == '\r');
    default: known = false; return false;
  }
}

// Whether "\<c>" is an escape the matcher knows: a class letter, \b (outside a set), or a
// character that is not a letter or digit (which then stands for itself).
static inline bool botRxEscOk(char c, bool in_set) {
  bool known;
  botRxShort(c, 'a', known);
  if (known) return true;
  if (c == 'b') return !in_set;
  return !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'));
}

// nullptr when pat is a pattern the matcher supports, else what is wrong with it.
static inline const char* botRxCheck(const char* pat) {
  if (!pat || !pat[0]) return "the pattern is empty";
  size_t len = strlen(pat);
  if (len > BOT_RX_PATTERN_MAX) return "the pattern is longer than 48 bytes";
  int depth = 0, groups = 0;
  bool atom = false;   // something a repeat may follow
  for (size_t i = 0; i < len; i++) {
    char c = pat[i];
    switch (c) {
      case '\\':
        if (i + 1 >= len) return "a \\ at the end escapes nothing";
        i++;
        atom = pat[i] != 'b';
        if (pat[i] >= '1' && pat[i] <= '9') return "back-references (\\1) are not supported";
        if (!botRxEscOk(pat[i], false)) return "after a \\ only d w s D W S b or a punctuation character is supported";
        break;
      case '[': {
        size_t j = i + 1;
        if (j < len && pat[j] == '^') j++;
        size_t first = j;
        bool closed = false;
        for (; j < len; j++) {
          if (pat[j] == '\\') {
            if (j + 1 >= len) return "a \\ at the end escapes nothing";
            j++;
            if (!botRxEscOk(pat[j], true)) return "inside [ ] a \\ may only come before d w s D W S or a punctuation character";
            bool sh;
            botRxShort(pat[j], 'a', sh);
            if (sh && ((j + 2 < len && pat[j + 1] == '-' && pat[j + 2] != ']') || (j >= first + 3 && pat[j - 2] == '-' && pat[j - 3] != '\\')))
              return "a range in [ ] cannot start or end with \\d \\w or \\s";
            continue;
          }
          if (pat[j] == ']') { closed = true; break; }
          if (pat[j] == '-' && j > first && j + 1 < len && pat[j + 1] != ']' && pat[j - 1] != '\\' &&
              (uint8_t)pat[j - 1] > (uint8_t)(pat[j + 1] == '\\' ? 0xFF : pat[j + 1]))
            return "a range in [ ] runs backwards";
        }
        if (!closed) return "a [ is never closed";
        if (j == first) return "an empty [ ] matches nothing";
        i = j;
        atom = true;
        break;
      }
      case '(':
        if (i + 1 < len && pat[i + 1] == '?') {
          if (i + 2 >= len || pat[i + 2] != ':') return "only (?: ) is supported after (?";
          i += 2;
        } else if (++groups > BOT_RX_MAX_GROUPS) {
          return "at most 9 capturing groups";
        }
        if (++depth > 4) return "groups are nested more than 4 deep";
        atom = false;
        break;
      case ')':
        if (--depth < 0) return "a ) has no matching (";
        atom = true;
        break;
      case '*': case '+': case '?':
        if (!atom) return "a * + or ? has nothing to repeat (lazy repeats such as *? are not supported)";
        atom = false;
        break;
      case '{': case '}':
        return "{ } repeats are not supported (write \\{ for a brace)";
      case ']':
        return "a ] has no matching [ (write \\] for the character)";
      case '|': case '^': case '$':
        atom = false;
        break;
      default:
        atom = true;
        break;
    }
  }
  if (depth != 0) return "a ( is never closed";
  return nullptr;
}

// ---- matching (the pattern has passed botRxCheck) ----

struct BotRxCtx {
  const char* pat;
  const char* pe;
  const char* ts;
  const char* te;
  uint32_t steps;
  uint8_t depth;
  bool abort;
  BotRxMatch* m;
};

// What follows when the branch of the group being matched ends.
struct BotRxCont {
  const BotRxCont* up;
  const char* rest;     // the pattern after the group and its repeat
  const char* body;     // the group's body, for another round
  const char* at;       // where this round began in the text
  int8_t cap;           // capture number, or 0 for (?: )
  char quant;           // 0, '*', '+' or '?'
};

// The end of the atom at p (a character, an escape, a set or a whole group), without its repeat.
static inline const char* botRxAtomEnd(const char* p, const char* pe) {
  if (p >= pe) return pe;
  if (*p == '\\') return p + 2 <= pe ? p + 2 : pe;
  if (*p == '[') {
    const char* q = p + 1;
    for (; q < pe && *q != ']'; q++) if (*q == '\\') q++;
    return q < pe ? q + 1 : pe;
  }
  if (*p == '(') {
    int d = 0;
    for (const char* q = p; q < pe; q++) {
      if (*q == '\\') { q++; continue; }
      if (*q == '[') { q = botRxAtomEnd(q, pe) - 1; continue; }
      if (*q == '(') d++;
      else if (*q == ')' && --d == 0) return q + 1;
    }
    return pe;
  }
  return p + 1;
}

// Whether the one-character atom at a (not a group) accepts c.
static inline bool botRxOne(const char* a, const char* pe, char c) {
  if (*a == '.') return true;
  char lc = botRxLower(c);
  if (*a == '\\') {
    bool known;
    bool r = botRxShort(a[1], c, known);
    return known ? r : botRxLower(a[1]) == lc;
  }
  if (*a != '[') return botRxLower(*a) == lc;
  const char* q = a + 1;
  bool neg = q < pe && *q == '^';
  if (neg) q++;
  bool hit = false;
  for (; q < pe && *q != ']'; q++) {
    char lo = *q;
    if (lo == '\\') {
      lo = *++q;
      bool known;
      bool r = botRxShort(lo, c, known);
      if (known) { hit = hit || r; continue; }
    }
    char hi = lo;
    if (q + 2 < pe && q[1] == '-' && q[2] != ']') {
      q += 2;
      hi = *q;
      if (hi == '\\') hi = *++q;
    }
    char uc = (lc >= 'a' && lc <= 'z') ? (char)(lc - 32) : lc;
    if (((uint8_t)lc >= (uint8_t)lo && (uint8_t)lc <= (uint8_t)hi) || ((uint8_t)uc >= (uint8_t)lo && (uint8_t)uc <= (uint8_t)hi))
      hit = true;
  }
  return hit != neg;
}

// The capture number of the group opening at g: 1 for the first capturing '(' of the pattern.
static inline int8_t botRxGroupNo(const BotRxCtx& x, const char* g) {
  if (g + 1 < x.pe && g[1] == '?') return 0;
  int8_t n = 0;
  for (const char* q = x.pat; q <= g; q++) {
    if (*q == '\\') { q++; continue; }
    if (*q == '[') { q = botRxAtomEnd(q, x.pe) - 1; continue; }
    if (*q == '(' && !(q + 1 < x.pe && q[1] == '?')) n++;
  }
  return n;
}

static inline bool botRxSeq(BotRxCtx& x, const char* p, const char* s, const BotRxCont* k);

// Tries each alternative from p (the start of a group's body, or of the pattern) at s.
static inline bool botRxAlt(BotRxCtx& x, const char* p, const char* s, const BotRxCont* k) {
  for (;;) {
    if (botRxSeq(x, p, s, k)) return true;
    if (x.abort) return false;
    while (p < x.pe && *p != '|' && *p != ')') p = botRxAtomEnd(p, x.pe);
    if (p >= x.pe || *p == ')') return false;
    p++;   // past the '|'
  }
}

// A branch has ended at s: close the group it belongs to (k), or the whole pattern.
static inline bool botRxDone(BotRxCtx& x, const char* s, const BotRxCont* k) {
  if (!k) { x.m->e[0] = s; return true; }
  const char* os = nullptr; const char* oe = nullptr;
  if (k->cap > 0) { os = x.m->s[k->cap]; oe = x.m->e[k->cap]; x.m->s[k->cap] = k->at; x.m->e[k->cap] = s; }
  // Another round first (greedy), unless this one matched nothing (which would never end).
  if ((k->quant == '*' || k->quant == '+') && s != k->at) {
    BotRxCont again = *k;
    again.at = s;
    if (botRxAlt(x, k->body, s, &again)) return true;
    if (k->cap > 0) { x.m->s[k->cap] = k->at; x.m->e[k->cap] = s; }
  }
  if (!x.abort && botRxSeq(x, k->rest, s, k->up)) return true;
  if (k->cap > 0) { x.m->s[k->cap] = os; x.m->e[k->cap] = oe; }
  return false;
}

static inline bool botRxSeq(BotRxCtx& x, const char* p, const char* s, const BotRxCont* k) {
  if (x.abort) return false;
  if (++x.depth > BOT_RX_DEPTH_LIMIT) { x.abort = true; x.depth--; return false; }
  bool r = false;
  for (;;) {
    if (++x.steps > BOT_RX_STEP_LIMIT) { x.abort = true; break; }
    if (p >= x.pe || *p == '|' || *p == ')') { r = botRxDone(x, s, k); break; }
    if (*p == '^') { if (s != x.ts) break; p++; continue; }
    if (*p == '$') { if (s != x.te) break; p++; continue; }
    if (*p == '\\' && p[1] == 'b') {
      bool before = s > x.ts && botRxWord(s[-1]), after = s < x.te && botRxWord(*s);
      if (before == after) break;
      p += 2;
      continue;
    }
    const char* an = botRxAtomEnd(p, x.pe);
    char q = (an < x.pe && (*an == '*' || *an == '+' || *an == '?')) ? *an : 0;
    const char* rest = q ? an + 1 : an;
    if (*p == '(') {
      BotRxCont c;
      c.up = k;
      c.rest = rest;
      c.cap = botRxGroupNo(x, p);
      c.body = p + (c.cap ? 1 : 3);
      c.at = s;
      c.quant = q;
      r = botRxAlt(x, c.body, s, &c);
      if (!r && !x.abort && (q == '*' || q == '?')) {   // the group may be left out
        const char* os = nullptr; const char* oe = nullptr;
        if (c.cap > 0) { os = x.m->s[c.cap]; oe = x.m->e[c.cap]; x.m->s[c.cap] = nullptr; x.m->e[c.cap] = nullptr; }
        r = botRxSeq(x, rest, s, k);
        if (!r && c.cap > 0) { x.m->s[c.cap] = os; x.m->e[c.cap] = oe; }
      }
      break;
    }
    if (!q) {
      if (s >= x.te || !botRxOne(p, x.pe, *s)) break;
      s++;
      p = an;
      continue;
    }
    // A repeated character or set: take as many as possible, then give back one at a time.
    size_t avail = (size_t)(x.te - s);   // never past the end of the text
    size_t n = 0, max = (q == '?' && avail > 1) ? 1 : avail;
    while (n < max && botRxOne(p, x.pe, s[n])) n++;
    size_t min = q == '+' ? 1 : 0;
    for (;;) {
      if (n < min) break;
      if (botRxSeq(x, rest, s + n, k)) { r = true; break; }
      if (x.abort || n == 0) break;
      n--;
      if (++x.steps > BOT_RX_STEP_LIMIT) { x.abort = true; break; }
    }
    break;
  }
  x.depth--;
  return r;
}

// Looks for pat anywhere in text (use ^ and $ to tie it to the ends). True with m filled in when
// found; false when not found, or when the search was given up as too costly. pat must have
// passed botRxCheck.
static inline bool botRxSearch(const char* pat, const char* text, BotRxMatch& m) {
  if (!pat || !text) return false;
  BotRxCtx x;
  x.pat = pat;
  x.pe = pat + strlen(pat);
  x.ts = text;
  x.te = text + strlen(text);
  x.steps = 0;
  x.depth = 0;
  x.abort = false;
  x.m = &m;
  for (const char* s = text; s <= x.te; s++) {
    memset(&m, 0, sizeof(m));
    m.s[0] = s;
    x.steps = 0;   // each starting point has its own budget
    if (botRxAlt(x, pat, s, nullptr)) return true;
    if (x.abort) break;
  }
  memset(&m, 0, sizeof(m));
  return false;
}
