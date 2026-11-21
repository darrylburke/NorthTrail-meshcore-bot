#ifdef WITH_BOT

#include "BotNet.h"

#include <ArduinoJson.h>
#include <helpers/bot/BotJsonAlloc.h>
#include <helpers/bot/BotSpace.h>
#include <helpers/bot/BotText.h>
#include <helpers/bot/BotUrl.h>
#include <helpers/bot/BotWatch.h>
#include <helpers/bot/BotWx.h>

static const char* const NO_LOCATION = "I don't know my own location";

static const char* kindId(uint8_t kind) {
  switch (kind) {
    case BNK_WX: return "wx";
    case BNK_AURORA: return "aurora";
    case BNK_ALERTS: return "alerts";
    case BNK_NODES: return "nodes";
    default: return "";
  }
}

BotNet::BotNet()
    : _fetch(nullptr), _cfg(nullptr), _last_outcome(BFO_NONE), _last_http(0), _last_ms(0), _last_dns_ms(0),
      _last_conn_ms(0), _last_xchg_ms(0) {
  memset(_slot, 0, sizeof(_slot));
  memset(_alerts, 0, sizeof(_alerts));
  _url[0] = '\0';
  _scratch[0] = '\0';
}

void BotNet::begin(BotFetch* fetch) { _fetch = fetch; }

// ---- starting ----

void BotNet::failText(const char* cmd, const BotReplyDest& dest, char* out, size_t cap) const {
  if (!out || cap == 0) return;
  out[0] = '\0';
  // A channel stays silent; a direct message (or a try from the page) is told.
  if (dest.kind != BD_CHANNEL) snprintf(out, cap, "%s unavailable", cmd ? cmd : "");
}

bool BotNet::cacheKey(uint8_t kind, const char* name, const char* args, char* key, size_t cap) const {
  if (kind == BNK_CUSTOM) {
    char n[20];
    snprintf(n, sizeof(n), "+%s", name ? name : "");   // custom names never clash with built-in ids
    return botNetCacheKey(key, cap, BOT_CACHE_KEY_MAX, n, args);
  }
  return botNetCacheKey(key, cap, BOT_CACHE_KEY_MAX, kindId(kind), kind == BNK_WX ? args : "");
}

bool BotNet::alreadyParked(uint8_t kind, const BotReplyDest& dest, const char* args, const char* cmd) const {
  // Compared as the slot keeps them (cut and cleaned the same way).
  char a[sizeof(((Slot*)0)->args)], c[sizeof(((Slot*)0)->cmd)];
  botCopyClean(a, sizeof(a), args);
  botCopyClean(c, sizeof(c), cmd);
  for (int i = 0; i < BOT_PARK_SLOTS; i++) {
    BotParkState st = _table.state(i);
    if (st != BPS_WAITING && st != BPS_INFLIGHT) continue;
    const Slot& s = _slot[i];
    if (botParkSameCommand(s.kind, s.cmd, s.args, s.dest, _table.generationOf(i), kind, c, a, dest, _table.generation()))
      return true;
  }
  return false;
}

int BotNet::park(uint8_t kind, const BotReplyDest& dest, const char* sender, const char* request, const char* args,
                 const char* cmd, size_t budget, uint32_t now_ms) {
  int i = _table.park(now_ms);
  if (i < 0) return -1;
  Slot& s = _slot[i];
  memset(&s, 0, sizeof(s));
  s.dest = dest;
  botClamp(s.sender, sizeof(s.sender), sender, sizeof(s.sender) - 1);
  botClamp(s.request, sizeof(s.request), request, sizeof(s.request) - 1);
  botCopyClean(s.args, sizeof(s.args), args);   // cut on a character boundary, no ellipsis
  botCopyClean(s.cmd, sizeof(s.cmd), cmd);
  s.kind = kind;
  s.budget = budget < sizeof(s.text) - 1 ? budget : sizeof(s.text) - 1;
  return i;
}

