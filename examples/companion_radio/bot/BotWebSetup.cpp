#ifdef WITH_BOT

// Setup mode (the open access point, its DNS responder, the setup form) and mDNS.

#include "BotWeb.h"
#include "BotTask.h"
#include "BotWebInternal.h"

#include <WiFi.h>
#include <AsyncTCP.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <helpers/bot/BotConfig.h>

using namespace botweb;

extern BotConfig bot_web_scratch;

// ---- loop task ----

// Raised only when WiFi is not usable, so main.cpp has started neither the station nor the
// companion TCP interface: the access point never runs next to a station connection.
void BotWeb::startSetupAp() {
  const uint8_t* pk = _mesh->self_id.pub_key;
  snprintf(_ap_ssid, sizeof(_ap_ssid), "MeshBot-%02X%02X", pk[0], pk[1]);
  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(_ap_ssid)) {
    Serial.println("bot web: could not start the setup access point");
    return;
  }
  delay(100);   // let the AP interface settle before reading its address
  IPAddress ip = WiFi.softAPIP();
  _dns = new DNSServer();
  _dns->start(53, "*", ip);   // captive portal: every name resolves to the node
  _ap_up = true;
  _ap_last_ms = millis();
  refreshSnapshot();          // the Host allowlist needs the AP address before the first request
  if (startServer())
    Serial.printf("bot web: setup access point '%s' open, browse to http://%s/ (closes after 15 min unused)\n",
                  _ap_ssid, ip.toString().c_str());
}

// DNS, and the idle timeout: 15 minutes without a request from an access point client
// closes the access point until the next reboot.
void BotWeb::loopSetupAp() {
  if (!_ap_up) return;
  if (_dns) _dns->processNextRequest();
  uint32_t last = _ap_last_ms.load();   // read before the clock, and compared signed
  if (_restart_at == 0 && botApIdleExpired(millis(), last, AP_IDLE_MS)) {
    _ap_up = false;
    if (_dns) _dns->stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    refreshSnapshot();
    Serial.printf("bot web: setup access point '%s' closed after 15 min unused; reboot to reopen it\n", _ap_ssid);
  }
}

// mDNS "meshbot-xxxx.local" once the station has an address (station modes only).
void BotWeb::loopMdns() {
  if (_mdns_started || WiFi.status() != WL_CONNECTED) return;
  _mdns_started = true;   // one attempt per boot
  if (MDNS.begin(_mdns_name)) MDNS.addService("http", "tcp", 80);
  else Serial.println("bot web: mDNS did not start");
}

void BotWeb::applySetupReq(uint8_t& status, char* msg, size_t cap) {
  const BotSetupData& d = _pend_setup;
  // Same network, empty password field: keep the stored password (null = keep), but only for
  // a caller who proved the web password. handleSetup refused the others; checked again here.
  const NodePrefs* np = _mesh->getNodePrefs();
  BotWifiPwdRule rule = botSetupWifiPasswordRule(d.ssid, d.wifi_password, np->wifi_ssid, np->wifi_pwd[0] != 0, _pend_setup_authed);
  if (rule == BOT_WIFI_REFUSE) {
    status = RS_REJECTED;
    snprintf(msg, cap, "Type the WiFi password for %s", d.ssid);
    return;
  }
  bool wifi_ok = _mesh->botSaveWifi(d.ssid, rule == BOT_WIFI_KEEP_STORED ? nullptr : d.wifi_password);
  bool pw_ok = d.web_password[0] ? _bot->setWebPassword(d.web_password) : _bot->hasWebPassword();
  memcpy(&bot_web_scratch, &_bot->config(), sizeof(bot_web_scratch));
  bot_web_scratch.lat = d.lat;
  bot_web_scratch.lon = d.lon;
  memset(bot_web_scratch.scope, 0, sizeof(bot_web_scratch.scope));
  StrHelper::strncpy(bot_web_scratch.scope, d.scope, sizeof(bot_web_scratch.scope));
  bool cfg_ok = _bot->applyConfig(bot_web_scratch);
  botWipe(&bot_web_scratch, sizeof(bot_web_scratch));   // a whole copy, passwords and hash included
  bool name_ok = _bot->applyNodeName(d.name);
  if (wifi_ok && pw_ok && cfg_ok && name_ok) {
    status = RS_OK;
    snprintf(msg, cap, "Saved. The node restarts now and joins %s", d.ssid);
    _restart_at = millis() + 1500;
    if (_restart_at == 0) _restart_at = 1;
  } else {
    status = RS_NOT_SAVED;
    snprintf(msg, cap, "Could not save (%s%s%s%s). Try again.", wifi_ok ? "" : "WiFi ", pw_ok ? "" : "password ",
             cfg_ok ? "" : "settings ", name_ok ? "" : "name ");
  }
}

