#pragma once

#include <stddef.h>
#include <stdint.h>

// Chat command parsing for the bot (pure C; host-testable).

struct BotParsed {
  char cmd[17];      // lower-cased command word, letters and digits, up to 16 chars
  const char* args;  // points into the input; "" when none (callers trim trailing spaces)
  bool prefixed;     // the command prefix was written (always true in a channel; a DM may omit it)
};

static inline bool botIsAlnum(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

// Parses a message body into command + args. Leading spaces are skipped; "@[" (mention
// reply) is rejected; in a channel the prefix is required, in a DM it is optional; the
// prefix must be directly followed by a letter or digit. Decisions: null/empty body is
// rejected; the word is ASCII letters/digits only (any other byte ends it); a word over
// 16 characters is rejected; out is left unmodified on failure.
static inline bool botParse(const char* body, char prefix, bool is_dm, BotParsed& out) {
  if (!body) return false;
  const char* p = body;
  while (*p == ' ') p++;
  if (p[0] == '@' && p[1] == '[') return false;
  bool prefixed = *p == prefix;
  if (prefixed) {
    p++;
  } else if (!is_dm) {
    return false;
  }
  if (!botIsAlnum(*p)) return false;
  char cmd[17];
  size_t n = 0;
  while (botIsAlnum(*p)) {
    if (n >= 16) return false;
    char c = *p++;
    cmd[n++] = (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
  }
  cmd[n] = '\0';
  while (*p == ' ') p++;
  for (size_t i = 0; i <= n; i++) out.cmd[i] = cmd[i];
  out.args = p;
  out.prefixed = prefixed;
  return true;
}