BotNetStart BotNet::start(uint8_t kind, const BotConfig& cfg, const BotReplyDest& dest, const char* sender,
                          const char* request, const char* args, size_t body_budget, uint32_t now_ms, uint32_t now_unix,
                          int32_t utc_offset_s, char* out, size_t out_cap) {
  (void)now_unix;
  if (!out || out_cap == 0) return BNS_BUSY;
  out[0] = '\0';
  _cfg = &cfg;
  if (!args) args = "";
  uint8_t nk;
  switch (kind) {
    case BB_WX: nk = BNK_WX; break;
    case BB_AURORA: nk = BNK_AURORA; break;
    case BB_ALERTS: nk = BNK_ALERTS; break;
    case BB_NODES: nk = BNK_NODES; break;
    default: return BNS_REPLY;   // not an internet command: no reply
  }
  const char* cmd = cfg.builtin[kind].name;
  bool here = !(cfg.lat == 0 && cfg.lon == 0);
  bool has_place = nk == BNK_WX && args[0];
  if ((nk == BNK_WX && !has_place && !here) || (nk == BNK_ALERTS && !here)) {
    botClamp(out, out_cap, NO_LOCATION, body_budget);
    return BNS_REPLY;
  }
  if (nk == BNK_NODES && cfg.num_watch == 0) {
    botClamp(out, out_cap, "no nodes are being watched", body_budget);
    return BNS_REPLY;
  }
  // A cached reply is used only when it fits this sender's budget whole.
  char key[BOT_CACHE_KEY_MAX + 1];
  if (cacheKey(nk, nullptr, args, key, sizeof(key)) && _cache.get(key, now_ms, _scratch, sizeof(_scratch)) &&
      strlen(_scratch) <= body_budget) {
    botClamp(out, out_cap, _scratch, body_budget);
    return BNS_REPLY;
  }
  if (!_fetch || !_fetch->ready()) {
    failText(cmd, dest, out, out_cap);
    return BNS_REPLY;
  }
  if (alreadyParked(nk, dest, args, cmd)) return BNS_DUPLICATE;
  int i = park(nk, dest, sender, request, args, cmd, body_budget, now_ms);
  if (i < 0) return BNS_BUSY;
  Slot& s = _slot[i];
  s.step = (int8_t)botNetFirstStep(nk, has_place);
  s.utc_offset_s = utc_offset_s;
  s.lat = cfg.lat;
  s.lon = cfg.lon;
  return BNS_PARKED;
}

BotNetStart BotNet::startCustom(const BotCustomCmd& cmd, const BotVars& vars, const BotReplyDest& dest,
                                const char* sender, const char* request, size_t body_budget, uint32_t now_ms, char* out,
                                size_t out_cap) {
  if (!out || out_cap == 0) return BNS_BUSY;
  out[0] = '\0';
  // The settings layer checked these when they were saved; a damaged file must still never turn
  // into a request to an unchecked address.
  if (!cmd.url[0] || botUrlCheckTemplate(cmd.url) != BU_OK || !botJsonPathCheck(cmd.path) || !_fetch ||
      !_fetch->ready()) {
    failText(cmd.trigger, dest, out, out_cap);
    return BNS_REPLY;
  }
  BotVars v = vars;
  // The cache holds the looked-up value, not the reply, so {sender} and the rest stay this message's.
  char key[BOT_CACHE_KEY_MAX + 1];
  char val[BOT_FETCH_VALUE_MAX + 1];
  if (cmd.cache_s > 0 && cacheKey(BNK_CUSTOM, cmd.trigger, vars.args, key, sizeof(key)) &&
      _cache.get(key, now_ms, val, sizeof(val))) {
    v.fetch = val;
    botRender(_scratch, sizeof(_scratch), cmd.reply, v);
    botClamp(out, out_cap, _scratch, body_budget);
    return BNS_REPLY;
  }
  if (alreadyParked(BNK_CUSTOM, dest, vars.args, cmd.trigger)) return BNS_DUPLICATE;
  int i = park(BNK_CUSTOM, dest, sender, request, vars.args, cmd.trigger, body_budget, now_ms);
  if (i < 0) return BNS_BUSY;
  Slot& s = _slot[i];
  s.custom = &cmd;
  s.step = 0;
  // Everything but {fetch} is filled now, while this message's details are at hand.
  static const char mark[2] = {BOT_FETCH_MARK, '\0'};
  v.fetch = mark;
  botRender(s.text, sizeof(s.text), cmd.reply, v);
  return BNS_PARKED;
}

// ---- running ----

const BotCustomCmd* BotNet::liveCustom(const Slot& s) const {
  const BotCustomCmd* c = s.custom;
  if (!c || !c->enabled || !c->url[0] || strcmp(c->trigger, s.cmd) != 0) return nullptr;   // changed meanwhile
  return c;
}

