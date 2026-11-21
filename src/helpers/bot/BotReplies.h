#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "BotText.h"

// Reply text formatters for the chat bot (pure C/C++, no heap, host-testable).
// Every formatter: NUL-terminates when cap > 0, never writes past cap, returns the bytes
// written excluding the NUL (a result that did not fit is cut on a UTF-8 character
// boundary and the return value is the cut length), and treats null strings as "".

struct BotMsgInfo {
  const char* sender;
  uint8_t hops;
  uint8_t hash_size;     // 1, 2 or 3 bytes per path hash
  const uint8_t* path;   // hops * hash_size bytes; not read (may be null) when direct or hops == 0
  float snr;
  int rssi;
  bool direct;           // true when the packet was not a flood (path unknown)
};

typedef const char* (*BotNameLookup)(const uint8_t* hash, uint8_t hash_size, void* ctx);

// ---- bounded output builder (shared with BotTemplate.h) ----
struct BotOut {
  char* d;
  size_t cap;
  size_t n;
  bool full;   // set once anything was cut; later appends are dropped so output stays a prefix
};

static inline BotOut botOutInit(char* dst, size_t cap) {
  BotOut o = {dst, cap, 0, false};
  if (dst && cap > 0) dst[0] = '\0';
  else o.full = true;  // no buffer: nothing can be written
  return o;
}

static inline void botOutAppendN(BotOut& o, const char* s, size_t len) {
  if (o.full || !s || len == 0) return;
  size_t room = o.cap - 1 - o.n;
  size_t cut = len;
  if (len > room) {
    cut = room;
    while (cut > 0 && botIsUtf8Cont(s[cut])) cut--;  // back off to a character start
    o.full = true;
  }
  memcpy(o.d + o.n, s, cut);
  o.n += cut;
  o.d[o.n] = '\0';
}

static inline void botOutAppend(BotOut& o, const char* s) { if (s) botOutAppendN(o, s, strlen(s)); }

static inline void botOutInt(BotOut& o, long v) {
  char t[24];
  int k = snprintf(t, sizeof(t), "%ld", v);
  if (k > 0) botOutAppendN(o, t, (size_t)k);
}

// SNR is a multiple of 0.25: up to two decimals, trailing zeros and dot stripped, never "-0".
// Decisions: NaN prints 0; values are clamped to +-100000.
static inline void botOutSnr(BotOut& o, float snr) {
  if (!(snr == snr)) snr = 0;
  if (snr > 100000.0f) snr = 100000.0f;
  if (snr < -100000.0f) snr = -100000.0f;
  long q = (long)(snr * 4 + (snr >= 0 ? 0.5f : -0.5f));  // quarter-dB units
  bool neg = q < 0;
  unsigned long a = (unsigned long)(neg ? -q : q);
  char t[24];
  int k = snprintf(t, sizeof(t), "%s%lu", (neg && a != 0) ? "-" : "", a / 4);
  unsigned frac = (unsigned)(a % 4) * 25;  // 0, 25, 50, 75
  if (frac) k += snprintf(t + k, sizeof(t) - (size_t)k, frac % 10 ? ".%02u" : ".%u", frac % 10 ? frac : frac / 10);
  botOutAppendN(o, t, (size_t)k);
}

static inline void botOutMention(BotOut& o, const char* sender) {
  botOutAppend(o, "@[");
  botOutAppend(o, sender);
  botOutAppend(o, "]");
}

static inline void botOutHops(BotOut& o, unsigned hops) {
  botOutInt(o, (long)hops);
  botOutAppend(o, hops == 1 ? " hop" : " hops");
}

static inline bool botPathUnknown(const BotMsgInfo& m) {
  // Direct, zero hops, or an unusable hash size: the path is not read.
  return m.direct || m.hops == 0 || !m.path || m.hash_size < 1 || m.hash_size > 3;
}

static inline void botOutHash(BotOut& o, const uint8_t* h, uint8_t size, bool upper) {
  const char* digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
  char t[6];
  for (uint8_t i = 0; i < size; i++) { t[2 * i] = digits[h[i] >> 4]; t[2 * i + 1] = digits[h[i] & 15]; }
  botOutAppendN(o, t, (size_t)size * 2);
}

