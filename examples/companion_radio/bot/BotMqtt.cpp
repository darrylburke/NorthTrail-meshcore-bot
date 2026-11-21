#ifdef WITH_BOT

#include "BotMqtt.h"

#include <WiFi.h>
#include <esp_system.h>
#include <esp_timer.h>
#include "../MyMesh.h"   // FIRMWARE_VERSION

// Memory taken for good, item by item (the sizes are the constants named, never counted by hand):
//   static, in bot_task:  this object (the announced-command table, three BotMqttConn, the topics,
//                         the control task's copies of the settings) and BotTask's BotAskTable
//   once, when MQTT is first enabled (allocate(); never freed):
//     _scratch            BOT_MQTT_SCRATCH   the loop's record: one command or one inbox message
//     _crx                BOT_MQTT_SCRATCH   the control task's record being run
//     _hbuf               BOT_MQTT_REC_MAX   the handler's record (also its subscribe topics)
//     _mb                 BOT_MQTT_INBOX     the inbox (plus its control block)
//     _cmd_mb             BOT_MQTT_CMDQ      the command buffer (plus its control block)
//     bot_mqtt_ctl        BOT_MQTT_CTL_STACK the control task's stack (plus its TCB)
//   while a client exists (freed by esp_mqtt_client_destroy):
//     esp-mqtt's task stack BOT_MQTT_TASK_STACK, receive buffer BOT_MQTT_IN_BUF, send buffer
//     BOT_MQTT_OUT_BUF, and its own state (client, copied settings, event loop, transport, socket)
// The command list's buffer is the one exception: allocated for one publish (at most
// BOT_MQTT_LIST_HEAP_MAX bytes) and freed by the control task once it is published or dropped.
//
// esp-mqtt behaviour relied on (ESP-IDF 4.4; the component sources are not in the Arduino package):
//   - esp_mqtt_client_init copies the configuration strings (the password is wiped right after it;
//     the other strings stay unchanged while the client lives anyway).
//   - esp_mqtt_client_publish writes a payload longer than the send buffer in pieces from the
//     caller's memory (mqtt_client.h: "publishing payloads longer than internal buffer (due to
//     message fragmentation)"); it may block, which only the control task does.
//   - The event handler is called on the MQTT task, synchronously, with topic and data pointing
//     into the receive buffer; a message larger than the buffer arrives in several DATA events and
//     only the first carries the topic.
//   - A client's task has run (client->run is set) once it has dispatched its first event; before
//     that esp_mqtt_client_stop reports failure. The control task waits for that event (up to 2 s)
//     and retries a failed stop before it destroys the client.
//   - esp_mqtt_client_start fails only before it created the task, so a client it refused can be
//     destroyed at once.

static_assert(MQTT_ERROR_TYPE_TCP_TRANSPORT == BOT_MQTT_ERRTYPE_TRANSPORT, "error type numbers");
static_assert(MQTT_ERROR_TYPE_CONNECTION_REFUSED == BOT_MQTT_ERRTYPE_REFUSED, "error type numbers");
static_assert(MQTT_CONNECTION_REFUSE_PROTOCOL == BOT_MQTT_RC_PROTOCOL, "CONNACK codes");
static_assert(MQTT_CONNECTION_REFUSE_ID_REJECTED == BOT_MQTT_RC_ID_REJECTED, "CONNACK codes");
static_assert(MQTT_CONNECTION_REFUSE_SERVER_UNAVAILABLE == BOT_MQTT_RC_UNAVAILABLE, "CONNACK codes");
static_assert(MQTT_CONNECTION_REFUSE_BAD_USERNAME == BOT_MQTT_RC_BAD_USER, "CONNACK codes");
static_assert(MQTT_CONNECTION_REFUSE_NOT_AUTHORIZED == BOT_MQTT_RC_NOT_AUTHORIZED, "CONNACK codes");

static_assert(BOT_MQTT_REC_MAX == BOT_MQTT_REC_HEAD + BOT_MQTT_TAIL_MAX + BOT_MQTT_REPLY_IN_MAX, "a record holds the largest reply");
static_assert(BOT_MQTT_SCRATCH >= BOT_MQTT_REC_MAX, "the scratch holds any inbox record");
static_assert(BOT_MQTT_INBOX >= 4 * (BOT_MQTT_REC_MAX + 4), "the inbox holds at least four of the largest records");
static_assert(BOT_MQTT_CMDQ >= 2 * (BOT_MQTT_SCRATCH + 4), "the command buffer holds two of the largest requests");
static_assert(BOT_MQTT_REPLY_IN_MAX + BOT_MQTT_TOPIC_MAX + 5 + 2 <= BOT_MQTT_IN_BUF, "a whole reply fits the receive buffer");
// CONNECT: fixed header 5, protocol 10, client id, will topic and message, user, password, each with 2 length bytes.
static_assert(5 + 10 + (2 + 15) + (2 + BOT_MQTT_TOPIC_MAX) + (2 + 19) + (2 + 32) + (2 + 64) <= BOT_MQTT_OUT_BUF,
              "CONNECT fits the send buffer");

