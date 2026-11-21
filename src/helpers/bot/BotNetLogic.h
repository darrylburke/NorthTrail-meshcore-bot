#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <ArduinoJson.h>
#include "BotText.h"
#include "BotReplies.h"
#include "BotJsonPath.h"
#include "BotTime.h"
#include "BotLimits.h"
#include "BotOutgoing.h"   // BotDestKind
#include "BotConfig.h"     // botConfigHasChannel

// The decisions behind the bot's internet commands that do not need the device (pure C/C++
// apart from ArduinoJson, no heap of its own, host-testable): the parked-command table, the
// step order of each command, cache keys, joining watched-node entries, the {fetch} value, the
// URL form handed to the HTTP client, and the holder for a command tried from the web page.
// All times are millis() values compared by unsigned subtraction, so they survive the wrap.

#define BOT_PARK_SLOTS 2
// A request on the worker that has not answered this long after it was submitted fails that one
// step (measured on the device: one HTTPS request takes about 4 to 5 s, so a watched-nodes
// command with 4 entries needs about 16 s and must not be timed as a whole).
#define BOT_PARK_STEP_MS 15000u
// Whatever its state, a parked command is answered (or failed) this long after it was parked.
#define BOT_PARK_CEILING_MS 45000u
#define BOT_TRY_TIMEOUT_MS 50000u    // a web try still pending after this completes with "" (above the ceiling)
#define BOT_FETCH_VALUE_MAX 60       // bytes of a {fetch} value
#define BOT_FETCH_MARK '\x01'        // stands for {fetch} in a pre-rendered reply (never in config text)

// ---- parked commands ----

enum BotParkState : uint8_t { BPS_FREE, BPS_WAITING, BPS_INFLIGHT, BPS_DONE };

// Lifecycle of the parked slots. A slot is parked WAITING, goes INFLIGHT while its request is on
// the worker (remembering the request's sequence number), back to WAITING for a further step, and
// DONE when its reply is ready; release() frees it. Turns on the single worker go to the WAITING
// slot with the oldest ticket; a slot that goes back to WAITING draws a new ticket, so two
// commands alternate in arrival order. A result is attributed only to the INFLIGHT slot holding
// its sequence number, so a result that arrives after its slot expired is never given to another.
//
// Two limits, both compared by unsigned subtraction of millis() values (wrap-safe):
//   - stepExpired: an INFLIGHT slot whose request was submitted BOT_PARK_STEP_MS ago. Only that
//     step fails; a WAITING slot (waiting for its turn) never expires on this limit.
//   - ceilingExpired: a WAITING or INFLIGHT slot parked BOT_PARK_CEILING_MS ago. The whole command
//     ends (what it already has is kept by the caller).
// Each slot also remembers the generation it was parked in; newGeneration() (after the settings a
// look-up depends on change) makes every slot parked before it stale().
class BotParkTable {
public:
  BotParkTable() : _next_ticket(1), _gen(0) {
    for (int i = 0; i < BOT_PARK_SLOTS; i++) {
      _st[i] = BPS_FREE; _ticket[i] = 0; _seq[i] = 0; _parked_ms[i] = 0; _step_ms[i] = 0; _slot_gen[i] = 0;
    }
  }

  // A free slot, now WAITING, parked at now_ms in the current generation; -1 when every slot is taken.
  int park(uint32_t now_ms) {
    for (int i = 0; i < BOT_PARK_SLOTS; i++) {
      if (_st[i] != BPS_FREE) continue;
      _st[i] = BPS_WAITING;
      _ticket[i] = _next_ticket++;
      _seq[i] = 0;
      _parked_ms[i] = now_ms;
      _step_ms[i] = now_ms;
      _slot_gen[i] = _gen;
      return i;
    }
    return -1;
  }