static inline void botOutPathHex(BotOut& o, const BotMsgInfo& m) {
  if (botPathUnknown(m)) { botOutAppend(o, "Direct"); return; }
  for (uint8_t i = 0; i < m.hops; i++) {
    if (i) botOutAppend(o, ",");
    botOutHash(o, m.path + (size_t)i * m.hash_size, m.hash_size, false);
  }
}

// Lower-case hex hashes joined by ','; "Direct" when direct or hops == 0.
static inline size_t botFmtPathHex(char* dst, size_t cap, const BotMsgInfo& m) {
  BotOut o = botOutInit(dst, cap);
  botOutPathHex(o, m);
  return o.n;
}

// "@[Dee] 3 hops, SNR 7.5"; a zero-hop flood prints "0 hops"; a packet that was not a
// flood (sent over a learned route) prints "direct" in place of the hop count.
static inline size_t botFmtPing(char* dst, size_t cap, const BotMsgInfo& m) {
  BotOut o = botOutInit(dst, cap);
  botOutMention(o, m.sender);
  botOutAppend(o, " ");
  if (m.direct) botOutAppend(o, "direct");
  else botOutHops(o, m.hops);
  botOutAppend(o, ", SNR ");
  botOutSnr(o, m.snr);
  return o.n;
}

// The ping reply followed by the route the message took, one repeater to a line:
//   "@[Dee] 3 hops, SNR 7.5\na53e: Vincent Repeater\nbcad: ?\n07f2: Hilltop"
// "?" stands for a repeater the node has no name for (fn null or returning none). Only whole
// lines are written; when the reply (at most limit bytes, and cap - 1) cannot hold them all, the
// last line says how many are left out: "+4 more". A direct or zero-hop message has no lines.
static inline size_t botFmtPingPath(char* dst, size_t cap, const BotMsgInfo& m, BotNameLookup fn, void* ctx, size_t limit) {
  size_t n = botFmtPing(dst, cap, m);
  if (!dst || cap == 0 || botPathUnknown(m)) return n;
  if (limit > cap - 1) limit = cap - 1;
  for (uint8_t i = 0; i < m.hops; i++) {
    const uint8_t* h = m.path + (size_t)i * m.hash_size;
    char line[48];
    BotOut o = botOutInit(line, sizeof(line));
    botOutAppend(o, "\n");
    botOutHash(o, h, m.hash_size, false);
    botOutAppend(o, ": ");
    const char* name = fn ? fn(h, m.hash_size, ctx) : nullptr;
    botOutAppend(o, (name && *name) ? name : "?");
    unsigned left = (unsigned)(m.hops - i - 1);
    char more[16] = "";
    if (left) snprintf(more, sizeof(more), "\n+%u more", left);
    if (n + o.n + strlen(more) > limit) {   // this line and a note for the rest do not both fit
      snprintf(more, sizeof(more), "\n+%u more", left + 1);
      size_t ml = strlen(more);
      if (n + ml <= limit) { memcpy(dst + n, more, ml + 1); n += ml; }
      break;
    }
    memcpy(dst + n, line, o.n + 1);
    n += o.n;
  }
  return n;
}

// "@[Dee] ack | a53e96,bcadb8 (2 hops) | SNR 7.5 | RSSI -98"; "Direct" has no hop count.
// The mention comes first, like every reply, so it is never added a second time.
static inline size_t botFmtTest(char* dst, size_t cap, const BotMsgInfo& m) {
  BotOut o = botOutInit(dst, cap);
  botOutMention(o, m.sender);
  botOutAppend(o, " ack | ");
  botOutPathHex(o, m);
  if (!botPathUnknown(m)) {
    botOutAppend(o, " (");
    botOutHops(o, m.hops);
    botOutAppend(o, ")");
  }
  botOutAppend(o, " | SNR ");
  botOutSnr(o, m.snr);
  botOutAppend(o, " | RSSI ");
  botOutInt(o, m.rssi);
  return o.n;
}

// "A53E96: Vincent Repeater / BCADB8: ?"; "?" when fn is null or returns no name.
static inline size_t botFmtPath(char* dst, size_t cap, const BotMsgInfo& m, BotNameLookup fn, void* ctx) {
  BotOut o = botOutInit(dst, cap);
  if (botPathUnknown(m)) { botOutAppend(o, "Direct"); return o.n; }
  for (uint8_t i = 0; i < m.hops; i++) {
    const uint8_t* h = m.path + (size_t)i * m.hash_size;
    if (o.full) break;  // buffer already full: skip the callback
    if (i) botOutAppend(o, " / ");
    botOutHash(o, h, m.hash_size, true);
    botOutAppend(o, ": ");
    const char* name = fn ? fn(h, m.hash_size, ctx) : nullptr;
    botOutAppend(o, (name && *name) ? name : "?");
  }
  return o.n;
}

