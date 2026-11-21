// Host tests for BotWebRules.h (the pure checks behind the bot's web page handlers).
#include <gtest/gtest.h>
#include <cstring>
#include <string>
#include <vector>
#include "helpers/bot/BotWebRules.h"
#include "helpers/bot/BotWebMqtt.h"

// ---- same-origin check ----

TEST(Origin, UrlHost) {
  char h[64];
  EXPECT_TRUE(botUrlHost("http://192.168.1.20", h, sizeof(h)));        EXPECT_STREQ(h, "192.168.1.20");
  EXPECT_TRUE(botUrlHost("http://192.168.1.20:80/x?y", h, sizeof(h))); EXPECT_STREQ(h, "192.168.1.20:80");
  EXPECT_TRUE(botUrlHost("https://Bot.Local/", h, sizeof(h)));         EXPECT_STREQ(h, "Bot.Local");
  EXPECT_FALSE(botUrlHost("null", h, sizeof(h)));
  EXPECT_FALSE(botUrlHost("ftp://a/", h, sizeof(h)));
  EXPECT_FALSE(botUrlHost("http://", h, sizeof(h)));
  EXPECT_FALSE(botUrlHost(nullptr, h, sizeof(h)));
  EXPECT_FALSE(botUrlHost("http://averyveryverylonghostname.example", h, 8));
}
TEST(Origin, OriginMatchesHost) {
  EXPECT_TRUE(botSameOrigin("http://192.168.1.20", nullptr, "192.168.1.20"));
  EXPECT_TRUE(botSameOrigin("http://BOT.local", nullptr, "bot.local"));
  EXPECT_FALSE(botSameOrigin("http://evil.example", nullptr, "192.168.1.20"));
  EXPECT_FALSE(botSameOrigin("http://192.168.1.20:8080", nullptr, "192.168.1.20"));
  EXPECT_FALSE(botSameOrigin("http://192.168.1.2", nullptr, "192.168.1.20"));
}
TEST(Origin, RefererUsedOnlyWithoutOrigin) {
  EXPECT_TRUE(botSameOrigin(nullptr, "http://192.168.1.20/", "192.168.1.20"));
  EXPECT_TRUE(botSameOrigin("", "http://192.168.1.20/page", "192.168.1.20"));
  EXPECT_FALSE(botSameOrigin("http://evil.example", "http://192.168.1.20/", "192.168.1.20"));
  EXPECT_FALSE(botSameOrigin("null", "http://192.168.1.20/", "192.168.1.20"));
}
TEST(Origin, MissingHeadersOrHost) {
  EXPECT_FALSE(botSameOrigin(nullptr, nullptr, "192.168.1.20"));
  EXPECT_FALSE(botSameOrigin("", "", "192.168.1.20"));
  EXPECT_FALSE(botSameOrigin("http://192.168.1.20", nullptr, nullptr));
  EXPECT_FALSE(botSameOrigin("http://192.168.1.20", nullptr, ""));
}

// ---- cookie ----

TEST(Cookie, FindsBotValue) {
  char v[40];
  EXPECT_TRUE(botCookieValue("bot=abc", "bot", v, sizeof(v)));                  EXPECT_STREQ(v, "abc");
  EXPECT_TRUE(botCookieValue("x=1; bot=abc; y=2", "bot", v, sizeof(v)));        EXPECT_STREQ(v, "abc");
  EXPECT_TRUE(botCookieValue("robot=zz;bot=abc", "bot", v, sizeof(v)));         EXPECT_STREQ(v, "abc");
  EXPECT_FALSE(botCookieValue("robot=zz", "bot", v, sizeof(v)));
  EXPECT_FALSE(botCookieValue("", "bot", v, sizeof(v)));
  EXPECT_FALSE(botCookieValue(nullptr, "bot", v, sizeof(v)));
  EXPECT_FALSE(botCookieValue("bot=0123456789", "bot", v, 5));                  // too long for out
}

// ---- login lockout (per client address, plus a global ceiling) ----

static const uint32_t IP_A = 0x0A00000Au, IP_B = 0x0B00000Bu;

TEST(Lockout, FiveFailsLockThatAddressSixtySeconds) {
  BotLoginLimiter l;
  for (int i = 0; i < 4; i++) { EXPECT_EQ(l.retryAfterS(IP_A, 1000), 0u); l.fail(IP_A, 1000); }
  EXPECT_EQ(l.retryAfterS(IP_A, 1000), 0u);
  l.fail(IP_A, 2000);
  EXPECT_EQ(l.retryAfterS(IP_A, 2000), 60u);
  EXPECT_EQ(l.retryAfterS(IP_A, 61500), 1u);
  EXPECT_EQ(l.retryAfterS(IP_A, 62000), 0u);
  l.fail(IP_A, 62000);                          // the count restarts after a lock ends
  EXPECT_EQ(l.retryAfterS(IP_A, 62000), 0u);
}
TEST(Lockout, OtherAddressNotLocked) {
  BotLoginLimiter l;
  for (int i = 0; i < 5; i++) l.fail(IP_A, 1000);
  EXPECT_GT(l.retryAfterS(IP_A, 1000), 0u);
  EXPECT_EQ(l.retryAfterS(IP_B, 1000), 0u);
}
TEST(Lockout, SuccessResetsOnlyThatAddress) {
  BotLoginLimiter l;
  for (int i = 0; i < 4; i++) { l.fail(IP_A, 0); l.fail(IP_B, 0); }
  l.success(IP_A);
  l.fail(IP_A, 10); l.fail(IP_B, 10);
  EXPECT_EQ(l.retryAfterS(IP_A, 10), 0u);
  EXPECT_GT(l.retryAfterS(IP_B, 10), 0u);
}
TEST(Lockout, FailWhileLockedDoesNotExtend) {
  BotLoginLimiter l;
  for (int i = 0; i < 5; i++) l.fail(IP_A, 0);
  l.fail(IP_A, 30000);
  EXPECT_EQ(l.retryAfterS(IP_A, 60000), 0u);
}
TEST(Lockout, GlobalCeilingTwentyPerMinute) {
  BotLoginLimiter l;
  for (uint32_t i = 0; i < 20; i++) l.fail(0x0C000000u + i, 1000 + i);   // 20 addresses, one wrong each
  EXPECT_GT(l.retryAfterS(IP_B, 1100), 0u);                              // everyone waits
  EXPECT_EQ(l.retryAfterS(IP_B, 1000 + 60000), 0u);                      // the oldest left the minute
  BotLoginLimiter m;
  for (uint32_t i = 0; i < 19; i++) m.fail(0x0C000000u + i, 1000);
  EXPECT_EQ(m.retryAfterS(IP_B, 1000), 0u);
}
TEST(Lockout, GlobalCeilingIsSliding) {
  BotLoginLimiter l;
  for (uint32_t i = 0; i < 19; i++) l.fail(0x0C000000u + i, 0);
  l.fail(IP_B, 59000);                                                    // 20 within the minute
  EXPECT_GT(l.retryAfterS(IP_A, 59000), 0u);
  EXPECT_EQ(l.retryAfterS(IP_A, 60000), 0u);                              // 19 left in the last minute
}
TEST(Lockout, TableOfEightReplacesOldest) {
  BotLoginLimiter l;
  for (int i = 0; i < 4; i++) l.fail(IP_A, 0);                            // IP_A: 4 strikes, oldest entry
  for (uint32_t i = 1; i <= 8; i++) l.fail(0x0D000000u + i, 100 * i);    // 8 newer addresses push it out
  l.fail(IP_A, 70000);                                                    // a fresh count: 1 strike
  EXPECT_EQ(l.retryAfterS(IP_A, 70000), 0u);
}
TEST(Lockout, SurvivesRollover) {
  BotLoginLimiter l;
  for (int i = 0; i < 5; i++) l.fail(IP_A, 0xFFFFFF00u);
  EXPECT_GT(l.retryAfterS(IP_A, 0x00000100u), 0u);
  EXPECT_EQ(l.retryAfterS(IP_A, 0xFFFFFF00u + 60000u), 0u);
}