  bool hasFree() const { for (int i = 0; i < BOT_PARK_SLOTS; i++) if (_st[i] == BPS_FREE) return true; return false; }
  int used() const { int n = 0; for (int i = 0; i < BOT_PARK_SLOTS; i++) if (_st[i] != BPS_FREE) n++; return n; }
  bool anyInflight() const { for (int i = 0; i < BOT_PARK_SLOTS; i++) if (_st[i] == BPS_INFLIGHT) return true; return false; }
  BotParkState state(int i) const { return valid(i) ? _st[i] : BPS_FREE; }

  // The WAITING slot whose turn it is (oldest ticket), or -1 (also while one is INFLIGHT).
  int nextToSubmit() const { return anyInflight() ? -1 : oldest(BPS_WAITING); }

  // WAITING -> INFLIGHT with the request's sequence number (0 is never a valid one), submitted at
  // now_ms: the per-step limit counts from here.
  void submitted(int i, uint32_t seq, uint32_t now_ms) {
    if (!valid(i) || _st[i] != BPS_WAITING || seq == 0) return;
    _st[i] = BPS_INFLIGHT;
    _seq[i] = seq;
    _step_ms[i] = now_ms;
  }

  // The INFLIGHT slot waiting for the result with this sequence number, else -1 (stale result).
  int resultFor(uint32_t seq) const {
    if (seq == 0) return -1;
    for (int i = 0; i < BOT_PARK_SLOTS; i++) if (_st[i] == BPS_INFLIGHT && _seq[i] == seq) return i;
    return -1;
  }

  // Back to WAITING for the next step, behind every slot already waiting.
  void requeue(int i) {
    if (!valid(i) || _st[i] == BPS_FREE || _st[i] == BPS_DONE) return;
    _st[i] = BPS_WAITING;
    _seq[i] = 0;
    _ticket[i] = _next_ticket++;
  }

  // The reply is ready (whatever state it was in, unless free).
  void finish(int i) {
    if (!valid(i) || _st[i] == BPS_FREE) return;
    _st[i] = BPS_DONE;
    _seq[i] = 0;
  }

  // An INFLIGHT slot whose request was submitted at least step_ms ago, else -1.
  int stepExpired(uint32_t now_ms, uint32_t step_ms = BOT_PARK_STEP_MS) const {
    for (int i = 0; i < BOT_PARK_SLOTS; i++)
      if (_st[i] == BPS_INFLIGHT && (uint32_t)(now_ms - _step_ms[i]) >= step_ms) return i;
    return -1;
  }

  // A WAITING or INFLIGHT slot parked at least ceiling_ms ago, else -1.
  int ceilingExpired(uint32_t now_ms, uint32_t ceiling_ms = BOT_PARK_CEILING_MS) const {
    for (int i = 0; i < BOT_PARK_SLOTS; i++)
      if ((_st[i] == BPS_WAITING || _st[i] == BPS_INFLIGHT) && (uint32_t)(now_ms - _parked_ms[i]) >= ceiling_ms) return i;
    return -1;
  }

  // The DONE slot parked first (by ticket order), else -1.
  int nextDone() const { return oldest(BPS_DONE); }

  void release(int i) {
    if (!valid(i)) return;
    _st[i] = BPS_FREE;
    _seq[i] = 0;
  }

  // Generations: see the class comment.
  void newGeneration() { _gen++; }
  uint32_t generation() const { return _gen; }
  uint32_t generationOf(int i) const { return valid(i) ? _slot_gen[i] : 0; }
  bool stale(int i) const { return valid(i) && _st[i] != BPS_FREE && _slot_gen[i] != _gen; }

private:
  static bool valid(int i) { return i >= 0 && i < BOT_PARK_SLOTS; }
  int oldest(BotParkState s) const {
    int best = -1;
    for (int i = 0; i < BOT_PARK_SLOTS; i++)
      if (_st[i] == s && (best < 0 || (int32_t)(_ticket[i] - _ticket[best]) < 0)) best = i;
    return best;
  }
  BotParkState _st[BOT_PARK_SLOTS];
  uint32_t _ticket[BOT_PARK_SLOTS];
  uint32_t _seq[BOT_PARK_SLOTS];
  uint32_t _parked_ms[BOT_PARK_SLOTS];
  uint32_t _step_ms[BOT_PARK_SLOTS];
  uint32_t _slot_gen[BOT_PARK_SLOTS];
  uint32_t _next_ticket;
  uint32_t _gen;
};

