#pragma once

// The bot's MQTT client: one connection (plain TCP, user name and password) on which a command the
// bot does not know is published as a request and answered by whatever program on the broker
// replies within the wait time. Built on esp-mqtt (ESP-IDF 4.4), which runs its own task.
//
// THREAD AND OWNERSHIP RULE (three tasks besides the loop's callers):
//   - Loop task: every public method here. It never calls an esp-mqtt function and never waits:
//     it posts fixed-size commands to the control task through _cmd_mb (xMessageBufferSend with
//     zero wait; a post that does not fit is "not done" and retried or refused), reads atomics, and
//     drains the inbox (_mb) the event handler fills.
//   - Control task ("bot_mqtt_ctl", long-lived, created once): the only owner of the client handle
//     and the only caller of esp_mqtt_client_init, _register_event, _start, _publish,
//     _get_outbox_size, _stop and _destroy. It runs the commands in the order they were posted
//     (start, publish, stop), so there are never two live clients and no handle is used after
//     destroy. It may block (publishing on a stalled link, stopping a client); the loop does not.
//   - esp-mqtt task: runs onEvent, which touches only the inbox (zero-wait send), its own assembly
//     buffer, atomics, and the live client's prefix (_ctl_prefix, written by the control task only
//     while no client exists). It calls esp_mqtt_client_subscribe, which is safe on that task.
//   - Handing over the settings: _snap holds the connection settings, password included. The loop
//     writes it only while _snap_busy is false, sets the flag and posts START; the control task
//     reads it, builds the client, wipes it and clears the flag. A START that could not be posted
//     is wiped by the loop.
// The broker password is never logged, published or shown; copies of it are wiped.

#ifdef WITH_BOT

#include <Arduino.h>
#include <atomic>
#include <mqtt_client.h>
#include <freertos/FreeRTOS.h>
#include <freertos/message_buffer.h>
#include <helpers/bot/BotConfig.h>
#include <helpers/bot/BotMqttLink.h>
#include <helpers/bot/BotMqttMsg.h>
#include <helpers/bot/BotMqttTopics.h>
#include <helpers/bot/BotMqttWait.h>

// Client settings and buffer sizes (see the memory notes in BotMqtt.cpp).
#define BOT_MQTT_TASK_STACK   4096    // esp-mqtt task (the handler is small; see status().stack_free)
#define BOT_MQTT_TASK_PRIO    1       // the loop task's priority: a flood cannot starve the radio loop
#define BOT_MQTT_IN_BUF       1024    // receive buffer: a whole reply (topic <= 96, payload <= 512) fits
#define BOT_MQTT_OUT_BUF      512     // send buffer: CONNECT, subscribe, status; longer publishes go out in pieces
#define BOT_MQTT_KEEPALIVE_S  60
#define BOT_MQTT_RECONNECT_MS 10000   // between attempts, also after a refused login
#define BOT_MQTT_NET_TIMEOUT_MS 5000  // connect and socket writes (only the control and MQTT tasks wait on it)
#define BOT_MQTT_CTL_STACK    3072    // the control task (see status().ctl_stack_free)
#define BOT_MQTT_CTL_PRIO     1
#define BOT_MQTT_INBOX        2176    // message buffer from the handler to the loop (>= 4 of the largest)
#define BOT_MQTT_LIST_HEAP_MAX 6144   // largest command list allocated for one publish (else "more":true)
#define BOT_MQTT_LIST_GAP_MS  5000u   // the command list goes out at most this often
#define BOT_MQTT_LIST_SETTLE_MS 1500u // after connecting, time for retained announcements to arrive
#define BOT_MQTT_TIMEOUT_Q    8

struct BotMqttStatus {
  BotMqttState state;
  char detail[BOT_MQTT_DETAIL_CAP];
  uint32_t connects, published, received, dropped, answered, timed_out;
  uint32_t capped;          // filled by BotTask::mqttStatus: broker replies the per-minute cap refused
  uint32_t stack_free;      // the MQTT task's unused stack in bytes (0 until an event was seen)
  uint32_t ctl_stack_free;  // the control task's unused stack in bytes (0 until it ran a command)
};

