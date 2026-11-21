// Host tests for BotWx.h, BotSpace.h, BotAlerts.h, BotWatch.h and BotTime.h.
#include <gtest/gtest.h>
#include <cstdio>
#include <string>
#include <ArduinoJson.h>
#include "helpers/bot/BotWx.h"
#include "helpers/bot/BotSpace.h"
#include "helpers/bot/BotAlerts.h"
#include "helpers/bot/BotWatch.h"

static const char* WX = "{\"utc_offset_seconds\":-18000,\"current\":{\"time\":\"2026-11-20T20:30\",\"temperature_2m\":6.1,\"apparent_temperature\":3.9,\"relative_humidity_2m\":79,\"weather_code\":0,\"wind_speed_10m\":3.3,\"wind_direction_10m\":359},\"daily\":{\"temperature_2m_max\":[15.2],\"temperature_2m_min\":[4.3],\"precipitation_probability_max\":[0]}}";
TEST(Wx, ParseAndFormat)   { JsonDocument d; deserializeJson(d,WX); BotWxNow w; ASSERT_TRUE(botWxParse(d,w)); char b[128]; botWxFormat(b,128,"Toronto",w,100); EXPECT_STREQ(b,"Toronto: 6\xC2\xB0" "C feels 4, clear, wind N 3 km/h, 79% RH | today 4/15\xC2\xB0" "C, 0% precip"); }
TEST(Wx, DropsSegmentsToFit){ JsonDocument d; deserializeJson(d,WX); BotWxNow w; botWxParse(d,w); char b[128]; botWxFormat(b,128,"Toronto",w,55); EXPECT_STREQ(b,"Toronto: 6\xC2\xB0" "C feels 4, clear, wind N 3 km/h, 79% RH"); botWxFormat(b,128,"Toronto",w,30); EXPECT_STREQ(b,"Toronto: 6\xC2\xB0" "C feels 4, clear"); }
TEST(Wx, MissingFieldsFail){ JsonDocument d; deserializeJson(d,"{\"current\":{\"temperature_2m\":\"warm\"}}"); BotWxNow w; EXPECT_FALSE(botWxParse(d,w)); deserializeJson(d,"[1,2]"); EXPECT_FALSE(botWxParse(d,w)); }
TEST(Wx, Urls)             { char b[320]; ASSERT_TRUE(botWxUrl(b,320,43.6534,-79.3841)); EXPECT_NE(strstr(b,"latitude=43.6534&longitude=-79.3841"),nullptr); EXPECT_EQ(strncmp(b,"https://api.open-meteo.com/v1/forecast?",39),0); ASSERT_TRUE(botGeoUrl(b,320,"St. John's & x")); EXPECT_NE(strstr(b,"name=St.%20John%27s%20%26%20x&"),nullptr); }
TEST(Wx, Geo)              { JsonDocument d; deserializeJson(d,"{\"results\":[{\"name\":\"Kingston\",\"latitude\":44.2312,\"longitude\":-76.4860,\"admin1\":\"Ontario\"}]}"); BotPlace p; ASSERT_TRUE(botGeoParse(d,p)); EXPECT_STREQ(p.name,"Kingston"); EXPECT_NEAR(p.lat,44.2312,1e-5); deserializeJson(d,"{\"generationtime_ms\":0.5}"); EXPECT_FALSE(botGeoParse(d,p)); }
TEST(Wx, WmoAndWind)       { char s0[16]; EXPECT_STREQ(botWmoText(73,s0,sizeof s0),"snow"); EXPECT_STREQ(botWmoText(96,s0,sizeof s0),"thunderstorm, hail"); char sc[16]; EXPECT_STREQ(botWmoText(12,sc,sizeof sc),"code 12"); EXPECT_STREQ(botWind8(359),"N"); EXPECT_STREQ(botWind8(225),"SW"); }
static const char* KP = "[{\"time_tag\":\"2026-11-20T18:00:00\",\"kp\":1.0,\"observed\":\"observed\"},{\"time_tag\":\"2026-11-20T21:00:00\",\"kp\":2.33,\"observed\":\"estimated\"},{\"time_tag\":\"2026-11-21T00:00:00\",\"kp\":4.33,\"observed\":\"predicted\"},{\"time_tag\":\"2026-11-22T03:00:00\",\"kp\":6.0,\"observed\":\"predicted\"}]";
TEST(Kp, ParseNowAndPeak)  { JsonDocument d; deserializeJson(d,KP); BotKp k; uint32_t now; ASSERT_TRUE(botIsoToUnix("2026-11-20T22:10:00",now)); ASSERT_TRUE(botKpParse(d,now,k)); EXPECT_NEAR(k.now,2.33,0.01); ASSERT_TRUE(k.has_max24); EXPECT_NEAR(k.max24,4.33,0.01); }
TEST(Kp, NoClock)          { JsonDocument d; deserializeJson(d,KP); BotKp k; ASSERT_TRUE(botKpParse(d,0,k)); EXPECT_NEAR(k.now,2.33,0.01); EXPECT_FALSE(k.has_max24); }
TEST(Kp, Wording)          { EXPECT_STREQ(botKpWord(2.9f),"quiet"); EXPECT_STREQ(botKpWord(5.0f),"minor storm"); EXPECT_STREQ(botAuroraChance(2.3f,44.0),"unlikely"); EXPECT_STREQ(botAuroraChance(5.0f,44.0),"possible"); EXPECT_STREQ(botAuroraChance(6.0f,44.0),"likely"); EXPECT_STREQ(botAuroraChance(3.0f,51.0),"likely"); }
TEST(Kp, Format)           { BotKp k{2.33f,4.33f,true}; char b[128]; botAuroraFormat(b,128,k,44.0,100); EXPECT_STREQ(b,"Kp 2.3 (quiet). Aurora unlikely here. Next 24h peak Kp 4.3"); botAuroraFormat(b,128,k,0.0,100); EXPECT_STREQ(b,"Kp 2.3 (quiet). Set a location for a local outlook. Next 24h peak Kp 4.3"); }
static const char* AL = "{\"type\":\"FeatureCollection\",\"features\":[{\"properties\":{\"alert_name_en\":\"snowfall warning\",\"alert_short_name_en\":\"Snowfall warning\",\"status_en\":\"issued\",\"event_end_datetime\":\"2026-11-21T19:00:00.000Z\",\"feature_name_en\":\"City of Toronto\"}},{\"properties\":{\"alert_name_en\":\"special weather statement\",\"alert_short_name_en\":\"Special weather\",\"status_en\":\"ended\",\"feature_name_en\":\"City of Toronto\"}}]}";
TEST(Alerts, ActiveOnly)   { JsonDocument d; deserializeJson(d,AL); BotAlert a[3]; uint32_t now; botIsoToUnix("2026-11-20T22:00:00",now); EXPECT_EQ(botAlertsParse(d,now,a,3, nullptr),1); EXPECT_STREQ(a[0].name,"Snowfall warning"); char b[128]; botAlertsFormat(b,128,a,1,1,-18000,100); EXPECT_STREQ(b,"1 alert: Snowfall warning (City of Toronto) until Sat 14:00"); }
TEST(Alerts, NoneAndBadDoc){ JsonDocument d; deserializeJson(d,"{\"type\":\"FeatureCollection\",\"features\":[]}"); BotAlert a[3]; EXPECT_EQ(botAlertsParse(d,0,a,3, nullptr),0); char b[64]; botAlertsFormat(b,64,a,0,0,0,60); EXPECT_STREQ(b,"No active alerts here"); deserializeJson(d,"{\"code\":\"InvalidParameterValue\"}"); EXPECT_EQ(botAlertsParse(d,0,a,3, nullptr),-1); }
TEST(Alerts, Url)          { char b[400]; ASSERT_TRUE(botAlertsUrl(b,400,43.6534,-79.3841)); EXPECT_NE(strstr(b,"bbox=-79.4341,43.6034,-79.3341,43.7034"),nullptr); EXPECT_NE(strstr(b,"skipGeometry=true"),nullptr); EXPECT_NE(strstr(b,"&limit=10&"),nullptr); EXPECT_NE(strstr(b,"&properties=alert_short_name_en,alert_name_en,status_en,event_end_datetime,expiration_datetime,feature_name_en&"),nullptr); EXPECT_LE(strlen(b),319u); }
TEST(Watch, ObserverAndNode){ JsonDocument d; BotWatchState s; deserializeJson(d,"{\"status\":\"online\",\"lastSeen\":1795225173598}"); ASSERT_TRUE(botWatchParse(d,BW_OBSERVER,1795225200,43200,s)); EXPECT_TRUE(s.online); deserializeJson(d,"{\"items\":[{\"name\":\"Hilltop\",\"lastSeen\":1795052400000}]}"); ASSERT_TRUE(botWatchParse(d,BW_NODE,1795225200,43200,s)); EXPECT_FALSE(s.online); char b[64]; botWatchFormatOne(b,64,"Hilltop",s,1795225200); EXPECT_STREQ(b,"Hilltop: offline 2d"); deserializeJson(d,"{\"items\":[]}"); ASSERT_TRUE(botWatchParse(d,BW_NODE,1795225200,43200,s)); EXPECT_FALSE(s.known); botWatchFormatOne(b,64,"Hilltop",s,1795225200); EXPECT_STREQ(b,"Hilltop: unknown"); }
TEST(Iso, Parse)           { uint32_t t; ASSERT_TRUE(botIsoToUnix("1970-01-02T00:00:00",t)); EXPECT_EQ(t,86400u); ASSERT_TRUE(botIsoToUnix("2026-11-21T08:11:27.047Z",t)); EXPECT_FALSE(botIsoToUnix("yesterday",t)); EXPECT_FALSE(botIsoToUnix("2026-13-40T99:00:00",t)); }