// ---- the same command twice ----

// The same destination: the same channel, the same contact, or the same web try.
static inline bool botDestSame(const BotReplyDest& a, const BotReplyDest& b) {
  if (a.kind != b.kind) return false;
  switch (a.kind) {
    case BD_CHANNEL: return strncmp(a.channel, b.channel, sizeof(a.channel)) == 0;
    case BD_DIRECT: return memcmp(a.key, b.key, sizeof(a.key)) == 0;
    case BD_TRY: return a.try_id == b.try_id;
    default: return false;
  }
}

// A command that is already parked: the same kind and name (as configured), the same arguments
// (ASCII case ignored, as in the cache key), the same destination and the same settings
// generation. Such a repeat is not parked again; the first one's reply serves. Null is "".
static inline bool botParkSameCommand(uint8_t kind_a, const char* cmd_a, const char* args_a, const BotReplyDest& dest_a,
                                      uint32_t gen_a, uint8_t kind_b, const char* cmd_b, const char* args_b,
                                      const BotReplyDest& dest_b, uint32_t gen_b) {
  if (kind_a != kind_b || gen_a != gen_b || !botDestSame(dest_a, dest_b)) return false;
  if (strcmp(cmd_a ? cmd_a : "", cmd_b ? cmd_b : "") != 0) return false;
  const char* x = args_a ? args_a : "";
  const char* y = args_b ? args_b : "";
  for (;; x++, y++) {
    char cx = (*x >= 'A' && *x <= 'Z') ? (char)(*x + 32) : *x;
    char cy = (*y >= 'A' && *y <= 'Z') ? (char)(*y + 32) : *y;
    if (cx != cy) return false;
    if (!cx) return true;
  }
}

// ---- steps ----

enum BotNetKind : uint8_t { BNK_WX, BNK_AURORA, BNK_ALERTS, BNK_NODES, BNK_CUSTOM };

// Step numbers: wx 0 = place lookup, 1 = weather; nodes 0..n-1 = one per watched entry; the
// others have only step 0. wx without a place starts at the weather.
static inline int botNetFirstStep(uint8_t kind, bool has_place) {
  return (kind == BNK_WX && !has_place) ? 1 : 0;
}

// The step after `step` finished (ok or not), or -1 when the command is complete. n_watch is the
// number of watched entries (at most 4 are asked). A failed place lookup ends wx; a failed node
// entry does not end nodes.
static inline int botNetNextStep(uint8_t kind, int step, bool ok, int n_watch) {
  if (kind == BNK_WX) return (step == 0 && ok) ? 1 : -1;
  if (kind == BNK_NODES) {
    int n = n_watch < 0 ? 0 : (n_watch > 4 ? 4 : n_watch);
    return step + 1 < n ? step + 1 : -1;
  }
  return -1;
}

// ---- cache keys ----

// "<name>|<args lower-cased (ASCII)>". False (dst "") when it does not fit cap-1 or max_len
// bytes: a long argument is not cached rather than cut, so two different arguments never share
// an entry. Null name or args is "".
static inline bool botNetCacheKey(char* dst, size_t cap, size_t max_len, const char* name, const char* args) {
  if (!dst || cap == 0) return false;
  dst[0] = '\0';
  if (!name) name = "";
  if (!args) args = "";
  size_t nl = strlen(name), al = strlen(args);
  size_t total = nl + 1 + al;
  if (total > cap - 1 || total > max_len) return false;
  memcpy(dst, name, nl);
  dst[nl] = '|';
  for (size_t i = 0; i < al; i++) {
    char c = args[i];
    dst[nl + 1 + i] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
  }
  dst[total] = '\0';
  return true;
}

