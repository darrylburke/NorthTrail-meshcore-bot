// Host tests for the device-side MQTT rules: BotMqttLink.h (settings, restarts, link state, what
// is shown), BotMqttAsk.h (waiting slots, request path fields, help and command-list merge), the
// broker routing rules in BotNetLogic.h and the per-slot wait additions in BotMqttWait.h.
#include <gtest/gtest.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "helpers/bot/BotCommandParser.h"
#include <ArduinoJson.h>
#include "helpers/bot/BotConfig.h"
#include "helpers/bot/BotMqttLink.h"
#include "helpers/bot/BotMqttAsk.h"
#include "helpers/bot/BotMqttWait.h"
#include "helpers/bot/BotMqttMsg.h"
#include "helpers/bot/BotNetLogic.h"
#include "helpers/bot/BotStatusLine.h"

static BotConfig g_cfg;   // never on the stack

// A plain allocator for documents larger than the default pool.
struct BigAlloc : ArduinoJson::Allocator {
  void* allocate(size_t n) override { return malloc(n); }
  void deallocate(void* p) override { free(p); }
  void* reallocate(void* p, size_t n) override { return realloc(p, n); }
} g_bigAlloc;

static void cfgBroker(BotConfig& c) {
  botConfigDefaults(c);
  c.mqtt_enabled = true;
  strcpy(c.mqtt_host, "broker.example.org");
  c.mqtt_port = 1883;
  strcpy(c.mqtt_user, "bot");
  strcpy(c.mqtt_pass, "secret-pass");
  c.mqtt_wait_s = 5;
}

// ---- settings and restarts ----

TEST(Conn, FromConfig) {
  cfgBroker(g_cfg);
  BotMqttConn c;
  botMqttConnFrom(c, g_cfg, "MeshBot");
  EXPECT_TRUE(c.on);
  EXPECT_STREQ(c.host, "broker.example.org");
  EXPECT_EQ(c.port, 1883);
  EXPECT_STREQ(c.user, "bot");
  EXPECT_STREQ(c.pass, "secret-pass");
  EXPECT_STREQ(c.prefix, "meshcore/MeshBot");
  strcpy(g_cfg.mqtt_prefix, "meshcore/Other");
  botMqttConnFrom(c, g_cfg, "MeshBot");
  EXPECT_STREQ(c.prefix, "meshcore/Other");
}

TEST(Conn, OnNeedsSwitchAndHost) {
  cfgBroker(g_cfg);
  BotMqttConn c;
  g_cfg.mqtt_host[0] = '\0';
  botMqttConnFrom(c, g_cfg, "MeshBot");
  EXPECT_FALSE(c.on);
  cfgBroker(g_cfg);
  g_cfg.mqtt_enabled = false;
  botMqttConnFrom(c, g_cfg, "MeshBot");
  EXPECT_FALSE(c.on);
}

TEST(Conn, WipeClearsPassword) {
  cfgBroker(g_cfg);
  BotMqttConn c;
  botMqttConnFrom(c, g_cfg, "MeshBot");
  botMqttConnWipe(c);
  const uint8_t* p = (const uint8_t*)&c;
  for (size_t i = 0; i < sizeof(c); i++) ASSERT_EQ(p[i], 0) << i;
}

static BotMqttConn s_a, s_b;

TEST(Restart, EachFieldButWait) {
  cfgBroker(g_cfg);
  botMqttConnFrom(s_a, g_cfg, "MeshBot");
  botMqttConnFrom(s_b, g_cfg, "MeshBot");
  EXPECT_FALSE(botMqttNeedsRestart(s_a, s_b));
  g_cfg.mqtt_wait_s = 15;                       // the wait alone never restarts
  botMqttConnFrom(s_b, g_cfg, "MeshBot");
  EXPECT_FALSE(botMqttNeedsRestart(s_a, s_b));
  struct { void (*change)(BotConfig&); const char* what; } cases[] = {
    { [](BotConfig& c) { c.mqtt_enabled = false; }, "enabled" },
    { [](BotConfig& c) { strcpy(c.mqtt_host, "other.example.org"); }, "host" },
    { [](BotConfig& c) { c.mqtt_port = 1884; }, "port" },
    { [](BotConfig& c) { strcpy(c.mqtt_user, "bot2"); }, "user" },
    { [](BotConfig& c) { strcpy(c.mqtt_pass, "secret-pasS"); }, "password" },
    { [](BotConfig& c) { strcpy(c.mqtt_prefix, "meshcore/MeshBot2"); }, "prefix" },
  };
  for (auto& k : cases) {
    cfgBroker(g_cfg);
    k.change(g_cfg);
    botMqttConnFrom(s_b, g_cfg, "MeshBot");
    EXPECT_TRUE(botMqttNeedsRestart(s_a, s_b)) << k.what;
  }
}

TEST(Restart, RenameFollowsDefaultPrefixOnly) {
  cfgBroker(g_cfg);
  botMqttConnFrom(s_a, g_cfg, "MeshBot");
  botMqttConnFrom(s_b, g_cfg, "MeshBot2");
  EXPECT_TRUE(botMqttNeedsRestart(s_a, s_b));     // no prefix configured: the derived one changed
  strcpy(g_cfg.mqtt_prefix, "meshcore/fixed");
  botMqttConnFrom(s_a, g_cfg, "MeshBot");
  botMqttConnFrom(s_b, g_cfg, "MeshBot2");
  EXPECT_FALSE(botMqttNeedsRestart(s_a, s_b));    // configured prefix: a rename changes nothing
  // A rename that strips to the same prefix is no change either.
  botConfigDefaults(g_cfg);
  cfgBroker(g_cfg);
  botMqttConnFrom(s_a, g_cfg, "Mesh Bot");
  botMqttConnFrom(s_b, g_cfg, "MeshBot!");
  EXPECT_FALSE(botMqttNeedsRestart(s_a, s_b));
}

