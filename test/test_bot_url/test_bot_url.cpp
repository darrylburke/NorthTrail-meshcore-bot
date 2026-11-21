// Host tests for BotUrl.h, BotJsonPath.h and BotCache.h.
#include <gtest/gtest.h>
#include <cstdio>
#include <string>
#include <ArduinoJson.h>
#include "helpers/bot/BotUrl.h"
#include "helpers/bot/BotJsonPath.h"
#include "helpers/bot/BotCache.h"

TEST(UrlEncode, Reserved)        { char b[64]; botUrlEncode(b,64,"a b&c=d/e?f#g%h"); EXPECT_STREQ(b,"a%20b%26c%3Dd%2Fe%3Ff%23g%25h"); }
TEST(UrlEncode, Utf8AndUnreserved){ char b[64]; botUrlEncode(b,64,"Montr\xC3\xA9" "al-1_2.3~"); EXPECT_STREQ(b,"Montr%C3%A9al-1_2.3~"); }
TEST(UrlEncode, NoFit)           { char b[6]; EXPECT_EQ(botUrlEncode(b,6,"a b c"),0u); EXPECT_STREQ(b,""); }
TEST(UrlCheck, Accepts)          { EXPECT_EQ(botUrlCheckTemplate("https://example.org/x?q={args}&n=1"),BU_OK); EXPECT_EQ(botUrlCheckTemplate("http://10.0.0.5:8080/status"),BU_OK); }
TEST(UrlCheck, Rejects)          { EXPECT_EQ(botUrlCheckTemplate("ftp://example.org/"),BU_SCHEME); EXPECT_EQ(botUrlCheckTemplate("https://user:pw@example.org/"),BU_CREDENTIALS); EXPECT_EQ(botUrlCheckTemplate("https://example.org/{args}"),BU_ARGS_PLACE); EXPECT_EQ(botUrlCheckTemplate("https://{args}.example.org/?a=1"),BU_ARGS_PLACE); EXPECT_EQ(botUrlCheckTemplate("https://exa mple.org/"),BU_CHARS); EXPECT_EQ(botUrlCheckTemplate("https:///x"),BU_HOST); EXPECT_EQ(botUrlCheckTemplate((std::string("https://example.org/?")+std::string(160,'a')).c_str()),BU_LENGTH); }
TEST(UrlBuild, HostileArgs)      { char b[256]; ASSERT_TRUE(botUrlBuild(b,256,"https://example.org/s?q={args}&k=1","../../x?y=1&z #frag")); EXPECT_STREQ(b,"https://example.org/s?q=..%2F..%2Fx%3Fy%3D1%26z%20%23frag&k=1"); }
TEST(UrlBuild, ArgsCutAt60)      { char b[512]; std::string a(200,'x'); ASSERT_TRUE(botUrlBuild(b,512,"http://e.org/?q={args}",a.c_str())); EXPECT_EQ(strlen(b),strlen("http://e.org/?q=")+60); }
TEST(JsonPath, Scalars)          { JsonDocument d; deserializeJson(d,"{\"current\":{\"temperature_2m\":6.1,\"code\":0,\"ok\":true},\"results\":[{\"name\":\"Ottawa\"}]}"); char o[32]; ASSERT_TRUE(botJsonPathGet(d,"current.temperature_2m",o,32)); EXPECT_STREQ(o,"6.1"); ASSERT_TRUE(botJsonPathGet(d,"current.code",o,32)); EXPECT_STREQ(o,"0"); ASSERT_TRUE(botJsonPathGet(d,"results[0].name",o,32)); EXPECT_STREQ(o,"Ottawa"); ASSERT_TRUE(botJsonPathGet(d,"current.ok",o,32)); EXPECT_STREQ(o,"true"); }
TEST(JsonPath, Failures)         { JsonDocument d; deserializeJson(d,"{\"a\":{\"b\":[1,2]},\"s\":\"" "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx" "\",\"n\":null}"); char o[16]; EXPECT_FALSE(botJsonPathGet(d,"a.b",o,16)); EXPECT_FALSE(botJsonPathGet(d,"a.c",o,16)); EXPECT_FALSE(botJsonPathGet(d,"a.b[5]",o,16)); EXPECT_FALSE(botJsonPathGet(d,"s",o,16)); EXPECT_FALSE(botJsonPathGet(d,"n",o,16)); EXPECT_FALSE(botJsonPathCheck("a..b")); EXPECT_FALSE(botJsonPathCheck("a[x]")); EXPECT_FALSE(botJsonPathCheck("")); }
TEST(Cache, HitMissExpiry)       { BotCache c; char o[128]; c.put("wx|toronto","Toronto: 6C",1000,600000); EXPECT_TRUE(c.get("wx|toronto",2000,o,128)); EXPECT_STREQ(o,"Toronto: 6C"); EXPECT_FALSE(c.get("wx|ottawa",2000,o,128)); EXPECT_FALSE(c.get("wx|toronto",601001,o,128)); }
TEST(Cache, RolloverAndNinth)    { BotCache c; char o[128]; c.put("k","v",0xFFFFFF00u,1000); EXPECT_TRUE(c.get("k",0x00000100u,o,128)); EXPECT_FALSE(c.get("k",0x00000500u,o,128)); char k[8]; for(int i=0;i<9;i++){ snprintf(k,8,"k%d",i); c.put(k,"v",5000+i,60000);} EXPECT_FALSE(c.get("k0",6000,o,128)); EXPECT_TRUE(c.get("k8",6000,o,128)); }