uint32_t BotNet::ttlMs(const Slot& s) const {
  switch (s.kind) {
    case BNK_WX: return 10u * 60000u;
    case BNK_AURORA: return 15u * 60000u;
    case BNK_ALERTS: return 5u * 60000u;
    case BNK_NODES: return 2u * 60000u;
    default: {
      const BotCustomCmd* c = liveCustom(s);
      return c ? (uint32_t)c->cache_s * 1000u : 0;
    }
  }
}

bool BotNet::buildUrl(const Slot& s, char* url, size_t cap) const {
  switch (s.kind) {
    case BNK_WX: return s.step == 0 ? botGeoUrl(url, cap, s.args) : botWxUrl(url, cap, s.lat, s.lon);
    case BNK_AURORA: return botClamp(url, cap, botKpUrl(), cap - 1) == strlen(botKpUrl());
    case BNK_ALERTS: return botAlertsUrl(url, cap, s.lat, s.lon);
    case BNK_NODES: {
      if (!_cfg || s.step < 0 || s.step >= _cfg->num_watch || s.step >= BOT_MAX_WATCH) return false;
      const BotWatchCfg& w = _cfg->watch[s.step];
      return botWatchUrl(url, cap, w.kind == BW_NODE ? BW_NODE : BW_OBSERVER, w.id);
    }
    case BNK_CUSTOM: {
      const BotCustomCmd* c = liveCustom(s);
      if (!c || botUrlCheckTemplate(c->url) != BU_OK) return false;   // checked again before every use
      return botUrlBuild(url, cap, c->url, s.args);
    }
    default: return false;
  }
}

void BotNet::loop(uint32_t now_ms, uint32_t now_unix) {
  // A request that has not answered BOT_PARK_STEP_MS after it was submitted fails that step (a
  // watched-nodes command goes on with its next entry); its late result matches no slot. A parked
  // command never waits longer than BOT_PARK_CEILING_MS in all, whatever the network does.
  for (int n = 0, i; n < BOT_PARK_SLOTS && (i = _table.stepExpired(now_ms)) >= 0; n++) {
    if (_table.stale(i)) finishFail(i);
    else stepFailed(i, now_ms, now_unix);
  }
  for (int n = 0, i; n < BOT_PARK_SLOTS && (i = _table.ceilingExpired(now_ms)) >= 0; n++) expire(i, now_ms);
  if (!_fetch) return;
  BotFetchResult r = _fetch->poll();
  if (r.done) {
    _last_outcome = r.outcome;
    _last_http = r.http_status;
    _last_ms = r.ms;
    _last_dns_ms = r.dns_ms;
    _last_conn_ms = r.conn_ms;
    _last_xchg_ms = r.xchg_ms;
    int i = _table.resultFor(r.seq);   // -1: its step already expired; the result is dropped
    if (i >= 0 && _table.stale(i)) finishFail(i);   // parked before a settings change
    else if (i >= 0) onResult(i, r, now_ms, now_unix);
  }
  submitNext(now_ms, now_unix);
}

void BotNet::submitNext(uint32_t now_ms, uint32_t now_unix) {
  // Each pass either submits, or ends a step (which moves a command on or finishes it), so this
  // stops after a few passes at most.
  for (int guard = 0; guard < 8 && !_fetch->busy(); guard++) {
    int i = _table.nextToSubmit();
    if (i < 0) return;
    if (_table.stale(i)) { finishFail(i); continue; }   // its settings changed: never built from the new ones
    uint32_t seq = 0;
    if (buildUrl(_slot[i], _url, sizeof(_url)) && _fetch->submit(_url, &seq)) {
      _table.submitted(i, seq, now_ms);
      return;
    }
    stepFailed(i, now_ms, now_unix);
  }
}

void BotNet::stepFailed(int i, uint32_t now_ms, uint32_t now_unix) {
  Slot& s = _slot[i];
  if (s.kind == BNK_NODES) {
    nodesEntry(s, nullptr, now_unix);
    nodesNext(i, now_ms);
  } else {
    finishFail(i);
  }
}