TEST(Restart, SameSourceKeepsAnnouncedCommands) {
  cfgBroker(g_cfg);
  botMqttConnFrom(s_a, g_cfg, "MeshBot");
  strcpy(g_cfg.mqtt_user, "other");
  strcpy(g_cfg.mqtt_pass, "other-pass");
  botMqttConnFrom(s_b, g_cfg, "MeshBot");
  EXPECT_TRUE(botMqttSameSource(s_a, s_b));       // a new login: same broker and prefix
  g_cfg.mqtt_port = 1884;
  botMqttConnFrom(s_b, g_cfg, "MeshBot");
  EXPECT_FALSE(botMqttSameSource(s_a, s_b));
  cfgBroker(g_cfg);
  strcpy(g_cfg.mqtt_host, "other.example.org");
  botMqttConnFrom(s_b, g_cfg, "MeshBot");
  EXPECT_FALSE(botMqttSameSource(s_a, s_b));
  cfgBroker(g_cfg);
  botMqttConnFrom(s_b, g_cfg, "Renamed");
  EXPECT_FALSE(botMqttSameSource(s_a, s_b));
}

// ---- link state ----

TEST(Link, RefusedCodes) {
  EXPECT_EQ(botMqttRefusedLink(BOT_MQTT_RC_BAD_USER), BML_REFUSED);
  EXPECT_EQ(botMqttRefusedLink(BOT_MQTT_RC_NOT_AUTHORIZED), BML_REFUSED);
  EXPECT_EQ(botMqttRefusedLink(BOT_MQTT_RC_UNAVAILABLE), BML_ERR_UNAVAILABLE);
  EXPECT_EQ(botMqttRefusedLink(BOT_MQTT_RC_PROTOCOL), BML_ERR_PROTOCOL);
  EXPECT_EQ(botMqttRefusedLink(BOT_MQTT_RC_ID_REJECTED), BML_ERR_ID);
  EXPECT_EQ(botMqttRefusedLink(0), BML_ERR_REFUSED);
  EXPECT_EQ(botMqttRefusedLink(77), BML_ERR_REFUSED);
}

TEST(Link, Errors) {
  EXPECT_EQ(botMqttLinkOnError(BML_IDLE, BOT_MQTT_ERRTYPE_REFUSED, BOT_MQTT_RC_NOT_AUTHORIZED), BML_REFUSED);
  EXPECT_EQ(botMqttLinkOnError(BML_RECONNECTING, BOT_MQTT_ERRTYPE_TRANSPORT, 0), BML_ERR_TRANSPORT);
  EXPECT_EQ(botMqttLinkOnError(BML_REFUSED, BOT_MQTT_ERRTYPE_TRANSPORT, 0), BML_ERR_TRANSPORT);
  EXPECT_EQ(botMqttLinkOnError(BML_IDLE, 0, 0), BML_ERR_OTHER);
  // While up, an error does not end the link; the disconnect that follows does.
  EXPECT_EQ(botMqttLinkOnError(BML_CONNECTED, BOT_MQTT_ERRTYPE_TRANSPORT, 0), BML_CONNECTED);
}

TEST(Link, Sequences) {
  // Refused login: ERROR then DISCONNECTED, retried; the reason stays until a connect works.
  uint8_t l = BML_IDLE;
  l = botMqttLinkOnError(l, BOT_MQTT_ERRTYPE_REFUSED, BOT_MQTT_RC_BAD_USER);
  l = botMqttLinkOnDisconnected(l);
  EXPECT_EQ(l, BML_REFUSED);
  l = botMqttLinkOnConnected(true);
  EXPECT_EQ(l, BML_CONNECTED);
  EXPECT_TRUE(botMqttLinkReady(l));
  // Broker goes away: transport error, then disconnect, then failed reconnects.
  l = botMqttLinkOnError(l, BOT_MQTT_ERRTYPE_TRANSPORT, 0);
  l = botMqttLinkOnDisconnected(l);
  EXPECT_EQ(l, BML_RECONNECTING);
  EXPECT_FALSE(botMqttLinkReady(l));
  l = botMqttLinkOnError(l, BOT_MQTT_ERRTYPE_TRANSPORT, 0);
  l = botMqttLinkOnDisconnected(l);
  EXPECT_EQ(l, BML_ERR_TRANSPORT);
  // A subscription that could not be sent is not ready.
  l = botMqttLinkOnConnected(false);
  EXPECT_EQ(l, BML_ERR_SUBSCRIBE);
  EXPECT_FALSE(botMqttLinkReady(l));
  EXPECT_EQ(botMqttLinkOnDisconnected(l), BML_RECONNECTING);
  for (uint8_t x = 0; x <= BML_ERR_OTHER; x++) EXPECT_EQ(botMqttLinkReady(x), x == BML_CONNECTED) << (int)x;
}

TEST(Link, UpForTheLastWordsBeforeAStop) {
  // The "offline" status and the emptied command list go out on any link that is connected, also
  // one whose subscription failed (it is not ready, but a clean stop would leave "online" retained).
  EXPECT_TRUE(botMqttLinkUp(botMqttLinkOnConnected(true)));
  EXPECT_TRUE(botMqttLinkUp(botMqttLinkOnConnected(false)));
  for (uint8_t x = 0; x <= BML_ERR_OTHER; x++)
    EXPECT_EQ(botMqttLinkUp(x), x == BML_CONNECTED || x == BML_ERR_SUBSCRIBE) << (int)x;
  EXPECT_FALSE(botMqttLinkUp(botMqttLinkOnDisconnected(BML_ERR_SUBSCRIBE)));
}

