#ifdef WITH_BOT

#include "BotTask.h"
#include "BotStore.h"
#include "BotWipe.h"

#include <WiFi.h>
#include <esp_sntp.h>
#include <time.h>
#include "BotPassword.h"
#include <helpers/AdvertDataHelpers.h>
#include <helpers/TxtDataHelpers.h>
#include <helpers/bot/BotText.h>
#include <helpers/bot/BotTemplate.h>
#include <helpers/bot/BotGeo.h>
#include <helpers/bot/BotStatusLine.h>

static_assert(BOT_KEY_BYTES == PUB_KEY_SIZE, "queued replies keep a whole public key");

static const uint32_t BOT_MIN_VALID_EPOCH = 1700000000u;
static const uint32_t BOT_RTC_RESYNC_MS = 6u * 3600u * 1000u;
static const uint32_t BOT_FIRST_ADVERT_MS = 60000u;

// Fixed one-liners for "help <cmd>", by BotBuiltin id.
static const char* const BOT_BUILTIN_HELP[] = {
  "hops, signal and route of your message", // ping
  "path, SNR and RSSI of your message",    // test
  "repeaters your message came through",   // path
  "list commands",                         // help
  "nodes heard in the last 24 h",          // heard
  "traffic summary for the last hour",     // mesh
  "how far and which way you are from me", // dist
  "sunrise, sunset and moon phase here",   // sun
  "roll dice, e.g. roll 2d6",              // roll
  "ask the magic 8-ball",                  // 8ball
  "weather here, or wx <place>",           // wx
  "Kp index and northern lights outlook",  // aurora
  "active weather alerts here",            // alerts
  "status of watched nodes",               // nodes
};
static_assert(sizeof(BOT_BUILTIN_HELP) / sizeof(BOT_BUILTIN_HELP[0]) == BB_COUNT,
              "one help line per BotBuiltin, in BotBuiltin order");

static bool botIsNetBuiltin(int b) { return b == BB_WX || b == BB_AURORA || b == BB_ALERTS || b == BB_NODES; }

// The reply formatters take a plain function pointer, so the RNG lives here.
static mesh::RNG* s_bot_rng = nullptr;
static uint32_t botRnd(uint32_t max_exclusive) {
  if (max_exclusive == 0 || !s_bot_rng) return 0;
  return s_bot_rng->nextInt(0, max_exclusive);
}

static void botHex(char* dst, const uint8_t* src, size_t n) {
  static const char digits[] = "0123456789abcdef";
  for (size_t i = 0; i < n; i++) { dst[2 * i] = digits[src[i] >> 4]; dst[2 * i + 1] = digits[src[i] & 15]; }
  dst[2 * n] = '\0';
}

BotTask::BotTask(MyMesh& mesh, mesh::RTCClock& rtc, fs::FS& fs)
    : _mesh(mesh), _rtc(rtc), _fs(fs), _log_head(0), _log_count(0), _uptime_ms(0), _last_millis(0),
      _ntp_started(false), _ntp_synced(false), _clock_valid(false), _last_rtc_sync_ms(0), _last_clock_check_ms(0),
      _first_advert_done(false), _last_zero_ms(0), _last_flood_ms(0), _adv_req_zero(false), _adv_req_flood(false),
      _dm_seen_next(0) {
  botConfigDefaults(_cfg);
  memset(_log, 0, sizeof(_log));
  memset(_dm_seen, 0, sizeof(_dm_seen));
}

void BotTask::begin() {
  s_bot_rng = _mesh.getRNG();
  _last_millis = millis();
  _uptime_ms = _last_millis;
  _last_zero_ms = _last_flood_ms = _last_millis;   // advert intervals count from boot
  botStoreLoad(_fs, _cfg);   // on failure _cfg holds defaults and the file is left alone
  applyTz();
  // The worker's 16 KB body buffer and task are made once, here; without them every internet
  // command answers "unavailable".
  _net.begin(_fetch.begin() ? &_fetch : nullptr);
  // The broker client starts from loop() once WiFi is up (and only when it is enabled).
  _mqtt.begin(_mesh.self_id.pub_key);
  _mqtt.applySettings(_cfg, _mesh.getNodeName());
  // Once the bot has been set up (a web password exists), its scope and location are the
  // node's: undo any drift (e.g. the app changed the default scope). Never on a fresh node.
  if (hasWebPassword()) {
    const NodePrefs* np = _mesh.getNodePrefs();
    if (strcmp(np->default_scope_name, _cfg.scope) != 0 || np->node_lat != _cfg.lat || np->node_lon != _cfg.lon)
      _mesh.botApplyNodeSettings(nullptr, _cfg.scope, _cfg.lat, _cfg.lon);
  }
}

void BotTask::loop() {
  uint32_t m = millis();
  _uptime_ms += (uint32_t)(m - _last_millis);
  _last_millis = m;
  loopClock();
  loopAdverts();
  loopNet();
  loopMqtt();
  loopReplies();
}

uint32_t BotTask::uptimeSeconds() const {
  return (uint32_t)((_uptime_ms + (uint32_t)(millis() - _last_millis)) / 1000u);
}

// ---- settings ----

// After a failed save the new values stay live (the bot keeps running consistently on them)
// and false is returned; they are lost at the next reboot unless a later save succeeds.

