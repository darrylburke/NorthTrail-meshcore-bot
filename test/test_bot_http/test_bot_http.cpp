// Host tests for BotHttp.h (URL split, request text, incremental header parser), BotJsonAlloc.h
// (bounded allocator) and botJsonPathFilter (BotJsonPath.h).
#include <gtest/gtest.h>
#include <cstring>
#include <string>
#include <ArduinoJson.h>
#include "helpers/bot/BotHttp.h"
#include "helpers/bot/BotJsonAlloc.h"
#include "helpers/bot/BotJsonPath.h"

// ---- URL ----

TEST(HttpUrl, Splits) {
  BotHttpUrl u;
  ASSERT_TRUE(botHttpParseUrl("https://api.open-meteo.com/v1/forecast?latitude=43.6&x=1", u));
  EXPECT_TRUE(u.https); EXPECT_STREQ(u.host, "api.open-meteo.com"); EXPECT_EQ(u.port, 443);
  EXPECT_STREQ(u.path, "/v1/forecast?latitude=43.6&x=1");
  ASSERT_TRUE(botHttpParseUrl("http://h.example:8080/?q=a%20b", u));
  EXPECT_FALSE(u.https); EXPECT_STREQ(u.host, "h.example"); EXPECT_EQ(u.port, 8080); EXPECT_STREQ(u.path, "/?q=a%20b");
  ASSERT_TRUE(botHttpParseUrl("http://h/", u)); EXPECT_EQ(u.port, 80); EXPECT_STREQ(u.path, "/");
  ASSERT_TRUE(botHttpParseUrl("http://h:65535/", u)); EXPECT_EQ(u.port, 65535);
}

TEST(HttpUrl, Refuses) {
  BotHttpUrl u;
  EXPECT_FALSE(botHttpParseUrl(nullptr, u));
  EXPECT_FALSE(botHttpParseUrl("ftp://h/", u));
  EXPECT_FALSE(botHttpParseUrl("https://h?q=1", u));        // not normalised: no '/'
  EXPECT_FALSE(botHttpParseUrl("https:///p", u));
  EXPECT_FALSE(botHttpParseUrl("https://user@h/", u));
  EXPECT_FALSE(botHttpParseUrl("https://h_x/", u));
  EXPECT_FALSE(botHttpParseUrl("http://h:0/", u));
  EXPECT_FALSE(botHttpParseUrl("http://h:65536/", u));
  EXPECT_FALSE(botHttpParseUrl("http://h:123456/", u));
  EXPECT_FALSE(botHttpParseUrl("http://h:/", u));
  EXPECT_FALSE(botHttpParseUrl("http://h/a b", u));
  EXPECT_FALSE(botHttpParseUrl("http://h/a\r\nX: y", u));    // no header injection
  EXPECT_FALSE(botHttpParseUrl("http://h/\x80", u));
  EXPECT_FALSE(botHttpParseUrl(("http://" + std::string(101, 'a') + "/").c_str(), u));
  EXPECT_TRUE(botHttpParseUrl(("http://" + std::string(100, 'a') + "/").c_str(), u));
  EXPECT_TRUE(botHttpParseUrl(("http://h/" + std::string(318, 'p')).c_str(), u));
  EXPECT_FALSE(botHttpParseUrl(("http://h/" + std::string(319, 'p')).c_str(), u));
}

TEST(HttpUrl, Request) {
  BotHttpUrl u;
  char b[512];
  ASSERT_TRUE(botHttpParseUrl("https://h.example/p?q=1", u));
  ASSERT_GT(botHttpRequest(b, sizeof b, u), 0u);
  EXPECT_STREQ(b, "GET /p?q=1 HTTP/1.0\r\nHost: h.example\r\nUser-Agent: meshcore-bot\r\nAccept: application/json\r\n"
                  "Accept-Encoding: identity\r\nConnection: close\r\n\r\n");
  ASSERT_TRUE(botHttpParseUrl("http://h:8080/", u));
  botHttpRequest(b, sizeof b, u);
  EXPECT_NE(strstr(b, "\r\nHost: h:8080\r\n"), nullptr);
  ASSERT_TRUE(botHttpParseUrl("https://h:443/", u));
  botHttpRequest(b, sizeof b, u);
  EXPECT_NE(strstr(b, "\r\nHost: h\r\n"), nullptr);
  EXPECT_EQ(botHttpRequest(b, 20, u), 0u); EXPECT_STREQ(b, "");
}

// ---- header parser ----

static uint8_t feedAll(BotHttpHead& h, const std::string& s, size_t* used = nullptr) {
  uint8_t st = BHS_MORE;
  size_t i = 0;
  for (; i < s.size() && st == BHS_MORE; i++) st = h.feed(s[i]);
  if (used) *used = i;
  return st;
}

