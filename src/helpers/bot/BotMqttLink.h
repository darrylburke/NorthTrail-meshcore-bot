#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "BotConfig.h"
#include "BotMqttTopics.h"

// The decisions behind the bot's MQTT connection that do not need the device (pure C/C++, no
// heap, host-testable): which settings the client is built from and when a change needs a new
// client, the link state the client's event handler records, and the state and words shown on the
// page and the serial status line. The words never contain the host, the user name or the password.

enum BotMqttState : uint8_t { BMS_OFF, BMS_WAIT_WIFI, BMS_CONNECTING, BMS_CONNECTED, BMS_REFUSED, BMS_ERROR };

// ---- the settings a client is built from ----

// Everything the client is made from. The effective prefix is stored (the configured one, or the
// default derived from the node name), so a rename with no configured prefix is a change.
struct BotMqttConn {
  bool on;                          // enabled and a host is set
  char host[65];
  uint16_t port;
  char user[33];
  char pass[65];                    // a secret: see botMqttConnWipe
  char prefix[BOT_MQTT_PREFIX_MAX + 1];
};

// Zeroes c in a way the compiler cannot leave out (the password is in it).
static inline void botMqttConnWipe(BotMqttConn& c) {
  volatile uint8_t* p = (volatile uint8_t*)&c;
  for (size_t i = 0; i < sizeof(c); i++) p[i] = 0;
}

// The connection the settings ask for. A configured prefix that is not valid (never stored by the
// loaders) falls back to the default, as an empty one does. The password is copied only when the
// connection is on: a disabled client keeps no copy of it.
static inline void botMqttConnFrom(BotMqttConn& out, const BotConfig& c, const char* node_name) {
  botMqttConnWipe(out);
  out.on = botConfigMqttUsable(c);
  memcpy(out.host, c.mqtt_host, sizeof(out.host));
  out.host[sizeof(out.host) - 1] = '\0';
  out.port = c.mqtt_port;
  memcpy(out.user, c.mqtt_user, sizeof(out.user));
  out.user[sizeof(out.user) - 1] = '\0';
  if (out.on) {
    memcpy(out.pass, c.mqtt_pass, sizeof(out.pass));
    out.pass[sizeof(out.pass) - 1] = '\0';
  }
  botMqttEffectivePrefix(out.prefix, sizeof(out.prefix), c.mqtt_prefix, node_name);
}

// Whether the running client (made from a) must be replaced to follow b: any change to whether it
// runs, the host, port, user name, password or effective prefix. The wait time is not in here: it
// applies to each request as it is made and never restarts the client.
static inline bool botMqttNeedsRestart(const BotMqttConn& a, const BotMqttConn& b) {
  return a.on != b.on || a.port != b.port || strcmp(a.host, b.host) != 0 || strcmp(a.user, b.user) != 0 ||
         strcmp(a.pass, b.pass) != 0 || strcmp(a.prefix, b.prefix) != 0;
}

// Whether announced commands learnt under a still hold under b: the same broker (host and port)
// and the same effective prefix. A new login alone keeps them.
static inline bool botMqttSameSource(const BotMqttConn& a, const BotMqttConn& b) {
  return a.port == b.port && strcmp(a.host, b.host) == 0 && strcmp(a.prefix, b.prefix) == 0;
}

// ---- the link as the client's event handler records it ----

enum BotMqttLink : uint8_t {
  BML_IDLE,            // client started, no outcome yet
  BML_CONNECTED,       // connected and both subscriptions sent
  BML_RECONNECTING,    // the connection was lost after it worked (the client retries)
  BML_REFUSED,         // the broker refused the user name or password
  BML_ERR_UNAVAILABLE, // the broker refused: server unavailable
  BML_ERR_PROTOCOL,    // the broker refused: protocol version
  BML_ERR_ID,          // the broker refused the client id
  BML_ERR_REFUSED,     // the broker refused for a reason the client does not name
  BML_ERR_TRANSPORT,   // TCP: no route, refused, DNS, timeout
  BML_ERR_SUBSCRIBE,   // connected, but a subscription could not be sent
  BML_ERR_OTHER,       // an error the client gave no type for
};