// ---- joining watched-node entries ----

// Appends entry to dst (length n so far) after " | " when the whole stays within limit (and
// cap-1). Once an entry does not fit, *closed is set and every later entry is ignored, so entries
// are dropped from the end and nothing marks the drop. The first entry is cut with botClamp when
// even it does not fit, so there is always some text. Returns true when the entry was added.
static inline bool botJoinAppend(char* dst, size_t cap, size_t& n, size_t limit, bool& closed, const char* entry) {
  if (!dst || cap == 0 || closed) return false;
  if (limit > cap - 1) limit = cap - 1;
  if (!entry) entry = "";
  size_t el = strlen(entry);
  size_t sep = n > 0 ? 3 : 0;
  if (n + sep + el <= limit) {
    if (sep) memcpy(dst + n, " | ", 3);
    memcpy(dst + n + sep, entry, el);
    n += sep + el;
    dst[n] = '\0';
    return true;
  }
  if (n == 0) n = botClamp(dst, cap, entry, limit);
  closed = true;
  return false;
}

// Whether the next watched-node entry, labelled label, can still be added to a reply of n bytes
// (limit in all) by botJoinAppend: not once an entry was dropped (closed), and not when even its
// shortest form ("<label>: online") and the separator would not fit. The first entry always can
// (botJoinAppend cuts it so the reply has some text). Further requests are not made otherwise.
static inline bool botWatchEntryMayFit(size_t n, size_t limit, bool closed, const char* label) {
  if (closed) return false;
  if (n == 0) return true;
  size_t shortest = (sizeof(" | ") - 1) + strlen(label ? label : "") + (sizeof(": online") - 1);
  return n + shortest <= limit;
}

// ---- {fetch} ----

// The value at path in root for a custom command's {fetch}: a string as it is, a number or
// boolean as botJsonPathGet writes it; control characters become spaces and the result is cut to
// BOT_FETCH_VALUE_MAX bytes (and cap-1) on a UTF-8 boundary. False (out "") for bad syntax, a
// missing value, null, an object or an array.
static inline bool botFetchValue(JsonVariantConst root, const char* path, char* out, size_t cap) {
  if (!out || cap == 0) return false;
  out[0] = '\0';
  JsonVariantConst v;
  if (!botJsonPathResolve(path, true, root, &v)) return false;
  size_t lim = cap - 1 < BOT_FETCH_VALUE_MAX ? cap - 1 : BOT_FETCH_VALUE_MAX;
  if (v.is<const char*>()) {
    const char* s = v.as<const char*>();
    if (!s) return false;
    botCopyClean(out, lim + 1, s);
    return true;
  }
  char num[72];
  if (!botJsonPathGet(root, path, num, sizeof num)) return false;
  botCopyClean(out, lim + 1, num);
  return true;
}

// Copies pre into dst with every BOT_FETCH_MARK replaced by value (BotOut rules: bounded by cap,
// cut on a UTF-8 boundary). Returns the bytes written.
static inline size_t botFetchFill(char* dst, size_t cap, const char* pre, const char* value) {
  BotOut o = botOutInit(dst, cap);
  if (!pre) return o.n;
  for (const char* p = pre; *p && !o.full;) {
    if (*p == BOT_FETCH_MARK) { botOutAppend(o, value); p++; continue; }
    const char* e = strchr(p, BOT_FETCH_MARK);
    size_t run = e ? (size_t)(e - p) : strlen(p);
    botOutAppendN(o, p, run);
    p += run;
  }
  return o.n;
}

// ---- URL form for the HTTP client ----

