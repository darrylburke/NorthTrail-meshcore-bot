#ifdef WITH_BOT

#include "BotWeb.h"
#include "BotTask.h"
#include "BotPassword.h"
#include "BotWebHtml.h"
#include "BotWebInternal.h"

#include <WiFi.h>
#include <AsyncTCP.h>
#include <esp_random.h>
#include <time.h>
#include <helpers/esp32/HttpPort80Lease.h>
#include <helpers/bot/BotConfig.h>
#include <helpers/bot/BotWebMqtt.h>

using namespace botweb;

BotWeb bot_web;

// The one scratch settings struct for applying a posted change (loop task only; sizeof(BotConfig),
// about 8 KB).
// Shared with BotWebSetup.cpp.
BotConfig bot_web_scratch;

static const char* const RES_NAMES[] = { "none", "pending", "ok", "not_saved", "rejected", "partial", "waiting" };

// Upper bound of the fixed parts buildState() writes ("status", "node" and "mqtt"), so a full
// recent list can only ever cost recent lines, never the state itself. JW::str (botJsonEscape) writes
// any byte as at most 6 ("\u00XX"); JW::kint at most 20 digits; a bool 5. Keys and punctuation
// come from the literals below with the values removed. If the bound is ever wrong the build
// still never overflows: JW stops, and the page is told "busy".
namespace {
constexpr size_t js6(size_t cap) { return 6 * (cap - 1) + 2; }
constexpr size_t JINT = 20, JBOOL = 5, NET_LAST = 20;
constexpr size_t STATE_STATUS_MAX =
    (sizeof("\"status\":{\"uptime_s\":,\"wifi_ssid\":,\"wifi_connected\":,\"ip\":,\"rssi\":,\"clock_valid\":,"
            "\"time\":,\"heard_24h\":,\"packets_hour\":,\"free_heap\":,\"min_free_heap\":,\"net_busy\":,"
            "\"net_parked\":,\"net_last\":,\"net_ms\":,\"net_dns_ms\":,\"net_conn_ms\":}") - 1) +
    11 * JINT + 3 * JBOOL + js6(sizeof(((NodePrefs*)0)->wifi_ssid)) + js6(16) + js6(NET_LAST);
constexpr size_t STATE_NODE_MAX =
    (sizeof(",\"node\":{\"name\":,\"mdns\":,\"public_key\":,\"default_scope\":,\"channels\":[]}") - 1) +
    js6(sizeof(((NodePrefs*)0)->node_name)) + js6(24) + js6(2 * PUB_KEY_SIZE + 1) +
    js6(sizeof(((NodePrefs*)0)->default_scope_name)) + MAX_GROUP_CHANNELS * (js6(32) + 1);
// The "mqtt" member is bounded beside its writer (BotWebMqtt.h, host-tested), plus its comma.
constexpr size_t STATE_MQTT_MAX = 1 + BOT_WEB_MQTT_JSON_MAX;
constexpr size_t STATE_FIXED_MAX = STATE_STATUS_MAX + STATE_NODE_MAX + STATE_MQTT_MAX + (sizeof(",\"recent\":[]") - 1) + 1;
// The snapshot buffer: the settings document, or the state's fixed parts at their longest (40
// channel names of 31 '<' each, every announced command at its longest) with at least
// BOT_WEB_STATE_RECENT_MIN bytes left for recent lines (a line is at most about 1.2 KB, usually 100
// to 250 bytes), whichever is larger (botWebStateCap in BotWebRules.h).
constexpr size_t STATE_CAP = botWebStateCap(STATE_FIXED_MAX);
static_assert(BOT_CONFIG_JSON_MAX + 1 <= STATE_CAP, "the settings document must fit the state buffer");
static_assert(STATE_FIXED_MAX + BOT_WEB_STATE_RECENT_MIN <= STATE_CAP, "the state's fixed parts must leave room for recent lines");
}  // namespace

namespace {

// Refuses, before any body byte is read, every request botGuard() says to refuse. It is
// registered first, so it sees every request; being "trivial" (the base class default), the
// server then skips the body instead of parsing urlencoded/multipart data into memory.
class GuardHandler : public AsyncWebHandler {
public:
  bool canHandle(AsyncWebServerRequest* r) const override { return bot_web.guardCode(r) != 0; }
  void handleRequest(AsyncWebServerRequest* r) override { bot_web.guardRespond(r, bot_web.guardCode(r)); }
  bool isRequestHandlerTrivial() const override { return true; }
};

// Collects a JSON body into request->_tempObject (freed by the request). The guard has
// already refused anything over its route's cap, so this never allocates more than that.
// A body may hold a password (settings, login, password change, setup), and a client that drops
// the connection mid-upload ends the request without any handler running. So the buffer is also
// wiped from the request's disconnect callback: in ESPAsyncWebServer 3.10.3,
// AsyncWebServerRequest::_onDisconnect() calls that callback and only then
// AsyncWebServer::_handleDisconnect(), which deletes the request, whose destructor frees
// _tempObject; the callback is owned by the request, so it cannot run after that. On a normal
// ending it runs too (a handler that took the body has set _tempObject to null).
// Every wipe of the body (here, wipeBody(), _pend_json_len) uses contentLength(), so the buffer
// is made only when total is that same length; the server rewrites its content length only while
// parsing a multipart body, which the early guard refuses. The buffer is total + 1 bytes (the
// NUL), so wiping contentLength() bytes never passes its end.
void collectBody(AsyncWebServerRequest* r, uint8_t* data, size_t len, size_t index, size_t total) {
  if (total == 0 || total > BotWeb::MAX_BODY || total != r->contentLength()) return;
  if (index == 0 && !r->_tempObject) {
    r->_tempObject = malloc(total + 1);
    if (r->_tempObject) {
      ((char*)r->_tempObject)[total] = 0;
      r->onDisconnect([r, total]() { if (r->_tempObject) botWipe(r->_tempObject, total); });   // the size allocated, less the NUL
    }
  }
  if (r->_tempObject && index + len <= total) memcpy((uint8_t*)r->_tempObject + index, data, len);
}

const char* headerOrNull(AsyncWebServerRequest* r, const char* name) {
  const AsyncWebHeader* h = r->getHeader(name);
  return h ? h->value().c_str() : nullptr;
}

void ipStr(char out[16], const IPAddress& a) {
  snprintf(out, 16, "%u.%u.%u.%u", a[0], a[1], a[2], a[3]);
}

}  // namespace