// ---- field checks ----

TEST(Fields, PasswordLength) {
  EXPECT_FALSE(botWebPasswordOk(nullptr));
  EXPECT_FALSE(botWebPasswordOk("1234567"));
  EXPECT_TRUE(botWebPasswordOk("12345678"));
  EXPECT_TRUE(botWebPasswordOk(std::string(63, 'a').c_str()));
  EXPECT_FALSE(botWebPasswordOk(std::string(64, 'a').c_str()));
  EXPECT_FALSE(botWebPasswordOk("1234567\x01"));
}
TEST(Fields, NodeName) {
  EXPECT_TRUE(botNodeNameOk("Mesh Bot \xF0\x9F\xA4\x96"));
  EXPECT_FALSE(botNodeNameOk(""));
  EXPECT_FALSE(botNodeNameOk(nullptr));
  EXPECT_TRUE(botNodeNameOk(std::string(31, 'a').c_str()));
  EXPECT_FALSE(botNodeNameOk(std::string(32, 'a').c_str()));
  EXPECT_FALSE(botNodeNameOk("a\tb"));
  EXPECT_FALSE(botNodeNameOk("a:b"));
  EXPECT_FALSE(botNodeNameOk("a,b"));
  EXPECT_FALSE(botNodeNameOk("[a]"));
}
TEST(Fields, Scope) {
  EXPECT_TRUE(botScopeOk(""));
  EXPECT_TRUE(botScopeOk("ontario-east2"));
  EXPECT_TRUE(botScopeOk(std::string(30, 'a').c_str()));
  EXPECT_FALSE(botScopeOk(std::string(31, 'a').c_str()));
  EXPECT_FALSE(botScopeOk("Ontario"));
  EXPECT_FALSE(botScopeOk("#ontario"));
  EXPECT_FALSE(botScopeOk("a b"));
  EXPECT_FALSE(botScopeOk(nullptr));
}

// ---- setup form ----

static const char* parseSetup(const char* json, BotSetupData& d) {
  JsonDocument doc;
  if (deserializeJson(doc, json)) return "bad json in test";
  return botParseSetup(doc.as<JsonVariantConst>(), d);
}

TEST(Setup, Valid) {
  BotSetupData d;
  EXPECT_EQ(parseSetup(R"({"ssid":"Home","wifi_password":"secretpw","web_password":"webpass1","name":"Mesh Bot","lat":45.5,"lon":-75.25,"scope":"ottawa"})", d), nullptr);
  EXPECT_STREQ(d.ssid, "Home");
  EXPECT_STREQ(d.wifi_password, "secretpw");
  EXPECT_STREQ(d.web_password, "webpass1");
  EXPECT_STREQ(d.name, "Mesh Bot");
  EXPECT_DOUBLE_EQ(d.lat, 45.5);
  EXPECT_DOUBLE_EQ(d.lon, -75.25);
  EXPECT_STREQ(d.scope, "ottawa");
}
TEST(Setup, OpenNetworkAndNoLocation) {
  BotSetupData d;
  EXPECT_EQ(parseSetup(R"({"ssid":"Cafe","wifi_password":"","web_password":"webpass1","name":"Bot","scope":""})", d), nullptr);
  EXPECT_STREQ(d.wifi_password, "");
  EXPECT_DOUBLE_EQ(d.lat, 0.0);
  EXPECT_DOUBLE_EQ(d.lon, 0.0);
}
TEST(Setup, Rejections) {
  BotSetupData d;
  const char* base_ok = R"({"ssid":"Home","wifi_password":"secretpw","web_password":"webpass1","name":"Bot","lat":1,"lon":2,"scope":"x"})";
  EXPECT_EQ(parseSetup(base_ok, d), nullptr);
  EXPECT_NE(parseSetup(R"({"ssid":"","wifi_password":"","web_password":"webpass1","name":"Bot"})", d), nullptr);
  EXPECT_NE(parseSetup(R"({"ssid":"123456789012345678901234567890123","web_password":"webpass1","name":"Bot"})", d), nullptr);
  EXPECT_NE(parseSetup(R"({"ssid":"Home","wifi_password":"short","web_password":"webpass1","name":"Bot"})", d), nullptr);
  EXPECT_NE(parseSetup(R"({"ssid":"Home","web_password":"short","name":"Bot"})", d), nullptr);
  EXPECT_NE(parseSetup(R"({"ssid":"Home","web_password":"webpass1","name":""})", d), nullptr);
  EXPECT_NE(parseSetup(R"({"ssid":"Home","web_password":"webpass1","name":"a\nb"})", d), nullptr);
  EXPECT_NE(parseSetup(R"({"ssid":"Home","web_password":"webpass1","name":"Bot","lat":91})", d), nullptr);
  EXPECT_NE(parseSetup(R"({"ssid":"Home","web_password":"webpass1","name":"Bot","lon":-180.5})", d), nullptr);
  EXPECT_NE(parseSetup(R"({"ssid":"Home","web_password":"webpass1","name":"Bot","lat":"45"})", d), nullptr);
  EXPECT_NE(parseSetup(R"({"ssid":"Home","web_password":"webpass1","name":"Bot","scope":"Bad Scope"})", d), nullptr);
  EXPECT_NE(parseSetup(R"({"ssid":"Home","web_password":"webpass1","name":"Bot","scope":5})", d), nullptr);
  EXPECT_NE(parseSetup(R"({"ssid":"Home","web_password":"webpass1"})", d), nullptr);
  EXPECT_NE(parseSetup(R"([1,2])", d), nullptr);
  EXPECT_NE(parseSetup(R"({"ssid":"Ho\u0000me","web_password":"webpass1","name":"Bot"})", d), nullptr);
}

// ---- JSON string escaping ----