// The URL as the request is made: http:// or https:// only; a "/" is put after the host (and
// port) when the path is missing, so "https://h?q=1" becomes "https://h/?q=1" and the HTTP client
// can never read the query as part of the host; a "#fragment" is dropped. False (dst "") for any
// other scheme, an empty host or a result that does not fit cap-1.
static inline bool botFetchNormalizeUrl(char* dst, size_t cap, const char* url) {
  if (!dst || cap == 0) return false;
  dst[0] = '\0';
  if (!url) return false;
  size_t p;
  if (strncmp(url, "https://", 8) == 0) p = 8;
  else if (strncmp(url, "http://", 7) == 0) p = 7;
  else return false;
  size_t end = strcspn(url, "#");
  size_t a = p;
  while (a < end && url[a] != '/' && url[a] != '?') a++;
  if (a == p) return false;
  bool add_slash = a >= end || url[a] != '/';
  size_t need = end + (add_slash ? 1 : 0);
  if (need > cap - 1) return false;
  memcpy(dst, url, a);
  size_t n = a;
  if (add_slash) dst[n++] = '/';
  memcpy(dst + n, url + a, end - a);
  n += end - a;
  dst[n] = '\0';
  return true;
}

// ---- local time ----

// Seconds east of UTC for the local broken-down time of now_unix (as localtime_r gives it).
static inline int32_t botUtcOffset(const struct tm& local, uint32_t now_unix) {
  int64_t t = botDaysFromCivil(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday) * 86400 +
              (int64_t)local.tm_hour * 3600 + local.tm_min * 60 + local.tm_sec;
  int64_t d = t - (int64_t)now_unix;
  if (d > 86400 || d < -86400) return 0;   // not a time zone: ignore
  return (int32_t)d;
}

// ---- reply routing and the per-minute cap ----

// What BotNet::start / startCustom did with an internet command.
enum BotNetStart : uint8_t {
  BNS_REPLY,    // answered at once (cache, or nothing to look up): the body is in out
  BNS_PARKED,   // parked; the reply comes from take()
  BNS_BUSY,     // 2 commands already waiting: nothing parked, nothing in out
  BNS_DUPLICATE,// the same command for the same destination is already parked: dropped, its reply serves
};

// Before an internet command is started: a channel or direct command the cap would refuse is not
// looked up (and nothing is recorded); a web try is never limited.
static inline bool botNetMayStart(uint8_t dest_kind, const BotRateCap& cap, uint32_t now_ms, uint8_t max_per_min) {
  return dest_kind == BD_TRY || cap.wouldAllow(now_ms, max_per_min);
}

enum BotNetAfter : uint8_t {
  BNA_PARKED,        // nothing more now; the cap was counted (unless a try)
  BNA_DELIVER,       // deliver the reply in out, counting the cap at delivery
  BNA_DELIVER_BUSY,  // deliver "busy, try again" (direct message or try), counting the cap
  BNA_SILENT,        // deliver nothing (busy on a channel, or a repeat): the cap is not counted
};

// After start: a parked command is the reply about to be sent, so it takes its place under the
// cap now (once); its result is later delivered with count_cap false.
static inline uint8_t botNetAfterStart(uint8_t dest_kind, uint8_t start, BotRateCap& cap, uint32_t now_ms,
                                       uint8_t max_per_min) {
  if (start == BNS_PARKED) {
    if (dest_kind != BD_TRY) cap.allow(now_ms, max_per_min);
    return BNA_PARKED;
  }
  if (start == BNS_BUSY) return dest_kind == BD_CHANNEL ? BNA_SILENT : BNA_DELIVER_BUSY;
  if (start == BNS_DUPLICATE) return BNA_SILENT;   // dropped silently, wherever it came from
  return BNA_DELIVER;
}

enum BotDeliverRoute : uint8_t {
  BDR_HOLDER,   // a web try: to the try holder, never queued or transmitted
  BDR_QUEUE,    // into the delayed-send ring
  BDR_DROP,     // nothing to send, the ring is full, or the cap refuses: logged with no reply
};

// Whether the bot still serves a reply's destination when it is delivered: a reply that waited (a
// finished internet look-up, a broker's reply) is not sent to a channel the owner removed from the
// bot's channel list meanwhile. A direct message or a try is always served.
static inline bool botReplyDestServed(const BotReplyDest& d, const BotConfig& c) {
  return d.kind != BD_CHANNEL || botConfigHasChannel(c, d.channel);
}