TEST(Show, StatesAndWords) {
  auto show = [](bool wanted, bool enabled, bool wifi, bool restarting, bool client, bool sf, bool rf, uint8_t link) {
    return botMqttShow(wanted, enabled, wifi, restarting, client, sf, rf, link);
  };
  BotMqttShown s = show(false, false, true, false, false, false, false, BML_IDLE);
  EXPECT_EQ(s.state, BMS_OFF); EXPECT_STREQ(s.detail, "off");
  s = show(false, true, true, false, false, false, false, BML_IDLE);
  EXPECT_EQ(s.state, BMS_OFF); EXPECT_STREQ(s.detail, "no broker set");
  s = show(true, true, false, false, true, false, false, BML_CONNECTED);
  EXPECT_EQ(s.state, BMS_WAIT_WIFI);
  s = show(true, true, true, true, false, false, false, BML_IDLE);
  EXPECT_EQ(s.state, BMS_CONNECTING); EXPECT_STREQ(s.detail, "restarting");
  s = botMqttShow(true, true, true, true, false, false, false, BML_IDLE, true);   // the stop keeps failing
  EXPECT_EQ(s.state, BMS_ERROR); EXPECT_STREQ(s.detail, "cannot restart: reboot the bot");
  s = botMqttShow(false, false, false, false, false, false, false, BML_IDLE, true);   // only while stopping
  EXPECT_EQ(s.state, BMS_OFF);
  s = show(true, true, true, false, true, false, true, BML_CONNECTED);
  EXPECT_EQ(s.state, BMS_ERROR); EXPECT_STREQ(s.detail, "cannot restart");
  s = show(true, true, true, false, false, false, false, BML_IDLE);
  EXPECT_EQ(s.state, BMS_CONNECTING); EXPECT_STREQ(s.detail, "starting");
  s = show(true, true, true, false, false, true, false, BML_IDLE);
  EXPECT_EQ(s.state, BMS_ERROR); EXPECT_STREQ(s.detail, "cannot start client");
  struct { uint8_t link; uint8_t state; const char* detail; } t[] = {
    { BML_IDLE, BMS_CONNECTING, "connecting" }, { BML_CONNECTED, BMS_CONNECTED, "connected" },
    { BML_RECONNECTING, BMS_CONNECTING, "reconnecting" }, { BML_REFUSED, BMS_REFUSED, "login refused" },
    { BML_ERR_UNAVAILABLE, BMS_ERROR, "broker unavailable" }, { BML_ERR_PROTOCOL, BMS_ERROR, "protocol refused" },
    { BML_ERR_ID, BMS_ERROR, "client id refused" }, { BML_ERR_REFUSED, BMS_ERROR, "connection refused" },
    { BML_ERR_TRANSPORT, BMS_ERROR, "cannot reach broker" }, { BML_ERR_SUBSCRIBE, BMS_ERROR, "subscribe failed" },
    { BML_ERR_OTHER, BMS_ERROR, "connection error" },
  };
  for (auto& k : t) {
    s = show(true, true, true, false, true, false, false, k.link);
    EXPECT_EQ(s.state, k.state) << k.detail;
    EXPECT_STREQ(s.detail, k.detail);
    EXPECT_LT(strlen(s.detail), 40u);   // fits BotMqttStatus::detail
  }
}

// ---- waiting slots ----

static BotReplyDest chanDest(const char* ch) { BotReplyDest d; memset(&d, 0, sizeof d); d.kind = BD_CHANNEL; strcpy(d.channel, ch); return d; }
static BotReplyDest dmDest(uint8_t k) { BotReplyDest d; memset(&d, 0, sizeof d); d.kind = BD_DIRECT; memset(d.key, k, sizeof d.key); return d; }
static BotReplyDest tryDest(uint32_t id) { BotReplyDest d; memset(&d, 0, sizeof d); d.kind = BD_TRY; d.try_id = id; return d; }

static BotVars someVars() {
  BotVars v;
  v.sender = "Dee"; v.args = "ipa"; v.path = "a5,bc"; v.channel = "#bot"; v.time = "12:04"; v.date = "2026-11-20";
  v.uptime = "3d 4h"; v.hops = 2; v.snr = 7.25f; v.rssi = -98; v.fetch = "x";
  return v;
}

TEST(Ask, TakeFullRelease) {
  BotAskTable t;
  for (int i = 0; i < BOT_ASK_SLOTS; i++) EXPECT_EQ(t.take(100), i);
  EXPECT_EQ(t.take(100), -1);
  EXPECT_EQ(t.count(), 4);
  t.release(2);
  EXPECT_FALSE(t.used(2));
  EXPECT_EQ(t.take(100), 2);
  t.release(-1); t.release(4);   // out of range: nothing
  EXPECT_EQ(t.count(), 4);
}

TEST(Ask, TokenNamesOneRequestOnly) {
  BotAskTable t;
  int i = t.take(0);
  uint8_t tok = t.token(i);
  EXPECT_EQ(t.fromToken(tok), i);
  t.release(i);
  EXPECT_EQ(t.fromToken(tok), -1);              // released
  int j = t.take(0);
  ASSERT_EQ(j, i);
  EXPECT_EQ(t.fromToken(tok), -1);              // the same slot, another request
  EXPECT_EQ(t.fromToken(t.token(j)), j);
  // Over many reuses a token is never 0 in its sequence part, and stale tokens never match.
  for (int k = 0; k < 200; k++) {
    uint8_t old = t.token(j);
    t.release(j);
    j = t.take(0);
    EXPECT_NE(t.fromToken(old), j);
    EXPECT_NE(t.token(j) >> 2, 0);
  }
}

TEST(Ask, FillAndVars) {
  BotAskTable t;
  int i = t.take(0);
  BotVars v = someVars();
  t.fill(i, chanDest("#bot"), "beer", "Dee", "!beer ipa", v, 93);
  BotVars o;
  t.vars(i, o);
  EXPECT_STREQ(o.sender, "Dee"); EXPECT_STREQ(o.args, "ipa"); EXPECT_STREQ(o.path, "a5,bc"); EXPECT_STREQ(o.channel, "#bot");
  EXPECT_STREQ(o.time, "12:04"); EXPECT_STREQ(o.date, "2026-11-20"); EXPECT_STREQ(o.uptime, "3d 4h");
  EXPECT_EQ(o.hops, 2); EXPECT_FLOAT_EQ(o.snr, 7.25f); EXPECT_EQ(o.rssi, -98); EXPECT_STREQ(o.fetch, "");
  EXPECT_EQ(t.at(i).budget, 93);
  EXPECT_STREQ(t.at(i).request, "!beer ipa");
  char out[128];
  botRender(out, sizeof out, "{sender} {hops} hops {snr} {channel} {fetch}|", o);
  EXPECT_STREQ(out, "Dee 2 hops 7.25 #bot |");
  // Values are owned: changing the source leaves the slot alone.
  int j = t.take(0);
  char name[8] = "Zed";
  v.sender = name;
  t.fill(j, dmDest(1), "x", name, "!x", v, 10);
  name[0] = 'Q';
  t.vars(j, o);
  EXPECT_STREQ(o.sender, "Zed");
  EXPECT_STREQ(o.channel, "");   // a direct message has no channel
}

TEST(Ask, FillCutsOnCharacterBoundary) {
  BotAskTable t;
  int i = t.take(0);
  BotVars v = someVars();
  std::string longArgs;
  for (int k = 0; k < 40; k++) longArgs += "\xC3\xA9";   // 80 bytes of 2-byte characters
  v.args = longArgs.c_str();
  t.fill(i, chanDest("#bot"), "c", "s", "r", v, 10);
  size_t n = strlen(t.at(i).args);
  EXPECT_LE(n, sizeof(t.at(i).args) - 1);
  EXPECT_EQ(n % 2, 0u);
}