TEST(Escape, Basic) {
  char b[64];
  EXPECT_EQ(botJsonEscape(b, sizeof(b), "plain"), 7u);                 EXPECT_STREQ(b, "\"plain\"");
  botJsonEscape(b, sizeof(b), "a\"b\\c");                               EXPECT_STREQ(b, "\"a\\\"b\\\\c\"");
  botJsonEscape(b, sizeof(b), "x\ny\x01");                              EXPECT_STREQ(b, "\"x\\ny\\u0001\"");
  botJsonEscape(b, sizeof(b), "<script>");                              EXPECT_STREQ(b, "\"\\u003cscript>\"");
  botJsonEscape(b, sizeof(b), "\xC3\xA9");                              EXPECT_STREQ(b, "\"\xC3\xA9\"");
  botJsonEscape(b, sizeof(b), nullptr);                                 EXPECT_STREQ(b, "\"\"");
}
TEST(Escape, DoesNotFit) {
  char b[6];
  EXPECT_EQ(botJsonEscape(b, sizeof(b), "abcd"), 0u);                   // needs 6 + NUL
  EXPECT_EQ(botJsonEscape(b, sizeof(b), "abc"), 5u);
  EXPECT_EQ(botJsonEscape(b, sizeof(b), "a\"b"), 0u);
  EXPECT_EQ(botJsonEscape(b, 0, "a"), 0u);
}
TEST(Escape, RoundTripsThroughArduinoJson) {
  const char* src = "Dee \xF0\x9F\x8C\xB2: \"hi\"\\ <b>\t\x7F";
  char b[128];
  ASSERT_GT(botJsonEscape(b, sizeof(b), src), 0u);
  JsonDocument doc;
  ASSERT_FALSE(deserializeJson(doc, b));
  EXPECT_STREQ(doc.as<const char*>(), src);
}

// ---- setup with an existing password ----

static const char* parseSetupPw(const char* json, BotSetupData& d) {
  JsonDocument doc;
  if (deserializeJson(doc, json)) return "bad json in test";
  return botParseSetup(doc.as<JsonVariantConst>(), d, true);
}
TEST(Setup, ExistingPasswordMayBeKept) {
  BotSetupData d;
  EXPECT_EQ(parseSetupPw(R"({"ssid":"Home","name":"Bot"})", d), nullptr);
  EXPECT_STREQ(d.web_password, "");
  EXPECT_EQ(parseSetupPw(R"({"ssid":"Home","web_password":"","name":"Bot"})", d), nullptr);
  EXPECT_EQ(parseSetupPw(R"({"ssid":"Home","web_password":"newpass12","name":"Bot"})", d), nullptr);
  EXPECT_STREQ(d.web_password, "newpass12");
  EXPECT_NE(parseSetupPw(R"({"ssid":"Home","web_password":"short","name":"Bot"})", d), nullptr);
  BotSetupData e;
  EXPECT_NE(parseSetup(R"({"ssid":"Home","web_password":"","name":"Bot"})", e), nullptr);   // none yet: required
}

// ---- content type, routes ----

TEST(Request, JsonContentType) {
  EXPECT_TRUE(botIsJsonType("application/json"));
  EXPECT_TRUE(botIsJsonType("Application/JSON; charset=utf-8"));
  EXPECT_TRUE(botIsJsonType("application/json ;charset=utf-8"));
  EXPECT_FALSE(botIsJsonType("application/jsonx"));
  EXPECT_FALSE(botIsJsonType("application/x-www-form-urlencoded"));
  EXPECT_FALSE(botIsJsonType("text/plain"));
  EXPECT_FALSE(botIsJsonType(""));
  EXPECT_FALSE(botIsJsonType(nullptr));
}
TEST(Request, Routes) {
  EXPECT_EQ(botRouteFor("/"), BR_PAGE);
  EXPECT_EQ(botRouteFor("/api/state"), BR_STATE);
  EXPECT_EQ(botRouteFor("/api/result"), BR_RESULT);
  EXPECT_EQ(botRouteFor("/api/setup"), BR_SETUP);
  EXPECT_EQ(botRouteFor("/api/login"), BR_LOGIN);
  EXPECT_EQ(botRouteFor("/api/logout"), BR_LOGOUT);
  EXPECT_EQ(botRouteFor("/api/config"), BR_CONFIG);
  EXPECT_EQ(botRouteFor("/api/password"), BR_PASSWORD);
  EXPECT_EQ(botRouteFor("/api/advert"), BR_ADVERT);
  EXPECT_EQ(botRouteFor("/api/try"), BR_TRY);
  EXPECT_EQ(botRouteFor("/api/try/"), BR_UNKNOWN);
  EXPECT_EQ(botRouteFor("/api/config/"), BR_UNKNOWN);
  EXPECT_EQ(botRouteFor("/API/state"), BR_UNKNOWN);
  EXPECT_EQ(botRouteFor("/generate_204"), BR_UNKNOWN);
  EXPECT_EQ(botRouteFor(nullptr), BR_UNKNOWN);
  EXPECT_EQ(botRouteBodyCap(BR_LOGIN), 512u);
  EXPECT_EQ(botRouteBodyCap(BR_SETUP), 1024u);
  EXPECT_EQ(botRouteBodyCap(BR_CONFIG), 16384u);             // a full custom list with look-ups
  EXPECT_EQ(botRouteBodyCap(BR_PASSWORD), 8192u);
  EXPECT_EQ(botRouteBodyCap(BR_TRY), 200u);
  EXPECT_EQ(botRouteBodyCap(BR_TRY), (size_t)BOT_TRY_BODY_CAP);
  EXPECT_TRUE(botRouteNeedsSession(BR_TRY));
  EXPECT_TRUE(botRouteNeedsSession(BR_CONFIG));
  EXPECT_FALSE(botRouteNeedsSession(BR_STATE));
}

// ---- Host allowlist ----

TEST(HostAllow, Matches) {
  const char* sta = "192.168.1.20"; const char* ap = "192.168.4.1"; const char* md = "meshbot-ab12";
  EXPECT_TRUE(botHostAllowed("192.168.1.20", sta, ap, md));
  EXPECT_TRUE(botHostAllowed("192.168.1.20:80", sta, ap, md));
  EXPECT_TRUE(botHostAllowed("192.168.4.1", sta, ap, md));
  EXPECT_TRUE(botHostAllowed("meshbot-ab12.local", sta, ap, md));
  EXPECT_TRUE(botHostAllowed("MeshBot-AB12.LOCAL.", sta, ap, md));
  EXPECT_TRUE(botHostAllowed("meshbot-ab12.local:80", sta, ap, md));
  EXPECT_TRUE(botHostAllowed("meshbot-ab12.local.:8080", sta, ap, md));
}
TEST(HostAllow, Refuses) {
  const char* sta = "192.168.1.20"; const char* ap = "192.168.4.1"; const char* md = "meshbot-ab12";
  EXPECT_FALSE(botHostAllowed("evil.example", sta, ap, md));
  EXPECT_FALSE(botHostAllowed("meshbot-ab12", sta, ap, md));                 // .local required
  EXPECT_FALSE(botHostAllowed("meshbot-ab12.local.evil.example", sta, ap, md));
  EXPECT_FALSE(botHostAllowed("x.meshbot-ab12.local", sta, ap, md));
  EXPECT_FALSE(botHostAllowed("192.168.1.2", sta, ap, md));
  EXPECT_FALSE(botHostAllowed("192.168.1.200", sta, ap, md));
  EXPECT_FALSE(botHostAllowed("192.168.1.20:", sta, ap, md));
  EXPECT_FALSE(botHostAllowed("192.168.1.20:80x", sta, ap, md));
  EXPECT_FALSE(botHostAllowed("[::1]", sta, ap, md));
  EXPECT_FALSE(botHostAllowed("", sta, ap, md));
  EXPECT_FALSE(botHostAllowed(nullptr, sta, ap, md));
  EXPECT_FALSE(botHostAllowed("", "", "", ""));                               // empty entries never match
  EXPECT_FALSE(botHostAllowed(".local", "", "", ""));
}
TEST(HostAllow, MissingEntriesIgnored) {
  EXPECT_TRUE(botHostAllowed("192.168.4.1", nullptr, "192.168.4.1", nullptr));
  EXPECT_FALSE(botHostAllowed("192.168.1.20", nullptr, "192.168.4.1", nullptr));
}