static const char BOT_MQTT_OFFLINE[] = "{\"state\":\"offline\"}";

BotMqtt::BotMqtt()
    : _want_pending(false), _enabled(false), _scratch(nullptr), _phase(PH_IDLE), _run_gen(0), _stop_gen(0), _gen(0),
      _wait_ms(5000), _wifi(false), _tq_n(0), _seen_connects(0), _seen_disconnects(0), _need_status(false),
      _status_try_ms(0), _list_dirty(false), _list_ms(0), _start_failed(false), _start_retry_ms(0), _restart_failed(false),
      _restart_retry_ms(0), _connects(0), _published(0), _answered(0), _timed_out(0), _loop_dropped(0), _cmd_mb(nullptr),
      _snap_busy(false), _ctl_task(nullptr), _ctl_live(0), _ctl_failed(0), _ctl_stopped(0), _ctl_stack(0), _ctl_stop_fails(0), _crx(nullptr),
      _ctl_client(nullptr), _ctl_gen(0), _mb(nullptr), _hbuf(nullptr), _h_client(nullptr), _h_gen(0), _h_link(BML_IDLE),
      _h_events(0), _h_connects(0), _h_disconnects(0), _h_received(0), _h_dropped(0), _h_stack(0) {
  botMqttConnWipe(_cur);
  botMqttConnWipe(_want);
  botMqttConnWipe(_snap);
  _node[0] = _client_id[0] = _status_topic[0] = _topic[0] = '\0';
  _ctl_uri[0] = _ctl_user[0] = _ctl_lwt[0] = _ctl_prefix[0] = '\0';
}

void BotMqtt::begin(const uint8_t* pub_key) {
  if (pub_key) snprintf(_client_id, sizeof(_client_id), "meshbot-%02x%02x", pub_key[0], pub_key[1]);
  else snprintf(_client_id, sizeof(_client_id), "meshbot");
}

// Everything is made once, when MQTT is first wanted; nothing is freed.
bool BotMqtt::allocate() {
  if (!_scratch) _scratch = (char*)malloc(BOT_MQTT_SCRATCH);
  if (!_crx) _crx = (uint8_t*)malloc(BOT_MQTT_SCRATCH);
  if (!_hbuf) _hbuf = (uint8_t*)malloc(BOT_MQTT_REC_MAX);
  if (!_mb) _mb = xMessageBufferCreate(BOT_MQTT_INBOX);
  if (!_cmd_mb) _cmd_mb = xMessageBufferCreate(BOT_MQTT_CMDQ);
  if (!_scratch || !_crx || !_hbuf || !_mb || !_cmd_mb) return false;
  if (!_ctl_task && xTaskCreate(ctlEntry, "bot_mqtt_ctl", BOT_MQTT_CTL_STACK, this, BOT_MQTT_CTL_PRIO, &_ctl_task) != pdPASS) {
    _ctl_task = nullptr;
    return false;
  }
  return true;
}

void BotMqtt::applySettings(const BotConfig& cfg, const char* node_name) {
  _enabled = cfg.mqtt_enabled;
  _wait_ms = (uint32_t)(cfg.mqtt_wait_s < 1 ? 1 : cfg.mqtt_wait_s > 15 ? 15 : cfg.mqtt_wait_s) * 1000u;
  botClamp(_node, sizeof(_node), node_name ? node_name : "", sizeof(_node) - 1);
  botMqttConnFrom(_want, cfg, node_name);
  _want_pending = botMqttNeedsRestart(_cur, _want);
  if (!_want_pending) { botMqttConnWipe(_want); _restart_failed = false; }
  _list_dirty = true;   // the bot's own commands may have changed too
}

// Not while a settings change waits to be applied: the owner may have just switched MQTT off or
// moved it, and the old client must neither be offered commands nor deliver replies meanwhile (also
// when its stop could not be posted yet).
bool BotMqtt::ready() const {
  return _phase == PH_RUNNING && !_want_pending && _wifi && botMqttLinkReady(_h_link.load()) &&
         _seen_connects == _h_connects.load() && _seen_disconnects == _h_disconnects.load();
}