// ---- botUrlEncode edges ----
TEST(UrlEncode, EmptyAndNull)    { char b[4]="zzz"; EXPECT_EQ(botUrlEncode(b,4,""),0u); EXPECT_STREQ(b,""); strcpy(b,"zzz"); EXPECT_EQ(botUrlEncode(b,4,nullptr),0u); EXPECT_STREQ(b,""); EXPECT_EQ(botUrlEncode(nullptr,4,"a"),0u); }
TEST(UrlEncode, CapZeroWritesNothing) { char b[2]={'x','y'}; EXPECT_EQ(botUrlEncode(b,0,"a"),0u); EXPECT_EQ(b[0],'x'); }
TEST(UrlEncode, ExactFitAndOneShort) { char b[10]; EXPECT_EQ(botUrlEncode(b,10,"a b c"),9u); EXPECT_STREQ(b,"a%20b%20c"); EXPECT_EQ(botUrlEncode(b,9,"a b c"),0u); EXPECT_STREQ(b,""); }
TEST(UrlEncode, UpperCaseHexAndControl) { char b[16]; botUrlEncode(b,16,"\x01\x7F\xFF\n"); EXPECT_STREQ(b,"%01%7F%FF%0A"); }
TEST(UrlEncode, AllBytesOnlyUnreservedOrPercent) {
  for (int c = 1; c < 256; c++) {
    char s[2] = {(char)c, 0}; char b[8];
    size_t w = botUrlEncode(b,8,s); ASSERT_EQ(w, strlen(b)) << c;
    for (const char* p = b; *p; p++) EXPECT_TRUE(botUrlIsUnreserved((unsigned char)*p) || *p=='%' || strchr("0123456789ABCDEF",*p)) << c;
    if (!botUrlIsUnreserved((unsigned char)c)) EXPECT_EQ(b[0],'%') << c;
  }
}