// ---- the early request guard ----

static BotReqFacts post(BotRoute r, size_t len = 20) {
  BotReqFacts f;
  f.method = BOT_M_POST; f.route = r; f.length = len; f.chunked = false; f.multipart = false; f.has_content_type = true; f.json = true;
  f.host_ok = true; f.origin_ok = true; f.session = true; f.from_ap = false; f.mode = BWM_NORMAL;
  return f;
}
static BotReqFacts get(BotRoute r) {
  BotReqFacts f = post(r, 0);
  f.method = BOT_M_GET; f.has_content_type = false; f.json = false; f.origin_ok = false; f.session = false;
  return f;
}

TEST(Guard, NormalAccepts) {
  EXPECT_EQ(botGuard(get(BR_PAGE)), 0);
  EXPECT_EQ(botGuard(get(BR_STATE)), 0);
  BotReqFacts g = get(BR_RESULT); g.session = true; EXPECT_EQ(botGuard(g), 0);
  EXPECT_EQ(botGuard(post(BR_CONFIG, 16384)), 0);
  EXPECT_EQ(botGuard(post(BR_PASSWORD)), 0);
  EXPECT_EQ(botGuard(post(BR_ADVERT)), 0);
  EXPECT_EQ(botGuard(post(BR_LOGOUT)), 0);
  BotReqFacts l = post(BR_LOGIN, 512); l.session = false; EXPECT_EQ(botGuard(l), 0);
}
TEST(Guard, Method) {
  BotReqFacts f = get(BR_PAGE); f.method = BOT_M_OTHER; EXPECT_EQ(botGuard(f), 405);
  f = post(BR_CONFIG); f.method = BOT_M_OTHER; EXPECT_EQ(botGuard(f), 405);
}
TEST(Guard, Host) {
  BotReqFacts f = get(BR_PAGE); f.host_ok = false; EXPECT_EQ(botGuard(f), 421);
  f = post(BR_CONFIG); f.host_ok = false; EXPECT_EQ(botGuard(f), 421);
  f = get(BR_PAGE); f.host_ok = false; f.mode = BWM_SETUP; f.from_ap = true; EXPECT_EQ(botGuard(f), BOT_GUARD_REDIRECT);
  f = post(BR_SETUP); f.host_ok = false; f.mode = BWM_SETUP; f.from_ap = true; EXPECT_EQ(botGuard(f), 421);
  f = get(BR_PAGE); f.host_ok = false; f.mode = BWM_SETUP; f.from_ap = false; EXPECT_EQ(botGuard(f), 421);
}
TEST(Guard, GetRoutes) {
  EXPECT_EQ(botGuard(get(BR_RESULT)), 401);
  EXPECT_EQ(botGuard(get(BR_UNKNOWN)), 404);
  EXPECT_EQ(botGuard(get(BR_CONFIG)), 401);          // the settings: signed-in only
  EXPECT_EQ(botGuard(get(BR_TRY)), 401);
  EXPECT_EQ(botGuard(get(BR_PASSWORD)), 404);        // a POST route
  EXPECT_EQ(botGuard(get(BR_SETUP)), 404);
  BotReqFacts f = get(BR_PAGE); f.length = 5; EXPECT_EQ(botGuard(f), 400);
  f = get(BR_PAGE); f.chunked = true; EXPECT_EQ(botGuard(f), 411);
}
TEST(Guard, PostChecksInOrder) {
  BotReqFacts f = post(BR_CONFIG); f.origin_ok = false; f.json = false; f.session = false; EXPECT_EQ(botGuard(f), 403);
  f = post(BR_CONFIG); f.chunked = true; EXPECT_EQ(botGuard(f), 411);
  f = post(BR_CONFIG, 0); EXPECT_EQ(botGuard(f), 411);
  f = post(BR_CONFIG); f.json = false; f.session = false; EXPECT_EQ(botGuard(f), 415);
  f = post(BR_CONFIG, 16385); f.session = false; EXPECT_EQ(botGuard(f), 413);
  f = post(BR_PASSWORD, 8193); f.session = false; EXPECT_EQ(botGuard(f), 413);
  f = post(BR_CONFIG); f.session = false; EXPECT_EQ(botGuard(f), 401);
  // The largest settings body the route accepts, without a session, is still answered 401 by the
  // guard (and so never buffered): the size check passing must not let it reach the route.
  f = post(BR_CONFIG, BOT_CONFIG_BODY_CAP); f.session = false; EXPECT_EQ(BOT_CONFIG_BODY_CAP, 16384); EXPECT_EQ(botGuard(f), 401);
  f = post(BR_PASSWORD); f.session = false; EXPECT_EQ(botGuard(f), 401);
  f = post(BR_ADVERT); f.session = false; EXPECT_EQ(botGuard(f), 401);
  f = post(BR_LOGOUT); f.session = false; EXPECT_EQ(botGuard(f), 401);
  f = post(BR_LOGIN, 513); f.session = false; EXPECT_EQ(botGuard(f), 413);
}
TEST(Guard, PostRoutes) {
  EXPECT_EQ(botGuard(post(BR_UNKNOWN)), 404);
  EXPECT_EQ(botGuard(post(BR_STATE)), 404);
  EXPECT_EQ(botGuard(post(BR_PAGE)), 404);
  EXPECT_EQ(botGuard(post(BR_SETUP)), 404);          // not in setup mode
}
TEST(Guard, SetupMode) {
  BotReqFacts f = get(BR_PAGE); f.mode = BWM_SETUP; f.from_ap = true; EXPECT_EQ(botGuard(f), 0);
  f = get(BR_STATE); f.mode = BWM_SETUP; f.from_ap = true; EXPECT_EQ(botGuard(f), 0);
  f = get(BR_UNKNOWN); f.mode = BWM_SETUP; f.from_ap = true; EXPECT_EQ(botGuard(f), BOT_GUARD_REDIRECT);
  f = get(BR_RESULT); f.mode = BWM_SETUP; f.from_ap = true; f.session = true; EXPECT_EQ(botGuard(f), 404);
  f = get(BR_PAGE); f.mode = BWM_SETUP; f.from_ap = false; EXPECT_EQ(botGuard(f), 404);
  f = post(BR_SETUP, 1024); f.mode = BWM_SETUP; f.from_ap = true; f.session = false; EXPECT_EQ(botGuard(f), 0);
  f = post(BR_SETUP, 1025); f.mode = BWM_SETUP; f.from_ap = true; f.session = false; EXPECT_EQ(botGuard(f), 413);
  f = post(BR_SETUP); f.mode = BWM_SETUP; f.from_ap = true; f.origin_ok = false; EXPECT_EQ(botGuard(f), 403);
  f = post(BR_LOGIN); f.mode = BWM_SETUP; f.from_ap = true; EXPECT_EQ(botGuard(f), 404);
  f = post(BR_CONFIG); f.mode = BWM_SETUP; f.from_ap = true; EXPECT_EQ(botGuard(f), 404);
  f = post(BR_UNKNOWN); f.mode = BWM_SETUP; f.from_ap = true; EXPECT_EQ(botGuard(f), 404);
}
TEST(Guard, NeedsPasswordMode) {
  BotReqFacts f = get(BR_PAGE); f.mode = BWM_NEEDS_PASSWORD; EXPECT_EQ(botGuard(f), 0);
  f = get(BR_STATE); f.mode = BWM_NEEDS_PASSWORD; EXPECT_EQ(botGuard(f), 0);
  f = get(BR_RESULT); f.mode = BWM_NEEDS_PASSWORD; f.session = true; EXPECT_EQ(botGuard(f), 404);
  f = get(BR_UNKNOWN); f.mode = BWM_NEEDS_PASSWORD; EXPECT_EQ(botGuard(f), 404);
  BotRoute posts[] = { BR_SETUP, BR_LOGIN, BR_LOGOUT, BR_CONFIG, BR_PASSWORD, BR_ADVERT, BR_UNKNOWN };
  for (BotRoute r : posts) { f = post(r); f.mode = BWM_NEEDS_PASSWORD; EXPECT_EQ(botGuard(f), 404) << (int)r; }
}

