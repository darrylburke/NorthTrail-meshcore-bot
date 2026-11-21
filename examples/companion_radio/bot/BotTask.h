#pragma once

// The chat bot on the companion node: answers commands from one channel and from direct
// messages, keeps the heard table and traffic stats, sends periodic adverts and sets the
// clock from network time.
//
// THREAD RULE: everything in BotTask runs on the Arduino loop task (setup(), loop() and the
// MyMesh listener callbacks, which run inside the_mesh.loop()). The only exception is
// requestAdvert(), which just sets an atomic flag and may be called from any task; the
// advert itself is sent by the next loop(). Other tasks (the web server) must never call
// anything else here: they copy a snapshot under their own mutex from the loop task.
// Internet commands are parked in BotNet while the BotFetch worker task makes the requests;
// unknown commands are put to the MQTT broker through BotMqtt and wait in _ask for its reply.
// BotTask itself never waits on the network (see the thread rules in BotFetch.h and BotMqtt.h).

#ifdef WITH_BOT

#include <Arduino.h>
#include <FS.h>
#include <atomic>
#include "../MyMesh.h"
#include <helpers/bot/BotCommandParser.h>
#include <helpers/bot/BotConfig.h>
#include <helpers/bot/BotHeard.h>
#include <helpers/bot/BotLimits.h>
#include <helpers/bot/BotOutgoing.h>
#include <helpers/bot/BotReplies.h>
#include <helpers/bot/BotNetLogic.h>
#include <helpers/bot/BotTemplate.h>
#include <helpers/bot/BotMqttAsk.h>
#include "BotFetch.h"
#include "BotMqtt.h"
#include "BotNet.h"

struct BotLogEntry { uint32_t when; char sender[24]; char request[48]; char reply[128]; };

class BotTask : public MyMesh::Listener {
public:
  static const int LOG_SIZE = 50;

  BotTask(MyMesh& mesh, mesh::RTCClock& rtc, fs::FS& fs);
  void begin();
  void loop();
  const BotConfig& config() const { return _cfg; }
  // The three setters below run on the loop task only and return true only when the change was
  // persisted. After a failed save the new values stay live and false is returned.
  bool applyConfig(const BotConfig& next);        // saves /bot.json, then pushes scope/location to the node
  bool applyNodeName(const char* name);           // false, changing nothing, for an invalid name
  bool setWebPassword(const char* plain);         // false, changing nothing, unless 8..63 characters
  bool checkWebPassword(const char* plain) const;
  // Counts web password changes (any path: page or USB). The web layer compares it in its own
  // loop() to end sessions after a change it did not make itself.
  uint32_t webPasswordEpoch() const { return _pw_epoch; }
  bool hasWebPassword() const { return _cfg.web_hash[0] != 0; }
  void requestAdvert(bool flood);                 // safe from any task; done on next loop()
  int  recent(BotLogEntry* out, int max) const;   // newest first, up to LOG_SIZE kept
  const BotLogEntry* recentAt(int i) const;      // i-th newest (0 = newest), nullptr past the end
  const BotHeardTable& heard() const { return _heard; }
  const BotMeshStats& stats() const { return _stats; }
  uint32_t uptimeSeconds() const;
  bool clockValid() const { return _clock_valid; }

  // Called from MyMesh::logRxRaw for every received frame (counts it for BotMeshStats).
  void noteRawPacket(const uint8_t raw[], int len);
  // One-line status for the serial CLI ("get bot.status"), including the internet worker's state,
  // free heap (now and the lowest ever) and the worker's unused stack.
  void formatStatus(char* dst, size_t cap) const;