// ---------------------------------------------------------------------------------------------
// Additional tests
// ---------------------------------------------------------------------------------------------
#define DEG "\xC2\xB0"

static uint32_t iso(const char* s) { uint32_t t = 0; EXPECT_TRUE(botIsoToUnix(s, t)) << s; return t; }

static std::string wxText(const char* json, const char* place = "Toronto", size_t max = 100) {
  JsonDocument d; deserializeJson(d, json); BotWxNow w; if (!botWxParse(d, w)) return "<parse failed>";
  char b[160]; botWxFormat(b, sizeof b, place, w, max); return b;
}
static std::string wxCur(const char* cur, const char* daily = "") {
  std::string j = std::string("{\"current\":{") + cur + "}" + (*daily ? std::string(",\"daily\":{") + daily + "}" : "") + "}";
  return wxText(j.c_str());
}

// ---- time ----
TEST(Time, WeekdayCheckSaturday) {
  uint32_t t = iso("2026-11-21T19:00:00Z");
  EXPECT_EQ(t, 1795287600u);   // 20,778 days * 86400 + 19 h, computed by hand
  char b[16]; EXPECT_EQ(botWeekdayHm(t, -18000, b, sizeof b), 9u); EXPECT_STREQ(b, "Sat 14:00");
  botWeekdayHm(t, 0, b, sizeof b); EXPECT_STREQ(b, "Sat 19:00");
  botWeekdayHm(t, 5 * 3600, b, sizeof b); EXPECT_STREQ(b, "Sun 00:00");
  botWeekdayHm(0, 0, b, sizeof b); EXPECT_STREQ(b, "Thu 00:00");
  botWeekdayHm(0, -3600, b, sizeof b); EXPECT_STREQ(b, "Wed 23:00");
  botWeekdayHm(86400 * 3 + 59, 0, b, sizeof b); EXPECT_STREQ(b, "Sun 00:00");
}
TEST(Time, WeekdayCapAndNull) {
  char b[4] = "zzz"; size_t r = botWeekdayHm(0, 0, b, 4); EXPECT_EQ(r, strlen(b)); EXPECT_LE(r, 3u);
  EXPECT_EQ(botWeekdayHm(0, 0, b, 0), 0u); EXPECT_EQ(botWeekdayHm(0, 0, nullptr, 8), 0u);
}
TEST(Time, IsoBoundsAndLeap) {
  uint32_t t = 7;
  EXPECT_TRUE(botIsoToUnix("2024-02-29T00:00:00", t)); EXPECT_EQ(t, 1709164800u);
  EXPECT_FALSE(botIsoToUnix("2026-02-29T00:00:00", t));
  EXPECT_FALSE(botIsoToUnix("2100-02-29T00:00:00", t));
  EXPECT_TRUE(botIsoToUnix("2000-02-29T00:00:00", t));
  EXPECT_FALSE(botIsoToUnix("2026-04-31T00:00:00", t));
  EXPECT_FALSE(botIsoToUnix("1969-12-31T23:59:59", t));
  EXPECT_TRUE(botIsoToUnix("2105-12-31T23:59:59", t)); EXPECT_FALSE(botIsoToUnix("2106-12-31T23:59:59", t));
  t = 7;
  const char* bad[] = {nullptr, "", "2026-11-21", "2026-11-21T08:11", "2026-11-21 08:11:27", "2026-11-21T08:11:27+01:00",
                       "2026-11-21T08:11:27.Z", "2026-11-21T08:11:27Zx", "2026-11-21T24:00:00", "2026-11-21T08:60:00",
                       "2026-11-21T08:11:60", "2026-00-21T08:11:27", "2026-11-00T08:11:27", "20x6-11-21T08:11:27",
                       "2026-11-21T08:11:2"};
  for (const char* s : bad) { EXPECT_FALSE(botIsoToUnix(s, t)) << (s ? s : "(null)"); }
  EXPECT_EQ(t, 7u);   // failures leave the output alone
}

