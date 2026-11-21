#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "BotRegex.h"

// Text rules (pure C/C++, no heap, host-testable): the owner lists messages, by their exact text
// or by a pattern, and for each the command the bot runs as if "<prefix><run>" had been sent.
// The command may be a built-in, a custom command or one a program on the broker answers.

enum BotMatchKind : uint8_t { BMK_EXACT = 0, BMK_REGEX = 1 };
#define BOT_MAX_MATCH 8
struct BotMatchRule {
  uint8_t kind;      // BotMatchKind
  bool enabled;
  char text[49];     // BMK_EXACT: the whole message (ASCII case and outer spaces ignored); BMK_REGEX: a BotRegex pattern
  char run[49];      // "<command>[ <arguments>]"; {0} is the matched text, {1}..{9} a pattern's groups
};

static_assert(sizeof(((BotMatchRule*)0)->text) == BOT_RX_PATTERN_MAX + 1, "a rule holds exactly the longest pattern");

// nullptr when run is "<1 to 16 letters or digits>" optionally followed by a space and arguments
// (placeholders are allowed only there, so a stranger's text never chooses the command).
static inline const char* botMatchRunError(const char* run) {
  if (!run || !run[0]) return "the command to run is empty";
  size_t n = 0;
  while ((run[n] >= 'a' && run[n] <= 'z') || (run[n] >= 'A' && run[n] <= 'Z') || (run[n] >= '0' && run[n] <= '9')) n++;
  if (n == 0 || n > 16 || (run[n] != '\0' && run[n] != ' '))
    return "the command to run must start with a command name of 1 to 16 letters or digits, without the prefix";
  return nullptr;
}

// nullptr when the rule's text is usable for its kind.
static inline const char* botMatchTextError(uint8_t kind, const char* text) {
  if (kind == BMK_REGEX) return botRxCheck(text);
  if (!text) return "the text is empty";
  const char* p = text;
  while (*p == ' ') p++;
  if (!*p) return "the text is empty";
  if (p[0] == '@' && p[1] == '[') return "the text must not start with @[ (a bot's reply)";
  return nullptr;
}

// Whether body, without its outer spaces, is text without its outer spaces, ASCII case ignored.
static inline bool botMatchExact(const char* text, const char* body) {
  while (*text == ' ') text++;
  while (*body == ' ') body++;
  size_t tn = strlen(text), bn = strlen(body);
  while (tn > 0 && text[tn - 1] == ' ') tn--;
  while (bn > 0 && body[bn - 1] == ' ') bn--;
  if (tn != bn || tn == 0) return false;
  for (size_t i = 0; i < tn; i++) if (botRxLower(text[i]) != botRxLower(body[i])) return false;
  return true;
}

// Writes run with its placeholders filled in to out (cut to cap - 1). A '{' that does not begin
// a placeholder is copied as it is.
static inline size_t botMatchExpand(char* out, size_t cap, const char* run, const BotRxMatch& m) {
  if (!out || cap == 0) return 0;
  size_t n = 0;
  for (const char* p = run ? run : ""; *p && n + 1 < cap; p++) {
    if (p[0] == '{' && p[1] >= '0' && p[1] <= '9' && p[2] == '}') {
      int g = p[1] - '0';
      for (const char* q = m.s[g]; q && q < m.e[g] && n + 1 < cap; q++) out[n++] = *q;
      p += 2;
      continue;
    }
    out[n++] = *p;
  }
  out[n] = '\0';
  return n;
}

// The first enabled rule that body satisfies: its position, with the command line to run in out;
// -1 when none does. A message starting "@[" (a bot's reply) never matches, so a rule cannot
// make two bots answer each other.
static inline int botMatchFind(const BotMatchRule* rules, int count, const char* body, char* out, size_t cap) {
  if (!rules || !body || !out || cap == 0) return -1;
  const char* b = body;
  while (*b == ' ') b++;
  if (!*b || (b[0] == '@' && b[1] == '[')) return -1;
  for (int i = 0; i < count && i < BOT_MAX_MATCH; i++) {
    const BotMatchRule& r = rules[i];
    if (!r.enabled || !r.text[0] || !r.run[0]) continue;
    BotRxMatch m;
    memset(&m, 0, sizeof(m));
    if (r.kind == BMK_REGEX) {
      if (!botRxSearch(r.text, body, m)) continue;
    } else {
      if (!botMatchExact(r.text, body)) continue;
      m.s[0] = b;
      m.e[0] = b + strlen(b);
      while (m.e[0] > m.s[0] && m.e[0][-1] == ' ') m.e[0]--;
    }
    botMatchExpand(out, cap, r.run, m);
    return i;
  }
  return -1;
}