// ---- posting to the control task (loop task, never waits) ----

// The payload of a BMC_PUB is already at _scratch + BOT_MQTT_CTL_OFF; it is moved down behind the
// topic. False when the record does not fit the command buffer now (nothing was posted).
bool BotMqtt::post(uint8_t type, uint32_t gen, uint8_t qos, bool retain, const char* topic, size_t len, char* ptr) {
  if (!_cmd_mb || !_scratch) return false;
  _last_n = 0;   // the scratch buffer is reused: a reply kept there is gone
  BotMqttCtl h;
  memset(&h, 0, sizeof(h));
  h.type = type;
  h.qos = qos;
  h.retain = retain ? 1 : 0;
  h.gen = gen;
  size_t tl = topic ? strlen(topic) : 0;
  if (tl > BOT_MQTT_TOPIC_MAX) return false;
  h.topic_len = (uint8_t)tl;
  size_t n = sizeof(h);
  if (type == BMC_PUB || type == BMC_PUBP) {
    if (tl == 0) return false;
    memcpy(_scratch + n, topic, tl + 1);
    n += tl + 1;
    if (type == BMC_PUB) {
      if (len > BOT_MQTT_REQUEST_MAX) return false;
      memmove(_scratch + n, _scratch + BOT_MQTT_CTL_OFF, len);   // n <= BOT_MQTT_CTL_OFF: moves down
      n += len;
    } else {
      h.ptr = ptr;
      h.len = (uint32_t)len;
    }
  }
  memcpy(_scratch, &h, sizeof(h));
  return xMessageBufferSend(_cmd_mb, _scratch, n, 0) == n;
}

// ---- the client's life (loop task) ----

void BotMqtt::syncEvents(uint32_t now_ms) {
  uint32_t d = _h_disconnects.load(), c = _h_connects.load();
  if (d != _seen_disconnects) { _seen_disconnects = d; clearWaits(); }   // a lost connection ends every request
  if (c != _seen_connects) {
    _seen_connects = c;
    _connects++;
    _need_status = true;    // a will may have replaced the retained status
    _status_try_ms = now_ms - 2000u;
    _cmds.clear();          // the broker delivers the retained set again
    _list_dirty = true;
    _list_ms = now_ms - BOT_MQTT_LIST_GAP_MS + BOT_MQTT_LIST_SETTLE_MS;
  }
}

void BotMqtt::loop(uint32_t now_ms) {
  _wifi = WiFi.status() == WL_CONNECTED;
  if (_phase == PH_STARTING) {
    if (_ctl_live.load() == _run_gen) _phase = PH_RUNNING;
    else if (_ctl_failed.load() == _run_gen) { _phase = PH_IDLE; _start_failed = true; _start_retry_ms = now_ms; }
  }
  if (_phase == PH_STOPPING && _ctl_stopped.load() == _stop_gen) {
    _phase = PH_IDLE;
    xMessageBufferReset(_mb);   // what the old client left (no writer exists now)
  }
  syncEvents(now_ms);
  if (_want_pending) {
    if (_phase == PH_RUNNING || _phase == PH_STARTING) {
      if (!_restart_failed || (uint32_t)(now_ms - _restart_retry_ms) >= 5000u) beginStop(now_ms);
    } else if (_phase == PH_IDLE) {
      applyWanted();
    }
  }
  if (_phase == PH_IDLE && !_want_pending && _cur.on && _wifi &&
      (!_start_failed || (uint32_t)(now_ms - _start_retry_ms) >= BOT_MQTT_RECONNECT_MS))
    startClient(now_ms);
  if (_need_status && ready() && (uint32_t)(now_ms - _status_try_ms) >= 2000u) {
    _status_try_ms = now_ms;
    size_t n = botMqttBuildStatus(_scratch + BOT_MQTT_CTL_OFF, BOT_MQTT_REQUEST_MAX + 1, true, FIRMWARE_VERSION, _node,
                                  (uint32_t)(esp_timer_get_time() / 1000000));
    if (n && post(BMC_PUB, _gen, 1, true, _status_topic, n, nullptr)) _need_status = false;
  }
}

// The settings asked for become the current ones (no client exists).
void BotMqtt::applyWanted() {
  if (!botMqttSameSource(_cur, _want)) { _cmds.clear(); _list_dirty = true; }
  memcpy(&_cur, &_want, sizeof(_cur));   // a disabled connection carries no password
  botMqttConnWipe(_want);
  _want_pending = false;
  _restart_failed = false;
  _start_failed = false;
  if (!botMqttTopic(_status_topic, sizeof(_status_topic), _cur.prefix, "status", nullptr)) _cur.on = false;   // cannot happen: prefix <= 64
}