// ---- the settings GET and the try routes ----

TEST(GuardTry, GetConfigAndTryNeedASession) {
  BotReqFacts f = get(BR_CONFIG); f.session = true; EXPECT_EQ(botGuard(f), 0);
  f = get(BR_TRY); f.session = true; EXPECT_EQ(botGuard(f), 0);
  f = get(BR_TRY); f.session = false; EXPECT_EQ(botGuard(f), 401);
  f = get(BR_TRY); f.session = true; f.length = 3; EXPECT_EQ(botGuard(f), 400);        // no body on a GET
  f = get(BR_TRY); f.session = true; f.has_content_type = true; EXPECT_EQ(botGuard(f), 400);
  f = get(BR_TRY); f.session = true; f.host_ok = false; EXPECT_EQ(botGuard(f), 421);
}
TEST(GuardTry, PostTryAcceptedUpToItsCap) {
  EXPECT_EQ(botGuard(post(BR_TRY, 1)), 0);
  EXPECT_EQ(botGuard(post(BR_TRY, 200)), 0);
  EXPECT_EQ(botGuard(post(BR_TRY, 201)), 413);
  EXPECT_EQ(botGuard(post(BR_TRY, 8192)), 413);
  EXPECT_EQ(botGuard(post(BR_TRY, (size_t)-1)), 413);
}
TEST(GuardTry, PostTryChecksInOrder) {
  BotReqFacts f = post(BR_TRY); f.origin_ok = false; f.session = false; EXPECT_EQ(botGuard(f), 403);
  f = post(BR_TRY); f.chunked = true; EXPECT_EQ(botGuard(f), 411);
  f = post(BR_TRY, 0); EXPECT_EQ(botGuard(f), 411);
  f = post(BR_TRY); f.json = false; f.session = false; EXPECT_EQ(botGuard(f), 415);
  f = post(BR_TRY); f.multipart = true; EXPECT_EQ(botGuard(f), 415);
  f = post(BR_TRY, 201); f.session = false; EXPECT_EQ(botGuard(f), 413);
  f = post(BR_TRY); f.session = false; EXPECT_EQ(botGuard(f), 401);
}
TEST(GuardTry, OnlyInNormalMode) {
  BotReqFacts f = post(BR_TRY); f.mode = BWM_SETUP; f.from_ap = true; EXPECT_EQ(botGuard(f), 404);
  f = get(BR_TRY); f.mode = BWM_SETUP; f.from_ap = true; f.session = true; EXPECT_EQ(botGuard(f), 404);
  f = get(BR_CONFIG); f.mode = BWM_SETUP; f.from_ap = true; f.session = true; EXPECT_EQ(botGuard(f), 404);
  f = post(BR_TRY); f.mode = BWM_NEEDS_PASSWORD; EXPECT_EQ(botGuard(f), 404);
  f = get(BR_TRY); f.mode = BWM_NEEDS_PASSWORD; f.session = true; EXPECT_EQ(botGuard(f), 404);
  f = get(BR_CONFIG); f.mode = BWM_NEEDS_PASSWORD; f.session = true; EXPECT_EQ(botGuard(f), 404);
}

// ---- the text of a try ----

TEST(TryText, Accepts) {
  EXPECT_TRUE(botTryTextOk("wx toronto"));
  EXPECT_TRUE(botTryTextOk("x"));
  EXPECT_TRUE(botTryTextOk(" "));                                     // the bot decides; 1 byte is enough
  EXPECT_TRUE(botTryTextOk(std::string(120, 'a').c_str()));
  std::string e; for (int i = 0; i < 60; i++) e += "\xC3\xA9";       // 60 x 2-byte UTF-8 = 120 bytes
  EXPECT_TRUE(botTryTextOk(e.c_str()));
  EXPECT_TRUE(botTryTextOk("ping \xF0\x9F\x98\x80 <b>\"q\""));
}
TEST(TryText, Refuses) {
  EXPECT_FALSE(botTryTextOk(nullptr));
  EXPECT_FALSE(botTryTextOk(""));
  EXPECT_FALSE(botTryTextOk(std::string(121, 'a').c_str()));
  std::string e; for (int i = 0; i < 61; i++) e += "\xC3\xA9";       // 122 bytes: counted in bytes
  EXPECT_FALSE(botTryTextOk(e.c_str()));
  EXPECT_FALSE(botTryTextOk("wx\ntoronto"));
  EXPECT_FALSE(botTryTextOk("wx\ttoronto"));
  EXPECT_FALSE(botTryTextOk("wx\rtoronto"));
  EXPECT_FALSE(botTryTextOk("wx\x7F"));
  EXPECT_FALSE(botTryTextOk("\x01"));
}
TEST(TryText, FromPostedJson) {
  // The handler reads "text" with botJsonStr, which refuses non-strings and an embedded NUL.
  JsonDocument d;
  ASSERT_FALSE(deserializeJson(d, R"({"text":"wx toronto"})"));
  EXPECT_TRUE(botTryTextOk(botJsonStr(d["text"])));
  ASSERT_FALSE(deserializeJson(d, R"({"text":"a\u0000b"})"));
  EXPECT_FALSE(botTryTextOk(botJsonStr(d["text"])));
  ASSERT_FALSE(deserializeJson(d, R"({"text":"a\u0007b"})"));
  EXPECT_FALSE(botTryTextOk(botJsonStr(d["text"])));
  ASSERT_FALSE(deserializeJson(d, R"({"text":5})"));
  EXPECT_FALSE(botTryTextOk(botJsonStr(d["text"])));
  ASSERT_FALSE(deserializeJson(d, R"({"txt":"ping"})"));
  EXPECT_FALSE(botTryTextOk(botJsonStr(d["text"])));
}