// ---- weather ----
TEST(Wx, WmoGroups) {
  struct { int c; const char* s; } k[] = {{0,"clear"},{1,"mostly clear"},{2,"partly cloudy"},{3,"overcast"},{45,"fog"},{48,"fog"},
    {51,"drizzle"},{53,"drizzle"},{55,"drizzle"},{56,"freezing drizzle"},{57,"freezing drizzle"},{61,"light rain"},{63,"rain"},
    {65,"heavy rain"},{66,"freezing rain"},{67,"freezing rain"},{71,"light snow"},{73,"snow"},{75,"heavy snow"},{77,"snow grains"},
    {80,"showers"},{81,"showers"},{82,"heavy showers"},{85,"snow showers"},{86,"snow showers"},{95,"thunderstorm"},
    {96,"thunderstorm, hail"},{99,"thunderstorm, hail"},{4,"code 4"},{-1,"code -1"},{100,"code 100"},{97,"code 97"}};
  for (auto& e : k) { char sc[16]; EXPECT_STREQ(botWmoText(e.c, sc, sizeof sc), e.s) << e.c; }
  EXPECT_STREQ(botWmoText(12, nullptr, 0), "code"); char t3[4]; botWmoText(12345, t3, sizeof t3); EXPECT_EQ(strlen(t3), 3u);
}
TEST(Wx, Wind8AllPointsAndWrap) {
  const char* n[8] = {"N","NE","E","SE","S","SW","W","NW"};
  for (int i = 0; i < 8; i++) { EXPECT_STREQ(botWind8(i * 45), n[i]); EXPECT_STREQ(botWind8(i * 45 + 22), n[i]); EXPECT_STREQ(botWind8(i * 45 - 22), n[i]); }
  EXPECT_STREQ(botWind8(23), "NE"); EXPECT_STREQ(botWind8(-1), "N"); EXPECT_STREQ(botWind8(360), "N"); EXPECT_STREQ(botWind8(-45), "NW");
  EXPECT_STREQ(botWind8(2147483647), botWind8(2147483647 % 360));
}
TEST(Wx, FeelsOmittedWhenEqualAndRounding) {
  EXPECT_EQ(wxCur("\"temperature_2m\":6.1,\"apparent_temperature\":5.6,\"weather_code\":3,\"wind_speed_10m\":10"),
            "Toronto: 6" DEG "C, overcast, wind 10 km/h");   // feels 5.6 -> 6 == 6
  EXPECT_EQ(wxCur("\"temperature_2m\":6.1,\"weather_code\":3,\"wind_speed_10m\":10"), "Toronto: 6" DEG "C, overcast, wind 10 km/h");
  EXPECT_EQ(wxCur("\"temperature_2m\":6.5,\"apparent_temperature\":3.4,\"weather_code\":3,\"wind_speed_10m\":10"),
            "Toronto: 7" DEG "C feels 3, overcast, wind 10 km/h");
}
TEST(Wx, NegativeAndNegativeZero) {
  EXPECT_EQ(wxCur("\"temperature_2m\":-0.4,\"apparent_temperature\":-0.2,\"weather_code\":0,\"wind_speed_10m\":10"), "Toronto: 0" DEG "C, clear, wind 10 km/h");
  EXPECT_EQ(wxCur("\"temperature_2m\":-12.5,\"apparent_temperature\":-19.6,\"weather_code\":73,\"wind_speed_10m\":10"),
            "Toronto: -13" DEG "C feels -20, snow, wind 10 km/h");
  EXPECT_EQ(wxCur("\"temperature_2m\":1.2,\"apparent_temperature\":-0.4,\"weather_code\":0,\"wind_speed_10m\":10"), "Toronto: 1" DEG "C feels 0, clear, wind 10 km/h");
  EXPECT_EQ(wxCur("\"temperature_2m\":1,\"weather_code\":0,\"wind_speed_10m\":10,\"relative_humidity_2m\":50",
                  "\"temperature_2m_max\":[-0.4],\"temperature_2m_min\":[-8.6],\"precipitation_probability_max\":[40]"),
            "Toronto: 1" DEG "C, clear, wind 10 km/h, 50% RH | today -9/0" DEG "C, 40% precip");
}
TEST(Wx, CalmAndWindRounding) {
  EXPECT_EQ(wxCur("\"temperature_2m\":1,\"weather_code\":0,\"wind_speed_10m\":0.9,\"wind_direction_10m\":90"), "Toronto: 1" DEG "C, clear, calm");
  EXPECT_EQ(wxCur("\"temperature_2m\":1,\"weather_code\":0,\"wind_speed_10m\":0"), "Toronto: 1" DEG "C, clear, calm");
  EXPECT_EQ(wxCur("\"temperature_2m\":1,\"weather_code\":0,\"wind_speed_10m\":1.0,\"wind_direction_10m\":90"), "Toronto: 1" DEG "C, clear, wind E 1 km/h");
  EXPECT_EQ(wxCur("\"temperature_2m\":1,\"weather_code\":0,\"wind_speed_10m\":12.5,\"wind_direction_10m\":225"), "Toronto: 1" DEG "C, clear, wind SW 13 km/h");
}
TEST(Wx, OptionalFieldsDefault) {
  JsonDocument d; deserializeJson(d, "{\"current\":{\"temperature_2m\":6.1,\"weather_code\":2,\"wind_speed_10m\":3.3}}");
  BotWxNow w; ASSERT_TRUE(botWxParse(d, w));
  EXPECT_FLOAT_EQ(w.feels, w.temp); EXPECT_EQ(w.rh, -1); EXPECT_EQ(w.wind_deg, -1); EXPECT_FALSE(w.has_daily);
  char b[128]; botWxFormat(b, 128, nullptr, w, 100); EXPECT_STREQ(b, "Here: 6" DEG "C, partly cloudy, wind 3 km/h");
  botWxFormat(b, 128, "", w, 100); EXPECT_STREQ(b, "Here: 6" DEG "C, partly cloudy, wind 3 km/h");
  deserializeJson(d, "{\"current\":{\"temperature_2m\":6.1,\"weather_code\":2,\"wind_speed_10m\":3.3,\"apparent_temperature\":\"x\",\"relative_humidity_2m\":\"80\",\"wind_direction_10m\":null}}");
  ASSERT_TRUE(botWxParse(d, w)); EXPECT_FLOAT_EQ(w.feels, w.temp); EXPECT_EQ(w.rh, -1); EXPECT_EQ(w.wind_deg, -1);
}
TEST(Wx, HasDailyNeedsAllThree) {
  JsonDocument d; BotWxNow w;
  const char* cur = "\"current\":{\"temperature_2m\":6,\"weather_code\":0,\"wind_speed_10m\":3}";
  const char* days[] = {"{\"temperature_2m_max\":[15],\"temperature_2m_min\":[4]}", "{\"temperature_2m_max\":[15],\"precipitation_probability_max\":[0]}",
    "{\"temperature_2m_min\":[4],\"precipitation_probability_max\":[0]}", "{\"temperature_2m_max\":[],\"temperature_2m_min\":[4],\"precipitation_probability_max\":[0]}",
    "{\"temperature_2m_max\":[15],\"temperature_2m_min\":[4],\"precipitation_probability_max\":[null]}", "{\"temperature_2m_max\":[\"15\"],\"temperature_2m_min\":[4],\"precipitation_probability_max\":[0]}",
    "[1]", "7"};
  for (const char* dd : days) { std::string j = std::string("{") + cur + ",\"daily\":" + dd + "}"; deserializeJson(d, j.c_str()); ASSERT_TRUE(botWxParse(d, w)) << dd; EXPECT_FALSE(w.has_daily) << dd; }
  deserializeJson(d, WX); ASSERT_TRUE(botWxParse(d, w)); EXPECT_TRUE(w.has_daily); EXPECT_FLOAT_EQ(w.tmax, 15.2f); EXPECT_FLOAT_EQ(w.tmin, 4.3f); EXPECT_EQ(w.precip_pct, 0);
  EXPECT_EQ(w.rh, 79); EXPECT_EQ(w.wind_deg, 359); EXPECT_EQ(w.code, 0);
}
TEST(Wx, RhOmittedWhenUnknownAndDailyOptional) {
  EXPECT_EQ(wxCur("\"temperature_2m\":1,\"weather_code\":0,\"wind_speed_10m\":5,\"wind_direction_10m\":0",
                  "\"temperature_2m_max\":[3],\"temperature_2m_min\":[-2],\"precipitation_probability_max\":[70]"),
            "Toronto: 1" DEG "C, clear, wind N 5 km/h | today -2/3" DEG "C, 70% precip");
}
TEST(Wx, EveryDropStep) {
  JsonDocument d; deserializeJson(d, WX); BotWxNow w; ASSERT_TRUE(botWxParse(d, w));
  const char* full = "Toronto: 6" DEG "C feels 4, clear, wind N 3 km/h, 79% RH | today 4/15" DEG "C, 0% precip";
  const char* s3 = "Toronto: 6" DEG "C feels 4, clear, wind N 3 km/h, 79% RH";
  const char* s2 = "Toronto: 6" DEG "C feels 4, clear, wind N 3 km/h";
  const char* s1 = "Toronto: 6" DEG "C feels 4, clear";
  char b[128];
  size_t L[4] = {strlen(full), strlen(s3), strlen(s2), strlen(s1)}; const char* S[4] = {full, s3, s2, s1};
  for (int i = 0; i < 4; i++) {
    EXPECT_EQ(botWxFormat(b, 128, "Toronto", w, L[i]), L[i]); EXPECT_STREQ(b, S[i]);
    if (i < 3) { EXPECT_EQ(botWxFormat(b, 128, "Toronto", w, L[i] - 1), L[i + 1]); EXPECT_STREQ(b, S[i + 1]); }
  }
}
TEST(Wx, FirstSegmentCutAndBounds) {
  JsonDocument d; deserializeJson(d, WX); BotWxNow w; ASSERT_TRUE(botWxParse(d, w));
  char b[128];
  size_t n = botWxFormat(b, 128, "Toronto", w, 14);
  EXPECT_EQ(n, strlen(b)); EXPECT_LE(n, 14u); EXPECT_EQ(strncmp(b, "Toronto: 6", 10), 0);
  // every limit and cap: never past min(max, cap-1), always NUL-terminated, valid UTF-8 cut
  for (size_t cap = 1; cap < 100; cap += 3) for (size_t mx = 0; mx < 100; mx += 2) {
    char c[100]; memset(c, 'Q', sizeof c);
    size_t r = botWxFormat(c, cap, "Toronto", w, mx);
    size_t lim = mx < cap - 1 ? mx : cap - 1;
    ASSERT_LE(r, lim); ASSERT_EQ(c[r], '\0'); ASSERT_EQ(strlen(c), r);
    for (size_t i = cap; i < sizeof c; i++) ASSERT_EQ(c[i], 'Q');
  }
  char z[2] = {'x', 'y'}; EXPECT_EQ(botWxFormat(z, 0, "T", w, 50), 0u); EXPECT_EQ(z[0], 'x');
  EXPECT_EQ(botWxFormat(nullptr, 10, "T", w, 50), 0u);
}
TEST(Wx, FirstSegmentCutOnUtf8Boundary) {
  JsonDocument d; deserializeJson(d, WX); BotWxNow w; botWxParse(d, w);
  char b[128];
  for (size_t mx = 0; mx < 20; mx++) {
    botWxFormat(b, 128, "\xC3\xA9t\xC3\xA9 \xC3\xA9t\xC3\xA9 \xF0\x9F\x8C\xA7", w, mx);
    size_t n = strlen(b); ASSERT_LE(n, mx);
    for (size_t i = 0; i < n; ) { unsigned char c = (unsigned char)b[i]; size_t l = c < 0x80 ? 1 : c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : 2; ASSERT_FALSE(botIsUtf8Cont(b[i])); ASSERT_LE(i + l, n); i += l; }
  }
}
TEST(Wx, HostileDocuments) {
  const char* bad[] = {"null", "[]", "{}", "7", "\"s\"", "{\"current\":[]}", "{\"current\":null}", "{\"current\":{}}", "{\"current\":5}",
    "{\"current\":{\"temperature_2m\":\"6\",\"weather_code\":0,\"wind_speed_10m\":3}}", "{\"current\":{\"temperature_2m\":6,\"weather_code\":\"0\",\"wind_speed_10m\":3}}",
    "{\"current\":{\"temperature_2m\":6,\"weather_code\":0,\"wind_speed_10m\":null}}", "{\"current\":{\"temperature_2m\":6,\"weather_code\":0}}",
    "{\"current\":{\"temperature_2m\":true,\"weather_code\":0,\"wind_speed_10m\":3}}", "{\"current\":{\"temperature_2m\":[6],\"weather_code\":0,\"wind_speed_10m\":3}}"};
  BotWxNow w;
  for (const char* j : bad) { JsonDocument d; deserializeJson(d, j); EXPECT_FALSE(botWxParse(d, w)) << j; }
  EXPECT_FALSE(botWxParse(JsonVariantConst(), w));
  // out-of-range required values fail; out-of-range optional ones are treated as missing
  struct { const char* cur; bool ok; } r[] = {
    {"\"temperature_2m\":1e300,\"weather_code\":0,\"wind_speed_10m\":3", false},
    {"\"temperature_2m\":-1e300,\"weather_code\":0,\"wind_speed_10m\":3", false},
    {"\"temperature_2m\":70.1,\"weather_code\":0,\"wind_speed_10m\":3", false},
    {"\"temperature_2m\":-100.1,\"weather_code\":0,\"wind_speed_10m\":3", false},
    {"\"temperature_2m\":70,\"weather_code\":0,\"wind_speed_10m\":3", true},
    {"\"temperature_2m\":-100,\"weather_code\":999,\"wind_speed_10m\":500", true},
    {"\"temperature_2m\":6,\"weather_code\":1e30,\"wind_speed_10m\":3", false},
    {"\"temperature_2m\":6,\"weather_code\":1000,\"wind_speed_10m\":3", false},
    {"\"temperature_2m\":6,\"weather_code\":-1,\"wind_speed_10m\":3", false},
    {"\"temperature_2m\":6,\"weather_code\":0,\"wind_speed_10m\":-5", false},
    {"\"temperature_2m\":6,\"weather_code\":0,\"wind_speed_10m\":500.1", false},
    {"\"temperature_2m\":6,\"weather_code\":0,\"wind_speed_10m\":1e300", false}};
  for (auto& e : r) { std::string j = std::string("{\"current\":{") + e.cur + "}}"; JsonDocument d; deserializeJson(d, j.c_str()); EXPECT_EQ(botWxParse(d, w), e.ok) << e.cur; }
  JsonDocument d; deserializeJson(d, "{\"current\":{\"temperature_2m\":6,\"weather_code\":0,\"wind_speed_10m\":3,\"apparent_temperature\":1e300,\"relative_humidity_2m\":-3,\"wind_direction_10m\":-1e30},\"daily\":{\"temperature_2m_max\":[1e300],\"temperature_2m_min\":[1],\"precipitation_probability_max\":[5]}}");
  ASSERT_TRUE(botWxParse(d, w)); EXPECT_FLOAT_EQ(w.feels, 6); EXPECT_EQ(w.rh, -1); EXPECT_EQ(w.wind_deg, -1); EXPECT_FALSE(w.has_daily);
  deserializeJson(d, "{\"current\":{\"temperature_2m\":6,\"weather_code\":0,\"wind_speed_10m\":3,\"relative_humidity_2m\":100.4,\"wind_direction_10m\":360},\"daily\":{\"temperature_2m_max\":[1],\"temperature_2m_min\":[-1],\"precipitation_probability_max\":[101]}}");
  ASSERT_TRUE(botWxParse(d, w)); EXPECT_EQ(w.rh, -1); EXPECT_EQ(w.wind_deg, 0); EXPECT_FALSE(w.has_daily);
  deserializeJson(d, "{\"current\":{\"temperature_2m\":6,\"weather_code\":0,\"wind_speed_10m\":3,\"relative_humidity_2m\":99.6}}"); ASSERT_TRUE(botWxParse(d, w)); EXPECT_EQ(w.rh, 100);
}
TEST(Wx, GeoHostile) {
  const char* bad[] = {"null", "[]", "{}", "{\"results\":[]}", "{\"results\":{}}", "{\"results\":[1]}", "{\"results\":[{}]}", "{\"results\":[null]}",
    "{\"results\":[{\"name\":\"X\",\"latitude\":\"45\",\"longitude\":-79}]}", "{\"results\":[{\"name\":5,\"latitude\":45,\"longitude\":-79}]}",
    "{\"results\":[{\"name\":\"\",\"latitude\":45,\"longitude\":-79}]}", "{\"results\":[{\"name\":\"X\",\"latitude\":95,\"longitude\":-79}]}",
    "{\"results\":[{\"name\":\"X\",\"latitude\":45}]}"};
  BotPlace p;
  for (const char* j : bad) { JsonDocument d; deserializeJson(d, j); EXPECT_FALSE(botGeoParse(d, p)) << j; }
  EXPECT_FALSE(botGeoParse(JsonVariantConst(), p));
}
TEST(Wx, GeoNameTrimmed) {
  const char* j[] = {"{\"results\":[{\"name\":\"\\n\\t \",\"latitude\":1,\"longitude\":2}]}", "{\"results\":[{\"name\":\"   \",\"latitude\":1,\"longitude\":2}]}"};
  BotPlace p; for (const char* x : j) { JsonDocument d; deserializeJson(d, x); EXPECT_FALSE(botGeoParse(d, p)) << x; }
  JsonDocument d; deserializeJson(d, "{\"results\":[{\"name\":\"\\n St. John's \\t\",\"latitude\":1,\"longitude\":2}]}"); ASSERT_TRUE(botGeoParse(d, p)); EXPECT_STREQ(p.name, "St. John's");
}
TEST(Wx, GeoNameCleanedAndCut) {
  std::string j = "{\"results\":[{\"name\":\"A\\nB\\tC" + std::string(60, 'x') + "\",\"latitude\":1,\"longitude\":2}]}";
  JsonDocument d; deserializeJson(d, j.c_str()); BotPlace p; ASSERT_TRUE(botGeoParse(d, p));
  EXPECT_EQ(strlen(p.name), 39u); EXPECT_EQ(strncmp(p.name, "A B C", 5), 0);
  std::string k = "{\"results\":[{\"name\":\"" + std::string(38, 'a') + "\xC3\xA9\",\"latitude\":1,\"longitude\":2}]}";
  deserializeJson(d, k.c_str()); ASSERT_TRUE(botGeoParse(d, p)); EXPECT_EQ(strlen(p.name), 38u);   // 2-byte char would not fit whole
}
TEST(Wx, UrlRulesAndBounds) {
  char b[400];
  ASSERT_TRUE(botWxUrl(b, 400, 0.0, 0.0)); EXPECT_NE(strstr(b, "latitude=0.0000&longitude=0.0000&"), nullptr);
  ASSERT_TRUE(botWxUrl(b, 400, -0.00001, 5.5)); EXPECT_NE(strstr(b, "latitude=0.0000&longitude=5.5000&"), nullptr);
  ASSERT_TRUE(botWxUrl(b, 400, 43.65344999, -79.38415)); EXPECT_NE(strstr(b, "latitude=43.6534&longitude=-79.3842&"), nullptr);
  EXPECT_NE(strstr(b, "&timezone=auto&forecast_days=1"), nullptr);
  EXPECT_FALSE(botWxUrl(b, 400, 91, 0)); EXPECT_FALSE(botWxUrl(b, 400, 0, 181)); EXPECT_FALSE(botWxUrl(b, 400, NAN, 0));
  size_t full = strlen((botWxUrl(b, 400, 43.6534, -79.3841), b));
  EXPECT_TRUE(botWxUrl(b, full + 1, 43.6534, -79.3841)); EXPECT_FALSE(botWxUrl(b, full, 43.6534, -79.3841)); EXPECT_STREQ(b, "");
  EXPECT_FALSE(botWxUrl(b, 0, 1, 1)); EXPECT_FALSE(botWxUrl(nullptr, 9, 1, 1));
  EXPECT_FALSE(botGeoUrl(b, 400, nullptr)); EXPECT_FALSE(botGeoUrl(b, 400, ""));
  ASSERT_TRUE(botGeoUrl(b, 400, "Montr\xC3\xA9" "al")); EXPECT_NE(strstr(b, "name=Montr%C3%A9al&count=1&language=en&format=json"), nullptr);
  EXPECT_EQ(strncmp(b, "https://geocoding-api.open-meteo.com/v1/search?", 47), 0);
  ASSERT_TRUE(botGeoUrl(b, 400, "a&b=c#d")); EXPECT_EQ(strchr(strchr(b, '?'), '#'), nullptr);
  EXPECT_FALSE(botGeoUrl(b, 30, "Toronto"));
}
TEST(Wx, FilterKeepsOnlyReadFields) {
  const char* big = "{\"latitude\":43.6,\"elevation\":90,\"junk\":[1,2,3],\"utc_offset_seconds\":-18000,\"current_units\":{\"temperature_2m\":\"C\"},\"current\":{\"time\":\"2026-11-20T20:30\",\"interval\":900,\"temperature_2m\":6.1,\"apparent_temperature\":3.9,\"relative_humidity_2m\":79,\"weather_code\":0,\"wind_speed_10m\":3.3,\"wind_direction_10m\":359},\"daily\":{\"time\":[\"2026-11-20\"],\"temperature_2m_max\":[15.2,16],\"temperature_2m_min\":[4.3,5],\"precipitation_probability_max\":[0,10]}}";
  JsonDocument f; botWxFilter(f); JsonDocument a, b2; deserializeJson(a, big); deserializeJson(b2, big, DeserializationOption::Filter(f));
  BotWxNow x, y; ASSERT_TRUE(botWxParse(a, x)); ASSERT_TRUE(botWxParse(b2, y));
  EXPECT_TRUE(x.temp == y.temp && x.feels == y.feels && x.wind_kmh == y.wind_kmh && x.rh == y.rh && x.wind_deg == y.wind_deg && x.code == y.code &&
              x.tmin == y.tmin && x.tmax == y.tmax && x.precip_pct == y.precip_pct && x.has_daily == y.has_daily);
  EXPECT_TRUE(y.has_daily); EXPECT_FLOAT_EQ(y.tmax, 15.2f);
  EXPECT_TRUE(b2["junk"].isNull()); EXPECT_TRUE(b2["current"]["time"].isNull());
  deserializeJson(b2, WX, DeserializationOption::Filter(f)); ASSERT_TRUE(botWxParse(b2, y)); EXPECT_EQ(y.rh, 79);
  const char* g = "{\"generationtime_ms\":0.2,\"results\":[{\"id\":1,\"name\":\"Kingston\",\"latitude\":44.2312,\"longitude\":-76.4860,\"admin1\":\"Ontario\",\"country\":\"Canada\"},{\"name\":\"Other\",\"latitude\":1,\"longitude\":2}]}";
  JsonDocument gf; botGeoFilter(gf); JsonDocument gd; deserializeJson(gd, g, DeserializationOption::Filter(gf));
  BotPlace p; ASSERT_TRUE(botGeoParse(gd, p)); EXPECT_STREQ(p.name, "Kingston"); EXPECT_NEAR(p.lon, -76.4860, 1e-5); EXPECT_TRUE(gd["results"][0]["admin1"].isNull());
}