bool BotTask::applyConfig(const BotConfig& next) {
  bool tz_changed = strcmp(next.tz, _cfg.tz) != 0;
  // Cached internet answers depend on the location, the watch list and the custom look-ups.
  bool net_changed = next.lat != _cfg.lat || next.lon != _cfg.lon || next.num_watch != _cfg.num_watch ||
                     memcmp(next.watch, _cfg.watch, sizeof(_cfg.watch)) != 0 ||
                     next.watch_online_min != _cfg.watch_online_min || next.num_custom != _cfg.num_custom ||
                     memcmp(next.custom, _cfg.custom, sizeof(_cfg.custom)) != 0;
  if (&next != &_cfg) memcpy(&_cfg, &next, sizeof(_cfg));
  if (net_changed) _net.clearCache();
  _mqtt.applySettings(_cfg, _mesh.getNodeName());   // restarts the client only when the broker settings changed
  bool saved = botStoreSave(_fs, _cfg);
  bool node_saved = _mesh.botApplyNodeSettings(nullptr, _cfg.scope, _cfg.lat, _cfg.lon);
  if (tz_changed) applyTz();
  return saved && node_saved;
}

bool BotTask::applyNodeName(const char* name) {
  if (!name || !name[0] || !AdvertDataParser::isValidName(name)) return false;   // nothing changed
  bool saved = _mesh.botApplyNodeSettings(name, _cfg.scope, _cfg.lat, _cfg.lon);
  _mqtt.applySettings(_cfg, _mesh.getNodeName());   // the default prefix follows the name
  return saved;
}

bool BotTask::setWebPassword(const char* plain) {
  if (!plain) return false;
  size_t n = strlen(plain);
  if (n < 8 || n > 63) return false;   // nothing changed
  uint8_t salt[8];
  _mesh.getRNG()->random(salt, sizeof(salt));
  char hash_hex[65];
  if (!botPasswordHash(plain, salt, hash_hex)) return false;
  botHex(_cfg.web_salt, salt, sizeof(salt));
  memcpy(_cfg.web_hash, hash_hex, sizeof(_cfg.web_hash));
  botWipe(hash_hex, sizeof(hash_hex));
  _pw_epoch++;   // the new password is live from here, saved or not
  return botStoreSave(_fs, _cfg);
}

bool BotTask::checkWebPassword(const char* plain) const {
  if (!hasWebPassword()) return false;
  return botPasswordVerify(plain, _cfg.web_salt, _cfg.web_hash);
}

void BotTask::applyTz() {
  const char* posix = botTzToPosix(_cfg.tz);
  if (!posix) posix = "UTC0";
  if (_ntp_started) {
    configTzTime(posix, "pool.ntp.org");   // also sets TZ
  } else {
    setenv("TZ", posix, 1);
    tzset();
  }
}

// ---- clock and adverts ----

void BotTask::loopClock() {
  uint32_t now = millis();
  if (now - _last_clock_check_ms < 1000u) return;
  _last_clock_check_ms = now;
  if (!_ntp_started) {
    if (WiFi.status() != WL_CONNECTED) return;
    const char* posix = botTzToPosix(_cfg.tz);
    configTzTime(posix ? posix : "UTC0", "pool.ntp.org");   // starts SNTP in the background
    _ntp_started = true;
    return;
  }
  // The libc clock starts from a built-in placeholder date well past BOT_MIN_VALID_EPOCH, so
  // only a completed SNTP sync makes it trustworthy. The status reads COMPLETED once per sync.
  if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) _ntp_synced = true;
  time_t t = time(nullptr);
  if (!_clock_valid) {
    if (_ntp_synced && (uint32_t)t > BOT_MIN_VALID_EPOCH) {
      _rtc.setCurrentTime((uint32_t)t);
      _clock_valid = true;
      _last_rtc_sync_ms = now;
    }
  } else if (now - _last_rtc_sync_ms >= BOT_RTC_RESYNC_MS) {
    _rtc.setCurrentTime((uint32_t)t);
    _last_rtc_sync_ms = now;
  }
}

void BotTask::requestAdvert(bool flood) {
  if (flood) _adv_req_flood = true; else _adv_req_zero = true;
}

void BotTask::loopAdverts() {
  uint32_t now = millis();
  bool flood = _adv_req_flood.exchange(false);
  bool zero = _adv_req_zero.exchange(false);
  uint32_t zero_ms = (uint32_t)_cfg.advert_zero_hop_min * 60000u;
  uint32_t flood_ms = (uint32_t)_cfg.advert_flood_min * 60000u;
  // The boot advert is zero-hop only, so a reboot loop cannot flood the mesh; the flood
  // advert follows its own interval counted from boot.
  if (!_first_advert_done) {
    if (uptimeSeconds() >= BOT_FIRST_ADVERT_MS / 1000u) {
      _first_advert_done = true;
      if (zero_ms) zero = true;
      _last_zero_ms = now;
    }
  } else if (zero_ms && now - _last_zero_ms >= zero_ms) {
    zero = true;
  }
  if (flood_ms && now - _last_flood_ms >= flood_ms) flood = true;
  // A flood advert also reaches the zero-hop neighbours, so it restarts both timers.
  if (flood) {
    _mesh.botAdvert(true);
    _last_flood_ms = _last_zero_ms = now;
  } else if (zero) {
    _mesh.botAdvert(false);
    _last_zero_ms = now;
  }
}

// ---- tables ----

void BotTask::noteRawPacket(const uint8_t raw[], int len) {
  if (!raw || len < 2) return;
  uint8_t route = raw[0] & PH_ROUTE_MASK;
  int i = (route == ROUTE_TYPE_TRANSPORT_FLOOD || route == ROUTE_TYPE_TRANSPORT_DIRECT) ? 5 : 1;
  if (i >= len) return;
  bool flood = route == ROUTE_TYPE_FLOOD || route == ROUTE_TYPE_TRANSPORT_FLOOD;
  _stats.notePacket(millis(), flood ? (uint8_t)(raw[i] & 63) : 0);   // direct: path is not the route taken
}