// ---- async_tcp task ----

void BotWeb::sendSetupState(AsyncWebServerRequest* r) {
  char res[RES_JSON_CAP];
  char buf[RES_JSON_CAP + 640];   // + ap, mdns, name and ssid with every byte escaped
  JW w(buf, sizeof(buf));
  w.raw("{\"setup\":true,\"authed\":false,");
  {
    Lock l(_mux);
    w.kstr("ap", _ap_ssid);
    w.kbool("has_password", _hash[0] != 0);
    w.kstr("name", _node_name);
    // The saved network's name only once a web password protects setup (it is shown to
    // anyone who joins the open access point).
    if (_hash[0]) w.kstr("ssid", _wifi_ssid);
    writeResult(res, sizeof(res));
  }
  char host[24];
  snprintf(host, sizeof(host), "%s.local", _mdns_name);
  w.kstr("mdns", host);
  w.key("result");
  w.raw(res);
  w.raw("}");
  sendJson(r, 200, w.ok ? buf : "{\"setup\":true,\"authed\":false}");
}

void BotWeb::handleSetup(AsyncWebServerRequest* r) {
  if (!r->_tempObject) { sendErr(r, 400, "bad request"); return; }
  JsonDocument doc(WipingAllocator::instance());
  DeserializationError e = deserializeJson(doc, (const char*)r->_tempObject);
  wipeBody(r);
  if (e) { sendParseErr(r, e); return; }
  bool has_pw;
  {
    Lock l(_mux);
    if (_pend_kind != K_NONE) { sendErr(r, 503, "busy, try again"); return; }
    has_pw = _hash[0] != 0;
    // Without a web password nobody has proved anything: a stored WiFi password is never reused.
    if (!has_pw) {
      const char* ssid = botJsonStr(doc["ssid"]);
      const char* wpw = doc["wifi_password"].isNull() ? "" : botJsonStr(doc["wifi_password"]);
      if (botSetupWifiPasswordRule(ssid, wpw, _wifi_ssid, _wifi_has_pwd, false) == BOT_WIFI_REFUSE) {
        sendErr(r, 400, "type the WiFi password for this network");
        return;
      }
    }
  }
  // Once a web password exists, only someone who knows it may redo setup.
  if (has_pw) {
    int code = 400;
    char err[80];
    if (!verifyPassword(botJsonStr(doc["current"]), clientIp(r), code, err, sizeof(err))) {
      sendErr(r, code, code == 403 ? "current page password is wrong" : err);
      return;
    }
  }
  uint32_t id;
  {
    Lock l(_mux);
    if (_pend_kind != K_NONE) { sendErr(r, 503, "busy, try again"); return; }
    const char* err = botParseSetup(doc.as<JsonVariantConst>(), _pend_setup, has_pw);   // slot is free: ours to fill
    if (err) { botWipe(&_pend_setup, sizeof(_pend_setup)); sendErr(r, 400, err); return; }
    _pend_setup_authed = has_pw;   // has_pw: verifyPassword() above succeeded
    id = enqueue(K_SETUP);
  }
  sendId(r, id);
}

#endif  // WITH_BOT