// ---- Kp ----
TEST(Kp, WordingBoundaries) {
  struct { float k; const char* s; } c[] = {{0,"quiet"},{2.99f,"quiet"},{3,"unsettled"},{3.99f,"unsettled"},{4,"active"},{4.99f,"active"},{5,"minor storm"},{5.99f,"minor storm"},
    {6,"moderate storm"},{6.99f,"moderate storm"},{7,"strong storm"},{7.99f,"strong storm"},{8,"severe storm"},{9,"severe storm"},{-1,"quiet"}};
  for (auto& e : c) EXPECT_STREQ(botKpWord(e.k), e.s) << e.k;
  EXPECT_STREQ(botKpWord(NAN), "quiet");
}
TEST(Kp, AuroraBands) {
  struct { double lat; float need; } b[] = {{60,3},{50,3},{49.99,5},{45,5},{44.99,6},{42,6},{41.99,7},{0.5,7},{-30,7}};
  for (auto& e : b) {
    EXPECT_STREQ(botAuroraChance(e.need, e.lat), "likely") << e.lat;
    EXPECT_STREQ(botAuroraChance(e.need + 3, e.lat), "likely") << e.lat;
    EXPECT_STREQ(botAuroraChance(e.need - 0.01f, e.lat), "possible") << e.lat;
    EXPECT_STREQ(botAuroraChance(e.need - 1, e.lat), "possible") << e.lat;
    EXPECT_STREQ(botAuroraChance(e.need - 1.01f, e.lat), "unlikely") << e.lat;
    EXPECT_STREQ(botAuroraChance(0, e.lat), "unlikely") << e.lat;
  }
}
TEST(Kp, ParseOnlyPastOnlyFuture) {
  JsonDocument d; BotKp k;
  deserializeJson(d, "[{\"time_tag\":\"2026-11-20T18:00:00\",\"kp\":1.0,\"observed\":\"observed\"},{\"time_tag\":\"2026-11-20T21:00:00\",\"kp\":2.33,\"observed\":\"estimated\"}]");
  ASSERT_TRUE(botKpParse(d, iso("2026-11-21T22:00:00"), k)); EXPECT_NEAR(k.now, 2.33, 0.01); EXPECT_FALSE(k.has_max24);
  deserializeJson(d, "[{\"time_tag\":\"2026-11-21T00:00:00\",\"kp\":4.33,\"observed\":\"predicted\"},{\"time_tag\":\"2026-11-21T03:00:00\",\"kp\":5,\"observed\":\"predicted\"}]");
  EXPECT_FALSE(botKpParse(d, iso("2026-11-20T22:10:00"), k));
}
TEST(Kp, ParseExactBoundariesAndWindow) {
  JsonDocument d; BotKp k;
  deserializeJson(d, KP);
  ASSERT_TRUE(botKpParse(d, iso("2026-11-20T21:00:00"), k)); EXPECT_NEAR(k.now, 2.33, 0.01); EXPECT_NEAR(k.max24, 4.33, 0.01);   // at-or-before includes equal
  ASSERT_TRUE(botKpParse(d, iso("2026-11-20T21:00:01"), k)); EXPECT_NEAR(k.now, 2.33, 0.01);
  ASSERT_TRUE(botKpParse(d, iso("2026-11-21T00:00:00"), k)); EXPECT_NEAR(k.now, 4.33, 0.01); EXPECT_FALSE(k.has_max24);   // row 2026-11-22T03 is beyond 24 h
  ASSERT_TRUE(botKpParse(d, iso("2026-11-21T03:00:00"), k)); EXPECT_NEAR(k.max24, 6.0, 0.01); EXPECT_TRUE(k.has_max24);    // exactly +24 h included
  ASSERT_TRUE(botKpParse(d, iso("2026-11-21T02:59:59"), k)); EXPECT_NEAR(k.now, 4.33, 0.01); EXPECT_FALSE(k.has_max24);   // the 6.0 row is 24 h + 1 s ahead
  ASSERT_TRUE(botKpParse(d, iso("2026-11-21T03:00:01"), k)); EXPECT_NEAR(k.now, 4.33, 0.01); EXPECT_NEAR(k.max24, 6.0, 0.01);
}
TEST(Kp, ParsePeakIsLargestNotLast) {
  JsonDocument d; BotKp k;
  deserializeJson(d, "[{\"time_tag\":\"2026-11-20T21:00:00\",\"kp\":2,\"observed\":\"observed\"},{\"time_tag\":\"2026-11-21T00:00:00\",\"kp\":6.67,\"observed\":\"predicted\"},{\"time_tag\":\"2026-11-21T03:00:00\",\"kp\":3,\"observed\":\"predicted\"}]");
  ASSERT_TRUE(botKpParse(d, iso("2026-11-20T22:00:00"), k)); EXPECT_NEAR(k.max24, 6.67, 0.01);
}
TEST(Kp, NoClockUsesLastObservedOrEstimated) {
  JsonDocument d; BotKp k;
  deserializeJson(d, "[{\"time_tag\":\"x\",\"kp\":1,\"observed\":\"observed\"},{\"time_tag\":\"y\",\"kp\":2,\"observed\":\"estimated\"},{\"time_tag\":\"z\",\"kp\":5,\"observed\":\"predicted\"}]");
  ASSERT_TRUE(botKpParse(d, 0, k)); EXPECT_NEAR(k.now, 2, 0.01); EXPECT_FALSE(k.has_max24);
  deserializeJson(d, "[{\"kp\":5,\"observed\":\"predicted\"}]"); EXPECT_FALSE(botKpParse(d, 0, k));
}
TEST(Kp, HostileDocuments) {
  const char* bad[] = {"null", "{}", "7", "\"s\"", "[]", "[1,2]", "[null]", "[{}]", "[{\"kp\":2}]", "[{\"time_tag\":\"2026-11-20T18:00:00\"}]",
    "[{\"time_tag\":\"2026-11-20T18:00:00\",\"kp\":\"2.3\"}]", "[{\"time_tag\":5,\"kp\":2}]", "[{\"time_tag\":\"junk\",\"kp\":2}]",
    "[{\"time_tag\":\"2026-11-20T18:00:00\",\"kp\":99}]", "[{\"time_tag\":\"2026-11-20T18:00:00\",\"kp\":-1}]", "{\"time_tag\":\"2026-11-20T18:00:00\",\"kp\":2}"};
  BotKp k; uint32_t now = iso("2026-11-20T22:00:00");
  for (const char* j : bad) { JsonDocument d; deserializeJson(d, j); EXPECT_FALSE(botKpParse(d, now, k)) << j; EXPECT_FALSE(botKpParse(d, 0, k)) << j; }
  EXPECT_FALSE(botKpParse(JsonVariantConst(), now, k));
  JsonDocument d; deserializeJson(d, "[7,{\"time_tag\":\"2026-11-20T18:00:00\",\"kp\":\"x\"},{\"time_tag\":\"2026-11-20T19:00:00\",\"kp\":3.5,\"observed\":\"observed\"}]");
  ASSERT_TRUE(botKpParse(d, now, k)); EXPECT_NEAR(k.now, 3.5, 0.01);   // bad rows are skipped
}
TEST(Kp, UrlAndFilter) {
  EXPECT_STREQ(botKpUrl(), "https://services.swpc.noaa.gov/products/noaa-planetary-k-index-forecast.json");
  std::string big = "[{\"time_tag\":\"2026-11-20T18:00:00\",\"kp\":1.0,\"observed\":\"observed\",\"noaa_scale\":null},{\"time_tag\":\"2026-11-20T21:00:00\",\"kp\":2.33,\"observed\":\"estimated\",\"noaa_scale\":\"G1\"},{\"time_tag\":\"2026-11-21T00:00:00\",\"kp\":4.33,\"observed\":\"predicted\",\"noaa_scale\":null}]";
  JsonDocument f; botKpFilter(f); JsonDocument d; deserializeJson(d, big.c_str(), DeserializationOption::Filter(f));
  EXPECT_TRUE(d[1]["noaa_scale"].isNull()); EXPECT_EQ(d.size(), 3u);
  BotKp k; ASSERT_TRUE(botKpParse(d, iso("2026-11-20T22:10:00"), k)); EXPECT_NEAR(k.now, 2.33, 0.01); EXPECT_NEAR(k.max24, 4.33, 0.01);
  ASSERT_TRUE(botKpParse(d, 0, k)); EXPECT_NEAR(k.now, 2.33, 0.01);
  JsonDocument all; deserializeJson(all, KP, DeserializationOption::Filter(f)); ASSERT_TRUE(botKpParse(all, iso("2026-11-20T22:10:00"), k)); EXPECT_NEAR(k.max24, 4.33, 0.01);
}
TEST(Kp, FormatDropsAndBounds) {
  BotKp k{2.33f, 4.33f, true}; char b[128];
  const char* full = "Kp 2.3 (quiet). Aurora unlikely here. Next 24h peak Kp 4.3";
  EXPECT_EQ(botAuroraFormat(b, 128, k, 44.0, strlen(full)), strlen(full)); EXPECT_STREQ(b, full);
  EXPECT_EQ(botAuroraFormat(b, 128, k, 44.0, strlen(full) - 1), 36u); EXPECT_STREQ(b, "Kp 2.3 (quiet). Aurora unlikely here");
  EXPECT_EQ(botAuroraFormat(b, 128, k, 44.0, 35), 14u); EXPECT_STREQ(b, "Kp 2.3 (quiet)");
  size_t n = botAuroraFormat(b, 128, k, 44.0, 8); EXPECT_LE(n, 8u); EXPECT_EQ(strncmp(b, "Kp 2", 4), 0);
  k.has_max24 = false; botAuroraFormat(b, 128, k, 44.0, 100); EXPECT_STREQ(b, "Kp 2.3 (quiet). Aurora unlikely here");
  BotKp s{5.0f, 7.0f, true}; botAuroraFormat(b, 128, s, 51.0, 100); EXPECT_STREQ(b, "Kp 5.0 (minor storm). Aurora likely here. Next 24h peak Kp 7.0");
  botAuroraFormat(b, 128, s, 44.0, 100); EXPECT_STREQ(b, "Kp 5.0 (minor storm). Aurora possible here. Next 24h peak Kp 7.0");
  for (size_t cap = 1; cap < 70; cap += 2) for (size_t mx = 0; mx < 70; mx += 3) {
    char c[80]; memset(c, 'Q', sizeof c); size_t r = botAuroraFormat(c, cap, s, 0.0, mx);
    ASSERT_LE(r, (mx < cap - 1 ? mx : cap - 1)); ASSERT_EQ(strlen(c), r); for (size_t i = cap; i < sizeof c; i++) ASSERT_EQ(c[i], 'Q');
  }
  EXPECT_EQ(botAuroraFormat(b, 0, s, 1, 50), 0u); EXPECT_EQ(botAuroraFormat(nullptr, 5, s, 1, 50), 0u);
}