void BotTask::onDiscoveredContact(ContactInfo& contact, bool is_new, uint8_t path_len, const uint8_t* path) {
  uint8_t hops = (path && mesh::Packet::isValidPathLen(path_len)) ? (uint8_t)(path_len & 63) : 0;
  // Runs inside packet handling, so the radio's last SNR belongs to this advert.
  _heard.note(contact.id.pub_key, contact.name, uptimeSeconds(), hops, radio_driver.getLastSNR());
}

void BotTask::addLog(const char* sender, const char* request, const char* reply) {
  BotLogEntry& e = _log[_log_head];
  e.when = _clock_valid ? (uint32_t)time(nullptr) : 0;
  botClamp(e.sender, sizeof(e.sender), sender, sizeof(e.sender) - 1);
  botClamp(e.request, sizeof(e.request), request, sizeof(e.request) - 1);
  botClamp(e.reply, sizeof(e.reply), reply, sizeof(e.reply) - 1);
  _log_head = (_log_head + 1) % LOG_SIZE;
  if (_log_count < LOG_SIZE) _log_count++;
}

int BotTask::recent(BotLogEntry* out, int max) const {
  if (!out || max <= 0) return 0;
  int n = max < _log_count ? max : _log_count;
  for (int i = 0; i < n; i++) out[i] = _log[(_log_head - 1 - i + LOG_SIZE) % LOG_SIZE];
  return n;
}

const BotLogEntry* BotTask::recentAt(int i) const {
  if (i < 0 || i >= _log_count) return nullptr;
  return &_log[(_log_head - 1 - i + LOG_SIZE) % LOG_SIZE];
}

// The line's layout and its bound are in BotStatusLine.h (host-tested).
void BotTask::formatStatus(char* dst, size_t cap) const {
  if (!dst || cap == 0) return;
  bool up = WiFi.status() == WL_CONNECTED;
  char ip[16] = "";
  if (up) {
    IPAddress a = WiFi.localIP();
    snprintf(ip, sizeof(ip), "%u.%u.%u.%u", a[0], a[1], a[2], a[3]);
  }
  char net[8];
  if (_net.lastOutcome() == BFO_HTTP) snprintf(net, sizeof(net), "h%d", _net.lastHttpStatus());
  else snprintf(net, sizeof(net), "%s", BotFetch::outcomeCode(_net.lastOutcome()));
  const char* chans[BOT_MAX_CHANNELS];
  int nc = 0;
  for (int i = 0; i < _cfg.num_channels && i < BOT_MAX_CHANNELS; i++) chans[nc++] = _cfg.channels[i];
  BotMqttStatus ms;
  _mqtt.status(ms);   // the state's words only: never the host, user name or password
  BotStatusLine s;
  s.busy = _net.fetchBusy();
  s.parked = (unsigned)(_net.parked() < 0 ? 0 : _net.parked());
  s.net = net;
  s.ms = _net.lastMs(); s.dns_ms = _net.lastDnsMs(); s.conn_ms = _net.lastConnMs(); s.xchg_ms = _net.lastXchgMs();
  s.heap = ESP.getFreeHeap(); s.min_heap = ESP.getMinFreeHeap(); s.max_block = ESP.getMaxAllocHeap();
  s.stk = _net.workerStackFree();
  s.mq = botMqttDetailCode(ms.detail);
  s.mq_stk = ms.stack_free;
  s.scope = _cfg.scope;
  s.ip = ip;
  s.clock = _clock_valid;
  s.chans = chans;
  s.nchans = nc;
  botFormatStatus(dst, cap, s);
}

void BotTask::mqttStatus(BotMqttStatus& out) const {
  _mqtt.status(out);
  out.capped = _mqtt_capped;
}

// ---- message handling ----

void BotTask::onChannelMessageRecv(mesh::Packet* pkt, ChannelDetails& channel_details, const char* text) {
  if (!botConfigHasChannel(_cfg, channel_details.name)) return;
  char sender[32];
  const char* body = nullptr;
  if (!botSplitSender(text, sender, sizeof(sender), &body) || !sender[0]) return;
  handle(pkt, sender, body, nullptr, channel_details.name);
}

void BotTask::onMessageRecv(mesh::Packet* pkt, const ContactInfo& from, uint8_t txt_type, uint32_t sender_timestamp,
                            const char* text) {
  if (txt_type != TXT_TYPE_PLAIN || !from.name[0]) return;
  // A retry after a lost ACK is a new packet with the same sender timestamp: answer once.
  for (int i = 0; i < DM_SEEN; i++)
    if (_dm_seen[i].ts == sender_timestamp && memcmp(_dm_seen[i].key, from.id.pub_key, 4) == 0) return;
  memcpy(_dm_seen[_dm_seen_next].key, from.id.pub_key, 4);
  _dm_seen[_dm_seen_next].ts = sender_timestamp;
  _dm_seen_next = (_dm_seen_next + 1) % DM_SEEN;
  handle(pkt, from.name, text, &from, nullptr);
}