void BotMqtt::startClient(uint32_t now_ms) {
  if (_snap_busy.load()) return;   // the control task has not taken the last snapshot yet
  _start_retry_ms = now_ms;
  _start_failed = true;            // until the start is posted
  if (!allocate()) return;
  if (++_gen == 0) _gen = 1;
  memcpy(&_snap, &_cur, sizeof(_snap));
  _snap_busy.store(true);
  if (!post(BMC_START, _gen, 0, false, nullptr, 0, nullptr)) {
    botMqttConnWipe(_snap);
    _snap_busy.store(false);
    return;
  }
  _run_gen = _gen;
  _phase = PH_STARTING;
  _start_failed = false;
}

void BotMqtt::beginStop(uint32_t now_ms) {
  // While the link is up (whether or not ready(): a clean disconnect sends no will), and best
  // effort, the old client first leaves nothing stale under its prefix: the retained status says
  // "offline" and the retained command list is emptied (a retained message with no payload). Each is
  // posted only when the stop still fits the command buffer after it, so neither can keep the stop
  // from being posted; one that is not posted is simply skipped.
  bool linger = false, list_cleared = false;
  if (_cmd_mb && botMqttLinkUp(_h_link.load())) {
    const size_t rec = sizeof(BotMqttCtl) + sizeof(size_t);   // a record's head and the buffer's length word
    size_t free_b = xMessageBufferSpacesAvailable(_cmd_mb);
    size_t n = botMqttBuildStatus(_scratch + BOT_MQTT_CTL_OFF, BOT_MQTT_REQUEST_MAX + 1, false, nullptr, nullptr, 0);
    size_t need = rec + strlen(_status_topic) + 1 + n;
    if (n && free_b >= need + rec && post(BMC_PUB, _run_gen, 1, true, _status_topic, n, nullptr)) {
      linger = true;
      free_b -= need;
    }
    if (botMqttTopic(_topic, sizeof(_topic), _cur.prefix, "commandlist", nullptr)) {
      need = rec + strlen(_topic) + 1;
      list_cleared = free_b >= need + rec && post(BMC_PUB, _run_gen, 0, true, _topic, 0, nullptr);
    }
  }
  if (!post(BMC_STOP, _run_gen, linger ? 1 : 0, false, nullptr, 0, nullptr)) {   // keep it running; retried in 5 s
    _restart_failed = true;
    _restart_retry_ms = now_ms;
    if (linger) _need_status = true;      // undo the "offline" just posted
    if (list_cleared) _list_dirty = true; // and the emptied list
    return;
  }
  _stop_gen = _run_gen;
  _phase = PH_STOPPING;
  _restart_failed = false;
  if (++_gen == 0) _gen = 1;   // whatever the old client still delivers is dropped
  clearWaits();                // the session that asked is ending
}

// ---- the control task ----

void BotMqtt::ctlEntry(void* arg) { ((BotMqtt*)arg)->ctlRun(); }

void BotMqtt::ctlRun() {
  for (;;) {
    size_t n = xMessageBufferReceive(_cmd_mb, _crx, BOT_MQTT_SCRATCH, portMAX_DELAY);
    if (n < sizeof(BotMqttCtl)) continue;
    BotMqttCtl h;
    memcpy(&h, _crx, sizeof(h));
    const char* topic = (const char*)_crx + sizeof(h);
    switch (h.type) {
      case BMC_START: ctlStart(h.gen); break;
      case BMC_STOP: ctlStop(h.gen, h.qos != 0); break;
      case BMC_PUB:
        if (n >= sizeof(h) + h.topic_len + 1u)
          ctlPublish(h, topic, topic + h.topic_len + 1, n - sizeof(h) - h.topic_len - 1);
        break;
      case BMC_PUBP:
        if (n >= sizeof(h) + h.topic_len + 1u) ctlPublish(h, topic, h.ptr, h.len);
        free(h.ptr);   // owned from the post on, published or not
        break;
      default: break;
    }
    _ctl_stack.store((uint32_t)uxTaskGetStackHighWaterMark(nullptr));
  }
}