// ---- botUrlCheckTemplate: each error, order and edges ----
TEST(UrlCheck, Length)           { EXPECT_EQ(botUrlCheckTemplate(""),BU_LENGTH); EXPECT_EQ(botUrlCheckTemplate(nullptr),BU_LENGTH); std::string ok="https://example.org/?q="; ok+=std::string(160-ok.size(),'a'); ASSERT_EQ(ok.size(),160u); EXPECT_EQ(botUrlCheckTemplate(ok.c_str()),BU_OK); ok+="a"; EXPECT_EQ(botUrlCheckTemplate(ok.c_str()),BU_LENGTH); }
TEST(UrlCheck, LengthBeatsScheme) { EXPECT_EQ(botUrlCheckTemplate((std::string("ftp://")+std::string(200,'a')).c_str()),BU_LENGTH); }
TEST(UrlCheck, Scheme)           { EXPECT_EQ(botUrlCheckTemplate("HTTPS://example.org/"),BU_SCHEME); EXPECT_EQ(botUrlCheckTemplate("example.org/"),BU_SCHEME); EXPECT_EQ(botUrlCheckTemplate("https:/example.org"),BU_SCHEME); EXPECT_EQ(botUrlCheckTemplate("http://example.org"),BU_OK); }
TEST(UrlCheck, SchemeBeforeChars) { EXPECT_EQ(botUrlCheckTemplate("ftp://exa mple.org/"),BU_SCHEME); }
TEST(UrlCheck, Chars)            {
  EXPECT_EQ(botUrlCheckTemplate("https://example.org/a b"),BU_CHARS);
  EXPECT_EQ(botUrlCheckTemplate("https://example.org/a\tb"),BU_CHARS);
  EXPECT_EQ(botUrlCheckTemplate("https://example.org/a\x7F"),BU_CHARS);
  EXPECT_EQ(botUrlCheckTemplate("https://example.org/\xC3\xA9"),BU_CHARS);
  EXPECT_EQ(botUrlCheckTemplate("https://example.org/?q={arg}"),BU_CHARS);
  EXPECT_EQ(botUrlCheckTemplate("https://example.org/?q={args"),BU_CHARS);
  EXPECT_EQ(botUrlCheckTemplate("https://example.org/?q=args}"),BU_CHARS);
  EXPECT_EQ(botUrlCheckTemplate("https://example.org/?q={{args}}"),BU_CHARS);
  EXPECT_EQ(botUrlCheckTemplate("https://example.org/?q={ARGS}"),BU_CHARS);
}
TEST(UrlCheck, CharsBeforeCredentialsAndHost) { EXPECT_EQ(botUrlCheckTemplate("https://u ser@example.org/"),BU_CHARS); EXPECT_EQ(botUrlCheckTemplate("https://exa mple.org/@"),BU_CHARS); }
TEST(UrlCheck, Credentials)      { EXPECT_EQ(botUrlCheckTemplate("https://user@example.org/"),BU_CREDENTIALS); EXPECT_EQ(botUrlCheckTemplate("https://@example.org"),BU_CREDENTIALS); EXPECT_EQ(botUrlCheckTemplate("https://example.org?u=a@b"),BU_CREDENTIALS); EXPECT_EQ(botUrlCheckTemplate("https://example.org/x@y"),BU_OK); EXPECT_EQ(botUrlCheckTemplate("https://example.org/?m=a@b"),BU_OK); }
TEST(UrlCheck, CredentialsBeforeHost) { EXPECT_EQ(botUrlCheckTemplate("https://us_er@/x"),BU_CREDENTIALS); }
TEST(UrlCheck, HostRules)        {
  EXPECT_EQ(botUrlCheckTemplate("https://"),BU_HOST);
  EXPECT_EQ(botUrlCheckTemplate("https://?q=1"),BU_HOST);
  EXPECT_EQ(botUrlCheckTemplate("https://exa_mple.org/"),BU_HOST);
  EXPECT_EQ(botUrlCheckTemplate("https://example.org#x"),BU_HOST);
  EXPECT_EQ(botUrlCheckTemplate("https://[::1]/"),BU_HOST);
  EXPECT_EQ(botUrlCheckTemplate("https://a-b.c-d.example.org/"),BU_OK);
  std::string h100(100,'a'), h101(101,'a');
  EXPECT_EQ(botUrlCheckTemplate(("https://"+h100+"/").c_str()),BU_OK);
  EXPECT_EQ(botUrlCheckTemplate(("https://"+h101+"/").c_str()),BU_HOST);
}
TEST(UrlCheck, Port)             {
  EXPECT_EQ(botUrlCheckTemplate("https://example.org:1/"),BU_OK);
  EXPECT_EQ(botUrlCheckTemplate("https://example.org:65535"),BU_OK);
  EXPECT_EQ(botUrlCheckTemplate("https://example.org:12345?q={args}"),BU_OK);
  EXPECT_EQ(botUrlCheckTemplate("https://example.org:/"),BU_HOST);
  EXPECT_EQ(botUrlCheckTemplate("https://example.org:123456/"),BU_HOST);
  // 1 to 65535 only, the same range the HTTP client accepts (botHttpParseUrl).
  EXPECT_EQ(botUrlCheckTemplate("https://example.org:0/"),BU_HOST);
  EXPECT_EQ(botUrlCheckTemplate("https://example.org:00000/"),BU_HOST);
  EXPECT_EQ(botUrlCheckTemplate("https://example.org:65536/"),BU_HOST);
  EXPECT_EQ(botUrlCheckTemplate("https://example.org:99999/"),BU_HOST);
  EXPECT_EQ(botUrlCheckTemplate("https://example.org:00443/"),BU_OK);
  EXPECT_EQ(botUrlCheckTemplate("https://example.org:8080/?q={args}"),BU_OK);
  EXPECT_EQ(botUrlCheckTemplate("https://example.org:80a/"),BU_HOST);
  EXPECT_EQ(botUrlCheckTemplate("https://example.org:80:90/"),BU_HOST);
  EXPECT_EQ(botUrlCheckTemplate("https://:80/"),BU_HOST);
}
TEST(UrlCheck, ArgsPlace)        {
  EXPECT_EQ(botUrlCheckTemplate("https://example.org/{args}?a=1"),BU_ARGS_PLACE);
  EXPECT_EQ(botUrlCheckTemplate("https://example.org/{args}"),BU_ARGS_PLACE);
  EXPECT_EQ(botUrlCheckTemplate("https://example.org/?a={args}&b=1{args}"),BU_OK);
  EXPECT_EQ(botUrlCheckTemplate("https://example.org/?a=1&b={args}{args}"),BU_OK);
  EXPECT_EQ(botUrlCheckTemplate("https://example.org/{args}/?a={args}"),BU_ARGS_PLACE);
  EXPECT_EQ(botUrlCheckTemplate("https://example.org/p?"),BU_OK);
}
TEST(UrlBuild, ArgsTwice)        { char b[128]; ASSERT_TRUE(botUrlBuild(b,128,"https://example.org/?a={args}&b={args}","x y")); EXPECT_STREQ(b,"https://example.org/?a=x%20y&b=x%20y"); }
TEST(UrlBuild, NullAndEmptyArgs) { char b[128]; ASSERT_TRUE(botUrlBuild(b,128,"https://e.org/?q={args}&k=1",nullptr)); EXPECT_STREQ(b,"https://e.org/?q=&k=1"); ASSERT_TRUE(botUrlBuild(b,128,"https://e.org/?q={args}&k=1","")); EXPECT_STREQ(b,"https://e.org/?q=&k=1"); }
TEST(UrlBuild, NoPlaceholder)    { char b[64]; ASSERT_TRUE(botUrlBuild(b,64,"https://e.org/status","ignored")); EXPECT_STREQ(b,"https://e.org/status"); }
TEST(UrlBuild, NullTemplate)     { char b[8]="zz"; ASSERT_TRUE(botUrlBuild(b,8,nullptr,"a")); EXPECT_STREQ(b,""); }
TEST(UrlBuild, CapBoundaries)    {
  const char* t = "http://e.org/?q={args}";   // 16 + 3 chars for "a b" -> "a%20b" (5)
  char b[64]; ASSERT_TRUE(botUrlBuild(b,64,t,"a b")); size_t n = strlen(b); EXPECT_EQ(n,strlen("http://e.org/?q=")+5);
  char c[64]; EXPECT_TRUE(botUrlBuild(c,n+1,t,"a b")); EXPECT_STREQ(c,b);
  strcpy(c,"junk"); EXPECT_FALSE(botUrlBuild(c,n,t,"a b")); EXPECT_STREQ(c,"");
  EXPECT_FALSE(botUrlBuild(c,0,t,"a b"));
  EXPECT_FALSE(botUrlBuild(c,5,t,"a b"));   // fails in the literal part
  EXPECT_FALSE(botUrlBuild(nullptr,8,t,"a b"));
}
TEST(UrlBuild, CutAtUtf8Boundary) {
  // 59 x then a 2-byte char straddling byte 60: the whole char is dropped.
  std::string a = std::string(59,'x') + "\xC3\xA9" "tail"; char b[512];
  ASSERT_TRUE(botUrlBuild(b,512,"http://e.org/?q={args}",a.c_str())); EXPECT_EQ(strlen(b),strlen("http://e.org/?q=")+59);
  // exactly 60 bytes is kept whole
  std::string e = std::string(58,'x') + "\xC3\xA9"; ASSERT_TRUE(botUrlBuild(b,512,"http://e.org/?q={args}",e.c_str())); EXPECT_EQ(strlen(b),strlen("http://e.org/?q=")+58+6);
}
TEST(UrlBuild, WorstCaseSixtyEncodedBytes) { char b[512]; std::string a(200,'\n'); ASSERT_TRUE(botUrlBuild(b,512,"http://e.org/?q={args}&q2={args}",a.c_str())); EXPECT_EQ(strlen(b),strlen("http://e.org/?q=&q2=")+2*180); }
TEST(UrlBuild, HostileArgsNeverAddReservedChars) {
  const char* t = "https://example.org/s?q={args}&k=1";
  for (int c = 1; c < 256; c++) {
    char a[4] = {(char)c,(char)c,(char)c,0}; char b[256];
    ASSERT_TRUE(botUrlBuild(b,256,t,a));
    // Only the template's own '?', '&', '=' and '/' remain; the argument part has none.
    std::string s(b); size_t from = strlen("https://example.org/s?q="); size_t to = s.size() - strlen("&k=1");
    std::string mid = s.substr(from, to - from);
    EXPECT_EQ(mid.find_first_of("&=#/?{} "), std::string::npos) << c;
    EXPECT_EQ(s.substr(0,from),"https://example.org/s?q="); EXPECT_EQ(s.substr(to),"&k=1");
  }
}
TEST(UrlHttps, OnlyHttps)        { EXPECT_TRUE(botUrlIsHttps("https://example.org")); EXPECT_FALSE(botUrlIsHttps("http://example.org")); EXPECT_FALSE(botUrlIsHttps("HTTPS://example.org")); EXPECT_FALSE(botUrlIsHttps("https:/")); EXPECT_FALSE(botUrlIsHttps("")); EXPECT_FALSE(botUrlIsHttps(nullptr)); }