// ---- alerts ----
static std::string alertFeature(const char* props) { return std::string("{\"properties\":{") + props + "}}"; }
static std::string alertDoc(std::initializer_list<std::string> f) {
  std::string s = "{\"type\":\"FeatureCollection\",\"features\":["; bool first = true;
  for (auto& x : f) { if (!first) s += ","; s += x; first = false; } return s + "]}";
}
TEST(Alerts, ActiveRules) {
  JsonDocument d; BotAlert a[3]; uint32_t now = iso("2026-11-20T22:00:00");
  // ended in any case, status missing, expiration used when no event end, end exactly now is over
  auto doc = alertDoc({alertFeature("\"alert_name_en\":\"A\",\"status_en\":\"ENDED\""), alertFeature("\"alert_name_en\":\"B\""),
    alertFeature("\"alert_name_en\":\"C\",\"status_en\":\"issued\",\"expiration_datetime\":\"2026-11-20T22:00:00Z\""),
    alertFeature("\"alert_name_en\":\"D\",\"status_en\":\"issued\",\"expiration_datetime\":\"2026-11-21T12:00:00Z\"")});
  deserializeJson(d, doc.c_str()); ASSERT_EQ(botAlertsParse(d, now, a, 3, nullptr), 2);
  EXPECT_STREQ(a[0].name, "B"); EXPECT_EQ(a[0].ends_unix, 0u); EXPECT_STREQ(a[1].name, "D"); EXPECT_EQ(a[1].ends_unix, iso("2026-11-21T12:00:00"));
  EXPECT_STREQ(a[0].area, "");
  ASSERT_EQ(botAlertsParse(d, 0, a, 3, nullptr), 3);   // no clock: nothing expires, ended still skipped
  EXPECT_STREQ(a[2].name, "D");
}
TEST(Alerts, EventEndPreferredAndShortNameFallback) {
  JsonDocument d; BotAlert a[3];
  auto doc = alertDoc({alertFeature("\"alert_name_en\":\"Long name\",\"alert_short_name_en\":\"\",\"status_en\":\"issued\",\"event_end_datetime\":\"2026-11-21T10:00:00Z\",\"expiration_datetime\":\"2026-11-22T10:00:00Z\",\"feature_name_en\":\"Area 1\""),
    alertFeature("\"alert_name_en\":\"Other\",\"status_en\":\"issued\",\"event_end_datetime\":\"garbage\",\"expiration_datetime\":\"2026-11-22T10:00:00Z\""),
    alertFeature("\"status_en\":\"issued\"")});
  deserializeJson(d, doc.c_str()); ASSERT_EQ(botAlertsParse(d, 0, a, 3, nullptr), 2);
  EXPECT_STREQ(a[0].name, "Long name"); EXPECT_EQ(a[0].ends_unix, iso("2026-11-21T10:00:00")); EXPECT_STREQ(a[0].area, "Area 1");
  EXPECT_EQ(a[1].ends_unix, iso("2026-11-22T10:00:00"));
}
TEST(Alerts, DeduplicateAndLimit) {
  JsonDocument d; BotAlert a[3]; uint32_t now = iso("2026-11-20T22:00:00");
  auto F = [](const char* n, const char* ar, const char* end) { return alertFeature((std::string("\"alert_short_name_en\":\"") + n + "\",\"status_en\":\"issued\",\"feature_name_en\":\"" + ar + "\"" + (*end ? std::string(",\"event_end_datetime\":\"") + end + "\"" : "")).c_str()); };
  auto doc = alertDoc({F("Wind", "Toronto", "2026-11-21T10:00:00Z"), F("Wind", "Toronto", "2026-11-21T14:00:00Z"), F("Wind", "Toronto", ""), F("Wind", "Peel", ""), F("Snow", "Toronto", ""), F("Rain", "Toronto", ""), F("Fog", "Toronto", "")});
  deserializeJson(d, doc.c_str()); ASSERT_EQ(botAlertsParse(d, now, a, 3, nullptr), 3);
  EXPECT_STREQ(a[0].name, "Wind"); EXPECT_STREQ(a[0].area, "Toronto"); EXPECT_EQ(a[0].ends_unix, iso("2026-11-21T14:00:00")); EXPECT_STREQ(a[1].area, "Peel"); EXPECT_STREQ(a[2].name, "Snow");
  EXPECT_EQ(botAlertsParse(d, now, a, 1, nullptr), 1); EXPECT_EQ(botAlertsParse(d, now, a, 0, nullptr), 0); EXPECT_EQ(botAlertsParse(d, now, nullptr, 3, nullptr), 0);
}
TEST(Alerts, TotalCountsBeyondStored) {
  JsonDocument d; BotAlert a[3]; int total = -1; uint32_t now = iso("2026-11-20T22:00:00");
  auto F = [](const char* n) { return alertFeature((std::string("\"alert_short_name_en\":\"") + n + "\",\"status_en\":\"issued\",\"feature_name_en\":\"T\"").c_str()); };
  auto doc = alertDoc({F("A"), F("B"), F("A"), F("C"), F("D"), F("E")});
  deserializeJson(d, doc.c_str()); ASSERT_EQ(botAlertsParse(d, now, a, 3, &total), 3); EXPECT_EQ(total, 5);
  EXPECT_STREQ(a[2].name, "C");
  char b[160]; botAlertsFormat(b, 160, a, 3, total, 0, 150); EXPECT_STREQ(b, "5 alerts: A; B; C");
  botAlertsFormat(b, 160, a, 3, 2, 0, 150); EXPECT_STREQ(b, "3 alerts: A; B; C");     // total below stored: stored wins
  botAlertsFormat(b, 160, a, -4, 2, 0, 150); EXPECT_STREQ(b, "2 alerts");              // negative stored is 0
  botAlertsFormat(b, 160, a, 1, 4, 0, 150); EXPECT_STREQ(b, "4 alerts: A");            // single stored, area not shown
  botAlertsFormat(b, 160, a, 0, 0, 0, 150); EXPECT_STREQ(b, "No active alerts here");
  ASSERT_EQ(botAlertsParse(d, now, a, 0, &total), 0); EXPECT_EQ(total, 6);   // nothing stored to compare with: the repeated A is counted twice (accepted over-count)
  ASSERT_EQ(botAlertsParse(d, now, nullptr, 3, &total), 0); EXPECT_EQ(total, 6);
  ASSERT_EQ(botAlertsParse(d, now, a, 3, nullptr), 3);
  deserializeJson(d, "{\"code\":1}"); total = 9; EXPECT_EQ(botAlertsParse(d, now, a, 3, &total), -1); EXPECT_EQ(total, 0);
  deserializeJson(d, "{\"features\":[]}"); EXPECT_EQ(botAlertsParse(d, now, a, 3, &total), 0); EXPECT_EQ(total, 0);
  // reads only a[0..stored-1]: guard bytes after the stored entries stay untouched
  BotAlert one[2] = {}; strcpy(one[0].name, "X"); memset(&one[1], 0x7F, sizeof one[1]);
  botAlertsFormat(b, 160, one, 1, 3, 0, 150); EXPECT_STREQ(b, "3 alerts: X");
}
TEST(Alerts, TextCleanedAndCut) {
  JsonDocument d; BotAlert a[3];
  std::string doc = alertDoc({alertFeature((std::string("\"alert_name_en\":\"Bad\\nname\\u0001") + std::string(60, 'y') + "\",\"feature_name_en\":\"" + std::string(38, 'a') + "\xC3\xA9\"").c_str())});
  deserializeJson(d, doc.c_str()); ASSERT_EQ(botAlertsParse(d, 0, a, 3, nullptr), 1);
  EXPECT_EQ(strncmp(a[0].name, "Bad name ", 9), 0); EXPECT_EQ(strlen(a[0].name), 47u); EXPECT_EQ(strlen(a[0].area), 38u);
}
TEST(Alerts, HostileDocuments) {
  const char* notFc[] = {"null", "[]", "7", "\"s\"", "{}", "{\"features\":{}}", "{\"features\":null}", "{\"features\":\"x\"}", "{\"features\":5}"};
  BotAlert a[3];
  for (const char* j : notFc) { JsonDocument d; deserializeJson(d, j); EXPECT_EQ(botAlertsParse(d, 0, a, 3, nullptr), -1) << j; }
  EXPECT_EQ(botAlertsParse(JsonVariantConst(), 0, a, 3, nullptr), -1);
  const char* none[] = {"{\"features\":[]}", "{\"features\":[1,null,[],\"x\"]}", "{\"features\":[{}]}", "{\"features\":[{\"properties\":5}]}", "{\"features\":[{\"properties\":[]}]}",
    "{\"features\":[{\"properties\":{\"alert_name_en\":5}}]}", "{\"features\":[{\"properties\":{\"alert_name_en\":\"\"}}]}", "{\"features\":[{\"properties\":{\"alert_name_en\":null,\"alert_short_name_en\":[]}}]}"};
  for (const char* j : none) { JsonDocument d; deserializeJson(d, j); EXPECT_EQ(botAlertsParse(d, 0, a, 3, nullptr), 0) << j; }
  JsonDocument d; deserializeJson(d, "{\"features\":[{\"properties\":{\"alert_name_en\":\"X\",\"status_en\":7,\"event_end_datetime\":5,\"expiration_datetime\":[],\"feature_name_en\":{}}}]}");
  ASSERT_EQ(botAlertsParse(d, 100, a, 3, nullptr), 1); EXPECT_STREQ(a[0].name, "X"); EXPECT_EQ(a[0].ends_unix, 0u); EXPECT_STREQ(a[0].area, "");
}
TEST(Alerts, FormatTwoAlertsAndMissingEnd) {
  BotAlert a[3] = {}; strcpy(a[0].name, "Snowfall warning"); strcpy(a[0].area, "Toronto"); a[0].ends_unix = iso("2026-11-21T19:00:00");
  strcpy(a[1].name, "Wind warning"); strcpy(a[1].area, "Toronto");
  char b[160];
  botAlertsFormat(b, 160, a, 2,2, -18000, 150); EXPECT_STREQ(b, "2 alerts: Snowfall warning until Sat 14:00; Wind warning");
  botAlertsFormat(b, 160, a + 1, 1,1, -18000, 150); EXPECT_STREQ(b, "1 alert: Wind warning (Toronto)");
  a[1].area[0] = 0; botAlertsFormat(b, 160, a + 1, 1,1, -18000, 150); EXPECT_STREQ(b, "1 alert: Wind warning");
  strcpy(a[2].name, "Fog"); a[2].ends_unix = iso("2026-11-22T04:30:00");
  botAlertsFormat(b, 160, a, 3,3, -18000, 150); EXPECT_STREQ(b, "3 alerts: Snowfall warning until Sat 14:00; Wind warning; Fog until Sat 23:30");
  botAlertsFormat(b, 160, a, 3,3, 3600, 150); EXPECT_NE(strstr(b, "until Sat 20:00;"), nullptr);
}
TEST(Alerts, FormatDropsFromRightKeepsCount) {
  BotAlert a[3] = {}; strcpy(a[0].name, "Snowfall warning"); a[0].ends_unix = iso("2026-11-21T19:00:00"); strcpy(a[1].name, "Wind warning"); strcpy(a[2].name, "Fog");
  char b[160];
  EXPECT_EQ(botAlertsFormat(b, 160, a, 3,3, -18000, 150), strlen("3 alerts: Snowfall warning until Sat 14:00; Wind warning; Fog"));
  size_t two = strlen("3 alerts: Snowfall warning until Sat 14:00; Wind warning");
  EXPECT_EQ(botAlertsFormat(b, 160, a, 3,3, -18000, two), two); EXPECT_STREQ(b, "3 alerts: Snowfall warning until Sat 14:00; Wind warning");
  size_t one = strlen("3 alerts: Snowfall warning until Sat 14:00");
  EXPECT_EQ(botAlertsFormat(b, 160, a, 3,3, -18000, two - 1), one); EXPECT_STREQ(b, "3 alerts: Snowfall warning until Sat 14:00");
  size_t r = botAlertsFormat(b, 160, a, 3,3, -18000, one - 1); EXPECT_LE(r, one - 1); EXPECT_EQ(strncmp(b, "3 alerts: Snowfall", 18), 0);
}
TEST(Alerts, FormatAreaOnlyWhenFits) {
  BotAlert a[1] = {}; strcpy(a[0].name, "Snowfall warning"); strcpy(a[0].area, "City of Toronto"); a[0].ends_unix = iso("2026-11-21T19:00:00");
  char b[128];
  size_t withArea = strlen("1 alert: Snowfall warning (City of Toronto) until Sat 14:00");
  EXPECT_EQ(botAlertsFormat(b, 128, a, 1,1, -18000, withArea), withArea);
  EXPECT_EQ(botAlertsFormat(b, 128, a, 1,1, -18000, withArea - 1), strlen("1 alert: Snowfall warning until Sat 14:00")); EXPECT_STREQ(b, "1 alert: Snowfall warning until Sat 14:00");
  for (size_t cap = 1; cap < 70; cap += 2) for (size_t mx = 0; mx < 70; mx += 3) {
    char c[80]; memset(c, 'Q', sizeof c); size_t r = botAlertsFormat(c, cap, a, 1,1, 0, mx);
    ASSERT_LE(r, (mx < cap - 1 ? mx : cap - 1)); ASSERT_EQ(strlen(c), r); for (size_t i = cap; i < sizeof c; i++) ASSERT_EQ(c[i], 'Q');
  }
  EXPECT_EQ(botAlertsFormat(b, 128, nullptr, 0,0, 0, 50), 21u); EXPECT_STREQ(b, "No active alerts here");
  size_t r0 = botAlertsFormat(b, 128, a, 0,0, 0, 10); EXPECT_EQ(r0, strlen(b)); EXPECT_LE(r0, 10u);
  EXPECT_EQ(botAlertsFormat(b, 0, a, 1,1, 0, 50), 0u);
}
TEST(Alerts, UrlBounds) {
  char b[400];
  ASSERT_TRUE(botAlertsUrl(b, 400, 0.0, 0.0)); EXPECT_NE(strstr(b, "bbox=-0.0500,-0.0500,0.0500,0.0500"), nullptr);
  EXPECT_EQ(strncmp(b, "https://api.weather.gc.ca/collections/weather-alerts/items?f=json", 64), 0);
  ASSERT_TRUE(botAlertsUrl(b, 400, 45.4215, -75.6972)); EXPECT_NE(strstr(b, "bbox=-75.7472,45.3715,-75.6472,45.4715"), nullptr); EXPECT_NE(strstr(b, "limit=10"), nullptr);
  size_t full = strlen(b); EXPECT_FALSE(botAlertsUrl(b, full, 45.4215, -75.6972)); EXPECT_STREQ(b, "");
  EXPECT_FALSE(botAlertsUrl(b, 400, 100, 0)); EXPECT_FALSE(botAlertsUrl(b, 400, 0, NAN)); EXPECT_FALSE(botAlertsUrl(b, 0, 0, 0));
}
TEST(Alerts, FilterRoundTrip) {
  std::string big = "{\"type\":\"FeatureCollection\",\"numberReturned\":2,\"links\":[{\"href\":\"x\"}],\"features\":[{\"id\":\"a\",\"type\":\"Feature\",\"properties\":{\"alert_name_en\":\"snowfall warning\",\"alert_short_name_en\":\"Snowfall warning\",\"alert_type\":\"warning\",\"status_en\":\"issued\",\"event_end_datetime\":\"2026-11-21T19:00:00.000Z\",\"expiration_datetime\":\"2026-11-21T20:00:00Z\",\"feature_name_en\":\"City of Toronto\",\"alert_text_en\":\"long long text\"}},{\"properties\":{\"alert_name_en\":\"special weather statement\",\"status_en\":\"ended\",\"feature_name_en\":\"City of Toronto\"}}]}";
  JsonDocument f; botAlertsFilter(f); JsonDocument d, all; deserializeJson(d, big.c_str(), DeserializationOption::Filter(f)); deserializeJson(all, big.c_str());
  EXPECT_TRUE(d["features"][0]["properties"]["alert_text_en"].isNull()); EXPECT_TRUE(d["links"].isNull());
  BotAlert a[3], b[3]; uint32_t now = iso("2026-11-20T22:00:00");
  ASSERT_EQ(botAlertsParse(d, now, a, 3, nullptr), 1); ASSERT_EQ(botAlertsParse(all, now, b, 3, nullptr), 1);
  EXPECT_STREQ(a[0].name, b[0].name); EXPECT_STREQ(a[0].area, b[0].area); EXPECT_EQ(a[0].ends_unix, b[0].ends_unix);
  JsonDocument e; deserializeJson(e, "{\"type\":\"FeatureCollection\",\"features\":[]}", DeserializationOption::Filter(f)); EXPECT_EQ(botAlertsParse(e, now, a, 3, nullptr), 0);
  JsonDocument g; deserializeJson(g, AL, DeserializationOption::Filter(f)); EXPECT_EQ(botAlertsParse(g, now, a, 3, nullptr), 1);
}