void BotTask::handle(mesh::Packet* pkt, const char* raw_sender, const char* body, const ContactInfo* dm_from,
                     const char* channel) {
  bool is_dm = dm_from != nullptr;
  if (botIsIgnored(raw_sender, _mesh.getNodeName(), _cfg.ignore)) return;
  BotParsed p;
  if (!resolve(body, is_dm, p)) return;
  uint32_t now_ms = millis();
  if (!_cooldown.allow(raw_sender, now_ms, (uint32_t)_cfg.cooldown_s * 1000u)) return;
  // The name as it may be echoed on air: no control characters.
  char sender[32];
  botStripControls(sender, sizeof(sender), raw_sender);

  BotMsgInfo m;
  m.sender = sender;
  m.direct = !pkt->isRouteFlood();
  m.hops = m.direct ? 0 : pkt->getPathHashCount();
  m.hash_size = pkt->getPathHashSize();
  m.path = pkt->path;
  m.snr = pkt->getSNR();
  m.rssi = _mesh.botLastRssi();
  BotReplyDest d;
  memset(&d, 0, sizeof(d));
  d.kind = is_dm ? BD_DIRECT : BD_CHANNEL;
  if (is_dm) memcpy(d.key, dm_from->id.pub_key, sizeof(d.key));
  else StrHelper::strncpy(d.channel, channel, sizeof(d.channel));   // the channel the command came on
  process(p, m, d, sender, body, dm_from, channel, now_ms);
}

// What a message asks for, if anything. A text rule is tried first: the message then stands for
// the command line the owner gave it, exactly as if that had been sent with the prefix (so it may
// also go to the broker). Otherwise the message is read as a command. One of the bot's own is
// taken only in a form the owner listed for it (botFormTaken). Any other word goes on when it
// was written with the prefix (the broker may know it), or without it when a program on the
// broker has announced it and the owner allows those bare, or in a direct message (as before).
bool BotTask::resolve(const char* body, bool is_dm, BotParsed& p) {
  if (botMatchFind(_cfg.match, _cfg.num_match, body, _line, sizeof(_line)) >= 0) {
    if (!botParse(_line, _cfg.prefix, true, p)) return false;
    p.prefixed = true;
    return true;
  }
  if (!botParse(body, _cfg.prefix, true, p)) return false;
  for (int i = 0; i < BB_COUNT; i++)
    if (_cfg.builtin[i].enabled && strcmp(_cfg.builtin[i].name, p.cmd) == 0)
      return botFormTaken(p.prefixed, _cfg.builtin[i].pfx, _cfg.builtin[i].bare, is_dm);
  for (int i = 0; i < _cfg.num_custom && i < BOT_MAX_CUSTOM; i++)
    if (_cfg.custom[i].enabled && strcmp(_cfg.custom[i].trigger, p.cmd) == 0)
      return botFormTaken(p.prefixed, _cfg.custom[i].pfx, _cfg.custom[i].bare, is_dm);
  if (p.prefixed) return true;
  if (_cfg.bare && _mqtt.ready() && _mqtt.commands().find(p.cmd) != nullptr) {
    p.prefixed = true;
    return true;
  }
  return is_dm;
}

bool BotTask::tryCommand(const char* text, uint32_t id) {
  uint32_t now_ms = millis();
  if (!text || !_try.begin(id, now_ms)) return false;   // one at a time
  BotParsed p;
  if (!resolve(text, true, p)) {
    _try.complete(id, "");   // not a command: the bot would stay silent
    return true;
  }
  static const char* const WEB = "web";
  BotMsgInfo m;
  m.sender = WEB;
  m.direct = true;   // no path: as a direct message
  m.hops = 0;
  m.hash_size = 1;
  m.path = nullptr;
  m.snr = 0;
  m.rssi = 0;
  BotReplyDest d;
  memset(&d, 0, sizeof(d));
  d.kind = BD_TRY;
  d.try_id = id;
  process(p, m, d, WEB, text, nullptr, nullptr, now_ms);
  return true;
}

void BotTask::process(const BotParsed& p, const BotMsgInfo& m, const BotReplyDest& dest, const char* sender,
                      const char* body, const ContactInfo* dm_from, const char* channel, uint32_t now_ms) {
  bool is_dm = dest.kind != BD_CHANNEL;   // a try runs as a direct message
  int builtin = -1, custom = -1;
  for (int i = 0; i < BB_COUNT && builtin < 0; i++)
    if (_cfg.builtin[i].enabled && strcmp(_cfg.builtin[i].name, p.cmd) == 0) builtin = i;
  for (int i = 0; builtin < 0 && custom < 0 && i < _cfg.num_custom && i < BOT_MAX_CUSTOM; i++)
    if (_cfg.custom[i].enabled && strcmp(_cfg.custom[i].trigger, p.cmd) == 0) custom = i;

  size_t max_bytes = botReplyBudget(_mesh.getNodeName());
  _built[0] = '\0';
  bool net = (builtin >= 0 && botIsNetBuiltin(builtin)) || (custom >= 0 && _cfg.custom[custom].url[0]);
  if (net) {
    // The cap rules live in BotNetLogic.h (host-tested): a command the cap would refuse is not
    // looked up; a parked one counts now, once; busy is silent on a channel.
    if (!botNetMayStart(dest.kind, _cap, now_ms, _cfg.max_per_min)) {
      deliver(dest, "", BRT_BUILTIN, sender, body, now_ms, false);
      return;
    }
    BotNetStart r = startNet(builtin, custom, p, m, dest, sender, body, channel, max_bytes, now_ms);
    switch (botNetAfterStart(dest.kind, r, _cap, now_ms, _cfg.max_per_min)) {
      case BNA_PARKED: return;
      case BNA_SILENT: deliver(dest, "", BRT_BUILTIN, sender, body, now_ms, false); return;
      case BNA_DELIVER_BUSY: StrHelper::strncpy(_built, "busy, try again", sizeof(_built)); break;
      default: break;
    }
  } else if (builtin >= 0 || custom >= 0) {
    buildReply(builtin, custom, p, m, is_dm, dm_from, channel, _built, sizeof(_built), max_bytes);
  } else if (askBroker(p, m, dest, sender, body, dm_from, channel, max_bytes, now_ms)) {
    return;   // waiting for the broker: its reply, or nothing, finishes the command later
  }
  // A custom command's reply is its template filled in (a stranger's {args} among it): never taken
  // as the bot's own mention.
  bool capped = false;
  deliver(dest, _built, custom >= 0 ? BRT_FOREIGN : BRT_BUILTIN, sender, body, now_ms, botCapAtDelivery(BRO_LOCAL), &capped);
  // A ping or path whose route did not fit one message, built while nothing else was queued: the rest follows, BOT_PART_GAP_MS apart, each
  // message counted against the cap and the remainder dropped once one is refused. (A try has
  // no route.)
  if ((builtin == BB_PING || builtin == BB_PATH) && dest.kind != BD_TRY) {
    uint8_t parts = _ping_parts;
    for (uint8_t k = 1; k < parts && !capped; k++) {
      botFmtRoutePart(_built, sizeof(_built), m, lookupPathName, this, max_bytes, k, _ping_max, nullptr, builtin == BB_PATH);
      deliver(dest, _built, BRT_BUILTIN, sender, body, now_ms, botCapAtDelivery(BRO_LOCAL), &capped, false,
              (uint32_t)k * BOT_PART_GAP_MS);
    }
  }
}