// ---------------------------------------------------------------------------
// Loop task
// ---------------------------------------------------------------------------

void BotWeb::begin(BotTask& bot, MyMesh& mesh) {
  _bot = &bot;
  _mesh = &mesh;
  _mux = xSemaphoreCreateMutex();
  memset(&_pend_setup, 0, sizeof(_pend_setup));
  const uint8_t* pk = mesh.self_id.pub_key;
  snprintf(_mdns_name, sizeof(_mdns_name), "meshbot-%02x%02x", pk[0], pk[1]);
  const NodePrefs* np = mesh.getNodePrefs();
  // Same test main.cpp uses to start the station and the companion TCP interface.
  bool wifi_usable = np->wifi_enabled && np->getWifiSSID()[0];
  _mode = !wifi_usable ? BWM_SETUP : !bot.hasWebPassword() ? BWM_NEEDS_PASSWORD : BWM_NORMAL;
  _pw_epoch_seen = bot.webPasswordEpoch();
  refreshSnapshot();
  if (mode() == BWM_SETUP) {
    startSetupAp();
  } else {
    if (mode() == BWM_NEEDS_PASSWORD)
      Serial.println("bot web: no web password yet; set one with: set bot.webpwd <8 to 63 characters>");
    _state = (char*)malloc(STATE_CAP);
    if (!_state) Serial.println("bot web: no memory for the page state");
  }
}

bool BotWeb::startServer() {
  if (_started) return true;
  if (!HttpPort80Lease::acquire(HttpPort80Lease::Owner::WebConfig)) {
    Serial.printf("bot web: port 80 is in use by %s\n", HttpPort80Lease::ownerName());
    return false;
  }
  DefaultHeaders::Instance().addHeader("Cache-Control", "no-store");
  _server = new AsyncWebServer(80);
  AsyncWebServer& s = *_server;
  s.addHandler(new GuardHandler());   // first: it must see every request before any route
  s.on(AsyncURIMatcher::exact("/"), HTTP_GET, [](AsyncWebServerRequest* r) { bot_web.handlePage(r); });
  s.on(AsyncURIMatcher::exact("/api/state"), HTTP_GET, [](AsyncWebServerRequest* r) { bot_web.handleState(r); });
  s.on(AsyncURIMatcher::exact("/api/result"), HTTP_GET, [](AsyncWebServerRequest* r) { bot_web.handleResult(r); });
  s.on(AsyncURIMatcher::exact("/api/setup"), HTTP_POST, [](AsyncWebServerRequest* r) { bot_web.handleSetup(r); }, nullptr, collectBody);
  s.on(AsyncURIMatcher::exact("/api/login"), HTTP_POST, [](AsyncWebServerRequest* r) { bot_web.handleLogin(r); }, nullptr, collectBody);
  s.on(AsyncURIMatcher::exact("/api/logout"), HTTP_POST, [](AsyncWebServerRequest* r) { bot_web.handleLogout(r); }, nullptr, collectBody);
  s.on(AsyncURIMatcher::exact("/api/config"), HTTP_GET, [](AsyncWebServerRequest* r) { bot_web.handleConfigGet(r); });
  s.on(AsyncURIMatcher::exact("/api/config"), HTTP_POST, [](AsyncWebServerRequest* r) { bot_web.handleConfig(r); }, nullptr, collectBody);
  s.on(AsyncURIMatcher::exact("/api/try"), HTTP_GET, [](AsyncWebServerRequest* r) { bot_web.handleTryGet(r); });
  s.on(AsyncURIMatcher::exact("/api/try"), HTTP_POST, [](AsyncWebServerRequest* r) { bot_web.handleTry(r); }, nullptr, collectBody);
  s.on(AsyncURIMatcher::exact("/api/password"), HTTP_POST, [](AsyncWebServerRequest* r) { bot_web.handlePassword(r); }, nullptr, collectBody);
  s.on(AsyncURIMatcher::exact("/api/advert"), HTTP_POST, [](AsyncWebServerRequest* r) { bot_web.handleAdvert(r); }, nullptr, collectBody);
  s.onNotFound([](AsyncWebServerRequest* r) { bot_web.handleNotFound(r); });
  s.begin();
  _started = true;
  return true;
}