void BotMqtt::ctlStart(uint32_t gen) {
  if (_ctl_client) ctlStop(_ctl_gen, false);   // never two clients (the loop never asks for this)
  if (!_snap_busy.load()) { _ctl_failed.store(gen); return; }
  // Settings the client keeps pointing at are copied to buffers only this task writes, while no client exists.
  memcpy(_ctl_prefix, _snap.prefix, sizeof(_ctl_prefix));
  snprintf(_ctl_uri, sizeof(_ctl_uri), "mqtt://%s:%u", _snap.host, (unsigned)_snap.port);
  memcpy(_ctl_user, _snap.user, sizeof(_ctl_user));
  bool topic_ok = botMqttTopic(_ctl_lwt, sizeof(_ctl_lwt), _ctl_prefix, "status", nullptr);
  esp_mqtt_client_config_t c;
  memset(&c, 0, sizeof(c));
  c.uri = _ctl_uri;
  c.client_id = _client_id;      // written once in begin()
  c.username = _ctl_user[0] ? _ctl_user : nullptr;
  c.password = _snap.pass[0] ? _snap.pass : nullptr;
  c.lwt_topic = _ctl_lwt;
  c.lwt_msg = BOT_MQTT_OFFLINE;
  c.lwt_msg_len = (int)(sizeof(BOT_MQTT_OFFLINE) - 1);
  c.lwt_qos = 1;
  c.lwt_retain = 1;
  c.disable_clean_session = 0;
  c.keepalive = BOT_MQTT_KEEPALIVE_S;
  c.disable_auto_reconnect = false;
  c.reconnect_timeout_ms = BOT_MQTT_RECONNECT_MS;
  c.network_timeout_ms = BOT_MQTT_NET_TIMEOUT_MS;
  c.task_prio = BOT_MQTT_TASK_PRIO;
  c.task_stack = BOT_MQTT_TASK_STACK;
  c.buffer_size = BOT_MQTT_IN_BUF;
  c.out_buffer_size = BOT_MQTT_OUT_BUF;
  c.protocol_ver = MQTT_PROTOCOL_V_3_1_1;
  // No certificate, bundle or global CA store: mqtt:// only uses the TCP transport.
  esp_mqtt_client_handle_t cl = topic_ok ? esp_mqtt_client_init(&c) : nullptr;
  c.password = nullptr;
  botMqttConnWipe(_snap);        // init has taken its copy
  _snap_busy.store(false);
  if (!cl) { _ctl_failed.store(gen); return; }
  _h_link.store(BML_IDLE);
  _h_gen.store(gen);
  _h_events.store(0);
  _h_client.store(cl);
  if (esp_mqtt_client_register_event(cl, MQTT_EVENT_ANY, onEvent, this) != ESP_OK || esp_mqtt_client_start(cl) != ESP_OK) {
    _h_client.store(nullptr);
    esp_mqtt_client_destroy(cl);   // never started: there is no task to join
    _ctl_failed.store(gen);
    return;
  }
  _ctl_client = cl;
  _ctl_gen = gen;
  _ctl_live.store(gen);
}

void BotMqtt::ctlStop(uint32_t gen, bool linger) {
  if (!_ctl_client || gen != _ctl_gen) { _ctl_stopped.store(gen); return; }
  esp_mqtt_client_handle_t cl = _ctl_client;
  // A client started a moment ago may not have run yet: wait for its first event (up to 2 s).
  for (int i = 0; i < 100 && _h_events.load() == 0; i++) vTaskDelay(pdMS_TO_TICKS(20));
  if (linger)   // the "offline" status posted before this: up to 500 ms for its acknowledgement
    for (int i = 0; i < 10 && esp_mqtt_client_get_outbox_size(cl) > 0; i++) vTaskDelay(pdMS_TO_TICKS(50));
  bool stopped = false;
  for (int i = 0; i < 5 && !stopped; i++) {
    stopped = esp_mqtt_client_stop(cl) == ESP_OK;   // joins the MQTT task
    if (!stopped) vTaskDelay(pdMS_TO_TICKS(200));
  }
  // Still not stopped: its task may be running (or about to), so the client must not be destroyed
  // under it. Keep the handle and retry with a back-off (1 s, 2 s, 4 s, then every 8 s) until the
  // stop succeeds. The loop stays in its stopping ("restarting") state meanwhile, so nothing new is
  // started, and the commands that still arrive are drained (publishes dropped, owned buffers
  // freed) so the command buffer never fills. A stop also fails for good when the client's task has
  // already ended; after BOT_MQTT_STOP_STUCK failed retries (about 31 s) the page and the status
  // line say "cannot restart: reboot the bot" while the retries go on every 8 s.
  _ctl_stop_fails.store(0);
  for (uint32_t wait_ms = 1000; !stopped; wait_ms = wait_ms < 8000 ? wait_ms * 2 : 8000) {
    ctlDrain(wait_ms);
    stopped = esp_mqtt_client_stop(cl) == ESP_OK;
    if (!stopped) _ctl_stop_fails.fetch_add(1);
  }
  _ctl_stop_fails.store(0);
  _h_client.store(nullptr);      // from here on its events (if any) are ignored
  esp_mqtt_client_destroy(cl);   // frees the client, its buffers and its copies of the settings
  _ctl_client = nullptr;
  _ctl_live.store(0);
  _ctl_stopped.store(gen);       // only now does the loop empty the inbox and go on
}