void BotTask::deliver(const BotReplyDest& dest, const char* body_text, uint8_t source, const char* sender, const char* request,
                      uint32_t now_ms, bool count_cap, bool* capped, bool broker, uint32_t extra_delay_ms) {
  size_t max_bytes = botReplyBudget(_mesh.getNodeName());
  // The one place every reply passes: it begins "@[", so no other bot takes it as a command.
  botFinishReply(_reply, sizeof(_reply), body_text, sender, max_bytes, source);
  // A full queue, or a channel the bot no longer serves (removed while a look-up or the broker was
  // awaited), drops the reply without using up the rate cap.
  uint8_t route = botDeliverRoute(dest.kind, _reply[0] != '\0', _outq.full(), count_cap, _cap, now_ms, _cfg.max_per_min,
                                  capped, botReplyDestServed(dest, _cfg));
  if (route == BDR_HOLDER) {
    _try.complete(dest.try_id, _reply, broker);   // never queued, never transmitted, not logged
    return;
  }
  BotQueuedReply* q = route == BDR_QUEUE ? _outq.push(now_ms + BOT_REPLY_DELAY_MS + extra_delay_ms) : nullptr;
  if (!q) {
    addLog(sender, request, "");
    return;
  }
  q->dest = dest;
  StrHelper::strncpy(q->text, _reply, sizeof(q->text));
  botClamp(q->sender, sizeof(q->sender), sender, sizeof(q->sender) - 1);
  botClamp(q->request, sizeof(q->request), request, sizeof(q->request) - 1);
}

BotNetStart BotTask::startNet(int builtin, int custom, const BotParsed& p, const BotMsgInfo& m, const BotReplyDest& dest,
                              const char* sender, const char* body, const char* channel, size_t max_bytes,
                              uint32_t now_ms) {
  size_t budget = botMentionBodyBudget(sender, max_bytes);
  uint32_t now_unix = 0;
  int32_t utc = 0;
  struct tm lt;
  if (_clock_valid) {
    now_unix = (uint32_t)time(nullptr);
    if (localTime(now_unix, lt)) utc = botUtcOffset(lt, now_unix);
  }
  cleanArgs(p.args);
  if (builtin >= 0)
    return _net.start((uint8_t)builtin, _cfg, dest, sender, body, _args, budget, now_ms, now_unix, utc, _built,
                      sizeof(_built));
  BotVars v;
  makeVars(v, m, channel);
  return _net.startCustom(_cfg.custom[custom], v, dest, sender, body, budget, now_ms, _built, sizeof(_built));
}

void BotTask::loopNet() {
  uint32_t now_ms = millis();
  _net.loop(now_ms, _clock_valid ? (uint32_t)time(nullptr) : 0);
  _try.tick(now_ms);
  // Each finished command goes out the way a local reply does; its cap was counted when parked.
  while (_net.take(_netrep))
    deliver(_netrep.dest, _netrep.text, _netrep.source, _netrep.sender, _netrep.request, now_ms, botCapAtDelivery(BRO_NET));
}

// ---- unknown commands answered by the MQTT broker ----