void BotNet::onResult(int i, const BotFetchResult& r, uint32_t now_ms, uint32_t now_unix) {
  Slot& s = _slot[i];
  if (!r.ok) {
    stepFailed(i, now_ms, now_unix);
    return;
  }
  // Both documents live only in this function, so they are freed before the next request. The
  // response document can never hold more than BOT_JSON_HEAP_LIMIT bytes of heap.
  BotJsonBoundedAllocator limit;
  JsonDocument doc(&limit);
  JsonDocument filter;
  switch (s.kind) {
    case BNK_WX:
      if (s.step == 0) {
        botGeoFilter(filter);
        filter["error"] = true;   // Open-Meteo's error object, so it is not taken for "no match"
      } else {
        botWxFilter(filter);
      }
      break;
    case BNK_AURORA: botKpFilter(filter); break;
    case BNK_ALERTS: botAlertsFilter(filter); break;
    case BNK_NODES: {
      bool node = _cfg && s.step >= 0 && s.step < _cfg->num_watch && _cfg->watch[s.step].kind == BW_NODE;
      botWatchFilter(filter, node ? BW_NODE : BW_OBSERVER);
      break;
    }
    default: {   // custom: only the branch the JSON path leads to
      const BotCustomCmd* c = liveCustom(s);
      if (!c || !botJsonPathFilter(c->path, filter)) { finishFail(i); return; }
      break;
    }
  }
  DeserializationError err = deserializeJson(doc, (const char*)r.body, r.len, DeserializationOption::Filter(filter),
                                             DeserializationOption::NestingLimit(BOT_JSON_NESTING));
  if (err || doc.overflowed()) {
    stepFailed(i, now_ms, now_unix);
    return;
  }
  JsonVariantConst root = doc.as<JsonVariantConst>();
  switch (s.kind) {
    case BNK_WX: {
      if (s.step == 0) {
        BotPlace pl;
        if (botGeoParse(root, pl)) {
          botCopyClean(s.place, sizeof(s.place), pl.name);
          s.lat = pl.lat;
          s.lon = pl.lon;
          s.step = (int8_t)botNetNextStep(BNK_WX, s.step, true, 0);
          _table.requeue(i);
          return;
        }
        // No match: an object without results (or with an empty list) and not an error object.
        JsonVariantConst res = root["results"];
        if (root.is<JsonObjectConst>() && root["error"].isNull() &&
            (res.isNull() || (res.is<JsonArrayConst>() && res.size() == 0))) {
          char place[31];
          botCopyClean(place, sizeof(place), s.args);   // at most 30 bytes, on a character boundary
          snprintf(_scratch, sizeof(_scratch), "no place called %s", place);
          finishOk(i, _scratch, now_ms, false);   // a negative answer is never cached
        } else {
          finishFail(i);
        }
        return;
      }
      BotWxNow w;
      if (!botWxParse(root, w)) { finishFail(i); return; }
      botWxFormat(_scratch, sizeof(_scratch), s.place, w, s.budget);
      finishOk(i, _scratch, now_ms, true);
      return;
    }
    case BNK_AURORA: {
      BotKp k;
      if (!botKpParse(root, now_unix, k)) { finishFail(i); return; }
      botAuroraFormat(_scratch, sizeof(_scratch), k, s.lat, s.budget);
      finishOk(i, _scratch, now_ms, true);
      return;
    }
    case BNK_ALERTS: {
      int total = 0;
      int n = botAlertsParse(root, now_unix, _alerts, 3, &total);
      if (n < 0) { finishFail(i); return; }
      botAlertsFormat(_scratch, sizeof(_scratch), _alerts, n, total, s.utc_offset_s, s.budget);
      finishOk(i, _scratch, now_ms, true);
      return;
    }
    case BNK_NODES: {
      bool node = _cfg && s.step >= 0 && s.step < _cfg->num_watch && _cfg->watch[s.step].kind == BW_NODE;
      BotWatchState st;
      uint32_t within = _cfg ? (uint32_t)_cfg->watch_online_min * 60u : 0;
      bool ok = botWatchParse(root, node ? BW_NODE : BW_OBSERVER, now_unix, within, st);
      nodesEntry(s, ok ? &st : nullptr, now_unix);
      nodesNext(i, now_ms);
      return;
    }
    default: {
      const BotCustomCmd* c = liveCustom(s);
      char val[BOT_FETCH_VALUE_MAX + 1];
      if (!c || !botFetchValue(root, c->path, val, sizeof(val))) { finishFail(i); return; }
      char key[BOT_CACHE_KEY_MAX + 1];
      uint32_t ttl = ttlMs(s);
      if (ttl && !_table.stale(i) && cacheKey(BNK_CUSTOM, c->trigger, s.args, key, sizeof(key)))
        _cache.put(key, val, now_ms, ttl);
      botFetchFill(_scratch, sizeof(_scratch), s.text, val);
      finishOk(i, _scratch, now_ms, false);
      return;
    }
  }
}