// A command from the loop to the control task: this head, then topic_len topic bytes and a NUL
// (publish only), then the payload (BMC_PUB) — or, for BMC_PUBP, the payload is ptr/len on the
// heap, owned by the control task from the moment the post succeeds.
enum BotMqttCtlType : uint8_t { BMC_START = 1, BMC_STOP, BMC_PUB, BMC_PUBP };
struct BotMqttCtl {
  uint8_t type;
  uint8_t qos;        // BMC_STOP: 1 when an "offline" status was posted just before (let it go out)
  uint8_t retain;
  uint8_t topic_len;
  uint32_t gen;       // the client generation the command is for
  uint32_t len;       // BMC_PUBP: payload length
  char* ptr;          // BMC_PUBP: payload
};
#define BOT_MQTT_CTL_OFF  (sizeof(BotMqttCtl) + BOT_MQTT_TOPIC_MAX + 1)          // where a payload starts
#define BOT_MQTT_SCRATCH  (BOT_MQTT_CTL_OFF + BOT_MQTT_REQUEST_MAX + 1)          // a whole command record
#define BOT_MQTT_CMDQ     (2 * (BOT_MQTT_SCRATCH + 4))                           // two of the largest

class BotMqtt {
public:
  BotMqtt();
  // The client id is "meshbot-" and the first two bytes of the node's public key in hex.
  void begin(const uint8_t* pub_key);
  // Records the wanted connection; loop() restarts or stops the client when anything relevant
  // changed (botMqttNeedsRestart). The wait time applies to requests made from now on.
  void applySettings(const BotConfig& cfg, const char* node_name);
  void loop(uint32_t now_ms);
  // Connected, subscribed, WiFi up, and the loop has seen the current connection.
  bool ready() const;
  // False when not ready, 4 already waiting, over 30 a minute, or the request could not be posted
  // to the control task; nothing is waiting then. Fills id and reply_to itself.
  bool request(const BotMqttRequest& r, uint8_t user_slot, uint32_t now_ms);
  // One matched reply (its text cleaned, at most 200 bytes; cap must be at least 201). Drains the
  // messages from the broker, handling announcements on the way.
  bool takeReply(uint8_t& user_slot, char* text, size_t cap);
  // How many messages the reply takeReply() just returned carries (1 to BOT_MQTT_REPLY_PARTS_MAX), and
  // message number part (1 and up) of it. Both are valid only until the next call into this class.
  uint8_t replyParts() const { return _last_parts; }
  bool replyPart(uint8_t part, char* text, size_t cap);
  // One request that will get no reply: its wait ran out, or the connection was lost or replaced.
  bool takeTimeout(uint8_t& user_slot);
  const BotMqttCommands& commands() const { return _cmds; }
  // The command list is due (changed, ready, and 5 s since the last attempt).
  bool commandListDue(uint32_t now_ms) const;
  void markCommandsChanged() { _list_dirty = true; }
  // Builds the whole list into a buffer allocated for this publish (its exact length, at most
  // BOT_MQTT_LIST_HEAP_MAX bytes) and hands it to the control task, which publishes and frees it.
  // When the allocation or the post fails, the list stays due for the next attempt in 5 s.
  void publishCommandList(const char* const* names, const char* const* descs, const char* const* sources, int count,
                          uint32_t now_ms);
  void status(BotMqttStatus& out) const;      // capped is left 0
  const char* nodeName() const { return _node; }

private:
  enum Phase : uint8_t { PH_IDLE, PH_STARTING, PH_RUNNING, PH_STOPPING };

  static void onEvent(void* arg, esp_event_base_t base, int32_t id, void* data);   // MQTT task
  void onData(const esp_mqtt_event_t* e);                                          // MQTT task
  static void ctlEntry(void* arg);                                                 // control task
  void ctlRun();
  void ctlStart(uint32_t gen);
  void ctlStop(uint32_t gen, bool linger);
  void ctlDrain(uint32_t wait_ms);
  void ctlPublish(const BotMqttCtl& h, const char* topic, const char* data, size_t len);