// For wait_ms, takes the commands that arrive while a stop is being retried and runs none of
// them: a publish is dropped (a BMC_PUBP's buffer freed), a start is refused (its settings wiped)
// and a stop is already in hand. The loop posts nothing while it is stopping; this is a guard.
void BotMqtt::ctlDrain(uint32_t wait_ms) {
  TickType_t start = xTaskGetTickCount(), span = pdMS_TO_TICKS(wait_ms);
  for (;;) {
    TickType_t gone = xTaskGetTickCount() - start;
    if (gone >= span) return;
    size_t n = xMessageBufferReceive(_cmd_mb, _crx, BOT_MQTT_SCRATCH, span - gone);
    if (n < sizeof(BotMqttCtl)) continue;
    BotMqttCtl h;
    memcpy(&h, _crx, sizeof(h));
    if (h.type == BMC_PUBP) free(h.ptr);
    else if (h.type == BMC_START) {
      if (_snap_busy.load()) { botMqttConnWipe(_snap); _snap_busy.store(false); }
      _ctl_failed.store(h.gen);
    } else if (h.type == BMC_STOP && h.gen != _ctl_gen) {
      _ctl_stopped.store(h.gen);   // not the client being stopped: there is nothing to stop
    }
    _ctl_stack.store((uint32_t)uxTaskGetStackHighWaterMark(nullptr));
  }
}

// Only on the live client while its link is up. A payload of no bytes is published only retained
// (it clears the retained message); esp-mqtt takes a length of 0 as strlen(data), so it is given "".
void BotMqtt::ctlPublish(const BotMqttCtl& h, const char* topic, const char* data, size_t len) {
  if (!_ctl_client || h.gen != _ctl_gen || !botMqttLinkUp(_h_link.load()) || !data || (len == 0 && !h.retain)) return;   // dropped
  esp_mqtt_client_publish(_ctl_client, topic, len ? data : "", (int)len, h.qos, h.retain);
}

// ---- requests and the command list (loop task) ----

bool BotMqtt::request(const BotMqttRequest& r, uint8_t user_slot, uint32_t now_ms) {
  if (!ready() || _wait.count() >= BOT_MQTT_WAIT_SLOTS || !_rate.wouldAllow(now_ms)) return false;
  char id[9];
  int slot = -1;
  for (int t = 0; t < 3 && slot < 0; t++) {
    botMqttMakeId(id, esp_random());
    slot = _wait.add(id, now_ms, _gen, _wait_ms);
  }
  if (slot < 0) return false;
  BotMqttRequest q = r;
  q.id = id;
  q.prefix = _cur.prefix;
  size_t n = botMqttBuildRequest(_scratch + BOT_MQTT_CTL_OFF, BOT_MQTT_REQUEST_MAX + 1, q);
  if (n == 0 || !botMqttTopic(_topic, sizeof(_topic), _cur.prefix, "request", nullptr) ||
      !post(BMC_PUB, _gen, 0, false, _topic, n, nullptr)) {
    _wait.release(slot);
    return false;
  }
  _rate.allow(now_ms);
  _wait.setUser(slot, user_slot);
  _published++;
  return true;
}

bool BotMqtt::commandListDue(uint32_t now_ms) const {
  return _list_dirty && ready() && (uint32_t)(now_ms - _list_ms) >= BOT_MQTT_LIST_GAP_MS;
}

// QoS 0, retained: it is republished on every connection, and a QoS 1 copy kept for resending would
// hold only what fits the send buffer.
void BotMqtt::publishCommandList(const char* const* names, const char* const* descs, const char* const* sources, int count,
                                 uint32_t now_ms) {
  _list_ms = now_ms;   // a failed attempt waits the same 5 s
  if (!ready() || !botMqttTopic(_topic, sizeof(_topic), _cur.prefix, "commandlist", nullptr)) return;
  size_t need = botMqttCommandListLen(names, descs, sources, count);
  size_t cap = (need < BOT_MQTT_LIST_HEAP_MAX ? need : BOT_MQTT_LIST_HEAP_MAX) + 1;
  char* b = (char*)malloc(cap);
  if (!b) return;
  size_t n = botMqttBuildCommandList(b, cap, names, descs, sources, count);
  if (n == 0 || !post(BMC_PUBP, _gen, 0, true, _topic, n, b)) { free(b); return; }   // posted: the control task frees it
  _list_dirty = false;
}