// ---- the settings document fits its buffer ----

// Fills a stored string with as many bytes as its buffer takes, every one a '"' (written as \"),
// keeping the rules the loaders apply to that field.
template <size_t N> static void fillQuotes(char (&dst)[N], const char* head = "", const char* tail = "") {
  memset(dst, 0, N);
  size_t h = strlen(head), t = strlen(tail);
  memcpy(dst, head, h);
  for (size_t i = h; i + t < N - 1; i++) dst[i] = '"';
  memcpy(dst + N - 1 - t, tail, t);
}
template <size_t N> static void fillChar(char (&dst)[N], char c) { memset(dst, 0, N); memset(dst, c, N - 1); }

static BotConfig g_worst;   // ~8 KB: never on the stack

static void makeWorstConfig(BotConfig& c) {
  botConfigDefaults(c);
  c.num_channels = BOT_MAX_CHANNELS;
  for (int i = 0; i < BOT_MAX_CHANNELS; i++) fillQuotes(c.channels[i], "#");
  fillQuotes(c.scope);
  c.prefix = '"';
  c.cooldown_s = 65535; c.max_per_min = 255;
  fillQuotes(c.ignore);
  c.lat = -0.000123456789012345; c.lon = -179.123456789012345;
  fillQuotes(c.tz, "", "1");                     // anything with a digit is taken as a POSIX string
  fillQuotes(c.help_link);
  c.advert_zero_hop_min = 65535; c.advert_flood_min = 65535;
  for (int i = 0; i < BB_COUNT; i++) { fillChar(c.builtin[i].name, 'a' + i); c.builtin[i].enabled = false; }
  c.num_custom = BOT_MAX_CUSTOM;
  for (int i = 0; i < BOT_MAX_CUSTOM; i++) {
    BotCustomCmd& u = c.custom[i];
    fillChar(u.trigger, 'q'); u.trigger[0] = (char)('a' + i);
    fillQuotes(u.reply); fillQuotes(u.desc);
    u.enabled = false;
    fillQuotes(u.url, "http://a?");
    fillChar(u.path, 'k');
    u.cache_s = 65535;
  }
  c.num_watch = BOT_MAX_WATCH;
  for (int i = 0; i < BOT_MAX_WATCH; i++) {
    fillQuotes(c.watch[i].label);
    c.watch[i].kind = BW_OBSERVER;
    fillChar(c.watch[i].id, 'F');
  }
  c.watch_online_min = 65535;
  c.mqtt_enabled = false;                        // "false" is the longest boolean; mqtt_pass_set is false too
  fillChar(c.mqtt_host, 'h');
  c.mqtt_port = 65535;
  fillQuotes(c.mqtt_user);
  fillChar(c.mqtt_prefix, 'p');
  c.mqtt_wait_s = 15;                            // two digits, the 3-byte worst case needs 255 (not valid)
}

TEST(StateSize, WorstSettingsFitTheBound) {
  makeWorstConfig(g_worst);
  JsonDocument doc;
  ASSERT_TRUE(botConfigToJsonChecked(g_worst, doc, false));
  size_t n = measureJson(doc);
  EXPECT_LE(n, BOT_CONFIG_JSON_MAX);
  // The bound follows the serializer exactly: it is not so loose that a new key could hide in
  // it. The worst real document still falls short of it by a known amount, added back here as
  // slack, for these reasons only:
  //  - the built-in ids are shorter than the 16 bytes counted for them;
  //  - the two coordinates are shorter than the 24 bytes counted for a double;
  //  - some bytes cannot be '"' and so are not escaped (each url's "http://a?", the '#' of each
  //    channel, the digit of the time zone), though the bound counts every byte as escaped;
  //  - the last element of each of the four lists (channels, custom, watch, and the built-in
  //    members) has no comma after it, though the bound counts one per element;
  //  - mqtt_wait_s is at most 15, two digits, where the bound counts three (any uint8_t).
  size_t slack = 0;
  for (int i = 0; i < BB_COUNT; i++) slack += 16 - strlen(botBuiltinId(i));
  slack += 2 * BOT_JN_DBL - measureJson(doc["lat"]) - measureJson(doc["lon"]);
  slack += BOT_MAX_CUSTOM * strlen("http://a?") + BOT_MAX_CHANNELS + 1 + 4;
  slack += 1;   // mqtt_wait_s 15 is two digits, the bound counts three
  EXPECT_EQ(n + slack, BOT_CONFIG_JSON_MAX) << n;
  // The page never receives the password salt or hash.
  std::string s; serializeJson(doc, s);
  EXPECT_EQ(s.find("web_salt"), std::string::npos);
  EXPECT_EQ(s.find("web_hash"), std::string::npos);
  EXPECT_EQ(s.find("\"mqtt_pass\""), std::string::npos);
}
TEST(StateSize, WorstCustomListPostFitsTheBodyCap) {
  // The page posts {"custom":[...]} with the same members; JSON.stringify escapes as ArduinoJson does.
  makeWorstConfig(g_worst);
  JsonDocument doc;
  ASSERT_TRUE(botConfigToJsonChecked(g_worst, doc, false));
  JsonDocument post;
  post["custom"] = doc["custom"];
  EXPECT_LE(measureJson(post), (size_t)BOT_CONFIG_BODY_CAP);
  EXPECT_GT(measureJson(post), (size_t)8192);   // why the cap is no longer 8 KB
}
TEST(StateSize, SettingsFitTheStateBuffer) {
  // The buffer is the larger of the settings document (with its NUL) and the state's fixed parts
  // plus the room kept for recent lines; never larger than that.
  EXPECT_EQ(botWebStateCap(0), BOT_CONFIG_JSON_MAX + 1);
  EXPECT_EQ(botWebStateCap(BOT_CONFIG_JSON_MAX + 1 - BOT_WEB_STATE_RECENT_MIN), BOT_CONFIG_JSON_MAX + 1);
  EXPECT_EQ(botWebStateCap(BOT_CONFIG_JSON_MAX + 2 - BOT_WEB_STATE_RECENT_MIN), BOT_CONFIG_JSON_MAX + 2);
  EXPECT_EQ(botWebStateCap(20000), 20000u + BOT_WEB_STATE_RECENT_MIN);
  const size_t edge = BOT_CONFIG_JSON_MAX + 1 - BOT_WEB_STATE_RECENT_MIN;   // where the state starts to decide
  for (size_t f : {(size_t)0, (size_t)5000, edge - 1, edge, edge + 1, (size_t)30000}) {
    EXPECT_GE(botWebStateCap(f), BOT_CONFIG_JSON_MAX + 1) << f;
    EXPECT_GE(botWebStateCap(f), f + BOT_WEB_STATE_RECENT_MIN) << f;
  }
}
TEST(StateSize, EmptyListsAreSmall) {
  BotConfig& c = g_worst;
  botConfigDefaults(c);
  JsonDocument doc;
  ASSERT_TRUE(botConfigToJsonChecked(c, doc, false));
  EXPECT_LT(measureJson(doc), 2048u);
  std::string s; serializeJson(doc, s);
  EXPECT_NE(s.find("\"custom\":[]"), std::string::npos);
  EXPECT_NE(s.find("\"watch\":[]"), std::string::npos);
}

