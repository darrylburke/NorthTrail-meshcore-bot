#pragma once

// The bot's configuration web page (port 80, plain HTTP) and its first-time setup.
//
// The mode is chosen at boot:
//  - SETUP when WiFi is not usable (no SSID saved, or WiFi disabled in prefs). main.cpp then
//    starts neither the station nor the companion TCP interface, and this raises an open
//    access point "MeshBot-XXXX" (never next to a station connection) with a DNS responder
//    that sends every name to the node. Only clients on that access point are served, and
//    only the setup form. If a web password already exists, saving needs it. Without one, the
//    saved network's name is not shown and its stored WiFi password is never reused (the form
//    must carry it). The access point closes after 15 minutes without a request from it
//    (the open page pings it every minute), until the next reboot. It is never reopened
//    automatically: wrong WiFi details are corrected over USB (set wifi.ssid / wifi.pwd).
//    A web password changed over USB ends every session at the next loop().
//  - NEEDS_PASSWORD when WiFi is usable but no web password is set: the page, on the station
//    interface, shows only a notice to set it over USB (`set bot.webpwd ...`). Once set,
//    the page switches to NORMAL without a reboot.
//  - NORMAL: password login, then the dashboard.
// The page is reachable as the station IP, the access point IP or http://meshbot-xxxx.local
// (mDNS); requests naming any other Host are refused (DNS rebinding).
//
// THREAD MODEL. Route handlers run on the async_tcp task. They never touch BotTask, MyMesh,
// the file system or the radio (the one exception is BotTask::requestAdvert(), documented as
// safe from any task). They read only a snapshot that loop() (Arduino loop task) refreshes,
// and they change things only by placing one request in a single-slot queue that loop()
// drains and applies on the loop task. Snapshot, queue, result, sessions, the login lockout
// and the Host allowlist are guarded by _mux. Password checks (about a second of PBKDF2) run
// on the web task against a copy of the salt and hash, never holding _mux.
//
// EARLY GUARD. A handler registered first (BotWeb.cpp) looks at every request once its
// headers are in and, through the pure botGuard() in BotWebRules.h, answers it at once when
// it must be refused. That handler is "trivial", so the server then discards the body
// without parsing or buffering it. Only requests that pass reach a route and have their
// (size-capped) JSON body buffered.
//
// SNAPSHOT BUFFER. One STATE_CAP buffer, allocated once, holds the text of either the
// signed-in part of /api/state (status, node, recent) or the settings document for
// GET /api/config; _state_kind says which. A handler asks the loop task for the kind it needs
// and copies it into its response under _mux.
//
// A SETTINGS SAVE WAITS FOR LOOK-UPS. Applying a full save needs a few tens of KB of heap for a
// moment, and a TLS request needs about 52 KB: together they could exhaust the heap. So drain()
// leaves a queued settings save pending while the worker is busy or a command is parked, and
// applies it on the first pass when the network side is idle (after CONFIG_HOLD_MS at the latest).
// Meanwhile GET /api/result reports it as "waiting".
//
// TRYING A COMMAND. POST /api/try queues the text (kind K_TRY, the same single slot); loop()
// hands it to BotTask::tryCommand() and polls BotTask::takeTryResult() on every pass, keeping
// the most recent try's reply (id, done, text, whether the broker answered it) under _mux for
// GET /api/try. Nothing a try produces is ever transmitted.
//
// THE BROKER ON THE PAGE. The state carries an "mqtt" member (BotWebMqtt.h): the connection's
// state and words, the prefix, counters and the announced commands, built on the loop task from
// BotTask::mqttStatus() and the announced list; never the password, user name or host. A settings
// save that moves the broker (host or port) without a new password clears the stored one
// (botCfgBrokerMoveClearsPass) and says so in the save result.

#ifdef WITH_BOT

#include <Arduino.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <helpers/bot/BotSession.h>
#include <helpers/bot/BotWebRules.h>

class AsyncWebServer;
class AsyncWebServerRequest;
class DNSServer;
class BotTask;
class MyMesh;

class BotWeb {
public:
  void begin(BotTask& bot, MyMesh& mesh);
  void loop();
  bool inSetupMode() const { return mode() == BWM_SETUP; }