// ---- waiting requests ----

void BotMqtt::pushTimeout(uint8_t user) {
  _timed_out++;
  if (_tq_n < BOT_MQTT_TIMEOUT_Q) _tq[_tq_n++] = user;   // never full: at most 4 wait (BotTask's backstop covers it anyway)
}

void BotMqtt::clearWaits() {
  uint8_t u;
  while (_wait.takeAny(&u) >= 0) pushTimeout(u);
}

bool BotMqtt::takeTimeout(uint8_t& user_slot) {
  if (_tq_n > 0) {
    user_slot = _tq[0];
    for (int i = 1; i < _tq_n; i++) _tq[i - 1] = _tq[i];
    _tq_n--;
    return true;
  }
  uint8_t u;
  if (_wait.expireOwn(millis(), &u) < 0) return false;
  _timed_out++;
  user_slot = u;
  return true;
}

bool BotMqtt::takeReply(uint8_t& user_slot, char* text, size_t cap) {
  if (text && cap) text[0] = '\0';
  _last_n = 0;
  _last_parts = 0;
  if (!_mb || !_scratch || !text || cap == 0 || _phase == PH_STOPPING) return false;   // the inbox is emptied once the old client is gone
  uint32_t now_ms = millis();
  for (int i = 0; i < 16; i++) {   // a bounded amount of work per call
    size_t n = xMessageBufferReceive(_mb, _scratch, BOT_MQTT_SCRATCH, 0);
    if (n == 0) return false;
    if (handleMessage(n, user_slot, text, cap, now_ms)) return true;
  }
  return false;
}

// Message number `part` (1 and up) of the reply takeReply() just returned. Valid only until the next
// call into this class: the record is read again from the scratch buffer.
bool BotMqtt::replyPart(uint8_t part, char* text, size_t cap) {
  if (text && cap) text[0] = '\0';
  BotMqttRec r;
  if (!_scratch || !text || cap == 0 || _last_n == 0 || part == 0 || part >= _last_parts) return false;
  if (!botMqttRecDecode((const uint8_t*)_scratch, _last_n, r) || r.kind != BMI_REPLY) return false;
  return botMqttParseReplyPartLimited(r.payload, r.len, part, text, cap, BOT_MQTT_PARSE_HEAP);
}

// One record from the handler, in _scratch.
bool BotMqtt::handleMessage(size_t n, uint8_t& user, char* text, size_t cap, uint32_t now_ms) {
  BotMqttRec r;
  if (!botMqttRecDecode((const uint8_t*)_scratch, n, r) || r.gen != _gen) { _loop_dropped++; return false; }
  if (r.conn != _seen_connects) syncEvents(now_ms);   // it came on a connection the loop has not seen yet
  if (r.conn != _seen_connects) { _loop_dropped++; return false; }   // an earlier connection's
  if (r.kind == BMI_REPLY) {
    if (_want_pending) { _loop_dropped++; return false; }   // a settings change waits: nothing is delivered (see ready())
    // Parsed before it is matched, so a malformed reply does not use up the request.
    uint8_t parts = 0;
    if (!botMqttParseReplyPartLimited(r.payload, r.len, 0, text, cap, BOT_MQTT_PARSE_HEAP, &parts)) { _loop_dropped++; return false; }
    if (_wait.matchOwn(r.tail, _gen, now_ms, &user) < 0) { text[0] = '\0'; _loop_dropped++; return false; }   // late, repeated or unknown
    _answered++;
    _last_n = n;   // the record stays in _scratch for replyPart() until the next call into this class
    _last_parts = parts;
    return true;
  }
  char desc[BOT_MQTT_CMD_DESC_MAX + 1];
  bool remove = false;
  if (!botMqttParseAnnounce(r.payload, r.len, desc, sizeof(desc), &remove)) { _loop_dropped++; return false; }
  if (remove) {
    if (_cmds.remove(r.tail)) _list_dirty = true;
    return false;
  }
  const char* old = _cmds.find(r.tail);
  bool changed = !old || strcmp(old, desc) != 0;
  if (!_cmds.set(r.tail, desc)) { _loop_dropped++; return false; }   // table full
  if (changed) _list_dirty = true;
  return false;
}