// esp-mqtt's error types and MQTT 3.1.1 CONNACK return codes, as numbers (checked against
// mqtt_client.h with static_assert in BotMqtt.cpp).
#define BOT_MQTT_ERRTYPE_TRANSPORT 1
#define BOT_MQTT_ERRTYPE_REFUSED   2
#define BOT_MQTT_RC_PROTOCOL       1
#define BOT_MQTT_RC_ID_REJECTED    2
#define BOT_MQTT_RC_UNAVAILABLE    3
#define BOT_MQTT_RC_BAD_USER       4
#define BOT_MQTT_RC_NOT_AUTHORIZED 5

// A connection refused by the broker, by its return code.
static inline uint8_t botMqttRefusedLink(int return_code) {
  switch (return_code) {
    case BOT_MQTT_RC_BAD_USER: case BOT_MQTT_RC_NOT_AUTHORIZED: return BML_REFUSED;
    case BOT_MQTT_RC_UNAVAILABLE: return BML_ERR_UNAVAILABLE;
    case BOT_MQTT_RC_PROTOCOL: return BML_ERR_PROTOCOL;
    case BOT_MQTT_RC_ID_REJECTED: return BML_ERR_ID;
    default: return BML_ERR_REFUSED;
  }
}

// MQTT_EVENT_ERROR. The client keeps retrying at its own pace in every case. An error while the
// link is up does not end it: when the connection is lost, the disconnect event that follows says so.
static inline uint8_t botMqttLinkOnError(uint8_t cur, int error_type, int return_code) {
  if (cur == BML_CONNECTED) return cur;
  if (error_type == BOT_MQTT_ERRTYPE_REFUSED) return botMqttRefusedLink(return_code);
  if (error_type == BOT_MQTT_ERRTYPE_TRANSPORT) return BML_ERR_TRANSPORT;
  return BML_ERR_OTHER;
}

// MQTT_EVENT_CONNECTED: ready only when both subscriptions were sent (SUBACK is not awaited).
static inline uint8_t botMqttLinkOnConnected(bool subscribed) { return subscribed ? BML_CONNECTED : BML_ERR_SUBSCRIBE; }

// MQTT_EVENT_DISCONNECTED: the reason recorded by the error event just before it is kept, so the
// page goes on saying "login refused" while the client retries; a working link that drops (or
// one that never got as far as an outcome) shows as reconnecting.
static inline uint8_t botMqttLinkOnDisconnected(uint8_t cur) {
  return (cur == BML_CONNECTED || cur == BML_IDLE || cur == BML_ERR_SUBSCRIBE) ? BML_RECONNECTING : cur;
}

static inline bool botMqttLinkReady(uint8_t link) { return link == BML_CONNECTED; }
// The TCP link to the broker is up (ready, or connected with a subscription that failed): what the
// client may still publish on, such as the "offline" status and the emptied command list before a stop.
static inline bool botMqttLinkUp(uint8_t link) { return link == BML_CONNECTED || link == BML_ERR_SUBSCRIBE; }

// ---- what the page and the status line show ----

struct BotMqttShown { uint8_t state; const char* detail; };
#define BOT_MQTT_DETAIL_CAP 40   // every detail word below fits (host test StatusLine.EveryWordHasAShortCode)