// Reads decimal digits at *p; returns the digit count. The value saturates at 1000000.
static inline int botParseUInt(const char** p, uint32_t* v) {
  int digits = 0;
  uint32_t x = 0;
  while (**p >= '0' && **p <= '9') {
    if (x < 1000000) x = x * 10 + (uint32_t)(**p - '0');
    (*p)++;
    digits++;
  }
  *v = x;
  return digits;
}

// Accepts "NdM", "dM" or empty (1d6) with spaces around; N in 1..10, M in 2..100.
static inline bool botParseRoll(const char* args, uint32_t* dice, uint32_t* sides) {
  if (!args) args = "";
  while (*args == ' ') args++;
  *dice = 1; *sides = 6;
  if (*args == '\0') return true;
  uint32_t n = 1, m = 0;
  if (*args != 'd' && *args != 'D' && botParseUInt(&args, &n) == 0) return false;
  if (*args != 'd' && *args != 'D') return false;
  args++;
  if (botParseUInt(&args, &m) == 0) return false;
  while (*args == ' ') args++;
  if (*args != '\0') return false;
  if (n < 1 || n > 10 || m < 2 || m > 100) return false;
  *dice = n; *sides = m;
  return true;
}

// "@[Dee] 2d6: 3+5 = 8"; rnd(M) is in [0, M), each die shows rnd(M)+1.
static inline size_t botFmtRoll(char* dst, size_t cap, const char* sender, const char* args,
                                uint32_t (*rnd)(uint32_t max_exclusive)) {
  BotOut o = botOutInit(dst, cap);
  botOutMention(o, sender);
  uint32_t dice, sides;
  if (!botParseRoll(args, &dice, &sides)) { botOutAppend(o, " try: roll 2d6"); return o.n; }
  botOutAppend(o, " ");
  botOutInt(o, (long)dice);
  botOutAppend(o, "d");
  botOutInt(o, (long)sides);
  botOutAppend(o, ": ");
  long sum = 0;
  for (uint32_t i = 0; i < dice; i++) {
    uint32_t r = rnd ? rnd(sides) : 0;
    if (r >= sides) r %= sides;  // defend against a callback outside [0, sides)
    long v = (long)r + 1;
    sum += v;
    if (i) botOutAppend(o, "+");
    botOutInt(o, v);
  }
  botOutAppend(o, " = ");
  botOutInt(o, sum);
  return o.n;
}

// "@[Dee] <answer>" using rnd(20) to pick one of the 20 classic answers.
static inline size_t botFmt8Ball(char* dst, size_t cap, const char* sender, uint32_t (*rnd)(uint32_t)) {
  static const char* const ANSWERS[20] = {
    "It is certain.", "It is decidedly so.", "Without a doubt.", "Yes definitely.",
    "You may rely on it.", "As I see it, yes.", "Most likely.", "Outlook good.", "Yes.",
    "Signs point to yes.", "Reply hazy, try again.", "Ask again later.",
    "Better not tell you now.", "Cannot predict now.", "Concentrate and ask again.",
    "Don't count on it.", "My reply is no.", "My sources say no.", "Outlook not so good.",
    "Very doubtful."};
  BotOut o = botOutInit(dst, cap);
  botOutMention(o, sender);
  botOutAppend(o, " ");
  uint32_t i = rnd ? rnd(20) : 0;
  botOutAppend(o, ANSWERS[i % 20]);  // modulo defends against a callback outside [0, 20)
  return o.n;
}


static inline int botDigits(long v) { int d = 1; while (v >= 10) { v /= 10; d++; } return d; }

// Text around the help link on page 1: " | visit <link> for more info".
#define BOT_HELP_LINK_PRE  " | visit "
#define BOT_HELP_LINK_POST " for more info"
#define BOT_HELP_LINK_EXTRA (sizeof(BOT_HELP_LINK_PRE) - 1 + sizeof(BOT_HELP_LINK_POST) - 1)