// ---- the event handler (MQTT task) ----

void BotMqtt::onEvent(void* arg, esp_event_base_t, int32_t id, void* data) {
  BotMqtt* self = (BotMqtt*)arg;
  const esp_mqtt_event_t* e = (const esp_mqtt_event_t*)data;
  if (!self || !e || e->client != self->_h_client.load()) return;   // a client being destroyed
  self->_h_events.fetch_add(1);
  self->_h_stack.store((uint32_t)uxTaskGetStackHighWaterMark(nullptr));   // bytes on ESP-IDF
  switch (id) {
    case MQTT_EVENT_CONNECTED: {
      // Both subscriptions are QoS 0 and not awaited; a call that fails is an error state.
      char* t = (char*)self->_hbuf;
      bool ok = botMqttTopic(t, BOT_MQTT_TOPIC_MAX + 1, self->_ctl_prefix, "reply", "+") &&
                esp_mqtt_client_subscribe(e->client, t, 0) >= 0 &&
                botMqttTopic(t, BOT_MQTT_TOPIC_MAX + 1, self->_ctl_prefix, "commands", "+") &&
                esp_mqtt_client_subscribe(e->client, t, 0) >= 0;
      self->_h_link.store(botMqttLinkOnConnected(ok));
      self->_h_connects.fetch_add(1);
      break;
    }
    case MQTT_EVENT_DISCONNECTED:
      self->_h_link.store(botMqttLinkOnDisconnected(self->_h_link.load()));
      self->_h_disconnects.fetch_add(1);
      break;
    case MQTT_EVENT_ERROR:
      if (e->error_handle)
        self->_h_link.store(botMqttLinkOnError(self->_h_link.load(), (int)e->error_handle->error_type,
                                               (int)e->error_handle->connect_return_code));
      break;
    case MQTT_EVENT_DATA:
      self->onData(e);
      break;
    default:
      break;
  }
}

// Only a whole message in one event, with a topic of at most 96 bytes and a payload of at most 512
// (256 for an announcement), whose topic is exactly one of the two subscribed shapes. Everything
// else, and anything that does not fit the inbox, is counted as dropped. Continuation pieces of a
// longer message (offset > 0) belong to one already counted.
void BotMqtt::onData(const esp_mqtt_event_t* e) {
  if (e->current_data_offset != 0) return;
  if (!e->topic || e->topic_len <= 0 || e->topic_len > BOT_MQTT_TOPIC_MAX || e->data_len < 0 ||
      e->data_len != e->total_data_len || e->data_len > BOT_MQTT_REPLY_IN_MAX || (e->data_len > 0 && !e->data)) {
    _h_dropped.fetch_add(1);
    return;
  }
  char tail[BOT_MQTT_TAIL_MAX + 1];
  BotMqttIn kind = botMqttClassify(e->topic, (size_t)e->topic_len, _ctl_prefix, tail, sizeof(tail));
  if (kind == BMI_NONE || (kind == BMI_COMMAND && e->data_len > BOT_MQTT_ANNOUNCE_MAX)) {
    _h_dropped.fetch_add(1);
    return;
  }
  size_t n = botMqttRecEncode(_hbuf, BOT_MQTT_REC_MAX, (uint8_t)kind, _h_gen.load(), _h_connects.load(), tail, strlen(tail),
                              e->data, (size_t)e->data_len);
  if (n && xMessageBufferSend(_mb, _hbuf, n, 0) == n) _h_received.fetch_add(1);
  else _h_dropped.fetch_add(1);
}

// ---- status ----

BotMqttShown BotMqtt::shown() const {
  bool wanted = _want_pending ? _want.on : _cur.on;
  return botMqttShow(wanted, _enabled, _wifi, _phase == PH_STOPPING, _phase == PH_RUNNING, _start_failed, _restart_failed,
                     _h_link.load(), _ctl_stop_fails.load() >= BOT_MQTT_STOP_STUCK);
}

void BotMqtt::status(BotMqttStatus& out) const {
  memset(&out, 0, sizeof(out));
  BotMqttShown s = shown();
  out.state = (BotMqttState)s.state;
  snprintf(out.detail, sizeof(out.detail), "%s", s.detail);
  out.connects = _connects;
  out.published = _published;
  out.received = _h_received.load();
  out.dropped = _h_dropped.load() + _loop_dropped;
  out.answered = _answered;
  out.timed_out = _timed_out;
  out.stack_free = _h_stack.load();
  out.ctl_stack_free = _ctl_stack.load();
}

#endif  // WITH_BOT