  bool allocate();
  bool post(uint8_t type, uint32_t gen, uint8_t qos, bool retain, const char* topic, size_t len, char* ptr);
  void syncEvents(uint32_t now_ms);
  void startClient(uint32_t now_ms);
  void beginStop(uint32_t now_ms);
  void applyWanted();
  void clearWaits();
  void pushTimeout(uint8_t user);
  bool handleMessage(size_t n, uint8_t& user, char* text, size_t cap, uint32_t now_ms);
  BotMqttShown shown() const;

  // ---- loop task ----
  BotMqttConn _cur;             // the connection the client (if any) was made from
  BotMqttConn _want;            // asked for by applySettings, until loop() applies it
  bool _want_pending;
  bool _enabled;                // the switch alone (for "no broker set")
  char _node[33];
  char _client_id[16];
  char _status_topic[BOT_MQTT_TOPIC_MAX + 1];
  char _topic[BOT_MQTT_TOPIC_MAX + 1];
  char* _scratch;               // BOT_MQTT_SCRATCH bytes, allocated once: command records, inbox records
  Phase _phase;
  uint32_t _run_gen;            // the generation started (PH_STARTING, PH_RUNNING)
  uint32_t _stop_gen;           // the generation being stopped (PH_STOPPING)
  uint32_t _gen;                // current generation: bumped when a client is started and when a stop begins
  uint32_t _wait_ms;
  bool _wifi;
  BotMqttWaitTable _wait;
  BotMqttRate _rate;
  BotMqttCommands _cmds;
  uint8_t _tq[BOT_MQTT_TIMEOUT_Q];
  int _tq_n;
  size_t _last_n = 0;         // size of the reply record still in _scratch (0: none)
  uint8_t _last_parts = 0;
  uint32_t _seen_connects, _seen_disconnects;
  bool _need_status;
  uint32_t _status_try_ms;
  bool _list_dirty;
  uint32_t _list_ms;
  bool _start_failed;
  uint32_t _start_retry_ms;
  bool _restart_failed;
  uint32_t _restart_retry_ms;
  uint32_t _connects, _published, _answered, _timed_out, _loop_dropped;

  // ---- shared: loop -> control ----
  MessageBufferHandle_t _cmd_mb;
  BotMqttConn _snap;                 // see the hand-over rule above
  std::atomic<bool> _snap_busy;
  TaskHandle_t _ctl_task;
  // control -> loop
  std::atomic<uint32_t> _ctl_live, _ctl_failed, _ctl_stopped, _ctl_stack;
  std::atomic<uint32_t> _ctl_stop_fails;   // failed stop retries of the client being stopped (0 when none)

  // ---- control task only ----
  uint8_t* _crx;                     // BOT_MQTT_SCRATCH bytes: the command being run
  esp_mqtt_client_handle_t _ctl_client;
  uint32_t _ctl_gen;
  char _ctl_uri[80];                 // "mqtt://<host>:<port>": no credentials in it
  char _ctl_user[33];
  char _ctl_lwt[BOT_MQTT_TOPIC_MAX + 1];
  char _ctl_prefix[BOT_MQTT_PREFIX_MAX + 1];   // also read by the handler (see above)

  // ---- shared with the event handler ----
  MessageBufferHandle_t _mb;         // the inbox
  uint8_t* _hbuf;                    // handler only: assembles one record, builds subscribe topics
  std::atomic<esp_mqtt_client_handle_t> _h_client;   // the live client; events of any other are ignored
  std::atomic<uint32_t> _h_gen;                      // generation stamped on each record
  std::atomic<uint8_t> _h_link;                      // BotMqttLink (handler writes; control resets before a start)
  std::atomic<uint32_t> _h_events;                   // events of the live client (0 until its task has run)
  std::atomic<uint32_t> _h_connects, _h_disconnects, _h_received, _h_dropped, _h_stack;
};

#endif  // WITH_BOT