// ---- nodes ----

void BotNet::nodesEntry(Slot& s, const BotWatchState* st, uint32_t now_unix) {
  if (!_cfg || s.step < 0 || s.step >= _cfg->num_watch || s.step >= BOT_MAX_WATCH) return;   // list shrank meanwhile
  const BotWatchCfg& w = _cfg->watch[s.step];
  char e[64];
  if (st) {
    botWatchFormatOne(e, sizeof(e), w.label, *st, now_unix);
    s.oks++;
  } else {
    snprintf(e, sizeof(e), "%s: unavailable", w.label);
  }
  botJoinAppend(s.text, sizeof(s.text), s.text_len, s.budget, s.closed, e);
}

void BotNet::nodesNext(int i, uint32_t now_ms) {
  Slot& s = _slot[i];
  int next = botNetNextStep(BNK_NODES, s.step, true, _cfg ? _cfg->num_watch : 0);
  // No request for an entry that could not be added to the reply anyway (the reply itself gives
  // no sign of the drop; the page warns when the labels are that long).
  bool fits = next >= 0 && _cfg && next < BOT_MAX_WATCH &&
              botWatchEntryMayFit(s.text_len, s.budget, s.closed, _cfg->watch[next].label);
  if (next >= 0 && !fits) s.closed = true;
  if (next >= 0 && fits) {
    s.step = (int8_t)next;
    _table.requeue(i);
  } else if (s.oks == 0) {
    finishFail(i);   // every request failed
  } else {
    int asked = _cfg ? (_cfg->num_watch < BOT_MAX_WATCH ? _cfg->num_watch : BOT_MAX_WATCH) : 0;
    // Kept only when every entry answered and fitted: a reply with an "unavailable" entry, or one
    // that left entries out, is not cached.
    finishOk(i, s.text, now_ms, s.oks == asked && !s.closed);
  }
}

void BotNet::expire(int i, uint32_t now_ms) {
  Slot& s = _slot[i];
  if (s.kind == BNK_NODES && s.oks > 0 && !_table.stale(i)) {
    // The overall ceiling: what answered is kept; the entries still to come read "unavailable"
    // (as long as they fit: nodesEntry stops adding once one is dropped).
    int n = _cfg ? _cfg->num_watch : 0;
    for (int guard = 0; guard < BOT_MAX_WATCH && s.step < n; guard++) {
      nodesEntry(s, nullptr, 0);
      s.step++;
    }
    finishOk(i, s.text, now_ms, false);
    return;
  }
  finishFail(i);
}

// ---- finishing ----

void BotNet::finishOk(int i, const char* text, uint32_t now_ms, bool cache) {
  Slot& s = _slot[i];
  if (text != s.text) botClamp(s.text, sizeof(s.text), text, s.budget);
  s.ok = true;
  if (cache && s.kind != BNK_CUSTOM && !_table.stale(i)) {
    char key[BOT_CACHE_KEY_MAX + 1];
    if (cacheKey(s.kind, nullptr, s.args, key, sizeof(key))) _cache.put(key, s.text, now_ms, ttlMs(s));
  }
  _table.finish(i);
}

void BotNet::finishFail(int i) {
  Slot& s = _slot[i];
  failText(s.cmd, s.dest, s.text, sizeof(s.text));
  s.ok = false;
  _table.finish(i);
}

bool BotNet::take(BotNetReply& out) {
  int i = _table.nextDone();
  if (i < 0) return false;
  const Slot& s = _slot[i];
  out.ready = true;
  out.dest = s.dest;
  memcpy(out.sender, s.sender, sizeof(out.sender));
  memcpy(out.request, s.request, sizeof(out.request));
  memcpy(out.text, s.text, sizeof(out.text));
  out.ok = s.ok;
  out.source = s.kind == BNK_CUSTOM ? BRT_FOREIGN : BRT_BUILTIN;
  _table.release(i);
  return true;
}

#endif  // WITH_BOT
