#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "BotReplies.h"

// Placeholder filler for user-defined bot commands (pure C/C++, host-testable).

struct BotVars {
  const char* sender;
  const char* args;
  const char* path;
  const char* channel;
  const char* time;
  const char* date;
  const char* uptime;
  int hops;
  float snr;
  int rssi;
  const char* fetch = "";   // a custom lookup's value; "" for a command without one
};

// Replaces {sender} {hops} {snr} {rssi} {path} {args} {time} {date} {channel} {uptime} {fetch}
// (case-sensitive). Unknown {...}, a lone '{' and an unterminated '{' are copied unchanged;
// null string fields render as "". Always NUL-terminated when cap > 0, never exceeds cap;
// a cut falls on a UTF-8 character boundary. Returns the bytes written excluding the NUL.
static inline size_t botRender(char* dst, size_t cap, const char* tmpl, const BotVars& v) {
  BotOut o = botOutInit(dst, cap);
  if (!tmpl) return o.n;
  const char* p = tmpl;
  while (*p && !o.full) {
    if (*p != '{') {
      size_t run = strcspn(p, "{");
      botOutAppendN(o, p, run);
      p += run;
      continue;
    }
    const char* end = strchr(p + 1, '}');
    size_t len = end ? (size_t)(end - p - 1) : 0;
    const char* name = p + 1;
    bool known = true;
    if (!end) known = false;
    else if (len == 6 && !strncmp(name, "sender", 6)) botOutAppend(o, v.sender);
    else if (len == 4 && !strncmp(name, "hops", 4)) botOutInt(o, v.hops);
    else if (len == 3 && !strncmp(name, "snr", 3)) botOutSnr(o, v.snr);
    else if (len == 4 && !strncmp(name, "rssi", 4)) botOutInt(o, v.rssi);
    else if (len == 4 && !strncmp(name, "path", 4)) botOutAppend(o, v.path);
    else if (len == 4 && !strncmp(name, "args", 4)) botOutAppend(o, v.args);
    else if (len == 4 && !strncmp(name, "time", 4)) botOutAppend(o, v.time);
    else if (len == 4 && !strncmp(name, "date", 4)) botOutAppend(o, v.date);
    else if (len == 7 && !strncmp(name, "channel", 7)) botOutAppend(o, v.channel);
    else if (len == 6 && !strncmp(name, "uptime", 6)) botOutAppend(o, v.uptime);
    else if (len == 5 && !strncmp(name, "fetch", 5)) botOutAppend(o, v.fetch);
    else known = false;
    if (known) p = end + 1;
    else { botOutAppendN(o, p, 1); p++; }  // copy '{' literally; rest is scanned normally
  }
  return o.n;
}