// ---- JSON path edges ----
TEST(JsonPath, CheckAccepts)     { const char* ok[] = {"a","a.b","a_b-c","a[0]","a[0][1]","a[0].b","[0]","[0].name","[999]","[007]","a.b[1][2].c-d"}; for (const char* p : ok) EXPECT_TRUE(botJsonPathCheck(p)) << p; }
TEST(JsonPath, CheckRejects)     { const char* bad[] = {"",".a","a.","a..b","a[","a[]","a[x]","a[1000]","a[0","a[0]b","a[0].","a.[0]","a[-1]","a b","a/b","a]","[a]","a[0]]","a.b$"}; for (const char* p : bad) EXPECT_FALSE(botJsonPathCheck(p)) << p; EXPECT_FALSE(botJsonPathCheck(nullptr)); }
TEST(JsonPath, KeyAndPathLengths) {
  EXPECT_TRUE(botJsonPathCheck(std::string(32,'k').c_str())); EXPECT_FALSE(botJsonPathCheck(std::string(33,'k').c_str()));
  std::string p48 = std::string(32,'k') + "." + std::string(14,'j'); ASSERT_EQ(p48.size(),47u); p48 += "y"; ASSERT_EQ(p48.size(),48u);
  EXPECT_TRUE(botJsonPathCheck(p48.c_str())); EXPECT_FALSE(botJsonPathCheck((p48+"z").c_str()));
}
// At most BOT_JSON_PATH_STEPS (10) keys and indices in all, so a saved path can always be reached
// within the nesting limit (12) responses are parsed with.
TEST(JsonPath, AtMostTenSteps) {
  EXPECT_EQ(BOT_JSON_PATH_STEPS, 10);
  EXPECT_LT(BOT_JSON_PATH_STEPS, BOT_JSON_NESTING);
  EXPECT_TRUE(botJsonPathCheck("a.b.c.d.e.f.g.h.i.j"));           // 10 keys
  EXPECT_FALSE(botJsonPathCheck("a.b.c.d.e.f.g.h.i.j.k"));        // 11
  EXPECT_TRUE(botJsonPathCheck("a[0][1][2][3][4][5][6][7][8]"));  // 1 key + 9 indices
  EXPECT_FALSE(botJsonPathCheck("a[0][1][2][3][4][5][6][7][8][9]"));
  EXPECT_TRUE(botJsonPathCheck("[0].a[1].b[2].c[3].d[4].e"));     // 5 indices + 5 keys
  EXPECT_FALSE(botJsonPathCheck("[0].a[1].b[2].c[3].d[4].e[5]"));
  JsonDocument d; deserializeJson(d, "{\"a\":{\"b\":{\"c\":{\"d\":{\"e\":{\"f\":{\"g\":{\"h\":{\"i\":{\"j\":7}}}}}}}}}}",
                                   DeserializationOption::NestingLimit(BOT_JSON_NESTING));
  char o[8]; ASSERT_TRUE(botJsonPathGet(d, "a.b.c.d.e.f.g.h.i.j", o, sizeof o)); EXPECT_STREQ(o, "7");
  JsonDocument f; EXPECT_FALSE(botJsonPathFilter("a.b.c.d.e.f.g.h.i.j.k", f));
}
TEST(JsonPath, PathStartingWithIndex) { JsonDocument d; deserializeJson(d,"[{\"name\":\"Ottawa\"},7]"); char o[16]; ASSERT_TRUE(botJsonPathGet(d,"[0].name",o,16)); EXPECT_STREQ(o,"Ottawa"); ASSERT_TRUE(botJsonPathGet(d,"[1]",o,16)); EXPECT_STREQ(o,"7"); EXPECT_FALSE(botJsonPathGet(d,"[2]",o,16)); EXPECT_FALSE(botJsonPathGet(d,"[0]",o,16)); EXPECT_FALSE(botJsonPathGet(d,"name",o,16)); }
TEST(JsonPath, NestedArrays)     { JsonDocument d; deserializeJson(d,"{\"m\":[[1,2],[3,[4,\"deep\"]]]}"); char o[16]; ASSERT_TRUE(botJsonPathGet(d,"m[1][0]",o,16)); EXPECT_STREQ(o,"3"); ASSERT_TRUE(botJsonPathGet(d,"m[1][1][1]",o,16)); EXPECT_STREQ(o,"deep"); EXPECT_FALSE(botJsonPathGet(d,"m[1][1][2]",o,16)); EXPECT_FALSE(botJsonPathGet(d,"m[0][0][0]",o,16)); EXPECT_FALSE(botJsonPathGet(d,"m[2]",o,16)); }
TEST(JsonPath, WrongContainerKinds) { JsonDocument d; deserializeJson(d,"{\"a\":[1],\"o\":{\"k\":1},\"s\":\"str\"}"); char o[16]; EXPECT_FALSE(botJsonPathGet(d,"a.k",o,16)); EXPECT_FALSE(botJsonPathGet(d,"o[0]",o,16)); EXPECT_FALSE(botJsonPathGet(d,"s.x",o,16)); EXPECT_FALSE(botJsonPathGet(d,"s[0]",o,16)); EXPECT_FALSE(botJsonPathGet(d,"o",o,16)); }
TEST(JsonPath, Numbers)          {
  JsonDocument d; deserializeJson(d,"{\"i\":42,\"neg\":-7,\"z\":0,\"f1\":6.10,\"f2\":6.0,\"f3\":2.50,\"f4\":3.14159,\"f5\":-0.004,\"f6\":0.5,\"big\":12345678901,\"e\":1.5e2,\"fl\":2.999}");
  char o[32];
  struct { const char* p; const char* want; } t[] = {{"i","42"},{"neg","-7"},{"z","0"},{"f1","6.1"},{"f2","6"},{"f3","2.5"},{"f4","3.14"},{"f5","0"},{"f6","0.5"},{"big","12345678901"},{"e","150"},{"fl","3"}};
  for (auto& c : t) { ASSERT_TRUE(botJsonPathGet(d,c.p,o,32)) << c.p; EXPECT_STREQ(o,c.want) << c.p; }
}
TEST(JsonPath, Booleans)         { JsonDocument d; deserializeJson(d,"{\"t\":true,\"f\":false}"); char o[8]; ASSERT_TRUE(botJsonPathGet(d,"t",o,8)); EXPECT_STREQ(o,"true"); ASSERT_TRUE(botJsonPathGet(d,"f",o,8)); EXPECT_STREQ(o,"false"); char s[5]; EXPECT_FALSE(botJsonPathGet(d,"f",s,5)); EXPECT_STREQ(s,""); char s6[6]; ASSERT_TRUE(botJsonPathGet(d,"f",s6,6)); EXPECT_STREQ(s6,"false"); }
TEST(JsonPath, StringControlChars) { JsonDocument d; deserializeJson(d,"{\"s\":\"a\\nb\\tc\\u0001d\\u007fe\"}"); char o[32]; ASSERT_TRUE(botJsonPathGet(d,"s",o,32)); EXPECT_STREQ(o,"a b c d e"); }
TEST(JsonPath, StringUtf8Kept)   { JsonDocument d; deserializeJson(d,"{\"s\":\"Montr\\u00e9al\"}"); char o[32]; ASSERT_TRUE(botJsonPathGet(d,"s",o,32)); EXPECT_STREQ(o,"Montr\xC3\xA9" "al"); }
TEST(JsonPath, ExactlyCapMinusOne) {
  JsonDocument d; deserializeJson(d,"{\"s\":\"12345\",\"n\":12345,\"e\":\"\"}"); char o[6];
  ASSERT_TRUE(botJsonPathGet(d,"s",o,6)); EXPECT_STREQ(o,"12345");
  ASSERT_TRUE(botJsonPathGet(d,"n",o,6)); EXPECT_STREQ(o,"12345");
  char p[5]; EXPECT_FALSE(botJsonPathGet(d,"s",p,5)); EXPECT_STREQ(p,""); EXPECT_FALSE(botJsonPathGet(d,"n",p,5)); EXPECT_STREQ(p,"");
  char q[1]; EXPECT_FALSE(botJsonPathGet(d,"s",q,1)); EXPECT_STREQ(q,""); ASSERT_TRUE(botJsonPathGet(d,"e",q,1)); EXPECT_STREQ(q,"");
  EXPECT_FALSE(botJsonPathGet(d,"s",o,0)); EXPECT_FALSE(botJsonPathGet(d,"s",nullptr,6));
}
TEST(JsonPath, BadSyntaxAndNullPathFailInGet) { JsonDocument d; deserializeJson(d,"{\"a\":1}"); char o[8]="zz"; EXPECT_FALSE(botJsonPathGet(d,"a..b",o,8)); EXPECT_STREQ(o,""); EXPECT_FALSE(botJsonPathGet(d,nullptr,o,8)); EXPECT_FALSE(botJsonPathGet(d,"",o,8)); }
TEST(JsonPath, EmptyDocument)    { JsonDocument d; char o[8]; EXPECT_FALSE(botJsonPathGet(d,"a",o,8)); EXPECT_FALSE(botJsonPathGet(d,"[0]",o,8)); }
TEST(JsonPath, DeepPathNoRecursion) { JsonDocument d; deserializeJson(d,"{\"a\":{\"b\":{\"c\":{\"d\":{\"e\":\"deep\"}}}}}"); char o[8]; ASSERT_TRUE(botJsonPathGet(d,"a.b.c.d.e",o,8)); EXPECT_STREQ(o,"deep"); EXPECT_FALSE(botJsonPathGet(d,"a.b.c.d.e.f",o,8)); }