  // Entry points for the async_tcp task. Public only so the route lambdas can reach them.
  int  guardCode(AsyncWebServerRequest* r);
  void guardRespond(AsyncWebServerRequest* r, int code);
  void handlePage(AsyncWebServerRequest* r);
  void handleState(AsyncWebServerRequest* r);
  void handleResult(AsyncWebServerRequest* r);
  void handleSetup(AsyncWebServerRequest* r);
  void handleLogin(AsyncWebServerRequest* r);
  void handleLogout(AsyncWebServerRequest* r);
  void handleConfig(AsyncWebServerRequest* r);
  void handleConfigGet(AsyncWebServerRequest* r);
  void handleTry(AsyncWebServerRequest* r);      // BotWebTry.cpp
  void handleTryGet(AsyncWebServerRequest* r);   // BotWebTry.cpp
  void handlePassword(AsyncWebServerRequest* r);
  void handleAdvert(AsyncWebServerRequest* r);
  void handleNotFound(AsyncWebServerRequest* r);

  static const size_t MAX_BODY = BOT_CONFIG_BODY_CAP;   // the largest of botRouteBodyCap()
  // The snapshot buffer's size, STATE_CAP, is derived in BotWeb.cpp: the settings document (at
  // most BOT_CONFIG_JSON_MAX; the arithmetic is beside it in BotWebRules.h) or the state, whose
  // fixed parts (with the "mqtt" member, BOT_WEB_MQTT_JSON_MAX in BotWebMqtt.h) are bounded there
  // and whose oldest recent lines are dropped to fit.
  static const size_t RES_MSG = 128;
  static const size_t RES_JSON_CAP = RES_MSG * 6 + 64;   // the result object, every byte escaped
  static const uint32_t AP_IDLE_MS = 15u * 60u * 1000u;
  static const size_t TRY_REPLY = 128;   // a try's reply as kept (BotTryHolder keeps 127 bytes)
  // {"id":<u32>,"done":true,"busy":true,"broker":true,"reply":"<every byte escaped as \u00XX>"}
  static const size_t TRY_JSON_CAP =
      sizeof("{\"id\":4294967295,\"done\":true,\"busy\":true,\"broker\":true,\"reply\":\"\"}") + (TRY_REPLY - 1) * 6;

private:
  enum Kind : uint8_t { K_NONE = 0, K_CONFIG, K_PASSWORD, K_SETUP, K_TRY };
  enum SnapKind : uint8_t { SK_NONE = 0, SK_STATE, SK_CONFIG };   // what _state holds
  // RS_WAITING: a settings save held back while a look-up runs (see drain()).
  enum ResStatus : uint8_t { RS_NONE = 0, RS_PENDING, RS_OK, RS_NOT_SAVED, RS_REJECTED, RS_PARTIAL, RS_WAITING };
  // A held settings save is applied anyway after this long.
  static const uint32_t CONFIG_HOLD_MS = 60000u;

  BotWebMode mode() const { return (BotWebMode)_mode.load(); }

  // loop task (BotWeb.cpp)
  bool startServer();
  void refreshSnapshot();
  void buildState();
  void buildConfig();
  size_t writeMqtt(char* dst, size_t cap);   // BotWebMqtt.cpp: the state's "mqtt" member
  void drain();
  void applyConfigReq(uint8_t& status, char* msg, size_t cap);
  void applyPasswordReq(uint8_t& status, char* msg, size_t cap);
  // loop task (BotWebTry.cpp)
  void applyTryReq();
  void pollTry();
  // loop task (BotWebSetup.cpp)
  void startSetupAp();
  void loopSetupAp();
  void loopMdns();
  void applySetupReq(uint8_t& status, char* msg, size_t cap);