// ---- round 2: the guard decides from the values the server's parser uses ----

// The server takes its body length from Content-Length or X-Expected-Entity-Length (the last
// one wins) and the guard is given that value, whatever header carried it.
TEST(GuardBody, GetWithAnyLengthRefused) {
  BotReqFacts f = get(BR_PAGE); f.length = 100000000; EXPECT_EQ(botGuard(f), 400);   // X-Expected-Entity-Length only
  f = get(BR_STATE); f.length = 1; EXPECT_EQ(botGuard(f), 400);
  f = get(BR_STATE); f.length = (size_t)-1; EXPECT_EQ(botGuard(f), 400);              // atoi("-1")
  f = get(BR_STATE); f.length = 0; EXPECT_EQ(botGuard(f), 0);
}
TEST(GuardBody, GetWithBodyTypeRefused) {
  BotReqFacts f = get(BR_STATE); f.multipart = true; EXPECT_EQ(botGuard(f), 400);
  f = get(BR_STATE); f.has_content_type = true; EXPECT_EQ(botGuard(f), 400);          // urlencoded, text/plain, ...
  f = get(BR_PAGE); f.has_content_type = true; f.json = true; EXPECT_EQ(botGuard(f), 400);
  f = get(BR_UNKNOWN); f.mode = BWM_SETUP; f.from_ap = true; f.multipart = true; EXPECT_EQ(botGuard(f), 400);
}
TEST(GuardBody, GetChunkedRefused) {
  BotReqFacts f = get(BR_STATE); f.chunked = true; EXPECT_EQ(botGuard(f), 411);
}
TEST(GuardBody, PostMultipartFlagWinsOverJsonType) {
  // "Content-Type: multipart/..." then "Content-Type: application/json": the server keeps
  // the multipart flag from the first and the type string from the second.
  BotReqFacts f = post(BR_CONFIG); f.multipart = true; EXPECT_EQ(botGuard(f), 415);
  f = post(BR_LOGIN); f.session = false; f.multipart = true; EXPECT_EQ(botGuard(f), 415);
  f = post(BR_SETUP); f.mode = BWM_SETUP; f.from_ap = true; f.multipart = true; EXPECT_EQ(botGuard(f), 415);
}
TEST(GuardBody, PostLengthFromParser) {
  BotReqFacts f = post(BR_CONFIG, (size_t)-1); EXPECT_EQ(botGuard(f), 413);             // atoi("-1")
  f = post(BR_LOGIN, 100000000); f.session = false; EXPECT_EQ(botGuard(f), 413);
  f = post(BR_CONFIG, 0); EXPECT_EQ(botGuard(f), 411);
  f = post(BR_CONFIG); f.chunked = true; EXPECT_EQ(botGuard(f), 411);
  f = post(BR_CONFIG); f.has_content_type = false; f.json = false; EXPECT_EQ(botGuard(f), 415);
}

// ---- access point idle timeout ----

TEST(ApIdle, Basic) {
  EXPECT_FALSE(botApIdleExpired(1000, 1000, 900000));
  EXPECT_FALSE(botApIdleExpired(900999, 1000, 900000));
  EXPECT_TRUE(botApIdleExpired(901000, 1000, 900000));
}
TEST(ApIdle, LastSlightlyAheadOfNow) {
  // A request stamped on the other task after the loop read millis().
  EXPECT_FALSE(botApIdleExpired(1000, 1001, 900000));
  EXPECT_FALSE(botApIdleExpired(1000, 1500, 900000));
  EXPECT_FALSE(botApIdleExpired(0xFFFFFFFFu, 0x00000002u, 900000));   // ahead, across the wrap
}
TEST(ApIdle, AcrossTheWrap) {
  EXPECT_FALSE(botApIdleExpired(0x00000100u, 0xFFFFFF00u, 900000));
  EXPECT_TRUE(botApIdleExpired(0xFFFFFF00u + 900000u, 0xFFFFFF00u, 900000));
}

// ---- setup: keep the saved WiFi password ----

TEST(SetupWifi, KeepsSavedPasswordForSameNetwork) {
  EXPECT_TRUE(botSetupKeepsWifiPassword("Home", "", "Home"));
  EXPECT_FALSE(botSetupKeepsWifiPassword("Home", "newpass12", "Home"));
  EXPECT_FALSE(botSetupKeepsWifiPassword("Cafe", "", "Home"));          // new network, empty = open
  EXPECT_FALSE(botSetupKeepsWifiPassword("home", "", "Home"));          // SSIDs are case-sensitive
  EXPECT_FALSE(botSetupKeepsWifiPassword("Home", "", ""));              // nothing saved
  EXPECT_FALSE(botSetupKeepsWifiPassword("Home", "", nullptr));
}

TEST(SetupWifi, StoredPasswordReusedOnlyAfterProof) {
  // Empty field, same network, a password stored: kept only for a caller who proved the web password.
  EXPECT_EQ(botSetupWifiPasswordRule("Home", "", "Home", true, true), BOT_WIFI_KEEP_STORED);
  EXPECT_EQ(botSetupWifiPasswordRule("Home", "", "Home", true, false), BOT_WIFI_REFUSE);
  // Nothing stored for it, or another network, or a password typed: the field is used as given.
  EXPECT_EQ(botSetupWifiPasswordRule("Home", "", "Home", false, false), BOT_WIFI_USE_NEW);
  EXPECT_EQ(botSetupWifiPasswordRule("Cafe", "", "Home", true, false), BOT_WIFI_USE_NEW);
  EXPECT_EQ(botSetupWifiPasswordRule("Home", "newpass12", "Home", true, false), BOT_WIFI_USE_NEW);
  EXPECT_EQ(botSetupWifiPasswordRule("Home", "", "", false, false), BOT_WIFI_USE_NEW);
  EXPECT_EQ(botSetupWifiPasswordRule("Home", "", nullptr, true, true), BOT_WIFI_USE_NEW);
}

// ---- the "mqtt" member of the state ----

static BotWebMqttView worstMqtt(std::string& det, std::string& p64, std::string names[], std::string& desc) {
  BotWebMqttView v;
  memset(&v, 0, sizeof v);
  v.state = BMS_WAIT_WIFI;                                   // the longest state word
  det.assign(BOT_MQTT_DETAIL_CAP - 1, '<');                  // a byte botJsonEscape writes as 6
  p64.assign(BOT_MQTT_PREFIX_MAX, 'p');
  v.detail = det.c_str(); v.prefix = p64.c_str(); v.default_prefix = p64.c_str();
  v.connects = v.published = v.received = v.dropped = v.answered = v.timed_out = v.capped = 0xFFFFFFFFu;
  for (int i = 0; i < BOT_MQTT_CMD_DESC_MAX / 2; i++) desc += "\xC3\xA9";   // stored text (botMqttCleanDesc) needs no escaping
  v.count = BOT_MQTT_CMD_SLOTS;
  for (int i = 0; i < BOT_MQTT_CMD_SLOTS; i++) {
    names[i].assign(BOT_MQTT_CMD_NAME_MAX, 'a' + i);
    v.names[i] = names[i].c_str(); v.descs[i] = desc.c_str(); v.hidden[i] = BAH_BUILTIN;
  }
  return v;
}