// ---- watched nodes ----
TEST(Watch, Urls) {
  char b[200];
  ASSERT_TRUE(botWatchUrl(b, 200, BW_OBSERVER, "0a1b2c3d-4e5f-6789-abcd-ef0123456789")); EXPECT_STREQ(b, "https://live.meshcore.ca/api/v1/observers/0a1b2c3d-4e5f-6789-abcd-ef0123456789");
  std::string key(64, 'A'); ASSERT_TRUE(botWatchUrl(b, 200, BW_NODE, key.c_str())); EXPECT_STREQ(b, (std::string("https://live.meshcore.ca/api/v1/nodes?pubkey=") + key).c_str());
  const char* bad[] = {nullptr, "", "xyz", "ab cd", "ab/cd", "ab?x=1", "ab&c=d", "ab#", "ab%20", "..", "ab\n", "ab\xC3\xA9", "g1"};
  for (const char* s : bad) { strcpy(b, "zz"); EXPECT_FALSE(botWatchUrl(b, 200, BW_NODE, s)); EXPECT_STREQ(b, ""); EXPECT_FALSE(botWatchUrl(b, 200, BW_OBSERVER, s)); }
  size_t full = strlen((botWatchUrl(b, 200, BW_NODE, "ab"), b)); EXPECT_FALSE(botWatchUrl(b, full, BW_NODE, "ab")); EXPECT_TRUE(botWatchUrl(b, full + 1, BW_NODE, "ab"));
  EXPECT_FALSE(botWatchUrl(b, 0, BW_NODE, "ab")); EXPECT_FALSE(botWatchUrl(nullptr, 9, BW_NODE, "ab"));
}
TEST(Watch, ObserverStates) {
  JsonDocument d; BotWatchState s; char b[64];
  deserializeJson(d, "{\"status\":\"offline\",\"lastSeen\":1795225173598}"); ASSERT_TRUE(botWatchParse(d, BW_OBSERVER, 1795225200, 600, s));
  EXPECT_TRUE(s.known); EXPECT_FALSE(s.online); EXPECT_EQ(s.last_seen_unix, 1795225173u);
  botWatchFormatOne(b, 64, "Roof", s, 1795225200); EXPECT_STREQ(b, "Roof: offline 0m");
  deserializeJson(d, "{\"status\":\"online\"}"); ASSERT_TRUE(botWatchParse(d, BW_OBSERVER, 0, 600, s)); EXPECT_TRUE(s.online); EXPECT_EQ(s.last_seen_unix, 0u);
  botWatchFormatOne(b, 64, "Roof", s, 0); EXPECT_STREQ(b, "Roof: online");
  deserializeJson(d, "{\"status\":\"offline\"}"); ASSERT_TRUE(botWatchParse(d, BW_OBSERVER, 1795225200, 600, s)); EXPECT_FALSE(s.online);
  botWatchFormatOne(b, 64, "Roof", s, 1795225200); EXPECT_STREQ(b, "Roof: offline");
  deserializeJson(d, "{\"status\":\"degraded\"}"); ASSERT_TRUE(botWatchParse(d, BW_OBSERVER, 1, 600, s)); EXPECT_FALSE(s.online);
}
TEST(Watch, NodeOnlineBoundaryAndNowZero) {
  JsonDocument d; BotWatchState s; uint32_t now = 1795225200;
  std::string j = "{\"items\":[{\"lastSeen\":" + std::to_string((uint64_t)(now - 43200) * 1000) + "}]}";
  deserializeJson(d, j.c_str()); ASSERT_TRUE(botWatchParse(d, BW_NODE, now, 43200, s)); EXPECT_TRUE(s.online);
  ASSERT_TRUE(botWatchParse(d, BW_NODE, now + 1, 43200, s)); EXPECT_FALSE(s.online);
  ASSERT_TRUE(botWatchParse(d, BW_NODE, 0, 43200, s)); EXPECT_TRUE(s.online); EXPECT_TRUE(s.known);   // clock not set: any lastSeen is online
  j = "{\"items\":[{\"lastSeen\":" + std::to_string((uint64_t)(now + 500) * 1000) + "}]}";   // in the future (clock skew)
  deserializeJson(d, j.c_str()); ASSERT_TRUE(botWatchParse(d, BW_NODE, now, 60, s)); EXPECT_TRUE(s.online);
  deserializeJson(d, "{\"items\":[{\"name\":\"n\"}]}"); ASSERT_TRUE(botWatchParse(d, BW_NODE, now, 60, s)); EXPECT_TRUE(s.known); EXPECT_FALSE(s.online); EXPECT_EQ(s.last_seen_unix, 0u);
  ASSERT_TRUE(botWatchParse(d, BW_NODE, 0, 60, s)); EXPECT_FALSE(s.online);
  deserializeJson(d, "{\"items\":[{\"lastSeen\":0}]}"); ASSERT_TRUE(botWatchParse(d, BW_NODE, 0, 60, s)); EXPECT_FALSE(s.online);
  deserializeJson(d, "{\"items\":[{\"lastSeen\":99999999999999999}]}"); ASSERT_TRUE(botWatchParse(d, BW_NODE, now, 60, s)); EXPECT_EQ(s.last_seen_unix, 0u);
}
TEST(Watch, FormatAgeUnits) {
  BotWatchState s{true, false, 0}; char b[64]; uint32_t now = 1795225200;
  struct { uint32_t age; const char* t; } c[] = {{0,"offline 0m"},{59,"offline 0m"},{60,"offline 1m"},{3599,"offline 59m"},{3600,"offline 1h"},{7200,"offline 2h"},
    {2 * 86400 - 1,"offline 47h"},{2 * 86400,"offline 2d"},{5 * 86400 + 3600,"offline 5d"}};
  for (auto& e : c) { s.last_seen_unix = now - e.age; botWatchFormatOne(b, 64, "N", s, now); EXPECT_EQ(std::string(b), std::string("N: ") + e.t) << e.age; }
  s.last_seen_unix = now + 100; botWatchFormatOne(b, 64, "N", s, now); EXPECT_STREQ(b, "N: offline 0m");
  s.last_seen_unix = 1000; botWatchFormatOne(b, 64, "N", s, 0); EXPECT_STREQ(b, "N: offline");   // clock not set
}
TEST(Watch, FormatLabelsAndBounds) {
  BotWatchState on{true, true, 5}; BotWatchState un{false, false, 0}; char b[64];
  botWatchFormatOne(b, 64, nullptr, on, 10); EXPECT_STREQ(b, ": online");
  botWatchFormatOne(b, 64, "Hill \xC3\xA9", un, 10); EXPECT_STREQ(b, "Hill \xC3\xA9: unknown");
  char c[8]; size_t r = botWatchFormatOne(c, 8, "Hilltop-long-label", on, 10); EXPECT_EQ(r, strlen(c)); EXPECT_LE(r, 7u);
  EXPECT_EQ(botWatchFormatOne(c, 0, "x", on, 1), 0u); EXPECT_EQ(botWatchFormatOne(nullptr, 5, "x", on, 1), 0u);
  for (size_t cap = 1; cap < 12; cap++) { char t[16]; memset(t, 'Q', 16); size_t k = botWatchFormatOne(t, cap, "Hilltop", on, 1); ASSERT_EQ(strlen(t), k); ASSERT_LT(k, cap); ASSERT_EQ(t[15], 'Q'); }
}
TEST(Watch, HostileDocuments) {
  const char* bad[] = {"null", "[]", "7", "\"s\"", "{}", "{\"status\":5}", "{\"status\":null}", "{\"status\":[]}", "{\"status\":\"online\",\"lastSeen\":\"1795225173598\"}",
    "{\"status\":\"online\",\"lastSeen\":[1]}", "{\"items\":[]}"};
  BotWatchState s;
  for (const char* j : bad) { JsonDocument d; deserializeJson(d, j); EXPECT_FALSE(botWatchParse(d, BW_OBSERVER, 100, 60, s)) << j; }
  const char* badN[] = {"null", "[]", "7", "{}", "{\"items\":{}}", "{\"items\":\"x\"}", "{\"items\":null}", "{\"items\":[1]}", "{\"items\":[null]}", "{\"items\":[[]]}",
    "{\"items\":[{\"lastSeen\":\"5\"}]}", "{\"items\":[{\"lastSeen\":{}}]}", "{\"status\":\"online\"}"};
  for (const char* j : badN) { JsonDocument d; deserializeJson(d, j); EXPECT_FALSE(botWatchParse(d, BW_NODE, 100, 60, s)) << j; }
  EXPECT_FALSE(botWatchParse(JsonVariantConst(), BW_NODE, 1, 1, s)); EXPECT_FALSE(botWatchParse(JsonVariantConst(), BW_OBSERVER, 1, 1, s));
}
TEST(Watch, FilterRoundTrip) {
  const char* o = "{\"id\":\"x\",\"name\":\"Roof\",\"status\":\"online\",\"lastSeen\":1795225173598,\"stats\":{\"a\":[1,2]}}";
  JsonDocument f; botWatchFilter(f, BW_OBSERVER); JsonDocument d; deserializeJson(d, o, DeserializationOption::Filter(f));
  BotWatchState s; ASSERT_TRUE(botWatchParse(d, BW_OBSERVER, 1795225200, 60, s)); EXPECT_TRUE(s.online); EXPECT_EQ(s.last_seen_unix, 1795225173u); EXPECT_TRUE(d["stats"].isNull());
  const char* n = "{\"total\":2,\"items\":[{\"name\":\"Hilltop\",\"pubkey\":\"ab\",\"lastSeen\":1795052400000},{\"name\":\"Other\",\"lastSeen\":1}]}";
  botWatchFilter(f, BW_NODE); deserializeJson(d, n, DeserializationOption::Filter(f));
  ASSERT_TRUE(botWatchParse(d, BW_NODE, 1795225200, 43200, s)); EXPECT_FALSE(s.online); EXPECT_EQ(s.last_seen_unix, 1795052400u); EXPECT_TRUE(d["items"][0]["name"].isNull());
  deserializeJson(d, "{\"items\":[]}", DeserializationOption::Filter(f)); ASSERT_TRUE(botWatchParse(d, BW_NODE, 1, 1, s)); EXPECT_FALSE(s.known);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