void BotWeb::loop() {
  if (!_bot) return;
  uint32_t now = millis();
  if (mode() == BWM_SETUP) {
    loopSetupAp();
  } else {
    if (!_started && WiFi.status() == WL_CONNECTED && now - _last_try_ms >= 5000) {
      _last_try_ms = now;
      if (startServer())
        Serial.printf("bot web: http://%s/ or http://%s.local/\n", WiFi.localIP().toString().c_str(), _mdns_name);
    }
    loopMdns();
    if (mode() == BWM_NEEDS_PASSWORD && _bot->hasWebPassword()) {   // set over USB meanwhile
      _mode = BWM_NORMAL;
      Serial.println("bot web: web password set, the page now asks for it");
    }
  }
  drain();
  pollTry();
  if (_bot->webPasswordEpoch() != _pw_epoch_seen) {   // changed elsewhere (USB): end every session
    _pw_epoch_seen = _bot->webPasswordEpoch();
    Lock l(_mux);
    _sessions.revokeAllExcept(nullptr);
  }
  if (now - _last_snap_ms >= 1000) {
    _last_snap_ms = now;
    refreshSnapshot();
  }
  // One build per pass: the buffer holds one kind at a time. A request left waiting is served
  // on the next pass.
  if (_state) {
    if (_config_req.exchange(false)) buildConfig();
    else if (_refresh_req.exchange(false)) buildState();
  }
  if (_restart_at && (int32_t)(millis() - _restart_at) >= 0) ESP.restart();
}

// Copies what the handlers may read: the password salt and hash, node name, saved SSID and
// the addresses the Host header may name.
void BotWeb::refreshSnapshot() {
  const BotConfig& c = _bot->config();
  const NodePrefs* np = _mesh->getNodePrefs();
  char sta[16] = "", ap[16] = "";
  if (mode() != BWM_SETUP && WiFi.status() == WL_CONNECTED) ipStr(sta, WiFi.localIP());
  if (_ap_up) ipStr(ap, WiFi.softAPIP());
  Lock l(_mux);
  memcpy(_salt, c.web_salt, sizeof(_salt));
  memcpy(_hash, c.web_hash, sizeof(_hash));
  StrHelper::strncpy(_node_name, np->node_name, sizeof(_node_name));
  StrHelper::strncpy(_wifi_ssid, np->wifi_ssid, sizeof(_wifi_ssid));
  _wifi_has_pwd = np->wifi_pwd[0] != 0;
  memcpy(_sta_ip, sta, sizeof(_sta_ip));
  memcpy(_ap_ip, ap, sizeof(_ap_ip));
}

// Writes the logged-in part of /api/state ("status", "node", "mqtt", "recent" members, without
// the outer braces) into _state. The settings are a separate document (buildConfig).
void BotWeb::buildState() {
  const NodePrefs* np = _mesh->getNodePrefs();
  uint32_t now = millis();
  bool up = WiFi.status() == WL_CONNECTED;
  char ip[16] = "";
  if (up) ipStr(ip, WiFi.localIP());
  char last[NET_LAST];
  if (_bot->netLastOutcome() == BFO_HTTP) snprintf(last, sizeof(last), "http %d", _bot->netLastHttpStatus());
  else snprintf(last, sizeof(last), "%s", _bot->netLastOutcomeText());

  Lock l(_mux);
  JW w(_state, STATE_CAP);
  w.raw("\"status\":{");
  w.kint("uptime_s", _bot->uptimeSeconds());
  w.kstr("wifi_ssid", np->wifi_ssid);
  w.kbool("wifi_connected", up);
  w.kstr("ip", ip);
  w.kint("rssi", up ? WiFi.RSSI() : 0);
  w.kbool("clock_valid", _bot->clockValid());
  w.kint("time", _bot->clockValid() ? (long long)time(nullptr) : 0);
  w.kint("heard_24h", _bot->heard().count(_bot->uptimeSeconds(), 86400u));
  w.kint("packets_hour", _bot->stats().packetsLastHour(now));
  w.kint("free_heap", ESP.getFreeHeap());
  w.kint("min_free_heap", ESP.getMinFreeHeap());
  w.kbool("net_busy", _bot->netFetchBusy());
  w.kint("net_parked", _bot->netParked());
  w.kstr("net_last", last);
  w.kint("net_ms", _bot->netLastMs());            // in all
  w.kint("net_dns_ms", _bot->netLastDnsMs());     // of which: DNS
  w.kint("net_conn_ms", _bot->netLastConnMs());   // TCP connect plus TLS handshake
  w.endObj();
  w.raw(",\"node\":{");
  w.kstr("name", np->node_name);
  char host[24];
  snprintf(host, sizeof(host), "%s.local", _mdns_name);
  w.kstr("mdns", host);
  char pk[2 * PUB_KEY_SIZE + 1];
  static const char hex[] = "0123456789abcdef";
  for (int i = 0; i < PUB_KEY_SIZE; i++) {
    pk[2 * i] = hex[_mesh->self_id.pub_key[i] >> 4];
    pk[2 * i + 1] = hex[_mesh->self_id.pub_key[i] & 15];
  }
  pk[2 * PUB_KEY_SIZE] = 0;
  w.kstr("public_key", pk);
  w.kstr("default_scope", np->default_scope_name);
  w.raw("\"channels\":[");
  char ch[32];
  for (int i = 0; i < MAX_GROUP_CHANNELS; i++)
    if (_mesh->botChannelName(i, ch)) { w.str(ch); w.raw(","); }
  w.endArr();
  w.raw("},");
  if (w.ok) {   // the broker connection (BotWebMqtt.cpp); it always fits (STATE_FIXED_MAX)
    size_t m = writeMqtt(w.b + w.n, w.cap - w.n);
    if (m) w.n += m; else w.ok = false;
  }
  w.raw(",\"recent\":[");
  // Newest first. Leave room for the closing bracket; stop at the first line that does not fit.
  size_t room = w.cap - 2;
  bool full = false;
  for (int i = 0; w.ok && !full && i < BotTask::LOG_SIZE; i++) {
    const BotLogEntry* e = _bot->recentAt(i);
    if (!e || w.n + 1 >= room) break;
    size_t mark = w.n;
    JW e_w(w.b + w.n, room - w.n);
    e_w.raw("{");
    e_w.kint("t", e->when);
    e_w.kstr("from", e->sender);
    e_w.kstr("req", e->request);
    e_w.kstr("reply", e->reply);
    e_w.endObj();
    e_w.raw(",");
    if (e_w.ok) w.n += e_w.n; else { w.n = mark; w.b[mark] = 0; full = true; }
  }
  w.endArr();
  _state_err = !w.ok;
  _state_kind = SK_STATE;
  _state_len = w.n;
  _state_ms = millis();
  _state_seq++;
}