  // Runs text as if it were a direct message from "web" (ignore list, cooldown, per-minute cap,
  // the delayed-send ring and the Recent log all bypassed) and keeps the reply for
  // takeTryResult() instead of transmitting it. False while another try is pending.
  bool tryCommand(const char* text, uint32_t id);
  // False until the try is done; then true once with its id and reply ("" = the bot would stay
  // silent; also after BOT_TRY_TIMEOUT_MS, 50 s, which is above the 45 s a parked command may
  // take), and the holder is empty again.
  // broker, when given, says whether the MQTT broker gave the reply.
  bool takeTryResult(uint32_t& id, char* out, size_t cap, bool* broker = nullptr) { return _try.take(id, out, cap, broker); }

  // Internet worker status (loop task only).
  bool netFetchBusy() const { return _net.fetchBusy(); }
  int netParked() const { return _net.parked(); }
  uint8_t netLastOutcome() const { return _net.lastOutcome(); }   // BotFetchOutcome
  const char* netLastOutcomeText() const { return BotFetch::outcomeText(_net.lastOutcome()); }
  int netLastHttpStatus() const { return _net.lastHttpStatus(); }
  uint32_t netLastMs() const { return _net.lastMs(); }           // the last request in all
  uint32_t netLastDnsMs() const { return _net.lastDnsMs(); }     // of which: DNS
  uint32_t netLastConnMs() const { return _net.lastConnMs(); }   // TCP connect plus TLS handshake
  uint32_t netLastXchgMs() const { return _net.lastXchgMs(); }   // request and reply
  uint32_t netWorkerStackFree() const { return _net.workerStackFree(); }

  // MQTT client status for the page (loop task only); never holds the password.
  void mqttStatus(BotMqttStatus& out) const;
  // The commands announced on the broker (loop task only).
  const BotMqttCommands& mqttCommands() const { return _mqtt.commands(); }

  // MyMesh::Listener
  void onMessageRecv(mesh::Packet* pkt, const ContactInfo& from, uint8_t txt_type, uint32_t sender_timestamp, const char* text) override;
  void onChannelMessageRecv(mesh::Packet* pkt, ChannelDetails& channel_details, const char* text) override;
  void onQueueSizeChanged(int msgcount) override { }
  void onDiscoveredContact(ContactInfo& contact, bool is_new, uint8_t path_len, const uint8_t* path) override;

private:
  void handle(mesh::Packet* pkt, const char* sender, const char* body, const ContactInfo* dm_from, const char* channel);
  // The command path shared by channel messages, direct messages and web tries.
  bool resolve(const char* body, bool is_dm, BotParsed& p);
  void process(const BotParsed& p, const BotMsgInfo& m, const BotReplyDest& dest, const char* sender,
               const char* body, const ContactInfo* dm_from, const char* channel, uint32_t now_ms);
  // The one place a reply body leaves: adds the mention (source BRT_FOREIGN, for text from a custom
  // template or the broker: always; BRT_BUILTIN: unless the formatter wrote one), then hands a try's
  // reply to the holder, or queues a channel/direct reply (counting it against the per-minute cap
  // when count_cap; *capped, when given, says whether the cap refused it; broker marks a try's reply
  // as the broker's).
  void deliver(const BotReplyDest& dest, const char* body_text, uint8_t source, const char* sender, const char* request,
               uint32_t now_ms, bool count_cap, bool* capped = nullptr, bool broker = false, uint32_t extra_delay_ms = 0);
  // An unknown command put to the broker: true when it is waiting (the reply comes later).
  bool askBroker(const BotParsed& p, const BotMsgInfo& m, const BotReplyDest& dest, const char* sender, const char* body,
                 const ContactInfo* dm_from, const char* channel, size_t max_bytes, uint32_t now_ms);
  // text null: no reply (timed out). parts > 1: the reply carries further messages, read from _mqtt.
  void finishAsk(int slot, const char* text, uint32_t now_ms, uint8_t parts = 1);
  void loopMqtt();      // runs BotMqtt, finishes answered and timed-out requests, publishes the command list
  void publishCommands(uint32_t now_ms);
  BotNetStart startNet(int builtin, int custom, const BotParsed& p, const BotMsgInfo& m, const BotReplyDest& dest, const char* sender,
                       const char* body, const char* channel, size_t max_bytes, uint32_t now_ms);
  void loopNet();       // runs BotNet and delivers its finished commands
  void loopReplies();   // sends a queued reply once its delay has passed
  void cleanArgs(const char* args);   // into _args: no control characters, no trailing spaces
  void makeVars(BotVars& v, const BotMsgInfo& m, const char* channel);
  bool buildReply(int builtin, int custom, const BotParsed& p, const BotMsgInfo& m, bool is_dm,
                  const ContactInfo* dm_from, const char* channel, char* out, size_t out_cap, size_t max_bytes);
  size_t buildHelp(const char* args, char* out, size_t out_cap, size_t max_bytes);
  size_t buildDist(const char* sender, const ContactInfo* dm_from, char* out, size_t out_cap);
  size_t buildSun(bool is_dm, const char* sender, char* out, size_t out_cap);
  void addLog(const char* sender, const char* request, const char* reply);
  void applyTz();
  void loopClock();
  void loopAdverts();
  void fmtUptime(char* dst, size_t cap) const;
  bool localTime(uint32_t t, struct tm& out) const;
  static const char* lookupPathName(const uint8_t* hash, uint8_t hash_size, void* ctx);