TEST(MqttState, WorstCaseIsTheBound) {
  std::string det, p64, names[BOT_MQTT_CMD_SLOTS], desc;
  BotWebMqttView v = worstMqtt(det, p64, names, desc);
  std::vector<char> b(BOT_WEB_MQTT_JSON_MAX + 1);
  size_t n = botWebMqttJson(b.data(), b.size(), v);
  ASSERT_GT(n, 0u);
  EXPECT_EQ(n, strlen(b.data()));
  EXPECT_EQ(n + 1, BOT_WEB_MQTT_JSON_MAX);                   // the bound less the last command's comma
  EXPECT_EQ(botWebMqttJson(b.data(), BOT_WEB_MQTT_JSON_MAX - 1, v), 0u);   // one byte short: nothing
  EXPECT_STREQ(b.data(), "");
  // It is valid JSON with every member.
  std::string doc = "{" + std::string(botWebMqttJson(b.data(), b.size(), v) ? b.data() : "") + "}";
  JsonDocument d;
  ASSERT_FALSE(deserializeJson(d, doc));
  JsonObjectConst m = d["mqtt"];
  EXPECT_STREQ(m["state"], "waiting for WiFi");
  EXPECT_EQ(std::string(m["detail"].as<const char*>()), det);
  EXPECT_EQ(m["capped"].as<uint32_t>(), 0xFFFFFFFFu);
  EXPECT_EQ(m["commands"].size(), (size_t)BOT_MQTT_CMD_SLOTS);
  EXPECT_EQ(std::string(m["commands"][15]["desc"].as<const char*>()), desc);
  EXPECT_EQ(doc.find('<'), std::string::npos);               // written as <
  // Every word written unescaped fits the bound's word sizes.
  for (uint8_t st = BMS_OFF; st <= BMS_ERROR + 1; st++) {
    const char* w = botMqttStateWord(st);
    char e[64];
    EXPECT_EQ(botJsonEscape(e, sizeof e, w), strlen(w) + 2) << w;
    EXPECT_LE(strlen(w) + 1, BOT_WEB_MQTT_STATE_WORD_CAP) << w;
  }
  for (uint8_t h = BAH_NONE; h <= BAH_CUSTOM + 1; h++) EXPECT_LE(strlen(botAskHiddenWord(h)) + 1, BOT_WEB_MQTT_HIDDEN_WORD_CAP);
}

TEST(MqttState, SitsInTheStateBuffer) {
  // The state's other fixed parts are bounded in BotWeb.cpp (static_assert); this part alone must
  // leave most of the buffer for them and the recent lines.
  EXPECT_LT(BOT_WEB_MQTT_JSON_MAX, botWebStateCap(0) / 2);
  // A description is counted at one byte a byte: 48 of the stored 48-byte description's bytes,
  // the quotes, and nothing for escaping.
  EXPECT_EQ(BOT_WEB_MQTT_CMD_JSON_MAX, (sizeof("{\"name\":,\"desc\":,\"hidden\":}") - 1) + (BOT_MQTT_CMD_NAME_MAX + 2) +
                                           (BOT_MQTT_CMD_DESC_MAX + 2) + (BOT_WEB_MQTT_HIDDEN_WORD_CAP + 1) + 1);
}

TEST(MqttState, TypicalAndHostile) {
  BotWebMqttView v;
  memset(&v, 0, sizeof v);
  v.state = BMS_REFUSED; v.detail = "login refused"; v.prefix = "meshcore/MeshBot"; v.default_prefix = "meshcore/NorthBot";
  v.connects = 2; v.published = 7; v.received = 9; v.dropped = 1; v.answered = 5; v.timed_out = 2; v.capped = 0;
  v.count = 4;
  char clean[BOT_MQTT_CMD_DESC_MAX + 1];   // as the announced-command table stores it
  botMqttCleanDesc(clean, sizeof clean, "tide <img src=x onerror=alert(1)> \"q\" \\");
  v.names[0] = "tide";  v.descs[0] = clean; v.hidden[0] = BAH_NONE;
  v.names[1] = "ping";  v.descs[1] = "shadowed"; v.hidden[1] = BAH_BUILTIN;
  v.names[2] = "<img>"; v.descs[2] = "bad name: left out"; v.hidden[2] = BAH_NONE;
  v.names[3] = "joke";  v.descs[3] = nullptr; v.hidden[3] = BAH_CUSTOM;
  char b[1024];
  size_t n = botWebMqttJson(b, sizeof b, v);
  ASSERT_GT(n, 0u);
  EXPECT_STREQ(b, "\"mqtt\":{\"state\":\"login refused\",\"detail\":\"login refused\",\"prefix\":\"meshcore/MeshBot\","
                  "\"default_prefix\":\"meshcore/NorthBot\",\"connects\":2,\"published\":7,\"received\":9,\"dropped\":1,"
                  "\"answered\":5,\"timed_out\":2,\"capped\":0,\"commands\":["
                  "{\"name\":\"tide\",\"desc\":\"tide (img src=x onerror=alert(1)> 'q' /\",\"hidden\":\"\"},"
                  "{\"name\":\"ping\",\"desc\":\"shadowed\",\"hidden\":\"builtin\"},"
                  "{\"name\":\"joke\",\"desc\":\"\",\"hidden\":\"custom\"}]}");
  // Out-of-contract input is written empty, never longer than the bound allows: a description
  // that was not stored through botMqttCleanDesc (it would need escaping) is left out.
  for (const char* raw : {"a<b", "say \"hi\"", "back\\slash", "tab\there", "\x01"}) {
    v.count = 1; v.names[0] = "tide"; v.descs[0] = raw;
    ASSERT_GT(botWebMqttJson(b, sizeof b, v), 0u);
    EXPECT_NE(std::string(b).find("{\"name\":\"tide\",\"desc\":\"\",\"hidden\":\"\"}"), std::string::npos) << raw;
  }
  v.count = 0;
  std::string longdet(BOT_MQTT_DETAIL_CAP, 'x'), badp = "a//b";
  v.detail = longdet.c_str(); v.prefix = badp.c_str(); v.default_prefix = nullptr; v.state = 200;
  botWebMqttJson(b, sizeof b, v);
  JsonDocument d;
  ASSERT_FALSE(deserializeJson(d, std::string("{") + b + "}"));
  EXPECT_STREQ(d["mqtt"]["detail"], ""); EXPECT_STREQ(d["mqtt"]["prefix"], ""); EXPECT_STREQ(d["mqtt"]["default_prefix"], "");
  EXPECT_STREQ(d["mqtt"]["state"], "error");
  EXPECT_EQ(d["mqtt"]["commands"].size(), 0u);
  // Never a key that could carry the login.
  std::string t = b;
  for (const char* k : {"pass", "user", "host"}) EXPECT_EQ(t.find(k), std::string::npos) << k;
  EXPECT_EQ(botWebMqttJson(b, 0, v), 0u);
  EXPECT_EQ(botWebMqttJson(nullptr, 10, v), 0u);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
