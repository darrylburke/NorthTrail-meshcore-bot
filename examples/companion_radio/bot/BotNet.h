#pragma once

// Internet commands for the bot: wx, aurora, alerts, nodes and custom commands with a lookup.
// A command that needs the internet is parked (2 slots) while its requests run one at a time on
// the BotFetch worker; its reply comes out of take() later and goes through BotTask's normal
// reply path. Results are parsed and worded here, on the loop task.
//
// THREAD RULE: everything here runs on the Arduino loop task. The only link to the worker is
// BotFetch's submit()/poll().

#ifdef WITH_BOT

#include <Arduino.h>
#include "BotFetch.h"
#include <helpers/bot/BotAlerts.h>
#include <helpers/bot/BotCache.h>
#include <helpers/bot/BotConfig.h>
#include <helpers/bot/BotNetLogic.h>
#include <helpers/bot/BotOutgoing.h>
#include <helpers/bot/BotWatch.h>
#include <helpers/bot/BotTemplate.h>

// One finished internet command. text is the reply body without the mention ("" when the bot
// stays silent: a failed command on a channel).
struct BotNetReply {
  bool ready;
  BotReplyDest dest;
  char sender[32];
  char request[48];
  char text[128];
  bool ok;
  uint8_t source;   // BRT_FOREIGN for a custom command (its template), else BRT_BUILTIN
};

// BotNetStart (BNS_REPLY, BNS_PARKED, BNS_BUSY) is in BotNetLogic.h.

class BotNet {
public:
  BotNet();
  void begin(BotFetch* fetch);   // null: every internet command fails at once

  // Starts built-in kind (BB_WX, BB_AURORA, BB_ALERTS or BB_NODES). cfg must be the bot's one live
  // config (it is read again at each step). args are already free of control characters.
  BotNetStart start(uint8_t kind, const BotConfig& cfg, const BotReplyDest& dest, const char* sender,
                    const char* request, const char* args, size_t body_budget, uint32_t now_ms, uint32_t now_unix,
                    int32_t utc_offset_s, char* out, size_t out_cap);
  // Starts a custom command with a lookup. cmd must be an element of the live config's custom[]
  // (it is looked at again when the result arrives; a changed command fails the request).
  BotNetStart startCustom(const BotCustomCmd& cmd, const BotVars& vars, const BotReplyDest& dest, const char* sender,
                          const char* request, size_t body_budget, uint32_t now_ms, char* out, size_t out_cap);
  void loop(uint32_t now_ms, uint32_t now_unix);
  bool take(BotNetReply& out);   // one finished command, if any
  // After the location, watch list or look-ups change: empties the cache and starts a new
  // generation, so a command parked before the change fails rather than answer from (or refill
  // the cache with) the old settings.
  void clearCache() { _cache.clear(); _table.newGeneration(); }

  // Status (loop task only).
  bool fetchBusy() const { return _fetch && _fetch->busy(); }
  int parked() const { return _table.used(); }
  uint8_t lastOutcome() const { return _last_outcome; }
  int lastHttpStatus() const { return _last_http; }
  uint32_t lastMs() const { return _last_ms; }         // the last request in all
  uint32_t lastDnsMs() const { return _last_dns_ms; }  // of which: the host name look-up
  uint32_t lastConnMs() const { return _last_conn_ms; }   // TCP connect plus TLS handshake
  uint32_t lastXchgMs() const { return _last_xchg_ms; }   // sending and reading
  uint32_t workerStackFree() const { return _fetch ? _fetch->stackFreeMin() : 0; }

private:
  struct Slot {
    BotReplyDest dest;
    char sender[32];
    char request[48];
    char args[64];
    char cmd[17];                 // the command as named in the config, for "<cmd> unavailable"
    uint8_t kind;                 // BotNetKind
    int8_t step;
    size_t budget;
    int32_t utc_offset_s;
    double lat, lon;              // wx: where (the bot, or the place found); aurora: the bot
    char place[40];               // wx: the place name found ("" = here)
    const BotCustomCmd* custom;   // custom: the command in the live config
    char text[128];               // nodes: entries so far; custom: the reply with BOT_FETCH_MARK for {fetch}
    size_t text_len;
    bool closed;                  // nodes: an entry did not fit, later ones are dropped
    uint8_t oks;                  // nodes: entries that answered
    bool ok;
  };

  bool alreadyParked(uint8_t kind, const BotReplyDest& dest, const char* args, const char* cmd) const;
  int park(uint8_t kind, const BotReplyDest& dest, const char* sender, const char* request, const char* args,
           const char* cmd, size_t budget, uint32_t now_ms);
  bool buildUrl(const Slot& s, char* url, size_t cap) const;
  void submitNext(uint32_t now_ms, uint32_t now_unix);
  void onResult(int i, const BotFetchResult& r, uint32_t now_ms, uint32_t now_unix);
  void stepFailed(int i, uint32_t now_ms, uint32_t now_unix);   // a step that could not run or did not answer
  void nodesEntry(Slot& s, const BotWatchState* st, uint32_t now_unix);
  void nodesNext(int i, uint32_t now_ms);
  void expire(int i, uint32_t now_ms);
  void finishOk(int i, const char* text, uint32_t now_ms, bool cache);
  void finishFail(int i);
  void failText(const char* cmd, const BotReplyDest& dest, char* out, size_t cap) const;
  bool cacheKey(uint8_t kind, const char* name, const char* args, char* key, size_t cap) const;
  const BotCustomCmd* liveCustom(const Slot& s) const;
  uint32_t ttlMs(const Slot& s) const;

  BotFetch* _fetch;
  const BotConfig* _cfg;
  BotParkTable _table;
  Slot _slot[BOT_PARK_SLOTS];
  BotCache _cache;
  BotAlert _alerts[3];
  char _url[BOT_FETCH_URL_MAX + 1];   // scratch for building a request
  char _scratch[192];                 // scratch for a reply before it is cut to the budget
  uint8_t _last_outcome;
  int _last_http;
  uint32_t _last_ms;
  uint32_t _last_dns_ms, _last_conn_ms, _last_xchg_ms;
};

#endif  // WITH_BOT