// Writes the settings document for GET /api/config into _state (never the salt or hash). It
// always fits (BOT_CONFIG_JSON_MAX, checked by a static_assert in BotWebRules.h); only running
// out of memory for the JsonDocument fails it, and then the page is told "busy": a partial
// document would show a short custom list that "Save" then writes back.
void BotWeb::buildConfig() {
  JsonDocument cfg;
  bool ok = botConfigToJsonChecked(_bot->config(), cfg, false);
  Lock l(_mux);
  _state_len = 0;
  if (ok) {
    size_t need = measureJson(cfg);
    if (need + 1 <= STATE_CAP) _state_len = serializeJson(cfg, _state, STATE_CAP);
    ok = _state_len == need;   // what was actually written, not what was expected
  }
  if (!ok) { _state_len = 0; _state[0] = 0; }
  _state_err = !ok;
  _state_kind = SK_CONFIG;
  _state_ms = millis();
  _state_seq++;
}

// Applies the pending request, if any, then records the outcome.
void BotWeb::drain() {
  Kind k;
  {
    Lock l(_mux);
    k = _pend_kind;
  }
  if (k == K_NONE) return;
  if (k == K_CONFIG) {
    // Held while a look-up runs (see BotWeb.h); never longer than CONFIG_HOLD_MS. The request
    // stays in the one slot meanwhile, so it can be neither lost nor queued a second time.
    bool net_busy = _bot->netFetchBusy() || _bot->netParked() > 0;
    uint32_t now = millis();
    if (!_cfg_held) { _cfg_held = true; _cfg_held_ms = now; }
    if (net_busy && now - _cfg_held_ms < CONFIG_HOLD_MS) {
      Lock l(_mux);
      if (_res_id == _pend_id && _res_status == RS_PENDING) _res_status = RS_WAITING;
      return;
    }
    _cfg_held = false;
  }
  uint8_t status = RS_REJECTED;
  char msg[RES_MSG] = "";
  if (k == K_TRY) {   // its outcome goes to the try record, not to the save result
    applyTryReq();
    memset(_pend_try, 0, sizeof(_pend_try));
    Lock l(_mux);
    _pend_kind = K_NONE;
    return;
  }
  if (k == K_CONFIG) applyConfigReq(status, msg, sizeof(msg));
  else if (k == K_PASSWORD) applyPasswordReq(status, msg, sizeof(msg));
  else if (k == K_SETUP) applySetupReq(status, msg, sizeof(msg));
  if (_pend_json) { botWipe(_pend_json, _pend_json_len); free(_pend_json); }
  _pend_json = nullptr;
  _pend_json_len = 0;
  botWipe(_pend_pw, sizeof(_pend_pw));
  botWipe(_pend_token, sizeof(_pend_token));
  botWipe(&_pend_setup, sizeof(_pend_setup));
  _pend_setup_authed = false;
  refreshSnapshot();
  {
    Lock l(_mux);
    _res_id = _pend_id;
    _res_status = status;
    StrHelper::strncpy(_res_msg, msg, sizeof(_res_msg));
    _pend_kind = K_NONE;
    _state_kind = SK_NONE;   // what the buffer holds may predate the change: build afresh
  }
}