  // async_tcp task
  bool authed(AsyncWebServerRequest* r, char token_out[33], bool touch);
  bool fromAp(AsyncWebServerRequest* r) const;
  bool waitFresh(SnapKind want);
  uint32_t enqueue(Kind k);   // under _mux; 0 when the slot is busy
  void writeResult(char* dst, size_t cap);   // under _mux
  void writeTry(char* dst, size_t cap);      // under _mux (BotWebTry.cpp)
  void sendSnapshot(AsyncWebServerRequest* r, SnapKind want, const char* pre, size_t pre_len, bool with_result);
  bool verifyPassword(const char* plain, uint32_t ip, int& code, char* err, size_t err_cap);
  void sendSetupState(AsyncWebServerRequest* r);   // BotWebSetup.cpp
  static uint32_t clientIp(AsyncWebServerRequest* r);
  static void wipeBody(AsyncWebServerRequest* r);

  BotTask* _bot = nullptr;
  MyMesh* _mesh = nullptr;
  AsyncWebServer* _server = nullptr;
  DNSServer* _dns = nullptr;
  SemaphoreHandle_t _mux = nullptr;
  std::atomic<uint8_t> _mode{BWM_NORMAL};
  bool _started = false;
  std::atomic<bool> _ap_up{false};   // written on the loop task, read by the guard
  bool _mdns_started = false;
  uint32_t _last_try_ms = 0;
  uint32_t _last_snap_ms = 0;
  uint32_t _restart_at = 0;
  uint32_t _pw_epoch_seen = 0;             // BotTask::webPasswordEpoch() last acted on (loop task)
  bool _cfg_held = false;                  // loop task: the pending K_CONFIG is being held back
  uint32_t _cfg_held_ms = 0;               // loop task: when the hold began
  std::atomic<uint32_t> _ap_last_ms{0};   // last request from an access point client
  char _ap_ssid[16] = {0};
  char _mdns_name[16] = {0};               // "meshbot-xxxx" (constant after begin)

  // ---- guarded by _mux ----
  BotSessions _sessions;
  BotLoginLimiter _limiter;
  char _salt[17] = {0};
  char _hash[65] = {0};
  char _node_name[32] = {0};
  char _wifi_ssid[33] = {0};
  bool _wifi_has_pwd = false;   // a WiFi password is stored for _wifi_ssid
  char _sta_ip[16] = {0};       // "" when the station has no address
  char _ap_ip[16] = {0};        // "" when the access point is down
  char* _state = nullptr;       // STATE_CAP bytes, allocated once (NORMAL mode only)
  size_t _state_len = 0;
  bool _state_err = false;      // the last build could not be completed: answer "busy"
  SnapKind _state_kind = SK_NONE;   // what _state holds (SK_NONE: nothing usable)
  uint32_t _state_ms = 0;       // millis() of the last build
  uint32_t _state_seq = 0;      // builds so far
  // The pending request. Its payload is written by a handler before _pend_kind is set, then
  // owned by the loop task until it sets _pend_kind back to K_NONE.
  Kind _pend_kind = K_NONE;
  uint32_t _pend_id = 0;
  char* _pend_json = nullptr;   // K_CONFIG: the posted body (heap, <= MAX_BODY)
  size_t _pend_json_len = 0;
  char _pend_token[33] = {0};   // K_PASSWORD: the caller's session, kept
  char _pend_pw[64] = {0};      // K_PASSWORD: the new password
  BotSetupData _pend_setup;     // K_SETUP
  bool _pend_setup_authed = false;   // K_SETUP: the caller proved the current web password
  char _pend_try[BOT_TRY_TEXT_MAX + 1] = {0};   // K_TRY: the text to try
  uint32_t _next_id = 0;
  uint32_t _res_id = 0;
  uint8_t _res_status = RS_NONE;
  char _res_msg[RES_MSG] = {0};
  // The most recent try (a new POST replaces it). Separate from _res_*, so a try never hides
  // the result of a save the page is still waiting for.
  uint32_t _try_id = 0;         // 0: none yet
  bool _try_done = false;
  bool _try_busy = false;       // BotTask was still running an earlier try
  bool _try_broker = false;     // the reply came from the MQTT broker
  char _try_reply[TRY_REPLY] = {0};

  std::atomic<bool> _refresh_req{false};   // a handler wants a fresh state
  std::atomic<bool> _config_req{false};    // a handler wants the settings document
};

extern BotWeb bot_web;

#endif  // WITH_BOT