// Bytes left for names on a page. Page text is "Cmds:" (page 1, plus the link part when a
// link exists) or "Cmds k/t:" (later pages), then " name" per name. `tw` is the digit width
// reserved for t.
static inline long botHelpAvail(long limit, size_t link_len, int tw, int page) {
  if (page == 1) return limit - 5 - (long)link_len;
  return limit - (5 + botDigits(page) + 1 + tw + 1);
}

static inline void botHelpOpen(BotOut& o, int page, int total) {
  if (page == 1) { botOutAppend(o, "Cmds:"); return; }
  botOutAppend(o, "Cmds ");
  botOutInt(o, page);
  botOutAppend(o, "/");
  botOutInt(o, total);
  botOutAppend(o, ":");
}

static inline void botHelpLink(BotOut& o, const char* link) {
  botOutAppend(o, BOT_HELP_LINK_PRE);
  botOutAppend(o, link);
  botOutAppend(o, BOT_HELP_LINK_POST);
}

// Lays out help pages and returns the page count. A name that fits on no page is skipped.
// When out is non-null the text of page `target` is written to it (`total` is then the real
// page count); `tw` is the digit width reserved for the total when measuring.
static inline int botHelpLayout(const char* const* names, int count, long limit, const char* link,
                                int tw, int target, int total, BotOut* out) {
  size_t link_len = (link && *link) ? BOT_HELP_LINK_EXTRA + strlen(link) : 0;
  int page = 1, on_page = 0;
  long used = 0;
  if (out && target == 1) botHelpOpen(*out, 1, total);
  for (int i = 0; i < count; i++) {
    size_t len = names && names[i] ? strlen(names[i]) : 0;
    if (len == 0) continue;
    long need = 1 + (long)len;
    for (;;) {
      if (used + need <= botHelpAvail(limit, link_len, tw, page)) {
        if (out && page == target) { botOutAppend(*out, " "); botOutAppend(*out, names[i]); }
        used += need;
        on_page++;
        break;
      }
      // No room here: move on only if the name fits an empty next page (page 1 may stay
      // name-less when the link leaves no room); otherwise skip the name.
      if ((on_page > 0 || page == 1) && need <= botHelpAvail(limit, link_len, tw, page + 1)) {
        if (out && page == 1 && target == 1 && link_len) botHelpLink(*out, link);
        page++; used = 0; on_page = 0;
        if (out && page == target) botHelpOpen(*out, page, total);
        continue;
      }
      break;
    }
  }
  if (out && page == 1 && target == 1 && link_len) botHelpLink(*out, link);
  return page;
}

// Page `page` (1-based) of the command list. No page is ever longer than
// min(max_bytes, cap-1); a smaller limit cuts the text (on a UTF-8 boundary).
// *total_pages is always set when non-null. A page outside 1..total returns 0 and writes "".
// Names are never split. The link part " | visit <link> for more info" is a whole or nothing: it is
// omitted (never truncated) when "Cmds:" plus the link does not fit the limit, or when
// keeping it would leave page 1 without the first name that would fit there without it.
// Total pages and page contents are computed with that same decision.
static inline size_t botFmtHelp(char* dst, size_t cap, const char* const* names, int count, int page,
                                const char* link, size_t max_bytes, int* total_pages) {
  long limit = cap > 0 ? (long)(max_bytes < cap - 1 ? max_bytes : cap - 1) : 0;
  BotOut o = botOutInit(dst, cap > 0 ? (size_t)limit + 1 : 0);
  if (count < 0) count = 0;
  if (link && *link) {
    long room = limit - 5 - (long)BOT_HELP_LINK_EXTRA - (long)strlen(link);  // space left on page 1 beside the link
    if (room < 0) {
      link = nullptr;
    } else {
      for (int i = 0; i < count; i++) {
        size_t len = names && names[i] ? strlen(names[i]) : 0;
        if (len == 0) continue;
        if (1 + (long)len > room && 1 + (long)len <= limit - 5) link = nullptr;  // first name wins page 1
        break;
      }
    }
  }
  // Smallest digit width tw for the total such that the total really has <= tw digits.
  int total = 1, tw = 1;
  for (; tw <= 10; tw++) {
    total = botHelpLayout(names, count, limit, link, tw, 0, 0, nullptr);
    if (botDigits(total) <= tw) break;
  }
  if (total_pages) *total_pages = total;
  if (page < 1 || page > total) return 0;
  botHelpLayout(names, count, limit, link, tw, page, total, &o);
  return o.n;
}