// Memory: at most one settings-sized document is alive at a time. The posted body (up to
// BOT_CONFIG_BODY_CAP, 16 KB) is parsed into `doc` (strings copied: about the body's size again)
// and freed at once, so the peak here is about body + document, roughly 30 KB for a full
// 16-command save. What was refused is worked out from the posted document and the two structs
// (botCfgRefusedKeys), with no "before"/"after" document. The posted document is gone before
// BotTask::applyConfig saves, whose own peak is its storage document plus the text written
// (about 2 x 16 KB). The struct copy lives in the static bot_web_scratch, not on the heap or stack.
// The posted document may hold the broker password, so it is parsed with the wiping allocator, and
// the scratch copy's password is wiped once applied. A save that moves the broker (host or port)
// without setting a password clears the stored one (botCfgBrokerMoveClearsPass): the old password
// is never offered to a new broker, and the result says BOT_MQTT_PASS_CLEARED_MSG.
void BotWeb::applyConfigReq(uint8_t& status, char* msg, size_t cap) {
  char refused[96] = "";
  char name[sizeof(((NodePrefs*)0)->node_name)] = "";
  bool has_name = false, name_ok = true, pass_cleared = false;
  BotApplyReport rep;
  {
    JsonDocument doc(WipingAllocator::instance());
    DeserializationError e = _pend_json ? deserializeJson(doc, (const char*)_pend_json) : DeserializationError::InvalidInput;
    // The document holds its own copy of every string: the body is no longer needed.
    if (_pend_json) { botWipe(_pend_json, _pend_json_len); free(_pend_json); }
    _pend_json = nullptr;
    _pend_json_len = 0;
    if (e || !doc.is<JsonObject>()) {
      status = RS_REJECTED;
      snprintf(msg, cap, e == DeserializationError::NoMemory ? "Node low on memory, nothing saved. Try again" : "bad request");
      return;
    }
    JsonObjectConst posted = doc.as<JsonObjectConst>();
    memcpy(&bot_web_scratch, &_bot->config(), sizeof(bot_web_scratch));
    botConfigFromWebJsonStrict(posted, bot_web_scratch, rep);
    botCfgRefusedKeys(posted, _bot->config(), bot_web_scratch, rep, refused, sizeof(refused));
    if (botCfgBrokerMoveClearsPass(_bot->config(), bot_web_scratch, posted["mqtt_pass"])) {
      botWipe(bot_web_scratch.mqtt_pass, sizeof(bot_web_scratch.mqtt_pass));
      pass_cleared = true;
    }
    JsonVariantConst nm = posted["node_name"];
    if (nm.is<const char*>()) {
      has_name = true;
      const char* s = nm.as<const char*>();
      name_ok = s && strlen(s) < sizeof(name);   // checked on the web task already
      if (name_ok) memcpy(name, s, strlen(s) + 1);
    }
  }   // the posted document is freed here, before anything is saved
  bool saved = _bot->applyConfig(bot_web_scratch);
  botWipe(&bot_web_scratch, sizeof(bot_web_scratch));   // a whole copy (broker password included); the live one is in BotTask
  if (has_name && (!name_ok || (strcmp(name, _mesh->getNodePrefs()->node_name) != 0 && !_bot->applyNodeName(name))))
    saved = false;
  if (!saved) {
    status = RS_NOT_SAVED;
    snprintf(msg, cap, "In use now, but saving to flash failed: the change is lost at restart");
  } else if (rep.channels[0] || rep.custom[0] || rep.watch[0] || rep.match[0] || rep.builtin[0] || refused[0]) {
    status = RS_PARTIAL;
    if (rep.channels[0]) snprintf(msg, cap, "Channels not changed: %s", rep.channels);
    else if (rep.custom[0]) snprintf(msg, cap, "Custom commands not changed: %s", rep.custom);
    else if (rep.watch[0]) snprintf(msg, cap, "Watched nodes not changed: %s", rep.watch);
    else if (rep.match[0]) snprintf(msg, cap, "Text rules not changed: %s", rep.match);
    else if (rep.builtin[0]) snprintf(msg, cap, "Saved, except: %s", rep.builtin);
    else snprintf(msg, cap, "Saved, but some values were not accepted: %s", refused);
  } else {
    status = RS_OK;
    snprintf(msg, cap, "Saved");
  }
  if (pass_cleared) {   // first, so a long message can never cut it off
    char rest[RES_MSG];
    StrHelper::strncpy(rest, msg, sizeof(rest));
    if (status == RS_OK) snprintf(msg, cap, "Saved; " BOT_MQTT_PASS_CLEARED_MSG);
    else snprintf(msg, cap, BOT_MQTT_PASS_CLEARED_MSG "; %s", rest);
  }
}

void BotWeb::applyPasswordReq(uint8_t& status, char* msg, size_t cap) {
  char before[65];
  memcpy(before, _bot->config().web_hash, sizeof(before));
  bool saved = _bot->setWebPassword(_pend_pw);
  bool changed = strcmp(before, _bot->config().web_hash) != 0;
  if (changed) {   // the new password is live (even if not saved): end every other session
    _pw_epoch_seen = _bot->webPasswordEpoch();   // done here, keeping the caller's session
    Lock l(_mux);
    _sessions.revokeAllExcept(_pend_token);
  }
  if (saved) { status = RS_OK; snprintf(msg, cap, "Password changed"); }
  else if (changed) { status = RS_NOT_SAVED; snprintf(msg, cap, "Password changed until restart, but saving to flash failed"); }
  else { status = RS_REJECTED; snprintf(msg, cap, "Password not changed"); }
}

// ---------------------------------------------------------------------------
// async_tcp task
// ---------------------------------------------------------------------------