// ---- cache edges ----
TEST(Cache, NullAndEmptyKey)     { BotCache c; char o[16]="zz"; c.put(nullptr,"v",0,1000); c.put("","v",0,1000); EXPECT_FALSE(c.get(nullptr,1,o,16)); EXPECT_STREQ(o,""); EXPECT_FALSE(c.get("",1,o,16)); }
TEST(Cache, NullTextStoresNothing) { BotCache c; char o[16]; c.put("k",nullptr,0,1000); EXPECT_FALSE(c.get("k",1,o,16)); c.put("k","old",0,1000); c.put("k",nullptr,1,1000); ASSERT_TRUE(c.get("k",2,o,16)); EXPECT_STREQ(o,"old"); }
TEST(Cache, KeyLengthLimit)      { BotCache c; char o[16]; std::string k47(47,'k'), k48(48,'k'); c.put(k47.c_str(),"v",0,1000); c.put(k48.c_str(),"v",0,1000); EXPECT_TRUE(c.get(k47.c_str(),1,o,16)); EXPECT_FALSE(c.get(k48.c_str(),1,o,16)); }
TEST(Cache, TextLengthLimit)     { BotCache c; char o[256]; std::string t127(127,'t'), t128(128,'t'); c.put("a",t127.c_str(),0,1000); c.put("b",t128.c_str(),0,1000); ASSERT_TRUE(c.get("a",1,o,256)); EXPECT_EQ(strlen(o),127u); EXPECT_FALSE(c.get("b",1,o,256)); }
TEST(Cache, ZeroTtlDoesNotStore) { BotCache c; char o[16]; c.put("k","v",0,0); EXPECT_FALSE(c.get("k",0,o,16)); c.put("k","v",100,0); EXPECT_FALSE(c.get("k",100,o,16)); }
TEST(Cache, ExpiryBoundary)      { BotCache c; char o[16]; c.put("k","v",1000,500); EXPECT_TRUE(c.get("k",1499,o,16)); EXPECT_FALSE(c.get("k",1500,o,16)); EXPECT_STREQ(o,""); }
TEST(Cache, CaseSensitiveKeys)   { BotCache c; char o[16]; c.put("Wx","A",0,1000); c.put("wx","B",0,1000); ASSERT_TRUE(c.get("Wx",1,o,16)); EXPECT_STREQ(o,"A"); ASSERT_TRUE(c.get("wx",1,o,16)); EXPECT_STREQ(o,"B"); EXPECT_FALSE(c.get("WX",1,o,16)); }
TEST(Cache, SameKeyOverwritesInPlace) { BotCache c; char o[16]; c.put("k","one",0,1000); c.put("k","two",500,1000); ASSERT_TRUE(c.get("k",1200,o,16)); EXPECT_STREQ(o,"two"); for (int i=0;i<7;i++){ char k[8]; snprintf(k,8,"x%d",i); c.put(k,"v",600,1000);} ASSERT_TRUE(c.get("k",700,o,16)); EXPECT_STREQ(o,"two"); }
TEST(Cache, SmallOutputBuffer)   { BotCache c; char o[4]; c.put("k","abcd",0,1000); EXPECT_FALSE(c.get("k",1,o,4)); EXPECT_STREQ(o,""); c.put("k","abc",0,1000); ASSERT_TRUE(c.get("k",1,o,4)); EXPECT_STREQ(o,"abc"); char z[1]; EXPECT_FALSE(c.get("k",1,z,1)); EXPECT_FALSE(c.get("k",1,nullptr,4)); EXPECT_FALSE(c.get("k",1,o,0)); }
TEST(Cache, EmptyTextIsStored)   { BotCache c; char o[4]="zz"; c.put("k","",0,1000); ASSERT_TRUE(c.get("k",1,o,4)); EXPECT_STREQ(o,""); }
TEST(Cache, GetDoesNotRefresh)   { BotCache c; char o[8]; c.put("k","v",0,1000); EXPECT_TRUE(c.get("k",900,o,8)); EXPECT_FALSE(c.get("k",1000,o,8)); }
TEST(Cache, FullReplacesOldestNotNewest) { BotCache c; char o[8]; char k[8]; for (int i=0;i<8;i++){ snprintf(k,8,"k%d",i); c.put(k,"v",100+i*10,100000);} c.put("new","v",500,100000); EXPECT_FALSE(c.get("k0",600,o,8)); for (int i=1;i<8;i++){ snprintf(k,8,"k%d",i); EXPECT_TRUE(c.get(k,600,o,8)) << i; } EXPECT_TRUE(c.get("new",600,o,8)); }
TEST(Cache, OldestIsRolloverSafe) { BotCache c; char o[8]; char k[8]; for (int i=0;i<8;i++){ snprintf(k,8,"k%d",i); c.put(k,"v",0xFFFFFFF0u+i,100000);} c.put("new","v",0x20u,100000); EXPECT_FALSE(c.get("k0",0x21u,o,8)); EXPECT_TRUE(c.get("k7",0x21u,o,8)); EXPECT_TRUE(c.get("new",0x21u,o,8)); }

int main(int argc, char** argv) { ::testing::InitGoogleTest(&argc, argv); return RUN_ALL_TESTS(); }