// The cooldown was used when the command was parsed. The cap is not counted here: a reply counts
// when it is delivered (botCapAtDelivery(BRO_BROKER)), so commands nobody answers never use it.
bool BotTask::askBroker(const BotParsed& p, const BotMsgInfo& m, const BotReplyDest& dest, const char* sender,
                        const char* body, const ContactInfo* dm_from, const char* channel, size_t max_bytes,
                        uint32_t now_ms) {
  // Only a command written with the prefix, never a switched-off one (botAskMayOffer, host-tested).
  if (!botAskMayOffer(dest.kind, p.prefixed, botAskKnownDisabled(_cfg, p.cmd), _mqtt.ready(), _cap, now_ms, _cfg.max_per_min))
    return false;
  cleanArgs(p.args);
  if (_ask.findSame(p.cmd, _args, dest) >= 0) return false;   // already waiting: not asked again
  int i = _ask.take(now_ms);
  if (i < 0) return false;
  size_t budget = botMentionBodyBudget(sender, max_bytes);
  BotVars v;
  makeVars(v, m, channel);
  _ask.fill(i, dest, p.cmd, sender, body, v, budget);
  // Fields only the request has (the slot already holds the {path} form).
  botAskPathHex(_path_hex, sizeof(_path_hex), m);
  botAskPathNames(_tmp, sizeof(_tmp), m, lookupPathName, this);
  char key[13] = "";
  if (dm_from) botHex(key, dm_from->id.pub_key, 6);
  BotMqttRequest r;
  memset(&r, 0, sizeof(r));
  r.cmd = p.cmd;
  r.args = _args;
  r.sender = sender;
  r.sender_key = key;
  r.channel = (dest.kind == BD_CHANNEL && channel) ? channel : "";
  r.path = _path_hex;
  r.path_names = _tmp;
  r.time = _var_time;
  r.date = _var_date;
  r.tz = _cfg.tz;
  r.bot = _mesh.getNodeName();
  r.scope = _cfg.scope;
  r.uptime = _var_uptime;
  if (dm_from && (dm_from->gps_lat != 0 || dm_from->gps_lon != 0)) {
    r.sender_lat = dm_from->gps_lat / 1000000.0;
    r.sender_lon = dm_from->gps_lon / 1000000.0;
  }
  r.bot_lat = _cfg.lat;
  r.bot_lon = _cfg.lon;
  r.snr = m.snr;
  r.rssi = m.rssi;
  r.hops = m.direct ? 0 : m.hops;
  r.dm = dest.kind != BD_CHANNEL;
  r.unix = _clock_valid ? (uint32_t)time(nullptr) : 0;
  r.max_len = (int)budget;
  if (!_mqtt.request(r, _ask.token(i), now_ms)) {   // as an unknown command: nothing sent
    _ask.release(i);
    return false;
  }
  return true;
}

// The reply text is a template, rendered once with the original message's values; it then leaves
// by the one exit, the cap being checked and counted now. No text (timed out, or a reply that
// renders to nothing) sends nothing; a try then completes with "".
// A reply with several messages sends each as a message of its own, BOT_PART_GAP_MS apart, each one
// counted against the cap; the rest are dropped once one is refused. A try shows only the first,
// marked "[1/N]", since the page holds one reply.
void BotTask::finishAsk(int i, const char* text, uint32_t now_ms, uint8_t parts) {
  if (!_ask.used(i)) return;
  BotAskSlot& a = _ask.at(i);
  BotVars v;
  _ask.vars(i, v);
  if (parts < 1 || !text) parts = 1;
  bool is_try = a.dest.kind == BD_TRY;
  for (uint8_t k = 0; k < parts; k++) {
    if (k > 0 && !_mqtt.replyPart(k, _tmp, sizeof(_tmp))) break;   // text is _tmp (loopMqtt)
    _built[0] = '\0';
    if (text && text[0]) {
      size_t off = 0;
      if (is_try && parts > 1) off = (size_t)snprintf(_built, sizeof(_built), "[1/%u] ", (unsigned)parts);
      botRender(_built + off, sizeof(_built) - off, text, v);
      if (!_built[off]) _built[0] = '\0';
    }
    bool capped = false;
    deliver(a.dest, _built, BRT_FOREIGN, a.sender, a.request, now_ms, botCapAtDelivery(BRO_BROKER), &capped, text != nullptr,
            (uint32_t)k * BOT_PART_GAP_MS);
    if (capped) { _mqtt_capped++; break; }
    if (is_try) break;
  }
  _ask.release(i);
}

void BotTask::loopMqtt() {
  static_assert(sizeof(_tmp) >= BOT_MQTT_REPLY_TEXT_MAX + 1, "_tmp holds a broker reply's text");
  uint32_t now_ms = millis();
  if ((uint32_t)(now_ms - _name_check_ms) >= 1000u) {   // the node may be renamed from the phone app
    _name_check_ms = now_ms;
    const char* nn = _mesh.getNodeName();
    if (strcmp(nn ? nn : "", _mqtt.nodeName()) != 0) _mqtt.applySettings(_cfg, nn);
  }
  _mqtt.loop(now_ms);
  uint8_t tok;
  while (_mqtt.takeReply(tok, _tmp, sizeof(_tmp))) {
    int i = _ask.fromToken(tok);
    if (i >= 0) finishAsk(i, _tmp, now_ms, _mqtt.replyParts());
  }
  while (_mqtt.takeTimeout(tok)) {
    int i = _ask.fromToken(tok);
    if (i >= 0) finishAsk(i, nullptr, now_ms);
  }
  for (int i; (i = _ask.overdue(now_ms)) >= 0;) finishAsk(i, nullptr, now_ms);   // never reported: a guard only
  if (_mqtt.commandListDue(now_ms)) publishCommands(now_ms);
}

// Every enabled custom command, the announced commands they and the built-ins do not shadow, then
// every enabled built-in (its configured name and fixed help line): botAskListEntries.
void BotTask::publishCommands(uint32_t now_ms) {
  static const int MAXN = BB_COUNT + BOT_MAX_CUSTOM + BOT_MQTT_CMD_SLOTS;
  const char* bn[BB_COUNT];
  const char* bd[BB_COUNT];
  const char* cn[BOT_MAX_CUSTOM];
  const char* cd[BOT_MAX_CUSTOM];
  int nb = 0, nc = 0;
  for (int i = 0; i < BB_COUNT; i++)
    if (_cfg.builtin[i].enabled) { bn[nb] = _cfg.builtin[i].name; bd[nb++] = BOT_BUILTIN_HELP[i]; }
  for (int i = 0; i < _cfg.num_custom && i < BOT_MAX_CUSTOM; i++)
    if (_cfg.custom[i].enabled) { cn[nc] = _cfg.custom[i].trigger; cd[nc++] = _cfg.custom[i].desc; }
  const char* names[MAXN];
  const char* descs[MAXN];
  const char* srcs[MAXN];
  int n = botAskListEntries(names, descs, srcs, MAXN, bn, bd, nb, cn, cd, nc, _mqtt.commands());
  _mqtt.publishCommandList(names, descs, srcs, n, now_ms);
}