TEST(Ask, SameRequestIsFoundOnce) {
  BotAskTable t;
  int i = t.take(0);
  BotVars v = someVars();
  v.args = "IPA cold";
  t.fill(i, chanDest("#bot"), "beer", "Dee", "!beer IPA cold", v, 90);
  EXPECT_EQ(t.findSame("beer", "ipa COLD", chanDest("#bot")), i);   // case ignored
  EXPECT_EQ(t.findSame("beer", "ipa", chanDest("#bot")), -1);
  EXPECT_EQ(t.findSame("beer", "IPA cold", chanDest("#test")), -1);   // another channel
  EXPECT_EQ(t.findSame("beer", "IPA cold", dmDest(3)), -1);
  EXPECT_EQ(t.findSame("wine", "IPA cold", chanDest("#bot")), -1);
  // Arguments longer than a slot keeps compare as stored.
  std::string a(100, 'x');
  int j = t.take(0);
  v.args = a.c_str();
  t.fill(j, tryDest(7), "c", "web", "!c", v, 90);
  EXPECT_EQ(t.findSame("c", a.c_str(), tryDest(7)), j);
  EXPECT_EQ(t.findSame("c", a.c_str(), tryDest(8)), -1);
  t.release(i);
  EXPECT_EQ(t.findSame("beer", "IPA cold", chanDest("#bot")), -1);
}

TEST(Ask, Backstop) {
  BotAskTable t;
  int i = t.take(0xFFFFFF00u);
  EXPECT_EQ(t.overdue(0xFFFFFF00u + BOT_ASK_BACKSTOP_MS - 1), -1);
  EXPECT_EQ(t.overdue(0xFFFFFF00u + BOT_ASK_BACKSTOP_MS), i);   // across the wrap
}

// ---- request path fields ----

static const char* lookupEven(const uint8_t* h, uint8_t, void*) { return (h[0] % 2 == 0) ? "Hill\x01top" : nullptr; }

TEST(AskPath, HexAndNames) {
  uint8_t path[6] = { 0xA4, 0x3E, 0x07, 0xBC, 0xAD, 0x10 };
  BotMsgInfo m; memset(&m, 0, sizeof m);
  m.path = path; m.hops = 3; m.hash_size = 2; m.direct = false;
  char b[64];
  EXPECT_EQ(botAskPathHex(b, sizeof b, m), strlen("a43e,07bc,ad10"));
  EXPECT_STREQ(b, "a43e,07bc,ad10");
  botAskPathNames(b, sizeof b, m, lookupEven, nullptr);
  EXPECT_STREQ(b, "Hilltop,?,?");   // control character removed; odd first bytes unknown
  m.direct = true;
  EXPECT_EQ(botAskPathHex(b, sizeof b, m), 0u); EXPECT_STREQ(b, "");
  EXPECT_EQ(botAskPathNames(b, sizeof b, m, lookupEven, nullptr), 0u); EXPECT_STREQ(b, "");
  m.direct = false; m.hops = 0;
  EXPECT_STREQ((botAskPathHex(b, sizeof b, m), b), "");
}

TEST(AskPath, WholeEntriesOnly) {
  uint8_t path[64];
  for (int i = 0; i < 64; i++) path[i] = (uint8_t)i;
  BotMsgInfo m; memset(&m, 0, sizeof m);
  m.path = path; m.hops = 64; m.hash_size = 1;
  for (size_t cap = 1; cap < 40; cap++) {
    char b[40];
    size_t n = botAskPathHex(b, cap, m);
    EXPECT_EQ(n, strlen(b));
    EXPECT_LT(n, cap);
    EXPECT_TRUE(n == 0 || (n + 1) % 3 == 0) << cap << " " << b;   // "xx" or "xx,yy",...
    n = botAskPathNames(b, cap, m, nullptr, nullptr);
    EXPECT_TRUE(n == 0 || (n + 1) % 2 == 0) << cap << " " << b;   // "?" or "?,?",...
  }
}

// ---- help and the command list ----

TEST(Merge, SkipsShadowedNames) {
  BotMqttCommands ann;
  ann.set("beer", "order a drink");
  ann.set("ping", "shadowed");
  ann.set("joke", "");
  ann.set("wx2", "x");
  const char* names[8] = { "ping", "wx2", "help" };
  const char* descs[8] = { "a", "b", "c" };
  const char* srcs[8] = { "builtin", "custom", "builtin" };
  int n = botAskMergeAnnounced(names, descs, srcs, 3, 8, ann);
  ASSERT_EQ(n, 5);
  EXPECT_STREQ(names[3], "beer"); EXPECT_STREQ(descs[3], "order a drink"); EXPECT_STREQ(srcs[3], "mqtt");
  EXPECT_STREQ(names[4], "joke"); EXPECT_STREQ(descs[4], ""); EXPECT_STREQ(srcs[4], "mqtt");
  // Without descriptions or sources (help), and bounded by max.
  const char* only[4] = { "ping" };
  EXPECT_EQ(botAskMergeAnnounced(only, nullptr, nullptr, 1, 2, ann), 2);
  EXPECT_STREQ(only[1], "beer");
  // The merged list becomes valid JSON with each source.
  char json[512];
  ASSERT_GT(botMqttBuildCommandList(json, sizeof json, names, descs, srcs, n), 0u);
  JsonDocument d;
  ASSERT_FALSE(deserializeJson(d, json));
  EXPECT_EQ(d["commands"].size(), 5u);
  EXPECT_STREQ(d["commands"][3]["source"], "mqtt");
}

// ---- routing and the cap ----