// Where a finished reply goes. The cap is consulted (and counted) only for a channel or direct
// reply that has text, fits the ring, goes somewhere still served (botReplyDestServed) and was not
// already counted when it was parked. *capped (when given) is set only when it is the cap that
// refused the reply.
static inline uint8_t botDeliverRoute(uint8_t dest_kind, bool has_text, bool ring_full, bool count_cap,
                                      BotRateCap& cap, uint32_t now_ms, uint8_t max_per_min, bool* capped = nullptr,
                                      bool dest_served = true) {
  if (capped) *capped = false;
  if (dest_kind == BD_TRY) return BDR_HOLDER;
  if (dest_kind != BD_CHANNEL && dest_kind != BD_DIRECT) return BDR_DROP;
  if (!has_text || ring_full || !dest_served) return BDR_DROP;
  if (count_cap && !cap.allow(now_ms, max_per_min)) {
    if (capped) *capped = true;
    return BDR_DROP;
  }
  return BDR_QUEUE;
}

// ---- unknown commands answered by the MQTT broker ----

// Where a reply comes from, for the per-minute cap: a local reply and a broker's reply are counted
// when they are delivered (a broker's request costs nothing, so commands nobody answers never use
// the cap); an internet command was counted when it was parked.
enum BotReplyOrigin : uint8_t { BRO_LOCAL, BRO_NET, BRO_BROKER };
static inline bool botCapAtDelivery(uint8_t origin) { return origin != BRO_NET; }

// Whether an unknown command is put to the broker: only while the client is ready and, as for an
// internet command, not when the cap would refuse the reply anyway (a web try is never limited).
// Nothing is counted here.
static inline bool botBrokerMayAsk(uint8_t dest_kind, bool ready, const BotRateCap& cap, uint32_t now_ms,
                                   uint8_t max_per_min) {
  return ready && botNetMayStart(dest_kind, cap, now_ms, max_per_min);
}

// ---- trying a command from the web page ----

// One try at a time. begin() starts one (false while another is pending; a finished result that
// was not taken is dropped); complete() ends it with the reply (only the pending try with that
// id; "" means the bot would stay silent; broker says the reply came from the MQTT broker, and is
// kept only with a reply); tick() completes it with "" once BOT_TRY_TIMEOUT_MS have passed;
// take() is false until it is done, then true once (with the broker flag when asked for) and the
// holder is empty again.
class BotTryHolder {
public:
  BotTryHolder() : _id(0), _start_ms(0), _pending(false), _done(false), _broker(false) { _text[0] = '\0'; }

  bool pending() const { return _pending; }

  bool begin(uint32_t id, uint32_t now_ms) {
    if (_pending) return false;
    _id = id;
    _start_ms = now_ms;
    _pending = true;
    _done = false;
    _broker = false;
    _text[0] = '\0';
    return true;
  }

  bool complete(uint32_t id, const char* text, bool broker = false) {
    if (!_pending || id != _id) return false;
    botClamp(_text, sizeof _text, text, sizeof _text - 1);
    _broker = broker && _text[0] != '\0';
    _pending = false;
    _done = true;
    return true;
  }

  void tick(uint32_t now_ms) {
    if (_pending && (uint32_t)(now_ms - _start_ms) >= BOT_TRY_TIMEOUT_MS) complete(_id, "");
  }

  bool take(uint32_t& id, char* out, size_t cap, bool* broker = nullptr) {
    if (!_done) return false;
    id = _id;
    if (out && cap > 0) botClamp(out, cap, _text, cap - 1);
    if (broker) *broker = _broker;
    _done = false;
    _broker = false;
    _text[0] = '\0';
    return true;
  }

private:
  uint32_t _id;
  uint32_t _start_ms;
  bool _pending, _done, _broker;
  char _text[128];
};