void BotTask::loopReplies() {
  BotQueuedReply* q = _outq.due(millis());
  if (!q) return;
  bool sent = false;
  switch (q->dest.kind) {
    case BD_DIRECT: sent = _mesh.botSendDirect(q->dest.key, q->text) > 0; break;   // contact gone (< 0) or failed (0): dropped
    case BD_CHANNEL: sent = _mesh.botSendChannel(q->dest.channel, q->text); break;
    default: break;   // BD_TRY is never queued (deliver() hands it to the holder)
  }
  addLog(q->sender, q->request, sent ? q->text : "(not sent)");
  _outq.pop();
}

const char* BotTask::lookupPathName(const uint8_t* hash, uint8_t hash_size, void* ctx) {
  return ((BotTask*)ctx)->_mesh.botLookupPathName(hash, hash_size);
}

bool BotTask::localTime(uint32_t t, struct tm& out) const {
  time_t tt = (time_t)t;
  return localtime_r(&tt, &out) != nullptr;
}

void BotTask::fmtUptime(char* dst, size_t cap) const {
  uint32_t s = uptimeSeconds();
  uint32_t d = s / 86400u, h = (s % 86400u) / 3600u, m = (s % 3600u) / 60u;
  if (d) snprintf(dst, cap, "%lud %luh", (unsigned long)d, (unsigned long)h);
  else if (h) snprintf(dst, cap, "%luh %lum", (unsigned long)h, (unsigned long)m);
  else snprintf(dst, cap, "%lum", (unsigned long)m);
}

void BotTask::cleanArgs(const char* args) {
  // Arguments without control characters (they may be echoed on air) or trailing spaces.
  botStripControls(_tmp, sizeof(_tmp), args);
  botClamp(_args, sizeof(_args), _tmp, sizeof(_args) - 1);
  size_t al = strlen(_args);
  while (al > 0 && _args[al - 1] == ' ') _args[--al] = '\0';
}

// Placeholders for a custom command; reads _args (cleanArgs first). Fills the _var_* buffers.
void BotTask::makeVars(BotVars& v, const BotMsgInfo& m, const char* channel) {
  _var_time[0] = _var_date[0] = '\0';
  struct tm lt;
  if (_clock_valid && localTime((uint32_t)time(nullptr), lt)) {
    snprintf(_var_time, sizeof(_var_time), "%02d:%02d", lt.tm_hour % 100, lt.tm_min % 100);
    snprintf(_var_date, sizeof(_var_date), "%04d-%02d-%02d", (lt.tm_year + 1900) % 10000, (lt.tm_mon + 1) % 100,
             lt.tm_mday % 100);
  }
  fmtUptime(_var_uptime, sizeof(_var_uptime));
  botFmtPathHex(_path_hex, sizeof(_path_hex), m);
  v.sender = m.sender;
  v.args = _args;
  v.path = _path_hex;
  v.channel = channel ? channel : "";
  v.time = _var_time;
  v.date = _var_date;
  v.uptime = _var_uptime;
  v.hops = m.direct ? 0 : m.hops;
  v.snr = m.snr;
  v.rssi = m.rssi;
  v.fetch = "";
}

bool BotTask::buildReply(int builtin, int custom, const BotParsed& p, const BotMsgInfo& m, bool is_dm,
                         const ContactInfo* dm_from, const char* channel, char* out, size_t out_cap,
                         size_t max_bytes) {
  cleanArgs(p.args);
  const char* sender = m.sender;
  // help and heard are not cut afterwards: give them the room left beside the mention.
  size_t body_max = botMentionBodyBudget(sender, max_bytes);
  _tmp[0] = '\0';
  if (custom >= 0) {
    BotVars v;
    makeVars(v, m, channel);
    botRender(_tmp, sizeof(_tmp), _cfg.custom[custom].reply, v);
  } else {
    switch (builtin) {
      case BB_PING:
        _ping_max = botPingPartsAllowed(_outq.size());
        botFmtPingPart(_tmp, sizeof(_tmp), m, lookupPathName, this, max_bytes, 0, _ping_max, &_ping_parts);
        break;
      case BB_TEST:  botFmtTest(_tmp, sizeof(_tmp), m); break;
      case BB_PATH:
        _ping_max = botPingPartsAllowed(_outq.size());
        botFmtRoutePart(_tmp, sizeof(_tmp), m, lookupPathName, this, max_bytes, 0, _ping_max, &_ping_parts, true);
        break;
      case BB_HELP:  return buildHelp(_args, out, out_cap, body_max) > 0;               // pages fit by design
      case BB_HEARD: return _heard.format(out, out_cap, uptimeSeconds(), body_max) > 0;   // whole names only
      case BB_MESH: {
        uint32_t now_ms = millis();
        snprintf(_tmp, sizeof(_tmp), "Mesh: %u pkts/hr, %d nodes, avg %.1f hops",
                 (unsigned)_stats.packetsLastHour(now_ms), _heard.count(uptimeSeconds(), 86400u),
                 (double)_stats.avgHops(now_ms));
        break;
      }
      case BB_DIST:  buildDist(sender, dm_from, _tmp, sizeof(_tmp)); break;
      case BB_SUN:   buildSun(is_dm, sender, _tmp, sizeof(_tmp)); break;
      case BB_ROLL:  botFmtRoll(_tmp, sizeof(_tmp), sender, _args, botRnd); break;
      case BB_8BALL: botFmt8Ball(_tmp, sizeof(_tmp), sender, botRnd); break;
      case BB_WX: case BB_AURORA: case BB_ALERTS: case BB_NODES: break;   // answered by BotNet, never here
      default: break;
    }
  }
  return botClamp(out, out_cap, _tmp, max_bytes) > 0;
}