bool BotWeb::fromAp(AsyncWebServerRequest* r) const {
  if (!_ap_up) return false;
  AsyncClient* c = r->client();
  return c && c->localIP() == WiFi.softAPIP();
}

uint32_t BotWeb::clientIp(AsyncWebServerRequest* r) {
  AsyncClient* c = r->client();
  return c ? (uint32_t)c->remoteIP() : 0;
}

// Zeroes the buffered body (it may hold passwords) before the request frees it.
void BotWeb::wipeBody(AsyncWebServerRequest* r) {
  if (r->_tempObject) botWipe(r->_tempObject, r->contentLength());
}

// A valid session cookie. touch refreshes its idle timer: POSTs do, background GETs do not.
bool BotWeb::authed(AsyncWebServerRequest* r, char token_out[33], bool touch) {
  if (mode() != BWM_NORMAL) return false;
  char tok[40];
  if (!botCookieValue(headerOrNull(r, "Cookie"), "bot", tok, sizeof(tok))) return false;
  bool ok;
  {
    Lock l(_mux);
    ok = _sessions.check(tok, millis(), touch);
  }
  if (ok && token_out) memcpy(token_out, tok, 33);
  return ok;
}

int BotWeb::guardCode(AsyncWebServerRequest* r) {
  BotReqFacts f;
  f.method = r->method() == HTTP_GET ? BOT_M_GET : r->method() == HTTP_POST ? BOT_M_POST : BOT_M_OTHER;
  f.route = botRouteFor(r->url().c_str());
  // The body facts are the parser's own values (see BotReqFacts in BotWebRules.h).
  f.length = r->contentLength();
  f.chunked = r->hasHeader("Transfer-Encoding");
  f.multipart = r->multipart();
  f.has_content_type = r->contentType().length() > 0;
  f.json = botIsJsonType(r->contentType().c_str());
  f.from_ap = fromAp(r);
  f.mode = mode();
  {
    Lock l(_mux);
    f.host_ok = botHostAllowed(r->host().c_str(), _sta_ip, _ap_ip, _mdns_name);
  }
  f.origin_ok = f.method == BOT_M_POST && botSameOrigin(headerOrNull(r, "Origin"), headerOrNull(r, "Referer"), r->host().c_str());
  f.session = botRouteNeedsSession(f.route) && authed(r, nullptr, false);
  if (f.from_ap) _ap_last_ms = millis();
  return botGuard(f);
}

void BotWeb::guardRespond(AsyncWebServerRequest* r, int code) {
  switch (code) {
    case BOT_GUARD_REDIRECT: r->redirect(String("http://") + WiFi.softAPIP().toString() + "/"); return;
    case 0:   sendErr(r, 503, "busy, try again"); return;   // passed on the second look: rare race
    case 400: sendErr(r, 400, "bad request"); return;
    case 401: sendErr(r, 401, "please sign in"); return;
    case 403: sendErr(r, 403, "cross-site request refused"); return;
    case 405: sendErr(r, 405, "method not allowed"); return;
    case 411: sendErr(r, 411, "a body with a Content-Length is required"); return;
    case 413: sendErr(r, 413, "request too large"); return;
    case 415: sendErr(r, 415, "send JSON"); return;
    case 421: sendErr(r, 421, "unknown host name"); return;
    default:  sendErr(r, 404, "not found"); return;
  }
}

// Makes sure the buffer holds the wanted kind: a state at most 2 s old, or a settings
// document built for this request. Asks the loop task and waits up to 1.5 s. The caller
// checks the kind again when it copies (under _mux): the buffer can change in between.
bool BotWeb::waitFresh(SnapKind want) {
  uint32_t seq;
  {
    Lock l(_mux);
    if (!_state) return false;
    if (want == SK_STATE && _state_kind == SK_STATE && _state_seq && millis() - _state_ms <= 2000) return true;
    seq = _state_seq;
  }
  std::atomic<bool>& ask = want == SK_CONFIG ? _config_req : _refresh_req;
  ask = true;
  for (int i = 0; i < 75; i++) {
    vTaskDelay(pdMS_TO_TICKS(20));
    Lock l(_mux);
    if (_state_seq == seq) continue;
    if (_state_kind == want) return true;
    seq = _state_seq;   // a build of the other kind came first: ask again
    ask = true;
  }
  return false;
}

uint32_t BotWeb::enqueue(Kind k) {
  if (_pend_kind != K_NONE) return 0;
  if (++_next_id == 0) _next_id = 1;
  _pend_id = _next_id;
  _pend_kind = k;
  if (k == K_TRY) {   // replaces the previous try; the save result stays as it is
    _try_id = _pend_id;
    _try_done = false;
    _try_busy = false;
    _try_broker = false;
    _try_reply[0] = 0;
    return _pend_id;
  }
  _res_id = _pend_id;
  _res_status = RS_PENDING;
  _res_msg[0] = 0;
  return _pend_id;
}

void BotWeb::writeResult(char* dst, size_t cap) {
  JW w(dst, cap);
  w.raw("{");
  w.kint("id", _res_id);
  w.kstr("status", RES_NAMES[_res_status]);
  w.kstr("message", _res_msg);
  w.endObj();
  if (!w.ok) snprintf(dst, cap, "{\"id\":%u,\"status\":\"%s\",\"message\":\"\"}", (unsigned)_res_id, RES_NAMES[_res_status]);
}