TEST(HttpHead, Normal) {
  BotHttpHead h(16384);
  std::string r = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 12\r\n\r\n{\"a\":1}";
  size_t used = 0;
  EXPECT_EQ(feedAll(h, r, &used), BHS_DONE);
  EXPECT_EQ(h.status(), 200); EXPECT_EQ(h.contentLength(), 12);
  EXPECT_EQ(r.substr(used), "{\"a\":1}");   // stops exactly at the body
  EXPECT_EQ(h.feed('x'), BHS_DONE);         // a final state sticks
}

TEST(HttpHead, NoLengthAndZeroLength) {
  BotHttpHead h(16384);
  EXPECT_EQ(feedAll(h, "HTTP/1.0 200 OK\r\nServer: x\r\n\r\n"), BHS_DONE);
  EXPECT_EQ(h.contentLength(), -1);
  h.reset(16384);
  EXPECT_EQ(feedAll(h, "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n"), BHS_DONE);
  EXPECT_EQ(h.contentLength(), 0);
  h.reset(16384);
  EXPECT_EQ(feedAll(h, "HTTP/1.0 200 OK\r\n\r\n"), BHS_DONE);   // only the status line
  EXPECT_EQ(h.contentLength(), -1);
}

TEST(HttpHead, StatusOtherThan200) {
  BotHttpHead h(16384);
  EXPECT_EQ(feedAll(h, "HTTP/1.1 404 Not Found\r\nContent-Length: 9\r\n\r\nnot found"), BHS_STATUS);
  EXPECT_EQ(h.status(), 404);
  h.reset(16384);
  EXPECT_EQ(feedAll(h, "HTTP/1.1 302 Found\r\nLocation: http://evil.example/\r\n\r\n"), BHS_STATUS);   // never followed
  EXPECT_EQ(h.status(), 302);
  h.reset(16384);
  EXPECT_EQ(feedAll(h, "HTTP/1.1 200\r\n\r\n"), BHS_DONE);   // no reason phrase
}

TEST(HttpHead, ChunkedAndEncoding) {
  BotHttpHead h(16384);
  EXPECT_EQ(feedAll(h, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n0\r\n\r\n"), BHS_CHUNKED);
  h.reset(16384);
  EXPECT_EQ(feedAll(h, "HTTP/1.1 200 OK\r\ntransfer-encoding: Identity\r\n\r\n"), BHS_DONE);
  h.reset(16384);
  EXPECT_EQ(feedAll(h, "HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\n\r\n"), BHS_ENCODING);
  h.reset(16384);
  EXPECT_EQ(feedAll(h, "HTTP/1.1 200 OK\r\nContent-Encoding: identity\r\n\r\n"), BHS_DONE);
}

TEST(HttpHead, LengthChecks) {
  BotHttpHead h(16384);
  EXPECT_EQ(feedAll(h, "HTTP/1.1 200 OK\r\nContent-Length: 16385\r\n\r\n"), BHS_LENGTH);
  h.reset(16384);
  EXPECT_EQ(feedAll(h, "HTTP/1.1 200 OK\r\nContent-Length: 16384\r\n\r\n"), BHS_DONE);
  h.reset(16384);
  EXPECT_EQ(feedAll(h, "HTTP/1.1 200 OK\r\nContent-Length: 12a\r\n\r\n"), BHS_BAD);
  h.reset(16384);
  EXPECT_EQ(feedAll(h, "HTTP/1.1 200 OK\r\nContent-Length: 9999999999\r\n\r\n"), BHS_BAD);
  h.reset(16384);
  EXPECT_EQ(feedAll(h, "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nContent-Length: 6\r\n\r\n"), BHS_BAD);
  h.reset(16384);
  EXPECT_EQ(feedAll(h, "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nContent-Length: 5\r\n\r\n"), BHS_DONE);
}

TEST(HttpHead, HeaderSizeLimits) {
  BotHttpHead h(16384);
  std::string status = "HTTP/1.1 200 OK\r\n";
  // Exactly 2048 bytes of headers including the final blank line: accepted.
  std::string fill;
  size_t room = BOT_HTTP_HEAD_MAX - status.size() - 2;   // minus the final "\r\n"
  while (fill.size() + 100 <= room) fill += "X-Pad: " + std::string(91, 'p') + "\r\n";   // 100 bytes a line
  size_t left = room - fill.size();
  ASSERT_GE(left, 9u);
  fill += "X: " + std::string(left - 5, 'q') + "\r\n";
  ASSERT_EQ(status.size() + fill.size() + 2, (size_t)BOT_HTTP_HEAD_MAX);
  EXPECT_EQ(feedAll(h, status + fill + "\r\n"), BHS_DONE);
  // One byte more: refused while reading.
  h.reset(16384);
  std::string more = "X: " + std::string(left - 4, 'q') + "\r\n";
  std::string fill2 = fill.substr(0, fill.size() - (left)) + more;
  ASSERT_EQ(status.size() + fill2.size() + 2, (size_t)BOT_HTTP_HEAD_MAX + 1);
  EXPECT_EQ(feedAll(h, status + fill2 + "\r\n"), BHS_TOO_BIG);
  // An endless header never grows anything: refused once over the limit.
  h.reset(16384);
  std::string endless = status;
  for (int i = 0; i < 100; i++) endless += "X-A: b\r\n";
  size_t used = 0;
  EXPECT_EQ(feedAll(h, endless + std::string(5000, 'z'), &used), BHS_TOO_BIG);
  EXPECT_LE(used, (size_t)BOT_HTTP_HEAD_MAX + 1);
}

TEST(HttpHead, LineLimit) {
  BotHttpHead h(16384);
  EXPECT_EQ(feedAll(h, "HTTP/1.1 200 OK\r\nX-Long: " + std::string(292, 'a') + "\r\n\r\n"), BHS_TOO_BIG);   // 300-byte line
  h.reset(16384);
  EXPECT_EQ(feedAll(h, "HTTP/1.1 200 OK\r\nX: " + std::string(253, 'a') + "\r\n\r\n"), BHS_DONE);   // 256 bytes: allowed
  h.reset(16384);
  EXPECT_EQ(feedAll(h, "HTTP/1.1 200 OK\r\nX: " + std::string(254, 'a') + "\r\n\r\n"), BHS_TOO_BIG);   // 257
  h.reset(16384);
  EXPECT_EQ(feedAll(h, "HTTP/1.1 200 OK\nX: " + std::string(254, 'a') + "\n\n"), BHS_TOO_BIG);         // bare \n too
}

TEST(HttpHead, SplitFeedsAndBareNewlines) {
  BotHttpHead h(16384);
  EXPECT_EQ(feedAll(h, "HTT"), BHS_MORE);
  EXPECT_EQ(feedAll(h, "P/1.1 2"), BHS_MORE);
  EXPECT_EQ(feedAll(h, "00 OK\r"), BHS_MORE);
  EXPECT_EQ(feedAll(h, "\nContent-Len"), BHS_MORE);
  EXPECT_EQ(feedAll(h, "gth: 3\r\n\r"), BHS_MORE);
  EXPECT_EQ(feedAll(h, "\n"), BHS_DONE);
  EXPECT_EQ(h.contentLength(), 3);
  h.reset(16384);
  EXPECT_EQ(feedAll(h, "HTTP/1.0 200 OK\nContent-Length: 2\n\n"), BHS_DONE);
  EXPECT_EQ(h.contentLength(), 2);
}

TEST(HttpHead, Garbage) {
  BotHttpHead h(16384);
  EXPECT_EQ(feedAll(h, "<html>hello</html>\r\n"), BHS_BAD);
  h.reset(16384);
  EXPECT_EQ(feedAll(h, "HTTP/2 200 OK\r\n\r\n"), BHS_BAD);
  h.reset(16384);
  EXPECT_EQ(feedAll(h, "HTTP/1.1 2x0 OK\r\n\r\n"), BHS_BAD);
  h.reset(16384);
  EXPECT_EQ(feedAll(h, "HTTP/1.1 2000 OK\r\n\r\n"), BHS_BAD);
  h.reset(16384);
  EXPECT_EQ(feedAll(h, std::string("HTTP/1.1 200 OK\r\nX: a\0b\r\n\r\n", 26)), BHS_BAD);
  h.reset(16384);
  EXPECT_EQ(feedAll(h, "HTTP/1.1 200 OK\r\nno colon here\r\n\r\n"), BHS_BAD);
  h.reset(16384);
  EXPECT_EQ(feedAll(h, "HTTP/1.1 200 OK\r\n folded: x\r\n\r\n"), BHS_BAD);
  h.reset(16384);
  EXPECT_EQ(feedAll(h, std::string(5000, 'g')), BHS_TOO_BIG);   // no newline at all
}

// ---- bounded allocator ----

TEST(JsonAlloc, RefusesBeyondLimit) {
  std::string big = "[";
  while (big.size() < 16380) big += "0,";
  big += "0]";
  BotJsonBoundedAllocator a(BOT_JSON_HEAP_LIMIT);
  {
    JsonDocument d(&a);
    DeserializationError e = deserializeJson(d, big.c_str(), big.size(), DeserializationOption::NestingLimit(12));
    EXPECT_TRUE(e == DeserializationError::NoMemory || d.overflowed()) << e.c_str();
    EXPECT_LE(a.peak(), (size_t)BOT_JSON_HEAP_LIMIT);
  }
  EXPECT_EQ(a.used(), 0u);   // everything given back
}

TEST(JsonAlloc, NormalResponseParses) {
  const char* body =
      "{\"current\":{\"temperature_2m\":6.1,\"apparent_temperature\":4.0,\"relative_humidity_2m\":79,"
      "\"weather_code\":0,\"wind_speed_10m\":3.2,\"wind_direction_10m\":350},\"daily\":{\"temperature_2m_max\":[15.0],"
      "\"temperature_2m_min\":[4.0],\"precipitation_probability_max\":[0]}}";
  BotJsonBoundedAllocator a;
  JsonDocument d(&a);
  ASSERT_EQ(deserializeJson(d, body, DeserializationOption::NestingLimit(12)), DeserializationError::Ok);
  EXPECT_FALSE(d.overflowed());
  EXPECT_EQ(d["current"]["relative_humidity_2m"].as<int>(), 79);
  EXPECT_GT(a.used(), 0u);
}

TEST(JsonAlloc, ReallocateCountsTheDifference) {
  BotJsonBoundedAllocator a(100);
  void* p = a.allocate(40);
  ASSERT_NE(p, nullptr); EXPECT_EQ(a.used(), 40u);
  EXPECT_EQ(a.allocate(61), nullptr);
  p = a.reallocate(p, 90); ASSERT_NE(p, nullptr); EXPECT_EQ(a.used(), 90u);
  EXPECT_EQ(a.reallocate(p, 101), nullptr); EXPECT_EQ(a.used(), 90u);   // the old block is kept
  p = a.reallocate(p, 10); EXPECT_EQ(a.used(), 10u);
  a.deallocate(p); EXPECT_EQ(a.used(), 0u);
  a.deallocate(nullptr);
  EXPECT_EQ(a.peak(), 90u);
}

// ---- filter from a JSON path ----

TEST(PathFilter, KeepsOnlyTheBranch) {
  JsonDocument f;
  ASSERT_TRUE(botJsonPathFilter("data.items[2].level", f));
  std::string body = "{\"noise\":[";
  for (int i = 0; i < 400; i++) body += (i ? ",\"" : "\"") + std::string(16, 'n') + std::to_string(1000 + i) + "\"";   // distinct
  body += "],\"data\":{\"other\":{\"deep\":[1,2,3,4,5,6,7,8,9]},\"items\":[{\"level\":1,\"x\":\"a\"},{\"level\":2},"
          "{\"level\":4.25,\"junk\":\"" + std::string(500, 'j') + "\"}]}}";
  BotJsonBoundedAllocator a;
  JsonDocument d(&a);
  ASSERT_EQ(deserializeJson(d, body, DeserializationOption::Filter(f), DeserializationOption::NestingLimit(12)),
            DeserializationError::Ok);
  EXPECT_TRUE(d["noise"].isNull());
  EXPECT_TRUE(d["data"]["other"].isNull());
  EXPECT_TRUE(d["data"]["items"][2]["junk"].isNull());
  char v[64];
  ASSERT_TRUE(botJsonPathGet(d.as<JsonVariantConst>(), "data.items[2].level", v, sizeof v));
  EXPECT_STREQ(v, "4.25");
  // The same body without the filter needs several times the memory (the pool's first block is
  // the floor for both).
  BotJsonBoundedAllocator b(1u << 20);
  {
    JsonDocument full(&b);
    ASSERT_EQ(deserializeJson(full, body), DeserializationError::Ok);
  }
  EXPECT_LT(a.peak() * 3, b.peak()) << a.peak() << " vs " << b.peak();
}

TEST(PathFilter, ShapesAndBadPaths) {
  JsonDocument f;
  ASSERT_TRUE(botJsonPathFilter("a", f));
  EXPECT_TRUE(f["a"].as<bool>());
  ASSERT_TRUE(botJsonPathFilter("[0][1].b", f));
  EXPECT_TRUE(f[0][0]["b"].as<bool>());
  ASSERT_TRUE(botJsonPathFilter("results[0].name", f));
  EXPECT_TRUE(f["results"][0]["name"].as<bool>());
  std::string deep;
  for (int i = 0; i < BOT_JSON_PATH_STEPS; i++) deep += i ? ".a" : "a";   // the deepest path allowed
  ASSERT_TRUE(botJsonPathFilter(deep.c_str(), f));
  deep += ".a";                                                         // one step more: refused
  EXPECT_FALSE(botJsonPathFilter(deep.c_str(), f)); EXPECT_TRUE(f.isNull());
  EXPECT_FALSE(botJsonPathFilter("a..b", f)); EXPECT_TRUE(f.isNull());
  EXPECT_FALSE(botJsonPathFilter("", f));
  EXPECT_FALSE(botJsonPathFilter("a[x]", f));
  EXPECT_FALSE(botJsonPathFilter(nullptr, f));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