TEST(Broker, CapCountedAtDelivery) {
  EXPECT_TRUE(botCapAtDelivery(BRO_LOCAL));
  EXPECT_TRUE(botCapAtDelivery(BRO_BROKER));
  EXPECT_FALSE(botCapAtDelivery(BRO_NET));
  BotRateCap cap;
  // Asking costs nothing: many asks, the cap is untouched.
  for (int i = 0; i < 50; i++) EXPECT_TRUE(botBrokerMayAsk(BD_CHANNEL, true, cap, 1000, 2));
  bool capped = true;
  EXPECT_EQ(botDeliverRoute(BD_CHANNEL, true, false, botCapAtDelivery(BRO_BROKER), cap, 1000, 2, &capped), BDR_QUEUE);
  EXPECT_FALSE(capped);
  EXPECT_EQ(botDeliverRoute(BD_DIRECT, true, false, botCapAtDelivery(BRO_BROKER), cap, 1001, 2, &capped), BDR_QUEUE);
  EXPECT_EQ(botDeliverRoute(BD_CHANNEL, true, false, botCapAtDelivery(BRO_BROKER), cap, 1002, 2, &capped), BDR_DROP);
  EXPECT_TRUE(capped);                                       // the cap refused it
  EXPECT_FALSE(botBrokerMayAsk(BD_CHANNEL, true, cap, 1003, 2));   // its reply could not be sent: not asked
  EXPECT_TRUE(botBrokerMayAsk(BD_TRY, true, cap, 1003, 2));        // a try is never limited
  EXPECT_TRUE(botBrokerMayAsk(BD_CHANNEL, true, cap, 61001, 2));   // a minute later
}

TEST(Broker, NotReadyNeverAsks) {
  BotRateCap cap;
  for (uint8_t k : { (uint8_t)BD_CHANNEL, (uint8_t)BD_DIRECT, (uint8_t)BD_TRY }) EXPECT_FALSE(botBrokerMayAsk(k, false, cap, 0, 6));
}

TEST(Broker, CappedFlagOnlyForTheCap) {
  BotRateCap cap;
  bool capped = true;
  EXPECT_EQ(botDeliverRoute(BD_CHANNEL, false, false, true, cap, 0, 1, &capped), BDR_DROP);   // empty
  EXPECT_FALSE(capped);
  EXPECT_EQ(botDeliverRoute(BD_CHANNEL, true, true, true, cap, 0, 1, &capped), BDR_DROP);     // ring full
  EXPECT_FALSE(capped);
  EXPECT_EQ(botDeliverRoute(BD_TRY, true, false, true, cap, 0, 0, &capped), BDR_HOLDER);       // a try: cap not involved
  EXPECT_FALSE(capped);
  EXPECT_TRUE(cap.wouldAllow(0, 1));                                                          // nothing counted so far
  // An empty reply (timed out, rendered to nothing) never counts.
  for (int i = 0; i < 10; i++) botDeliverRoute(BD_CHANNEL, false, false, true, cap, 0, 1, &capped);
  EXPECT_TRUE(cap.wouldAllow(0, 1));
}

// ---- the waiting table's per-slot wait ----

TEST(WaitOwn, EachRequestKeepsItsWait) {
  BotMqttWaitTable w;
  int a = w.add("aaaaaaaa", 1000, 1, 15000);
  int b = w.add("bbbbbbbb", 1000, 1, 1000);
  w.setUser(a, 7); w.setUser(b, 9);
  uint8_t u = 99;
  EXPECT_EQ(w.expireOwn(1999, &u), -1); EXPECT_EQ(u, 0);
  EXPECT_EQ(w.expireOwn(2000, &u), b); EXPECT_EQ(u, 9);       // the short one only
  EXPECT_EQ(w.matchOwn("bbbbbbbb", 1, 2000, &u), -1);          // gone
  EXPECT_EQ(w.matchOwn("aaaaaaaa", 2, 2000, &u), -1);          // another session
  EXPECT_EQ(w.matchOwn("aaaaaaaa", 1, 15999, &u), a); EXPECT_EQ(u, 7);
  EXPECT_EQ(w.matchOwn("aaaaaaaa", 1, 15999, &u), -1);         // once only
}

TEST(WaitOwn, LateReplyNeverMatchesAndExpires) {
  BotMqttWaitTable w;
  int a = w.add("aaaaaaaa", 0xFFFFF000u, 3, 5000);
  w.setUser(a, 5);
  uint8_t u;
  EXPECT_EQ(w.matchOwn("aaaaaaaa", 3, 0xFFFFF000u + 5000, &u), -1);   // late (across the wrap)
  EXPECT_EQ(w.count(), 1);
  EXPECT_EQ(w.expireOwn(0xFFFFF000u + 5000, &u), a); EXPECT_EQ(u, 5);
}

TEST(WaitOwn, OldestOverdueFirst) {
  BotMqttWaitTable w;
  w.add("aaaaaaaa", 0, 1, 10000);
  int b = w.add("bbbbbbbb", 0, 1, 2000);
  uint8_t u;
  EXPECT_EQ(w.expireOwn(12000, &u), b);   // 10 s over its wait, against 2 s
}

TEST(WaitOwn, TakeAnyAndRelease) {
  BotMqttWaitTable w;
  for (int i = 0; i < 4; i++) { char id[9]; botMqttMakeId(id, 0x100 + i); w.setUser(w.add(id, 0, 1, 5000), (uint8_t)(10 + i)); }
  w.release(1);
  int seen = 0; uint8_t u; int total = 0;
  while (w.takeAny(&u) >= 0) { seen |= 1 << (u - 10); total++; }
  EXPECT_EQ(total, 3);
  EXPECT_EQ(seen, 0b1101);
  EXPECT_EQ(w.count(), 0);
}

TEST(Rate, WouldAllowRecordsNothing) {
  BotMqttRate r;
  for (int i = 0; i < 100; i++) EXPECT_TRUE(r.wouldAllow(0));
  for (int i = 0; i < 30; i++) EXPECT_TRUE(r.allow(i));
  EXPECT_FALSE(r.wouldAllow(30));
  EXPECT_TRUE(r.wouldAllow(60000));
}

// ---- no password copy when off, inbox records, what may reach the broker, the command list ----

TEST(Conn, NoPasswordWhenDisabled) {
  cfgBroker(g_cfg);
  g_cfg.mqtt_enabled = false;
  botMqttConnFrom(s_a, g_cfg, "MeshBot");
  EXPECT_STREQ(s_a.pass, "");
  cfgBroker(g_cfg);
  g_cfg.mqtt_host[0] = '\0';                      // enabled but no host: not on, no password either
  botMqttConnFrom(s_a, g_cfg, "MeshBot");
  EXPECT_STREQ(s_a.pass, "");
  cfgBroker(g_cfg);
  botMqttConnFrom(s_b, g_cfg, "MeshBot");
  EXPECT_STREQ(s_b.pass, "secret-pass");
  EXPECT_TRUE(botMqttNeedsRestart(s_a, s_b));
}