// The login rule, shared by /api/login, /api/password and /api/setup: refused while this
// address (or everyone) is locked out; otherwise one PBKDF2 check against copies of the salt
// and hash, made without holding _mux.
bool BotWeb::verifyPassword(const char* plain, uint32_t ip, int& code, char* err, size_t err_cap) {
  char salt[17], hash[65];
  {
    Lock l(_mux);
    uint32_t wait_s = _limiter.retryAfterS(ip, millis());
    if (wait_s) {
      code = 429;
      snprintf(err, err_cap, "Too many wrong passwords. Try again in %u s", (unsigned)wait_s);
      return false;
    }
    memcpy(salt, _salt, sizeof(salt));
    memcpy(hash, _hash, sizeof(hash));
  }
  bool ok = plain && botPasswordVerify(plain, salt, hash);
  botWipe(hash, sizeof(hash));
  botWipe(salt, sizeof(salt));
  Lock l(_mux);
  if (ok) {
    _limiter.success(ip);
    return true;
  }
  _limiter.fail(ip, millis());
  code = 403;
  snprintf(err, err_cap, "Wrong password");
  return false;
}

void BotWeb::handlePage(AsyncWebServerRequest* r) {
  r->send(200, "text/html", (const uint8_t*)BOT_WEB_HTML, sizeof(BOT_WEB_HTML) - 1);
}

void BotWeb::handleState(AsyncWebServerRequest* r) {
  BotWebMode m = mode();
  if (m == BWM_SETUP) { sendSetupState(r); return; }
  if (m == BWM_NEEDS_PASSWORD) {
    char buf[96];
    snprintf(buf, sizeof(buf), "{\"setup\":false,\"authed\":false,\"needs_password\":true,\"mdns\":\"%s.local\"}", _mdns_name);
    sendJson(r, 200, buf);
    return;
  }
  if (!authed(r, nullptr, false)) { sendJson(r, 200, "{\"setup\":false,\"authed\":false}"); return; }
  static const char PRE[] = "{\"setup\":false,\"authed\":true,";
  sendSnapshot(r, SK_STATE, PRE, sizeof(PRE) - 1, true);
}

// The settings, as their own document (they no longer fit beside the state in one buffer).
void BotWeb::handleConfigGet(AsyncWebServerRequest* r) {
  if (!authed(r, nullptr, false)) { sendErr(r, 401, "please sign in"); return; }
  sendSnapshot(r, SK_CONFIG, "", 0, false);
}

// Answers with pre + the buffer's text (+ ",\"result\":{...}}" for the state), or 503 when the
// buffer cannot be had in time, holds something else, or the response cannot be allocated.
// Never a partial document.
void BotWeb::sendSnapshot(AsyncWebServerRequest* r, SnapKind want, const char* pre, size_t pre_len, bool with_result) {
  if (!waitFresh(want)) { sendErr(r, 503, "busy, try again"); return; }
  char res[RES_JSON_CAP];
  Lock l(_mux);
  if (_state_err || _state_kind != want) { sendErr(r, 503, "busy, try again"); return; }
  static const char MID[] = ",\"result\":";
  static const char END[] = "}";
  size_t tail = 0;
  if (with_result) {
    writeResult(res, sizeof(res));
    tail = (sizeof(MID) - 1) + strlen(res) + (sizeof(END) - 1);
  }
  // Exactly what is written below: sizes come from the literals and the text, never a hand count.
  size_t want_len = pre_len + _state_len + tail;
  AsyncResponseStream* s = r->beginResponseStream("application/json", want_len + 8);
  size_t put = s->write((const uint8_t*)pre, pre_len);
  put += s->write((const uint8_t*)_state, _state_len);
  if (with_result) {
    put += s->write((const uint8_t*)MID, sizeof(MID) - 1);
    put += s->write((const uint8_t*)res, strlen(res));
    put += s->write((const uint8_t*)END, sizeof(END) - 1);
  }
  if (put != want_len || s->available() != want_len) {   // the buffer could not grow
    delete s;
    sendErr(r, 503, "low memory, try again");
    return;
  }
  r->send(s);
}

void BotWeb::handleResult(AsyncWebServerRequest* r) {
  if (!authed(r, nullptr, false)) { sendErr(r, 401, "please sign in"); return; }
  char res[RES_JSON_CAP];
  {
    Lock l(_mux);
    writeResult(res, sizeof(res));
  }
  sendJson(r, 200, res);
}

void BotWeb::handleLogin(AsyncWebServerRequest* r) {
  if (!r->_tempObject) { sendErr(r, 400, "bad request"); return; }
  JsonDocument doc(WipingAllocator::instance());
  DeserializationError e = deserializeJson(doc, (const char*)r->_tempObject);
  wipeBody(r);
  if (e) { sendParseErr(r, e); return; }
  int code = 400;
  char err[80];
  if (!verifyPassword(botJsonStr(doc["password"]), clientIp(r), code, err, sizeof(err))) {
    sendErr(r, code == 403 ? 401 : code, err);
    return;
  }
  uint8_t rnd[16];
  esp_fill_random(rnd, sizeof(rnd));
  char tok[33];
  {
    Lock l(_mux);
    _sessions.create(rnd, millis(), tok);
  }
  botWipe(rnd, sizeof(rnd));
  AsyncWebServerResponse* res = r->beginResponse(200, "application/json", "{\"ok\":true}");
  char cookie[96];
  snprintf(cookie, sizeof(cookie), "bot=%s; HttpOnly; SameSite=Strict; Path=/", tok);
  res->addHeader("Set-Cookie", cookie);
  r->send(res);
}