// Where the device side is: wanted (enabled and host set), enabled (the switch alone), WiFi up, an
// old client being stopped, a client running, the last attempt to create one failed, the last
// attempt to post the old client's stop to the control task failed (the command buffer was full;
// retried in 5 s), and the link the handler recorded. stop_stuck: the old
// client's stop has kept failing (BOT_MQTT_STOP_STUCK retries); it is still retried, but only a
// reboot is sure to end it, so the owner is told.
#define BOT_MQTT_STOP_STUCK 6
static inline BotMqttShown botMqttShow(bool wanted, bool enabled, bool wifi, bool restarting, bool client,
                                       bool start_failed, bool restart_failed, uint8_t link, bool stop_stuck = false) {
  if (restart_failed) return { BMS_ERROR, "cannot restart" };   // the old client keeps running
  if (restarting) return stop_stuck ? BotMqttShown{ BMS_ERROR, "cannot restart: reboot the bot" }
                                    : BotMqttShown{ BMS_CONNECTING, "restarting" };
  if (!wanted) return { BMS_OFF, enabled ? "no broker set" : "off" };
  if (!wifi) return { BMS_WAIT_WIFI, "waiting for WiFi" };
  if (!client) return start_failed ? BotMqttShown{ BMS_ERROR, "cannot start client" } : BotMqttShown{ BMS_CONNECTING, "starting" };
  switch (link) {
    case BML_IDLE: return { BMS_CONNECTING, "connecting" };
    case BML_CONNECTED: return { BMS_CONNECTED, "connected" };
    case BML_RECONNECTING: return { BMS_CONNECTING, "reconnecting" };
    case BML_REFUSED: return { BMS_REFUSED, "login refused" };
    case BML_ERR_UNAVAILABLE: return { BMS_ERROR, "broker unavailable" };
    case BML_ERR_PROTOCOL: return { BMS_ERROR, "protocol refused" };
    case BML_ERR_ID: return { BMS_ERROR, "client id refused" };
    case BML_ERR_REFUSED: return { BMS_ERROR, "connection refused" };
    case BML_ERR_TRANSPORT: return { BMS_ERROR, "cannot reach broker" };
    case BML_ERR_SUBSCRIBE: return { BMS_ERROR, "subscribe failed" };
    default: return { BMS_ERROR, "connection error" };
  }
}

// ---- one message from the event handler to the loop ----

// A record in the inbox: kind (BMI_REPLY or BMI_COMMAND), the client generation and the
// connection number it arrived on (4 bytes each, host order: written and read on the same chip),
// the tail's length and the tail (1 to 16 bytes), then the payload (the rest).
#define BOT_MQTT_REC_HEAD 10
#define BOT_MQTT_REC_MAX  (BOT_MQTT_REC_HEAD + BOT_MQTT_TAIL_MAX + 512)

struct BotMqttRec {
  uint8_t kind;
  uint32_t gen, conn;
  char tail[BOT_MQTT_TAIL_MAX + 1];
  const char* payload;   // points into the record
  size_t len;
};

// Writes the record into dst; 0 (nothing usable written) for another kind, a tail of 0 or more than
// 16 bytes, a null payload with a length, or a record that does not fit cap.
static inline size_t botMqttRecEncode(uint8_t* dst, size_t cap, uint8_t kind, uint32_t gen, uint32_t conn, const char* tail,
                                      size_t tail_len, const char* payload, size_t len) {
  if (!dst || (kind != BMI_REPLY && kind != BMI_COMMAND) || !tail || tail_len == 0 || tail_len > BOT_MQTT_TAIL_MAX ||
      (len && !payload))
    return 0;
  size_t n = BOT_MQTT_REC_HEAD + tail_len + len;
  if (n > cap) return 0;
  dst[0] = kind;
  memcpy(dst + 1, &gen, 4);
  memcpy(dst + 5, &conn, 4);
  dst[9] = (uint8_t)tail_len;
  memcpy(dst + BOT_MQTT_REC_HEAD, tail, tail_len);
  if (len) memcpy(dst + BOT_MQTT_REC_HEAD + tail_len, payload, len);
  return n;
}

// Reads a record of n bytes; false for one shorter than its head and tail, a tail of 0 or more than
// 16 bytes, or an unknown kind. The tail is NUL-terminated; the payload points into src.
static inline bool botMqttRecDecode(const uint8_t* src, size_t n, BotMqttRec& out) {
  out.kind = BMI_NONE; out.gen = out.conn = 0; out.tail[0] = '\0'; out.payload = ""; out.len = 0;
  if (!src || n < BOT_MQTT_REC_HEAD) return false;
  size_t tl = src[9];
  if ((src[0] != BMI_REPLY && src[0] != BMI_COMMAND) || tl == 0 || tl > BOT_MQTT_TAIL_MAX || BOT_MQTT_REC_HEAD + tl > n) return false;
  out.kind = src[0];
  memcpy(&out.gen, src + 1, 4);
  memcpy(&out.conn, src + 5, 4);
  memcpy(out.tail, src + BOT_MQTT_REC_HEAD, tl);
  out.tail[tl] = '\0';
  out.payload = (const char*)src + BOT_MQTT_REC_HEAD + tl;
  out.len = n - BOT_MQTT_REC_HEAD - tl;
  return true;
}