TEST(Rec, RoundTrip) {
  uint8_t b[BOT_MQTT_REC_MAX];
  std::string pay(512, 'p');
  size_t n = botMqttRecEncode(b, sizeof b, BMI_REPLY, 0xA1B2C3D4u, 77, "0123456789abcdef", 16, pay.data(), pay.size());
  ASSERT_EQ(n, (size_t)BOT_MQTT_REC_MAX);
  BotMqttRec r;
  ASSERT_TRUE(botMqttRecDecode(b, n, r));
  EXPECT_EQ(r.kind, BMI_REPLY); EXPECT_EQ(r.gen, 0xA1B2C3D4u); EXPECT_EQ(r.conn, 77u);
  EXPECT_STREQ(r.tail, "0123456789abcdef");
  EXPECT_EQ(r.len, 512u); EXPECT_EQ(memcmp(r.payload, pay.data(), 512), 0);
  // An empty payload (an announcement removal) and a one-byte tail.
  n = botMqttRecEncode(b, sizeof b, BMI_COMMAND, 1, 2, "x", 1, nullptr, 0);
  ASSERT_EQ(n, (size_t)BOT_MQTT_REC_HEAD + 1);
  ASSERT_TRUE(botMqttRecDecode(b, n, r));
  EXPECT_EQ(r.kind, BMI_COMMAND); EXPECT_STREQ(r.tail, "x"); EXPECT_EQ(r.len, 0u);
}

TEST(Rec, EncodeRefuses) {
  uint8_t b[BOT_MQTT_REC_MAX];
  EXPECT_EQ(botMqttRecEncode(b, sizeof b, BMI_NONE, 1, 1, "a", 1, "x", 1), 0u);
  EXPECT_EQ(botMqttRecEncode(b, sizeof b, 9, 1, 1, "a", 1, "x", 1), 0u);
  EXPECT_EQ(botMqttRecEncode(b, sizeof b, BMI_REPLY, 1, 1, "", 0, "x", 1), 0u);
  EXPECT_EQ(botMqttRecEncode(b, sizeof b, BMI_REPLY, 1, 1, "01234567890123456", 17, "x", 1), 0u);
  EXPECT_EQ(botMqttRecEncode(b, sizeof b, BMI_REPLY, 1, 1, "a", 1, nullptr, 3), 0u);
  EXPECT_EQ(botMqttRecEncode(b, BOT_MQTT_REC_HEAD + 1, BMI_REPLY, 1, 1, "a", 1, "x", 1), 0u);   // does not fit
  EXPECT_EQ(botMqttRecEncode(b, BOT_MQTT_REC_HEAD + 2, BMI_REPLY, 1, 1, "a", 1, "x", 1), (size_t)BOT_MQTT_REC_HEAD + 2);
}

TEST(Rec, DecodeRefuses) {
  uint8_t b[64];
  size_t n = botMqttRecEncode(b, sizeof b, BMI_REPLY, 1, 1, "abcd", 4, "{}", 2);
  BotMqttRec r;
  for (size_t k = 0; k < BOT_MQTT_REC_HEAD + 4; k++) EXPECT_FALSE(botMqttRecDecode(b, k, r)) << k;   // truncated
  EXPECT_TRUE(botMqttRecDecode(b, BOT_MQTT_REC_HEAD + 4, r));                                         // no payload left: still a record
  EXPECT_EQ(r.len, 0u);
  uint8_t c[64]; memcpy(c, b, n);
  c[9] = 17; EXPECT_FALSE(botMqttRecDecode(c, n, r));                                                 // oversize tail
  c[9] = 0;  EXPECT_FALSE(botMqttRecDecode(c, n, r));
  memcpy(c, b, n); c[0] = BMI_NONE; EXPECT_FALSE(botMqttRecDecode(c, n, r));                          // unknown kind
  c[0] = 200; EXPECT_FALSE(botMqttRecDecode(c, n, r));
  EXPECT_STREQ(r.tail, ""); EXPECT_EQ(r.kind, BMI_NONE);
  EXPECT_FALSE(botMqttRecDecode(nullptr, n, r));
}

TEST(Offer, WhatMayLeaveTheDevice) {
  BotRateCap cap;
  // channel message with prefix: yes; DM with prefix: yes; DM without: no.
  EXPECT_TRUE(botAskMayOffer(BD_CHANNEL, true, false, true, cap, 0, 6));
  EXPECT_TRUE(botAskMayOffer(BD_DIRECT, true, false, true, cap, 0, 6));
  EXPECT_FALSE(botAskMayOffer(BD_DIRECT, false, false, true, cap, 0, 6));
  EXPECT_FALSE(botAskMayOffer(BD_TRY, false, false, true, cap, 0, 6));
  EXPECT_TRUE(botAskMayOffer(BD_TRY, true, false, true, cap, 0, 6));
  // a switched-off command: no; MQTT not ready: no.
  EXPECT_FALSE(botAskMayOffer(BD_CHANNEL, true, true, true, cap, 0, 6));
  EXPECT_FALSE(botAskMayOffer(BD_DIRECT, true, false, false, cap, 0, 6));
}

TEST(Offer, DmWithoutPrefixWhateverIsAnnounced) {
  BotParsed p;
  ASSERT_TRUE(botParse("hi there", '!', true, p));
  EXPECT_FALSE(p.prefixed);
  EXPECT_FALSE(botAskMayOffer(BD_DIRECT, p.prefixed, false, true, BotRateCap(), 0, 6));
  ASSERT_TRUE(botParse("  !hi there", '!', true, p));
  EXPECT_TRUE(p.prefixed);
  ASSERT_TRUE(botParse("!beer", '!', false, p));
  EXPECT_TRUE(p.prefixed);
}