void BotWeb::handleLogout(AsyncWebServerRequest* r) {
  char tok[33];
  if (!authed(r, tok, true)) { sendErr(r, 401, "please sign in"); return; }
  {
    Lock l(_mux);
    _sessions.revoke(tok);
  }
  AsyncWebServerResponse* res = r->beginResponse(200, "application/json", "{\"ok\":true}");
  res->addHeader("Set-Cookie", "bot=; Max-Age=0; HttpOnly; SameSite=Strict; Path=/");
  r->send(res);
}

// The body may hold the broker password: it is wiped on every way out of here, except when it is
// handed to the loop task, which wipes it once parsed (applyConfigReq) or when drain() ends.
void BotWeb::handleConfig(AsyncWebServerRequest* r) {
  if (!r->_tempObject) { sendErr(r, 400, "bad request"); return; }
  if (!authed(r, nullptr, true)) { wipeBody(r); sendErr(r, 401, "please sign in"); return; }
  {
    // Only the two keys checked here are kept (the whole body is still parsed, so a malformed
    // one is refused): a few hundred bytes instead of a settings-sized document on this task.
    JsonDocument filter;
    filter["node_name"] = true;
    filter["scope"] = true;
    JsonDocument doc(WipingAllocator::instance());
    DeserializationError e = deserializeJson(doc, (const char*)r->_tempObject, DeserializationOption::Filter(filter));
    if (e) { wipeBody(r); sendParseErr(r, e); return; }
    if (!doc.is<JsonObject>()) { wipeBody(r); sendErr(r, 400, "bad request"); return; }
    JsonVariantConst nm = doc["node_name"];
    if (!nm.isNull() && !botNodeNameOk(botJsonStr(nm))) {
      wipeBody(r);
      sendErr(r, 400, "name must be 1 to 31 characters, without [ ] \\ : , ? *");
      return;
    }
    JsonVariantConst sc = doc["scope"];   // same rule as the setup form
    if (!sc.isNull() && !botScopeOk(botJsonStr(sc))) {
      wipeBody(r);
      sendErr(r, 400, "scope: up to 30 lower-case letters, digits or hyphens");
      return;
    }
  }
  uint32_t id;
  {
    Lock l(_mux);
    if (_pend_kind != K_NONE) { wipeBody(r); sendErr(r, 503, "busy, try again"); return; }
    _pend_json = (char*)r->_tempObject;   // take the body: the request no longer frees it
    _pend_json_len = r->contentLength();
    r->_tempObject = nullptr;
    id = enqueue(K_CONFIG);
  }
  sendId(r, id);
}

void BotWeb::handlePassword(AsyncWebServerRequest* r) {
  if (!r->_tempObject) { sendErr(r, 400, "bad request"); return; }
  char tok[33];
  if (!authed(r, tok, true)) { wipeBody(r); sendErr(r, 401, "please sign in"); return; }
  JsonDocument doc(WipingAllocator::instance());
  DeserializationError e = deserializeJson(doc, (const char*)r->_tempObject);
  wipeBody(r);
  if (e) { sendParseErr(r, e); return; }
  const char* next = botJsonStr(doc["next"]);
  if (!botWebPasswordOk(next)) { sendErr(r, 400, "new password must be 8 to 63 characters"); return; }
  {
    Lock l(_mux);
    if (_pend_kind != K_NONE) { sendErr(r, 503, "busy, try again"); return; }
  }
  int code = 400;
  char err[80];
  if (!verifyPassword(botJsonStr(doc["current"]), clientIp(r), code, err, sizeof(err))) {
    sendErr(r, code, code == 403 ? "current password is wrong" : err);
    return;
  }
  uint32_t id;
  {
    Lock l(_mux);
    if (_pend_kind != K_NONE) { sendErr(r, 503, "busy, try again"); return; }
    StrHelper::strncpy(_pend_pw, next, sizeof(_pend_pw));
    memcpy(_pend_token, tok, sizeof(_pend_token));
    id = enqueue(K_PASSWORD);
  }
  sendId(r, id);
}

void BotWeb::handleAdvert(AsyncWebServerRequest* r) {
  if (!r->_tempObject) { sendErr(r, 400, "bad request"); return; }
  if (!authed(r, nullptr, true)) { sendErr(r, 401, "please sign in"); return; }
  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, (const char*)r->_tempObject);
  if (e) { sendParseErr(r, e); return; }
  if (!doc["flood"].is<bool>()) { sendErr(r, 400, "bad request"); return; }
  _bot->requestAdvert(doc["flood"].as<bool>());   // documented as safe from any task
  sendJson(r, 200, "{\"ok\":true}");
}

// Not normally reached: the guard answers every unknown path itself.
void BotWeb::handleNotFound(AsyncWebServerRequest* r) {
  sendErr(r, 404, "not found");
}

#endif  // WITH_BOT