size_t BotTask::buildHelp(const char* args, char* out, size_t out_cap, size_t max_bytes) {
  int page = 1;
  if (args[0]) {
    bool digits = true;
    for (const char* q = args; *q; q++) if (*q < '0' || *q > '9') { digits = false; break; }
    if (digits) {
      page = strlen(args) <= 4 ? atoi(args) : 0;
    } else {
      // "help <cmd>": first word, an optional command prefix stripped, lower-cased.
      const char* q = args;
      if (*q == _cfg.prefix) q++;
      char cmd[17];
      size_t n = 0;
      while (botIsAlnum(*q) && n < 16) { char c = *q++; cmd[n++] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }
      cmd[n] = '\0';
      if (n == 0 || botIsAlnum(*q)) return 0;
      for (int i = 0; i < BB_COUNT; i++) {
        if (_cfg.builtin[i].enabled && strcmp(_cfg.builtin[i].name, cmd) == 0) {
          const char* line = BOT_BUILTIN_HELP[i];
          snprintf(_tmp, sizeof(_tmp), "%s: %s", _cfg.builtin[i].name, line ? line : "no description");
          return botClamp(out, out_cap, _tmp, max_bytes);
        }
      }
      for (int i = 0; i < _cfg.num_custom && i < BOT_MAX_CUSTOM; i++) {
        const BotCustomCmd& c = _cfg.custom[i];
        if (c.enabled && strcmp(c.trigger, cmd) == 0) {
          snprintf(_tmp, sizeof(_tmp), "%s: %s", c.trigger, c.desc[0] ? c.desc : "no description");
          return botClamp(out, out_cap, _tmp, max_bytes);
        }
      }
      // An announced command (its name is not an enabled built-in or custom one, or it would
      // have been found above); only while it could be answered.
      const char* ad = _mqtt.ready() ? _mqtt.commands().find(cmd) : nullptr;
      if (ad) {
        snprintf(_tmp, sizeof(_tmp), "%s: %s", cmd, ad[0] ? ad : "no description");
        return botClamp(out, out_cap, _tmp, max_bytes);
      }
      return 0;   // unknown or disabled: no reply
    }
  }
  const char* names[BB_COUNT + BOT_MAX_CUSTOM + BOT_MQTT_CMD_SLOTS];
  int count = 0;
  for (int i = 0; i < BB_COUNT; i++) if (_cfg.builtin[i].enabled) names[count++] = _cfg.builtin[i].name;
  for (int i = 0; i < _cfg.num_custom && i < BOT_MAX_CUSTOM; i++)
    if (_cfg.custom[i].enabled) names[count++] = _cfg.custom[i].trigger;
  // Announced commands after the bot's own, skipping names those shadow; only while answerable.
  if (_mqtt.ready())
    count = botAskMergeAnnounced(names, nullptr, nullptr, count, (int)(sizeof(names) / sizeof(names[0])), _mqtt.commands());
  int total = 0;
  return botFmtHelp(out, out_cap, names, count, page, _cfg.help_link, max_bytes, &total);
}

size_t BotTask::buildDist(const char* sender, const ContactInfo* dm_from, char* out, size_t out_cap) {
  if (_cfg.lat == 0 && _cfg.lon == 0) return (size_t)snprintf(out, out_cap, "@[%s] I don't know my own location", sender);
  const ContactInfo* c = dm_from;
  if (!c) {
    int matches = 0;
    ContactInfo* found = _mesh.botFindContactByName(sender, &matches);
    c = matches == 1 ? found : nullptr;
  }
  if (!c || (c->gps_lat == 0 && c->gps_lon == 0))
    return (size_t)snprintf(out, out_cap, "@[%s] I don't know your location", sender);
  double lat = c->gps_lat / 1000000.0, lon = c->gps_lon / 1000000.0;
  double km = botDistanceKm(_cfg.lat, _cfg.lon, lat, lon);
  // "<dir> of me": the direction from the bot to the sender.
  return (size_t)snprintf(out, out_cap, "@[%s] %.1f km %s of me", sender, km, botBearing8(_cfg.lat, _cfg.lon, lat, lon));
}

size_t BotTask::buildSun(bool is_dm, const char* sender, char* out, size_t out_cap) {
  out[0] = '\0';
  if (!_clock_valid) return is_dm ? (size_t)snprintf(out, out_cap, "Clock not set yet") : 0;
  if (_cfg.lat == 0 && _cfg.lon == 0) return (size_t)snprintf(out, out_cap, "@[%s] I don't know my own location", sender);
  uint32_t now = (uint32_t)time(nullptr);
  const char* moon = botMoonPhase(now);
  // botSunTimes works on the UTC day of its argument; shift by local solar time so the day is
  // the one being lived here (e.g. 20:00 in Toronto is already tomorrow in UTC).
  uint32_t ref = (uint32_t)((int64_t)now + (int64_t)(_cfg.lon * 240.0));
  uint32_t rise = 0, set = 0;
  struct tm a, b;
  if (!botSunTimes(_cfg.lat, _cfg.lon, ref, rise, set) || !localTime(rise, a) || !localTime(set, b))
    return (size_t)snprintf(out, out_cap, "No sunrise or sunset today, moon %s", moon);
  return (size_t)snprintf(out, out_cap, "Sun %02d:%02d-%02d:%02d, moon %s", a.tm_hour % 100, a.tm_min % 100,
                          b.tm_hour % 100, b.tm_min % 100, moon);
}

#endif  // WITH_BOT