  MyMesh& _mesh;
  mesh::RTCClock& _rtc;
  fs::FS& _fs;
  BotConfig _cfg;   // the one live instance (sizeof(BotConfig), about 8 KB); never copied to the stack

  BotCooldown _cooldown;
  BotRateCap _cap;
  BotHeardTable _heard;
  BotMeshStats _stats;

  BotLogEntry _log[LOG_SIZE];
  int _log_head;    // next slot to write
  int _log_count;

  uint64_t _uptime_ms;     // accumulated in loop(); survives the 32-bit millis() wrap
  uint32_t _last_millis;

  bool _ntp_started;
  bool _ntp_synced;
  bool _clock_valid;
  uint32_t _last_rtc_sync_ms;
  uint32_t _last_clock_check_ms;

  bool _first_advert_done;
  uint32_t _last_zero_ms, _last_flood_ms;
  std::atomic<bool> _adv_req_zero, _adv_req_flood;   // set by requestAdvert() from any task

  // Replies wait BOT_REPLY_DELAY_MS before going on air, so repeaters still forwarding the
  // request are not talked over and a direct message's ACK goes out first.
  BotReplyQueue _outq;
  uint32_t _pw_epoch = 0;

  BotFetch _fetch;        // the HTTP(S) worker task
  BotNet _net;            // parked internet commands
  BotNetReply _netrep;    // one finished internet command being delivered
  BotTryHolder _try;      // the reply of a command tried from the web page
  BotMqtt _mqtt;          // the broker connection
  BotAskTable _ask;       // unknown commands waiting for the broker's reply
  uint32_t _mqtt_capped = 0;      // broker replies the per-minute cap refused
  uint32_t _name_check_ms = 0;    // the node name is compared once a second (it sets the default prefix)

  // Scratch buffers for building one reply (loop task only, kept off the stack).
  char _built[128];   // the formatter's output, before the mention
  char _reply[128];   // the on-air text
  char _tmp[208];     // also a broker reply's text (up to 200 bytes)
  char _args[160];
  char _line[128];    // the command line a text rule stands for
  char _path_hex[96];
  char _var_time[16], _var_date[24], _var_uptime[24];   // {time} {date} {uptime}

  // Recent direct messages (key prefix + sender timestamp) so an ACK-retry is answered once.
  static const int DM_SEEN = 8;
  struct DmSeen { uint8_t key[4]; uint32_t ts; };
  DmSeen _dm_seen[DM_SEEN];
  int _dm_seen_next;
};

extern BotTask bot_task;

#endif  // WITH_BOT