TEST(Offer, KnownDisabled) {
  botConfigDefaults(g_cfg);
  EXPECT_FALSE(botAskKnownDisabled(g_cfg, "dist"));        // enabled: not this rule's business
  EXPECT_FALSE(botAskKnownDisabled(g_cfg, "xyz"));
  for (int i = 0; i < BB_COUNT; i++) if (strcmp(g_cfg.builtin[i].name, "dist") == 0) g_cfg.builtin[i].enabled = false;
  EXPECT_TRUE(botAskKnownDisabled(g_cfg, "dist"));
  // A renamed, disabled built-in: by its configured name; the old name is just unknown.
  for (int i = 0; i < BB_COUNT; i++) if (strcmp(g_cfg.builtin[i].name, "sun") == 0) { strcpy(g_cfg.builtin[i].name, "sol"); g_cfg.builtin[i].enabled = false; }
  EXPECT_TRUE(botAskKnownDisabled(g_cfg, "sol"));
  EXPECT_FALSE(botAskKnownDisabled(g_cfg, "sun"));
  strcpy(g_cfg.custom[0].trigger, "beer"); g_cfg.custom[0].enabled = false;
  strcpy(g_cfg.custom[1].trigger, "wine"); g_cfg.custom[1].enabled = true;
  g_cfg.num_custom = 2;
  EXPECT_TRUE(botAskKnownDisabled(g_cfg, "beer"));
  EXPECT_FALSE(botAskKnownDisabled(g_cfg, "wine"));
  g_cfg.num_custom = 0;                                     // past num_custom: not a command
  EXPECT_FALSE(botAskKnownDisabled(g_cfg, "beer"));
  EXPECT_FALSE(botAskKnownDisabled(g_cfg, nullptr));
  EXPECT_FALSE(botAskMayOffer(BD_DIRECT, true, botAskKnownDisabled(g_cfg, "dist"), true, BotRateCap(), 0, 6));
}

TEST(ListOrder, CustomThenAnnouncedThenBuiltin) {
  BotMqttCommands ann;
  ann.set("beer", "order a drink");
  ann.set("ping", "shadowed by a built-in");
  ann.set("wine", "shadowed by a custom command");
  ann.set("joke", "");
  const char* bn[] = { "ping", "help" }; const char* bd[] = { "p", "h" };
  const char* cn[] = { "wine" };         const char* cd[] = { "w" };
  const char* n[10]; const char* d[10]; const char* s[10];
  int k = botAskListEntries(n, d, s, 10, bn, bd, 2, cn, cd, 1, ann);
  ASSERT_EQ(k, 5);
  const char* want[] = { "wine", "beer", "joke", "ping", "help" };
  const char* src[] = { "custom", "mqtt", "mqtt", "builtin", "builtin" };
  for (int i = 0; i < 5; i++) { EXPECT_STREQ(n[i], want[i]); EXPECT_STREQ(s[i], src[i]); }
  EXPECT_STREQ(d[1], "order a drink");
  EXPECT_EQ(botAskListEntries(n, d, s, 2, bn, bd, 2, cn, cd, 1, ann), 2);   // bounded
}

// The real worst case: 14 built-ins, 16 custom and 16 announced commands, names of 16 and
// descriptions of 48 bytes that need no escaping, fit one exact-length buffer and the heap cap.
TEST(ListSize, WorstCaseFitsAndIsExact) {
  std::vector<std::string> names, descs; std::vector<const char*> n, d, s;
  for (int i = 0; i < 46; i++) {
    char b[17]; snprintf(b, sizeof b, "c%015d", i);
    names.push_back(b);
    descs.push_back(std::string(i < 14 ? 37 : 48, 'd'));
  }
  for (int i = 0; i < 46; i++) { n.push_back(names[i].c_str()); d.push_back(descs[i].c_str()); s.push_back(i < 16 ? "custom" : i < 32 ? "mqtt" : "builtin"); }
  size_t need = botMqttCommandListLen(n.data(), d.data(), s.data(), 46);
  EXPECT_LE(need, 6144u);
  std::vector<char> b(need + 1, 'z');
  size_t k = botMqttBuildCommandList(b.data(), need + 1, n.data(), d.data(), s.data(), 46);
  EXPECT_EQ(k, need);
  JsonDocument doc(&g_bigAlloc);
  ASSERT_FALSE(deserializeJson(doc, b.data()));
  EXPECT_EQ(doc["commands"].size(), 46u);
  EXPECT_TRUE(doc["more"].isNull());
}

TEST(ListSize, LengthMatchesEscaping) {
  const char* n[] = { "a", nullptr, "", "b<" };
  const char* d[] = { "q\"\\\n\r\t<\x01", "x", "y", nullptr };
  const char* s[] = { "custom", "x", "y", nullptr };
  char b[512];
  size_t need = botMqttCommandListLen(n, d, s, 4);
  EXPECT_EQ(botMqttBuildCommandList(b, sizeof b, n, d, s, 4), need);
  EXPECT_EQ(strlen(b), need);
  EXPECT_EQ(botMqttCommandListLen(nullptr, nullptr, nullptr, 0), strlen("{\"commands\":[]}"));
  EXPECT_EQ(botJsonEscapedLen(nullptr), 2u);
  EXPECT_EQ(botJsonEscapedLen("<"), 8u);
}

// ---- the serial status line ----

// Every word botMqttShow can give, over all its inputs.
static std::vector<std::string> allShownWords() {
  std::vector<std::string> out;
  for (int bits = 0; bits < 256; bits++)
    for (int link = BML_IDLE; link <= BML_ERR_OTHER + 1; link++) {
      BotMqttShown s = botMqttShow(bits & 1, bits & 2, bits & 4, bits & 8, bits & 16, bits & 32, bits & 64, (uint8_t)link, bits & 128);
      if (std::find(out.begin(), out.end(), std::string(s.detail)) == out.end()) out.push_back(s.detail);
    }
  return out;
}

TEST(StatusLine, EveryWordHasAShortCode) {
  std::vector<std::string> words = allShownWords();
  EXPECT_EQ(words.size(), 19u);
  std::vector<std::string> codes;
  for (auto& w : words) {
    EXPECT_LT(w.size() + 1, (size_t)BOT_MQTT_DETAIL_CAP) << w;   // fits BotMqttStatus::detail
    const char* c = botMqttDetailCode(w.c_str());
    EXPECT_STRNE(c, "?") << w;
    EXPECT_LE(strlen(c), (size_t)BOT_STATUS_MQ_MAX) << w;
    EXPECT_EQ(std::find(codes.begin(), codes.end(), std::string(c)), codes.end()) << "two words share " << c;
    codes.push_back(c);
  }
  EXPECT_STREQ(botMqttDetailCode("cannot reach broker"), "unreach");
  EXPECT_STREQ(botMqttDetailCode("login refused"), "refused");
  EXPECT_STREQ(botMqttDetailCode("connected"), "ok");
  EXPECT_STREQ(botMqttDetailCode("broker.example.org"), "?");
  EXPECT_STREQ(botMqttDetailCode(nullptr), "?");
}

