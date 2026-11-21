#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// The one-line status for the serial CLI ("get bot.status"), pure and host-testable. The reply
// buffer is 160 bytes; every fixed item has a bounded length (numbers are clamped, words are short
// codes, the scope and the address are cut), so in the worst case they take 123 bytes and a
// separator, a channel name of 31 bytes and the terminator still leave 4 bytes spare (host test
// StatusLine.WorstCaseLeavesRoomForAChannel checks the 123 and the margin).
//
//   > idle p0 ok 4512ms 20/3900/590 kb 104/52/60 stk 4321 mq:ok/1980 sc:ca 192.168.1.20 clk #bot,#test
//
// internet worker busy or idle, parked commands, the last request's outcome code, its duration and
// phases (DNS / TCP connect plus TLS handshake / request and reply), free heap / lowest free heap
// ever / largest free block in KB ("kb"), the worker's unused stack, the broker connection's code and the
// MQTT task's unused stack (0 until it has run), the scope (first 12 characters, "-" when none),
// the station address ("-" while WiFi is not connected), the clock ("clk" set, "-clk" not), then
// the listed channels joined by ',' (whole names only: one that does not fit ends the list).

#define BOT_STATUS_LINE_CAP   160   // the shortest caller's buffer (MyMesh's CLI reply)
#define BOT_STATUS_FIXED_MAX  123   // every fixed item at its longest (host-tested)
#define BOT_STATUS_NET_MAX    6     // longest outcome code shown
#define BOT_STATUS_MQ_MAX     7     // longest broker code shown
#define BOT_STATUS_PARKED_MAX 9u
#define BOT_STATUS_MS_MAX     99999u     // a request ends within 45 s
#define BOT_STATUS_KB_MAX     9999u      // heap figures, in KB
#define BOT_STATUS_STK_MAX    99999u     // the worker's stack is 12 KB
#define BOT_STATUS_MQSTK_MAX  9999u      // the MQTT task's stack is 4 KB

// Short codes for the words botMqttShow (BotMqttLink.h) gives; "?" for anything else.
static inline const char* botMqttDetailCode(const char* detail) {
  static const struct { const char* w; const char* c; } T[] = {
    {"off", "off"}, {"no broker set", "nohost"}, {"waiting for WiFi", "nowifi"}, {"starting", "start"},
    {"cannot start client", "nostart"}, {"restarting", "restart"}, {"cannot restart", "stuck"}, {"cannot restart: reboot the bot", "reboot"},
    {"connecting", "conn"}, {"connected", "ok"}, {"reconnecting", "reconn"}, {"login refused", "refused"},
    {"broker unavailable", "unavail"}, {"protocol refused", "proto"}, {"client id refused", "badid"},
    {"connection refused", "denied"}, {"cannot reach broker", "unreach"}, {"subscribe failed", "nosub"},
    {"connection error", "error"},
  };
  if (!detail) return "?";
  for (size_t i = 0; i < sizeof(T) / sizeof(T[0]); i++) if (strcmp(detail, T[i].w) == 0) return T[i].c;
  return "?";
}

struct BotStatusLine {
  bool busy;                 // the internet worker is running a request
  unsigned parked;           // commands waiting for it
  const char* net;           // the last request's outcome code ("ok", "h404", "dns", ...)
  uint32_t ms, dns_ms, conn_ms, xchg_ms;
  uint32_t heap, min_heap, max_block;   // bytes
  uint32_t stk;              // the worker's unused stack, bytes
  const char* mq;            // botMqttDetailCode
  uint32_t mq_stk;           // the MQTT task's unused stack, bytes
  const char* scope;         // "" when none
  const char* ip;            // "" while WiFi is not connected
  bool clock;
  const char* const* chans;  // the listed channels
  int nchans;
};

static inline unsigned long botStatusClamp(uint32_t v, uint32_t hi) { return (unsigned long)(v > hi ? hi : v); }

// Writes the line into dst (cap bytes, always terminated); returns its length.
static inline size_t botFormatStatus(char* dst, size_t cap, const BotStatusLine& s) {
  if (!dst || cap == 0) return 0;
  int w = snprintf(dst, cap, "> %s p%lu %.6s %lums %lu/%lu/%lu kb %lu/%lu/%lu stk %lu mq:%.7s/%lu sc:%.12s %.15s %s",
                   s.busy ? "busy" : "idle", botStatusClamp(s.parked, BOT_STATUS_PARKED_MAX), s.net ? s.net : "-",
                   botStatusClamp(s.ms, BOT_STATUS_MS_MAX), botStatusClamp(s.dns_ms, BOT_STATUS_MS_MAX),
                   botStatusClamp(s.conn_ms, BOT_STATUS_MS_MAX), botStatusClamp(s.xchg_ms, BOT_STATUS_MS_MAX),
                   botStatusClamp(s.heap / 1024u, BOT_STATUS_KB_MAX), botStatusClamp(s.min_heap / 1024u, BOT_STATUS_KB_MAX),
                   botStatusClamp(s.max_block / 1024u, BOT_STATUS_KB_MAX), botStatusClamp(s.stk, BOT_STATUS_STK_MAX),
                   s.mq ? s.mq : "?", botStatusClamp(s.mq_stk, BOT_STATUS_MQSTK_MAX),
                   s.scope && s.scope[0] ? s.scope : "-", s.ip && s.ip[0] ? s.ip : "-", s.clock ? "clk" : "-clk");
  if (w < 0) { dst[0] = '\0'; return 0; }
  size_t n = (size_t)w < cap ? (size_t)w : cap - 1;
  for (int i = 0; s.chans && i < s.nchans; i++) {
    const char* c = s.chans[i] ? s.chans[i] : "";
    size_t l = strlen(c);
    if (n + 1 + l + 1 > cap) break;   // separator, name, terminator
    dst[n++] = i ? ',' : ' ';
    memcpy(dst + n, c, l);
    n += l;
    dst[n] = '\0';
  }
  return n;
}