// The longest every fixed item can be: an outcome code longer than any real one (cut to 6), the
// longest broker code, every number past its clamp, a 30-character scope (cut to 12), a 15-character
// address and no clock. One 31-byte channel name still fits whole after them.
static BotStatusLine worstLine(const char* const* chans, int n, const char* mq) {
  BotStatusLine s;
  s.busy = true; s.parked = 4000000000u; s.net = "connect failed, very long";
  s.ms = s.dns_ms = s.conn_ms = s.xchg_ms = 0xFFFFFFFFu;
  s.heap = s.min_heap = s.max_block = 0xFFFFFFFFu;
  s.stk = s.mq_stk = 0xFFFFFFFFu;
  s.mq = mq; s.scope = "abcdefghijklmnopqrstuvwxyz0123"; s.ip = "255.255.255.255"; s.clock = false;
  s.chans = chans; s.nchans = n;
  return s;
}

TEST(StatusLine, WorstCaseLeavesRoomForAChannel) {
  std::string ch31 = "#" + std::string(30, 'w');
  const char* chans[] = { ch31.c_str(), "#bot" };
  for (auto& w : allShownWords()) {
    char b[BOT_STATUS_LINE_CAP];
    BotStatusLine s = worstLine(chans, 2, botMqttDetailCode(w.c_str()));
    size_t n = botFormatStatus(b, sizeof b, s);
    EXPECT_EQ(n, strlen(b));
    EXPECT_LT(n, sizeof b);
    ASSERT_NE(strstr(b, ch31.c_str()), nullptr) << b;    // the first channel, whole
    EXPECT_NE(strstr(b, " -clk "), nullptr) << b;       // every fixed item is there, uncut
    EXPECT_NE(strstr(b, " 255.255.255.255 "), nullptr) << b;
    EXPECT_NE(strstr(b, (std::string(" mq:") + botMqttDetailCode(w.c_str()) + "/").c_str()), nullptr) << b;
  }
  // The fixed items alone, at their longest, are the line's fixed bound.
  char b[BOT_STATUS_LINE_CAP];
  BotStatusLine s = worstLine(nullptr, 0, "restart");
  size_t fixed = botFormatStatus(b, sizeof b, s);
  EXPECT_STREQ(b, "> busy p9 connec 99999ms 99999/99999/99999 kb 9999/9999/9999 stk 99999 mq:restart/9999 "
                  "sc:abcdefghijkl 255.255.255.255 -clk");
  EXPECT_EQ(fixed, (size_t)BOT_STATUS_FIXED_MAX);
  // separator + 31-byte name + terminator, with at least 4 bytes to spare
  EXPECT_LE(fixed + 1 + 31 + 1 + 4, (size_t)BOT_STATUS_LINE_CAP);
}

TEST(StatusLine, TypicalAndChannels) {
  const char* chans[] = { "#bot", "#test", "Public" };
  BotStatusLine s;
  memset(&s, 0, sizeof s);
  s.net = "ok"; s.ms = 4512; s.dns_ms = 20; s.conn_ms = 3900; s.xchg_ms = 590;
  s.heap = 106 * 1024 + 500; s.min_heap = 52 * 1024; s.max_block = 60 * 1024; s.stk = 4321;
  s.mq = "ok"; s.mq_stk = 1980; s.scope = "ca"; s.ip = "192.168.1.20"; s.clock = true; s.chans = chans; s.nchans = 3;
  char b[BOT_STATUS_LINE_CAP];
  botFormatStatus(b, sizeof b, s);
  EXPECT_STREQ(b, "> idle p0 ok 4512ms 20/3900/590 kb 106/52/60 stk 4321 mq:ok/1980 sc:ca 192.168.1.20 clk #bot,#test,Public");
  s.scope = ""; s.ip = ""; s.nchans = 0;
  botFormatStatus(b, sizeof b, s);
  EXPECT_STREQ(b, "> idle p0 ok 4512ms 20/3900/590 kb 106/52/60 stk 4321 mq:ok/1980 sc:- - clk");
  // A small buffer: always terminated, whole channel names only.
  char small[40];
  s.nchans = 3;
  size_t n = botFormatStatus(small, sizeof small, s);
  EXPECT_EQ(n, strlen(small));
  EXPECT_EQ(n, sizeof small - 1);
  EXPECT_EQ(botFormatStatus(small, 0, s), 0u);
}

// ---- an announced command hidden by the bot's own ----

TEST(Hidden, BuiltinOrCustomOnOrOff) {
  BotConfig& c = g_cfg;
  botConfigDefaults(c);
  c.num_custom = 2;
  memset(c.custom, 0, sizeof c.custom);
  strcpy(c.custom[0].trigger, "joke"); strcpy(c.custom[0].reply, "x"); c.custom[0].enabled = true;
  strcpy(c.custom[1].trigger, "quiet"); strcpy(c.custom[1].reply, "x"); c.custom[1].enabled = false;
  EXPECT_EQ(botAskHiddenBy(c, "ping"), BAH_BUILTIN);
  EXPECT_EQ(botAskHiddenBy(c, "joke"), BAH_CUSTOM);
  EXPECT_EQ(botAskHiddenBy(c, "quiet"), BAH_CUSTOM);         // switched off: it keeps the name silent
  EXPECT_EQ(botAskHiddenBy(c, "tides"), BAH_NONE);
  EXPECT_EQ(botAskHiddenBy(c, ""), BAH_NONE);
  EXPECT_EQ(botAskHiddenBy(c, nullptr), BAH_NONE);
  c.builtin[BB_PING].enabled = false;
  EXPECT_EQ(botAskHiddenBy(c, "ping"), BAH_BUILTIN);
  strcpy(c.builtin[BB_PING].name, "pong");                   // renamed: the new name hides, the old one is free
  EXPECT_EQ(botAskHiddenBy(c, "pong"), BAH_BUILTIN);
  EXPECT_EQ(botAskHiddenBy(c, "ping"), BAH_NONE);
  c.num_custom = 0;
  EXPECT_EQ(botAskHiddenBy(c, "joke"), BAH_NONE);            // only the listed custom commands
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
