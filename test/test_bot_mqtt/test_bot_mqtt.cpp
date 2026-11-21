// Host tests for BotMqttTopics.h, BotMqttMsg.h and BotMqttWait.h.
#include <gtest/gtest.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <ArduinoJson.h>
#include "helpers/bot/BotMqttTopics.h"
#include "helpers/bot/BotMqttMsg.h"
#include "helpers/bot/BotMqttWait.h"

// ---- the brief's tests, verbatim ----
TEST(Prefix, Valid)        { EXPECT_TRUE(botMqttPrefixValid("meshcore/MeshBot")); EXPECT_TRUE(botMqttPrefixValid("a")); EXPECT_FALSE(botMqttPrefixValid("")); EXPECT_FALSE(botMqttPrefixValid("/a")); EXPECT_FALSE(botMqttPrefixValid("a/")); EXPECT_FALSE(botMqttPrefixValid("a//b")); EXPECT_FALSE(botMqttPrefixValid("a/#")); EXPECT_FALSE(botMqttPrefixValid("a/+/b")); EXPECT_FALSE(botMqttPrefixValid("a b")); EXPECT_FALSE(botMqttPrefixValid(std::string(65,'a').c_str())); EXPECT_TRUE(botMqttPrefixValid(std::string(64,'a').c_str())); }
TEST(Prefix, Default)      { char b[80]; botMqttDefaultPrefix(b,80,"Mesh Bot \xF0\x9F\xA4\x96!"); EXPECT_STREQ(b,"meshcore/MeshBot"); botMqttDefaultPrefix(b,80,"\xF0\x9F\xA4\x96"); EXPECT_STREQ(b,"meshcore/bot"); }
TEST(Topic, Build)         { char b[96]; ASSERT_TRUE(botMqttTopic(b,96,"meshcore/MeshBot","reply","8f2a00c1")); EXPECT_STREQ(b,"meshcore/MeshBot/reply/8f2a00c1"); ASSERT_TRUE(botMqttTopic(b,96,"meshcore/MeshBot","request",nullptr)); EXPECT_STREQ(b,"meshcore/MeshBot/request"); EXPECT_FALSE(botMqttTopic(b,10,"meshcore/MeshBot","request",nullptr)); }
TEST(Classify, ReplyAndCommand){ char t[20]; const char* a="meshcore/MeshBot/reply/8f2a00c1"; EXPECT_EQ(botMqttClassify(a,strlen(a),"meshcore/MeshBot",t,20),BMI_REPLY); EXPECT_STREQ(t,"8f2a00c1"); const char* c="meshcore/MeshBot/commands/beer"; EXPECT_EQ(botMqttClassify(c,strlen(c),"meshcore/MeshBot",t,20),BMI_COMMAND); EXPECT_STREQ(t,"beer"); }
TEST(Classify, Rejects)    { char t[20]; for (const char* x : {"meshcore/MeshBot/reply/","meshcore/MeshBot/reply/a/b","meshcore/MeshBot/reply/ABC","meshcore/MeshBot2/reply/abc","meshcore/MeshBot/request","meshcore/MeshBot/reply/abcdefghijklmnopq","other/reply/abc","meshcore/MeshBot/commands/be er"}) EXPECT_EQ(botMqttClassify(x,strlen(x),"meshcore/MeshBot",t,20),BMI_NONE) << x; }
TEST(Classify, NotTerminated){ char raw[40]; memset(raw,'z',40); memcpy(raw,"p/reply/abc",11); char t[20]; EXPECT_EQ(botMqttClassify(raw,11,"p",t,20),BMI_REPLY); EXPECT_STREQ(t,"abc"); }
TEST(Request, Shape)       { BotMqttRequest r{}; r.id="8f2a00c1"; r.prefix="meshcore/MeshBot"; r.cmd="beer"; r.args="ipa \"cold\""; r.sender="Dee"; r.sender_key=""; r.channel="#bot"; r.path="a53e96,bcadb8"; r.path_names="Hilltop,?"; r.time="12:04"; r.date="2026-11-20"; r.tz="America/Toronto"; r.bot="MeshBot"; r.scope="yyz"; r.uptime="3d 4h"; r.snr=7.5f; r.rssi=-98; r.hops=2; r.dm=false; r.unix=1795200000u; r.max_len=100; char b[1024]; size_t n=botMqttBuildRequest(b,1024,r); ASSERT_GT(n,0u); JsonDocument d; ASSERT_FALSE(deserializeJson(d,b)); EXPECT_STREQ(d["id"],"8f2a00c1"); EXPECT_STREQ(d["reply_to"],"meshcore/MeshBot/reply/8f2a00c1"); EXPECT_STREQ(d["args"],"ipa \"cold\""); EXPECT_EQ(d["hops"].as<int>(),2); EXPECT_FALSE(d["dm"].as<bool>()); EXPECT_NEAR(d["snr"].as<double>(),7.5,1e-6); EXPECT_EQ(d["max_len"].as<int>(),100); EXPECT_EQ(d.as<JsonObject>().size(),25u); EXPECT_EQ(strncmp(b,"{\"id\":\"8f2a00c1\",\"reply_to\":",28),0); }
TEST(Request, ShortensToFit){ BotMqttRequest r{}; std::string names(700,'n'), path(300,'a'), args(300,'x'); r.id="00000001"; r.prefix="p"; r.cmd="c"; r.args=args.c_str(); r.sender="s"; r.sender_key=""; r.channel=""; r.path=path.c_str(); r.path_names=names.c_str(); r.time=""; r.date=""; r.tz=""; r.bot="b"; r.scope=""; r.uptime=""; char b[1100]; size_t n=botMqttBuildRequest(b,1100,r); ASSERT_GT(n,0u); EXPECT_LE(n,1000u); JsonDocument d; ASSERT_FALSE(deserializeJson(d,b)); EXPECT_STREQ(d["path_names"],""); }
TEST(Reply, Parse)         { char t[201]; const char* a="{\"text\":\"Hi {sender}\\n there \"}"; ASSERT_TRUE(botMqttParseReply(a,strlen(a),t,201)); EXPECT_STREQ(t,"Hi {sender}  there"); for (const char* x : {"","{","[]","{\"text\":5}","{\"text\":\"\"}","{\"text\":\"   \"}","{\"other\":\"x\"}","null"}) EXPECT_FALSE(botMqttParseReply(x,strlen(x),t,201)) << x; std::string big="{\"text\":\""+std::string(201,'a')+"\"}"; EXPECT_FALSE(botMqttParseReply(big.c_str(),big.size(),t,201)); std::string huge(600,'{'); EXPECT_FALSE(botMqttParseReply(huge.c_str(),huge.size(),t,201)); }
TEST(Announce, Parse)      { char d[49]; bool rm=false; const char* a="{\"desc\":\"order a drink\"}"; ASSERT_TRUE(botMqttParseAnnounce(a,strlen(a),d,49,&rm)); EXPECT_FALSE(rm); EXPECT_STREQ(d,"order a drink"); ASSERT_TRUE(botMqttParseAnnounce("",0,d,49,&rm)); EXPECT_TRUE(rm); ASSERT_TRUE(botMqttParseAnnounce("{}",2,d,49,&rm)); EXPECT_STREQ(d,""); EXPECT_FALSE(botMqttParseAnnounce("[1]",3,d,49,&rm)); }
TEST(Wait, MatchOnceOnly)  { BotMqttWaitTable w; EXPECT_EQ(w.add("aaaaaaaa",1000,1),0); EXPECT_EQ(w.match("bbbbbbbb",1,1000,5000),-1); EXPECT_EQ(w.match("aaaaaaaa",2,1000,5000),-1); EXPECT_EQ(w.match("aaaaaaaa",1,1000,5000),0); EXPECT_EQ(w.match("aaaaaaaa",1,1000,5000),-1); EXPECT_EQ(w.count(),0); }
TEST(Wait, FullAndExpiry)  { BotMqttWaitTable w; char id[9]; for(int i=0;i<4;i++){ botMqttMakeId(id,i); EXPECT_GE(w.add(id,1000+i,1),0);} EXPECT_EQ(w.add("ffffffff",2000,1),-1); EXPECT_EQ(w.expire(5999,5000),-1); EXPECT_GE(w.expire(6000,5000),0); EXPECT_EQ(w.count(),3); EXPECT_GE(w.add("ffffffff",6001,1),0); }
TEST(Wait, Rollover)       { BotMqttWaitTable w; w.add("aaaaaaaa",0xFFFFFF00u,1); EXPECT_EQ(w.expire(0x00000100u,5000),-1); EXPECT_GE(w.expire(0x00001400u,5000),0); }
TEST(Rate, ThirtyPerMinute){ BotMqttRate r; int ok=0; for(uint32_t t=0;t<60000;t+=500) if(r.allow(t)) ok++; EXPECT_EQ(ok,30); EXPECT_TRUE(r.allow(60001)); }
TEST(Id, Hex)              { char id[9]; botMqttMakeId(id,0x8F2A00C1u); EXPECT_STREQ(id,"8f2a00c1"); }
TEST(Commands, Table)      { BotMqttCommands c; EXPECT_TRUE(c.set("beer","order a drink")); EXPECT_TRUE(c.set("beer","two drinks")); EXPECT_EQ(c.count(),1); EXPECT_STREQ(c.find("beer"),"two drinks"); EXPECT_FALSE(c.set("Bad Name","x")); EXPECT_TRUE(c.remove("beer")); EXPECT_EQ(c.find("beer"),nullptr); char n[8]; for(int i=0;i<16;i++){ snprintf(n,8,"c%d",i); EXPECT_TRUE(c.set(n,"")); } EXPECT_FALSE(c.set("seventeen","")); }

// ---- prefix ----
TEST(PrefixRules, EachCharacterClass) {
  for (int c = 0; c < 256; c++) {
    std::string p = std::string("a") + (char)(c ? c : 'x') + "b";
    if (c == 0) continue;                       // NUL cannot be inside a C string
    bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '/';
    EXPECT_EQ(botMqttPrefixValid(p.c_str()), ok) << c;
  }
}
TEST(PrefixRules, SlashRules) {
  EXPECT_TRUE(botMqttPrefixValid("a/b/c")); EXPECT_TRUE(botMqttPrefixValid("A_b-1/x_y"));
  EXPECT_FALSE(botMqttPrefixValid("/")); EXPECT_FALSE(botMqttPrefixValid("//")); EXPECT_FALSE(botMqttPrefixValid("a/b/"));
  EXPECT_FALSE(botMqttPrefixValid("/a/b")); EXPECT_FALSE(botMqttPrefixValid("a///b")); EXPECT_FALSE(botMqttPrefixValid("a/b//"));
}
TEST(PrefixRules, WildcardsNullAndUtf8) {
  EXPECT_FALSE(botMqttPrefixValid(nullptr)); EXPECT_FALSE(botMqttPrefixValid("#")); EXPECT_FALSE(botMqttPrefixValid("+")); EXPECT_FALSE(botMqttPrefixValid("a#")); EXPECT_FALSE(botMqttPrefixValid("a+b"));
  EXPECT_FALSE(botMqttPrefixValid("caf\xC3\xA9")); EXPECT_FALSE(botMqttPrefixValid("a.b")); EXPECT_FALSE(botMqttPrefixValid("a\tb"));
  EXPECT_TRUE(botMqttPrefixValid("a")); EXPECT_TRUE(botMqttPrefixValid("0"));
}
TEST(PrefixDefault, EdgeCases) {
  char b[80];
  EXPECT_EQ(botMqttDefaultPrefix(b,80,""),12u);    EXPECT_STREQ(b,"meshcore/bot");
  EXPECT_EQ(botMqttDefaultPrefix(b,80,nullptr),12u); EXPECT_STREQ(b,"meshcore/bot");
  botMqttDefaultPrefix(b,80,"!!! ///"); EXPECT_STREQ(b,"meshcore/bot");
  botMqttDefaultPrefix(b,80,"Mesh_Bot-2"); EXPECT_STREQ(b,"meshcore/Mesh_Bot-2");
  botMqttDefaultPrefix(b,80,"a/b#c+d"); EXPECT_STREQ(b,"meshcore/abcd");
  EXPECT_EQ(botMqttDefaultPrefix(b,80,std::string(100,'a').c_str()),(size_t)BOT_MQTT_PREFIX_MAX); EXPECT_TRUE(botMqttPrefixValid(b));
  EXPECT_EQ(strlen(b),(size_t)BOT_MQTT_PREFIX_MAX);
}
TEST(PrefixDefault, SmallBuffers) {
  char b[80]; memset(b,'z',80);
  EXPECT_EQ(botMqttDefaultPrefix(b,0,"Zed"),0u); EXPECT_EQ(b[0],'z');
  EXPECT_EQ(botMqttDefaultPrefix(b,10,"Zed"),0u); EXPECT_STREQ(b,"");       // no room for even one name character
  EXPECT_EQ(botMqttDefaultPrefix(b,11,"Zed"),10u); EXPECT_STREQ(b,"meshcore/Z");
  EXPECT_EQ(botMqttDefaultPrefix(b,13,""),12u);                              // "meshcore/bot" needs 13 bytes
  EXPECT_EQ(botMqttDefaultPrefix(b,12,""),11u); EXPECT_STREQ(b,"meshcore/bo");
  for (size_t cap = 1; cap < 80; cap++) { size_t n = botMqttDefaultPrefix(b,cap,"Mesh Bot"); EXPECT_LT(n,cap); EXPECT_EQ(strlen(b),n); if (n) { EXPECT_TRUE(botMqttPrefixValid(b)) << cap; } }
}

// ---- topic ----
TEST(TopicBounds, Exact) {
  char b[128]; std::string p(64,'a');
  ASSERT_TRUE(botMqttTopic(b,128,p.c_str(),std::string(31,'l').c_str(),nullptr)); EXPECT_EQ(strlen(b),96u);   // exactly the maximum
  EXPECT_FALSE(botMqttTopic(b,128,p.c_str(),std::string(32,'l').c_str(),nullptr)); EXPECT_STREQ(b,"");        // one over
  EXPECT_TRUE(botMqttTopic(b,97,p.c_str(),std::string(31,'l').c_str(),nullptr));
  EXPECT_FALSE(botMqttTopic(b,96,p.c_str(),std::string(31,'l').c_str(),nullptr)); EXPECT_STREQ(b,"");        // buffer one short
  ASSERT_TRUE(botMqttTopic(b,128,p.c_str(),"commands","abcdefghijklmnop")); EXPECT_EQ(strlen(b),64u+1+8+1+16);
}
TEST(TopicBounds, TailAndNulls) {
  char b[96];
  ASSERT_TRUE(botMqttTopic(b,96,"p","commandlist","")); EXPECT_STREQ(b,"p/commandlist");
  ASSERT_TRUE(botMqttTopic(b,96,"p","commandlist",nullptr)); EXPECT_STREQ(b,"p/commandlist");
  EXPECT_FALSE(botMqttTopic(b,96,nullptr,"status",nullptr)); EXPECT_STREQ(b,"");
  EXPECT_FALSE(botMqttTopic(b,96,"p",nullptr,nullptr));
  EXPECT_FALSE(botMqttTopic(b,96,"","status",nullptr));
  EXPECT_FALSE(botMqttTopic(b,96,"p","",nullptr));
  b[0] = 'x'; EXPECT_FALSE(botMqttTopic(b,0,"p","status",nullptr)); EXPECT_EQ(b[0],'x');
  EXPECT_FALSE(botMqttTopic(nullptr,96,"p","status",nullptr));
  for (size_t cap = 1; cap <= 12; cap++) { memset(b,'z',sizeof(b)); EXPECT_EQ(botMqttTopic(b,cap,"p","status","ab"), cap >= 12); if (cap < 12) EXPECT_STREQ(b,""); else EXPECT_STREQ(b,"p/status/ab"); }
}

// ---- classify ----
static BotMqttIn cls(const std::string& t, const char* prefix, char* tail, size_t cap) { return botMqttClassify(t.data(), t.size(), prefix, tail, cap); }
TEST(ClassifyRules, TailLengthAndCharacters) {
  char t[20];
  EXPECT_EQ(cls("p/reply/a",   "p",t,20),BMI_REPLY);
  EXPECT_EQ(cls("p/reply/abcdefghijklmnop","p",t,20),BMI_REPLY); EXPECT_STREQ(t,"abcdefghijklmnop");   // 16
  EXPECT_EQ(cls("p/reply/abcdefghijklmnopq","p",t,20),BMI_NONE); EXPECT_STREQ(t,"");                   // 17
  EXPECT_EQ(cls("p/commands/0123456789","p",t,20),BMI_COMMAND); EXPECT_STREQ(t,"0123456789");
  for (const char* x : {"p/reply/ab-c","p/reply/ab_c","p/reply/AB","p/reply/aB","p/reply/ab.c","p/reply/ab c","p/reply/ab+","p/reply/#","p/reply/+","p/reply/ab/","p/reply//ab","p/reply/\xC3\xA9"})
    EXPECT_EQ(cls(x,"p",t,20),BMI_NONE) << x;
}
TEST(ClassifyRules, WrongShape) {
  char t[20];
  for (const char* x : {"","p","p/","p/reply","p/commands","p/reply/","p/commands/","p/replyx/abc","p/Reply/abc","p/REPLY/abc","p/commandlist","p/status","p/request","p/commands/a/b","p//reply/abc","/p/reply/abc","p/reply/abc/","pp/reply/abc","q/reply/abc","p/reply/abc/commands/x","p/reply","p/commandsabc","preply/abc","p reply abc","p/commands:abc"})
    EXPECT_EQ(cls(x,"p",t,20),BMI_NONE) << x;
  EXPECT_EQ(cls("a/b/reply/abc","a/b",t,20),BMI_REPLY);
  EXPECT_EQ(cls("a/reply/abc","a/b",t,20),BMI_NONE);          // topic shorter than the prefix
  EXPECT_EQ(cls("a/b/reply/abc","a",t,20),BMI_NONE);          // a longer topic is not a match for a shorter prefix
}
TEST(ClassifyRules, NullsAndTailCap) {
  char t[20];
  EXPECT_EQ(botMqttClassify(nullptr,5,"p",t,20),BMI_NONE);
  EXPECT_EQ(botMqttClassify("p/reply/abc",0,"p",t,20),BMI_NONE);
  EXPECT_EQ(botMqttClassify("p/reply/abc",11,nullptr,t,20),BMI_NONE);
  EXPECT_EQ(botMqttClassify("p/reply/abc",11,"",t,20),BMI_NONE);
  EXPECT_EQ(botMqttClassify("p/reply/abc",11,"p",nullptr,20),BMI_NONE);
  EXPECT_EQ(botMqttClassify("p/reply/abc",11,"p",t,0),BMI_NONE);
  strcpy(t,"zz"); EXPECT_EQ(botMqttClassify("p/reply/8f2a00c1",16,"p",t,8),BMI_NONE); EXPECT_STREQ(t,"");   // tail needs 9 bytes
  EXPECT_EQ(botMqttClassify("p/reply/8f2a00c1",16,"p",t,9),BMI_REPLY); EXPECT_STREQ(t,"8f2a00c1");
  EXPECT_EQ(botMqttClassify("p/reply/abc",11,"p",t,1),BMI_NONE);
  EXPECT_EQ(botMqttClassify("p/reply/abc",11,"p",t,4),BMI_REPLY); EXPECT_STREQ(t,"abc");
}
TEST(ClassifyRules, EmbeddedNulAndLengthBound) {
  char t[20];
  const char a[] = "p/reply/ab\0cd";            EXPECT_EQ(botMqttClassify(a,sizeof(a)-1,"p",t,20),BMI_NONE);
  const char b[] = "p/rep\0y/abc";             EXPECT_EQ(botMqttClassify(b,sizeof(b)-1,"p",t,20),BMI_NONE);
  const char c[] = "p\0reply/abc";             EXPECT_EQ(botMqttClassify(c,sizeof(c)-1,"p",t,20),BMI_NONE);
  const char d[] = "p/reply/\0";               EXPECT_EQ(botMqttClassify(d,sizeof(d)-1,"p",t,20),BMI_NONE);
  const char e[] = "mesh\0ore/reply/abc";      EXPECT_EQ(botMqttClassify(e,sizeof(e)-1,"mesh",t,20),BMI_NONE);
  // the length, not the terminator, ends the topic
  EXPECT_EQ(botMqttClassify("p/reply/abcdef",10,"p",t,20),BMI_REPLY); EXPECT_STREQ(t,"ab");
  EXPECT_EQ(botMqttClassify("p/reply/abcdef",8,"p",t,20),BMI_NONE);
  EXPECT_EQ(botMqttClassify("p/reply/abcdef",9,"p",t,20),BMI_REPLY); EXPECT_STREQ(t,"a");
  // a 4 KB topic is rejected by the tail rule
  std::string big = "p/reply/" + std::string(4096,'a'); EXPECT_EQ(cls(big,"p",t,20),BMI_NONE);
  // exactly the topic's bytes are read: a buffer with no terminator at all
  std::vector<char> v(11); memcpy(v.data(),"p/reply/abc",11); EXPECT_EQ(botMqttClassify(v.data(),v.size(),"p",t,20),BMI_REPLY);
}

// ---- request ----
static BotMqttRequest sampleReq() {
  static std::string keep;
  BotMqttRequest r{}; r.id="8f2a00c1"; r.prefix="meshcore/MeshBot"; r.cmd="beer"; r.args="ipa"; r.sender="Dee"; r.sender_key=""; r.channel="#bot";
  r.path="a5,bc"; r.path_names="Hilltop,?"; r.time="12:04"; r.date="2026-11-20"; r.tz="America/Toronto"; r.bot="MeshBot"; r.scope="yyz"; r.uptime="3d 4h";
  return r;
}
TEST(RequestJson, KeyOrder) {
  BotMqttRequest r = sampleReq(); char b[1024]; ASSERT_GT(botMqttBuildRequest(b,1024,r),0u);
  JsonDocument d; ASSERT_FALSE(deserializeJson(d,b));
  const char* want[] = {"id","reply_to","cmd","args","sender","sender_key","sender_lat","sender_lon","dm","channel","hops","path","path_names","snr","rssi","time","date","tz","unix","bot","scope","uptime","bot_lat","bot_lon","max_len"};
  size_t i = 0; for (JsonPair kv : d.as<JsonObject>()) { ASSERT_LT(i,25u); EXPECT_STREQ(kv.key().c_str(),want[i]) << i; i++; }
  EXPECT_EQ(i,25u);
}
TEST(RequestJson, EveryStringEscaped) {
  const char* nasty = "q\"\\<x>\n\r\t\x01\x1f z \xC3\xA9 end";
  BotMqttRequest r{}; r.id="8f2a00c1"; r.prefix="meshcore/MeshBot"; r.cmd=nasty; r.args=nasty; r.sender=nasty; r.sender_key=nasty; r.channel=nasty; r.path=nasty; r.path_names=nasty; r.time=nasty; r.date=nasty; r.tz=nasty; r.bot=nasty; r.scope=nasty; r.uptime=nasty;
  char b[1024]; ASSERT_GT(botMqttBuildRequest(b,1024,r),0u);
  EXPECT_EQ(strchr(b,'\n'),nullptr); EXPECT_EQ(strchr(b,'\t'),nullptr); EXPECT_EQ(strchr(b,'<'),nullptr);
  JsonDocument d; ASSERT_FALSE(deserializeJson(d,b));
  for (const char* k : {"cmd","args","sender","sender_key","channel","path","path_names","time","date","tz","bot","scope","uptime"}) EXPECT_STREQ(d[k],nasty) << k;
  EXPECT_EQ(d.as<JsonObject>().size(),25u);
}
TEST(RequestJson, NullStringsAreEmpty) {
  BotMqttRequest r{}; r.id="8f2a00c1"; r.prefix="p"; char b[1024]; ASSERT_GT(botMqttBuildRequest(b,1024,r),0u);
  JsonDocument d; ASSERT_FALSE(deserializeJson(d,b)); EXPECT_STREQ(d["cmd"],""); EXPECT_STREQ(d["tz"],""); EXPECT_STREQ(d["reply_to"],"p/reply/8f2a00c1");
  BotMqttRequest z{}; EXPECT_EQ(botMqttBuildRequest(b,1024,z),0u); EXPECT_STREQ(b,"");   // no id and no prefix: no topic, no request
}
TEST(RequestJson, NumberFormatting) {
  BotMqttRequest r = sampleReq(); r.sender_lat=45.4215; r.sender_lon=-75.6972; r.bot_lat=-0.000001; r.bot_lon=179.999994; r.snr=-3.25f; r.rssi=-120; r.hops=64; r.dm=true; r.unix=4294967295u; r.max_len=0;
  char b[1024]; size_t n=botMqttBuildRequest(b,1024,r); ASSERT_GT(n,0u); EXPECT_EQ(strlen(b),n);
  EXPECT_NE(strstr(b,"\"sender_lat\":45.42150,"),nullptr); EXPECT_NE(strstr(b,"\"sender_lon\":-75.69720,"),nullptr);
  EXPECT_NE(strstr(b,"\"bot_lat\":0.00000,"),nullptr);                   // a tiny negative never prints as -0.00000
  EXPECT_NE(strstr(b,"\"bot_lon\":179.99999,"),nullptr);
  EXPECT_NE(strstr(b,"\"snr\":-3.25,"),nullptr); EXPECT_NE(strstr(b,"\"rssi\":-120,"),nullptr); EXPECT_NE(strstr(b,"\"hops\":64,"),nullptr);
  EXPECT_NE(strstr(b,"\"dm\":true,"),nullptr); EXPECT_NE(strstr(b,"\"unix\":4294967295,"),nullptr); EXPECT_NE(strstr(b,"\"max_len\":0}"),nullptr);
  r.snr=7.0f; r.sender_lat=0; r.sender_lon=0; r.bot_lat=0; r.bot_lon=0; r.dm=false; r.unix=0; ASSERT_GT(botMqttBuildRequest(b,1024,r),0u);
  EXPECT_NE(strstr(b,"\"snr\":7,"),nullptr); EXPECT_NE(strstr(b,"\"sender_lat\":0.00000,"),nullptr); EXPECT_NE(strstr(b,"\"dm\":false,"),nullptr); EXPECT_NE(strstr(b,"\"unix\":0,"),nullptr);
  r.snr=0.5f; ASSERT_GT(botMqttBuildRequest(b,1024,r),0u); EXPECT_NE(strstr(b,"\"snr\":0.5,"),nullptr);
  r.snr=-0.25f; ASSERT_GT(botMqttBuildRequest(b,1024,r),0u); EXPECT_NE(strstr(b,"\"snr\":-0.25,"),nullptr);
  r.snr=(float)NAN; r.sender_lat=NAN; r.bot_lat=INFINITY; ASSERT_GT(botMqttBuildRequest(b,1024,r),0u);
  JsonDocument d; ASSERT_FALSE(deserializeJson(d,b));                    // never "nan" or "inf": still valid JSON
  EXPECT_NEAR(d["snr"].as<double>(),0,1e-9); EXPECT_NEAR(d["sender_lat"].as<double>(),0,1e-9);
}
static BotMqttRequest bigReq(std::string& names, std::string& path, std::string& args) {
  BotMqttRequest r = sampleReq(); r.path_names=names.c_str(); r.path=path.c_str(); r.args=args.c_str(); return r;
}
TEST(RequestShorten, NothingWhenItFits) {
  std::string n(100,'n'), p(100,'p'), a(100,'a'); BotMqttRequest r = bigReq(n,p,a); char b[1100]; size_t k = botMqttBuildRequest(b,1100,r); ASSERT_GT(k,0u);
  JsonDocument d; ASSERT_FALSE(deserializeJson(d,b)); EXPECT_STREQ(d["path_names"],n.c_str()); EXPECT_STREQ(d["path"],p.c_str()); EXPECT_STREQ(d["args"],a.c_str());
}
TEST(RequestShorten, StageOneEmptiesPathNamesOnly) {
  std::string n(800,'n'), p(20,'p'), a(50,'a'); BotMqttRequest r = bigReq(n,p,a); char b[1100]; size_t k = botMqttBuildRequest(b,1100,r); ASSERT_GT(k,0u); EXPECT_LE(k,1000u);
  JsonDocument d; ASSERT_FALSE(deserializeJson(d,b)); EXPECT_STREQ(d["path_names"],""); EXPECT_STREQ(d["path"],p.c_str()); EXPECT_STREQ(d["args"],a.c_str());
}
TEST(RequestShorten, StageTwoEmptiesPathToo) {
  std::string n(100,'n'), p(800,'p'), a(50,'a'); BotMqttRequest r = bigReq(n,p,a); char b[1100]; size_t k = botMqttBuildRequest(b,1100,r); ASSERT_GT(k,0u); EXPECT_LE(k,1000u);
  JsonDocument d; ASSERT_FALSE(deserializeJson(d,b)); EXPECT_STREQ(d["path_names"],""); EXPECT_STREQ(d["path"],""); EXPECT_STREQ(d["args"],a.c_str());
}
TEST(RequestShorten, StageThreeCutsArgsJustEnough) {
  std::string n(10,'n'), p(10,'p'), a(1500,'x'); BotMqttRequest r = bigReq(n,p,a); char b[1100]; size_t k = botMqttBuildRequest(b,1100,r); ASSERT_GT(k,0u); EXPECT_EQ(k,1000u);   // ASCII: cut to the byte
  JsonDocument d; ASSERT_FALSE(deserializeJson(d,b)); EXPECT_STREQ(d["path_names"],""); EXPECT_STREQ(d["path"],"");
  size_t al = strlen(d["args"]); EXPECT_GT(al,0u); EXPECT_LT(al,1500u); EXPECT_EQ(std::string(d["args"].as<const char*>()), std::string(al,'x'));
}
TEST(RequestShorten, StageThreeCutsOnUtf8Boundary) {
  std::string n, p, a; for (int i = 0; i < 1000; i++) a += "\xC3\xA9"; BotMqttRequest r = bigReq(n,p,a); char b[1100]; size_t k = botMqttBuildRequest(b,1100,r); ASSERT_GT(k,0u); EXPECT_LE(k,1000u); EXPECT_GE(k,998u);
  JsonDocument d; ASSERT_FALSE(deserializeJson(d,b)); std::string got = d["args"].as<const char*>(); ASSERT_EQ(got.size()%2,0u);
  for (size_t i = 0; i < got.size(); i += 2) { ASSERT_EQ((unsigned char)got[i],0xC3u); ASSERT_EQ((unsigned char)got[i+1],0xA9u); }
  std::string e4; for (int i = 0; i < 400; i++) e4 += "\xF0\x9F\xA4\x96"; BotMqttRequest r2 = bigReq(n,p,e4); ASSERT_GT(botMqttBuildRequest(b,1100,r2),0u);
  JsonDocument d2; ASSERT_FALSE(deserializeJson(d2,b)); std::string g2 = d2["args"].as<const char*>(); ASSERT_EQ(g2.size()%4,0u); EXPECT_EQ(g2.compare(0,4,"\xF0\x9F\xA4\x96"),0);
}
TEST(RequestShorten, EscapeHeavyArgsStillValid) {
  std::string n, p, a(900,'"'); BotMqttRequest r = bigReq(n,p,a); char b[1100]; size_t k = botMqttBuildRequest(b,1100,r); ASSERT_GT(k,0u); EXPECT_LE(k,1000u);
  JsonDocument d; ASSERT_FALSE(deserializeJson(d,b)); EXPECT_GT(strlen(d["args"]),250u);
}
TEST(RequestShorten, SmallCapBehavesLikeTheLimit) {
  std::string n(10,'n'), p(10,'p'), a(1500,'x'); BotMqttRequest r = bigReq(n,p,a); char b[1100];
  for (size_t cap : {500u, 700u, 999u, 1001u}) { size_t k = botMqttBuildRequest(b,cap,r); ASSERT_GT(k,0u) << cap; EXPECT_LT(k,cap); EXPECT_EQ(strlen(b),k); JsonDocument d; ASSERT_FALSE(deserializeJson(d,b)) << cap; }
}
TEST(RequestShorten, ReturnsZeroWhenItCannot) {
  std::string big(1200,'s'); BotMqttRequest r = sampleReq(); r.sender = big.c_str(); char b[1100]; memset(b,'z',sizeof(b));
  EXPECT_EQ(botMqttBuildRequest(b,1100,r),0u); EXPECT_STREQ(b,"");
  BotMqttRequest s = sampleReq(); EXPECT_EQ(botMqttBuildRequest(b,100,s),0u); EXPECT_STREQ(b,"");      // buffer too small even with nothing to shorten
  EXPECT_EQ(botMqttBuildRequest(b,0,s),0u); EXPECT_EQ(botMqttBuildRequest(nullptr,1100,s),0u);
  std::string longprefix(900,'p'); BotMqttRequest t = sampleReq(); t.prefix = longprefix.c_str(); EXPECT_EQ(botMqttBuildRequest(b,1100,t),0u);   // reply_to alone is too long
  BotMqttRequest u = sampleReq(); char c[1024]; size_t full = botMqttBuildRequest(c,1024,u); ASSERT_GT(full,0u);
  size_t shorter = botMqttBuildRequest(b,full,u); EXPECT_GT(shorter,0u); EXPECT_LT(shorter,full);   // one byte short: path names go, the request still fits
  EXPECT_EQ(botMqttBuildRequest(b,full+1,u),full);
}

// ---- status ----
TEST(Status, OnlineAndOffline) {
  char b[200]; size_t n = botMqttBuildStatus(b,200,true,"1.2.3","MeshBot",4294967295u); ASSERT_GT(n,0u); EXPECT_EQ(strlen(b),n);
  EXPECT_STREQ(b,"{\"state\":\"online\",\"version\":\"1.2.3\",\"bot\":\"MeshBot\",\"uptime_s\":4294967295}");
  n = botMqttBuildStatus(b,200,false,"ignored","ignored",5); EXPECT_EQ(n,19u); EXPECT_STREQ(b,"{\"state\":\"offline\"}");
  botMqttBuildStatus(b,200,true,"v\"1","a\\b\n",0); JsonDocument d; ASSERT_FALSE(deserializeJson(d,b)); EXPECT_STREQ(d["version"],"v\"1"); EXPECT_STREQ(d["bot"],"a\\b\n"); EXPECT_EQ(d["uptime_s"].as<int>(),0);
  botMqttBuildStatus(b,200,true,nullptr,nullptr,1); EXPECT_STREQ(b,"{\"state\":\"online\",\"version\":\"\",\"bot\":\"\",\"uptime_s\":1}");
}
TEST(Status, NeverOverflows) {
  char full[200]; size_t fn = botMqttBuildStatus(full,200,true,"1.2.3","MeshBot",77); char b[200];
  for (size_t cap = 1; cap < fn + 3; cap++) { memset(b,'z',sizeof(b)); size_t n = botMqttBuildStatus(b,cap,true,"1.2.3","MeshBot",77); if (cap > fn) { EXPECT_EQ(n,fn); EXPECT_STREQ(b,full); } else { EXPECT_EQ(n,0u); EXPECT_STREQ(b,""); } EXPECT_EQ(b[cap < 199 ? cap : 199],'z') << cap; }
  EXPECT_EQ(botMqttBuildStatus(b,18,false,nullptr,nullptr,0),0u); EXPECT_EQ(botMqttBuildStatus(b,19,false,nullptr,nullptr,0),0u); EXPECT_EQ(botMqttBuildStatus(b,20,false,nullptr,nullptr,0),19u);
  EXPECT_EQ(botMqttBuildStatus(b,0,false,nullptr,nullptr,0),0u); EXPECT_EQ(botMqttBuildStatus(nullptr,20,false,nullptr,nullptr,0),0u);
}

// ---- reply parser ----
static bool rp(const std::string& s, char* t, size_t cap) { return botMqttParseReply(s.data(), s.size(), t, cap); }
TEST(ReplyParse, WrongTypesAndShapes) {
  char t[201];
  for (const char* x : {"{\"text\":null}","{\"text\":true}","{\"text\":1.5}","{\"text\":[\"a\"]}","{\"text\":{\"a\":\"b\"}}","{\"Text\":\"a\"}","{\"text \":\"a\"}","[{\"text\":\"a\"}]","\"text\"","5","true","{}","{\"text\"}","{\"text\":\"a\"","{\"text\":\"a\",}","\xEF\xBB\xBF{\"text\":\"a\"}","{\"text\":\"a\\u0000b\"}"})
    EXPECT_FALSE(rp(x,t,201)) << x;
  EXPECT_STREQ(t,"");
}
TEST(ReplyParse, AcceptsAndIgnoresExtraKeys) {
  char t[201]; ASSERT_TRUE(rp("{\"id\":\"x\",\"text\":\"ok\",\"meta\":{\"a\":[1,2]}}",t,201)); EXPECT_STREQ(t,"ok");
  ASSERT_TRUE(rp("{\"text\":\"caf\\u00e9 \xF0\x9F\xA4\x96\"}",t,201)); EXPECT_STREQ(t,"caf\xC3\xA9 \xF0\x9F\xA4\x96");
}
TEST(ReplyParse, CleaningAndTrimming) {
  char t[201];
  ASSERT_TRUE(rp("{\"text\":\"\\t  a\\r\\nb\\u0001c\\u007fd  \\n\"}",t,201)); EXPECT_STREQ(t,"a  b c d");
  EXPECT_FALSE(rp("{\"text\":\"\\n\\t\\r\\u0001\\u007f \"}",t,201)); EXPECT_STREQ(t,"");
  ASSERT_TRUE(rp("{\"text\":\"  x\"}",t,201)); EXPECT_STREQ(t,"x");
}
TEST(ReplyParse, LengthLimits) {
  char t[300];
  ASSERT_TRUE(rp("{\"text\":\""+std::string(200,'a')+"\"}",t,300)); EXPECT_EQ(strlen(t),200u);
  EXPECT_FALSE(rp("{\"text\":\""+std::string(201,'a')+"\"}",t,300));
  ASSERT_TRUE(rp("{\"text\":\"   "+std::string(200,'a')+"   \"}",t,300)); EXPECT_EQ(strlen(t),200u);   // the limit is on the cleaned text
  EXPECT_FALSE(rp("{\"text\":\"   "+std::string(201,'a')+"\"}",t,300));
  ASSERT_TRUE(rp("{\"text\":\""+std::string(100,'\xC3')+"\"}",t,300)); EXPECT_EQ(strlen(t),100u);         // bytes, not characters
  char s[10]; EXPECT_FALSE(rp("{\"text\":\"1234567890\"}",s,10)); EXPECT_STREQ(s,"");                       // does not fit the caller's buffer: invalid, not cut
  ASSERT_TRUE(rp("{\"text\":\"123456789\"}",s,10)); EXPECT_STREQ(s,"123456789");
  EXPECT_FALSE(botMqttParseReply("{\"text\":\"a\"}",12,t,0)); EXPECT_FALSE(botMqttParseReply("{\"text\":\"a\"}",12,nullptr,300));
}
TEST(ReplyParse, PayloadLength) {
  char t[201]; std::string ok = "{\"text\":\"a\"}"; std::string p512 = ok + std::string(512 - ok.size(),' '); ASSERT_EQ(p512.size(),512u);
  EXPECT_TRUE(rp(p512,t,201)); EXPECT_STREQ(t,"a");
  EXPECT_FALSE(rp(p512+" ",t,201));
  EXPECT_FALSE(rp(std::string(1u<<20,' '),t,201)); EXPECT_FALSE(rp("",t,201)); EXPECT_FALSE(botMqttParseReply(nullptr,5,t,201)); EXPECT_FALSE(botMqttParseReply(nullptr,0,t,201));
  char raw[40]; memset(raw,'z',40); memcpy(raw,"{\"text\":\"abc\"}",14); ASSERT_TRUE(botMqttParseReply(raw,14,t,201)); EXPECT_STREQ(t,"abc");     // length decides, not a terminator
  EXPECT_FALSE(botMqttParseReply(raw,13,t,201));                                                                                                   // cut short: invalid
}
TEST(ReplyParse, HostileNesting) {
  char t[201]; std::string d;
  for (int n : {5,6,10,50,200,500}) { d = std::string(n,'[') + std::string(n,']'); EXPECT_FALSE(rp(d,t,201)) << n; }
  for (int n : {5,10,50,200}) { d = "{\"text\":\"a\",\"x\":"; for (int i=0;i<n;i++) d += "{\"a\":"; d += "1"; for (int i=0;i<n;i++) d += "}"; d += "}"; EXPECT_FALSE(rp(d,t,201)) << n; }
  d = "{\"text\":\"a\",\"x\":{\"y\":1}}"; EXPECT_TRUE(rp(d,t,201));
  d = "{\"x\":["; for (int i=0;i<100;i++) d += "[";  EXPECT_FALSE(rp(d,t,201));
  d = std::string(500,'{'); EXPECT_FALSE(rp(d,t,201)); d = std::string(500,'"'); EXPECT_FALSE(rp(d,t,201));
}
TEST(ReplyParse, NestingLimitIsFour) {
  char t[201];
  EXPECT_TRUE(rp("{\"text\":\"a\",\"x\":{\"y\":{\"z\":1}}}",t,201));              // 3 levels
  EXPECT_TRUE(rp("{\"text\":\"a\",\"x\":{\"y\":{\"z\":{}}}}",t,201));             // 4 levels
  EXPECT_FALSE(rp("{\"text\":\"a\",\"x\":{\"y\":{\"z\":{\"w\":{}}}}}",t,201));    // 5 levels
}

// ---- announce parser ----
static bool ap(const std::string& s, char* d, size_t cap, bool* rm) { return botMqttParseAnnounce(s.data(), s.size(), d, cap, rm); }
TEST(AnnounceParse, RemoveAndLengths) {
  char d[49]; bool rm = false;
  strcpy(d,"zz"); ASSERT_TRUE(botMqttParseAnnounce(nullptr,0,d,49,&rm)); EXPECT_TRUE(rm); EXPECT_STREQ(d,"");
  rm = false; ASSERT_TRUE(botMqttParseAnnounce("garbage",0,d,49,&rm)); EXPECT_TRUE(rm);
  rm = true; ASSERT_TRUE(ap("{\"desc\":\"x\"}",d,49,&rm)); EXPECT_FALSE(rm);                        // an earlier true is overwritten
  std::string ok = "{\"desc\":\"x\"}"; ASSERT_TRUE(ap(ok+std::string(256-ok.size(),' '),d,49,&rm)); EXPECT_STREQ(d,"x");
  EXPECT_FALSE(ap(ok+std::string(257-ok.size(),' '),d,49,&rm)); EXPECT_FALSE(rm); EXPECT_STREQ(d,"");
  EXPECT_FALSE(botMqttParseAnnounce(nullptr,5,d,49,&rm));
  EXPECT_FALSE(ap(std::string(1u<<20,'{'),d,49,&rm));
  ASSERT_TRUE(ap("{}",nullptr,0,nullptr)); ASSERT_TRUE(botMqttParseAnnounce("",0,nullptr,0,nullptr));
  char raw[30]; memset(raw,'z',30); memcpy(raw,"{\"desc\":\"abc\"}",14); ASSERT_TRUE(botMqttParseAnnounce(raw,14,d,49,&rm)); EXPECT_STREQ(d,"abc");
}
TEST(AnnounceParse, WrongTypes) {
  char d[49]; bool rm = false;
  for (const char* x : {"[1]","[]","null","5","true","\"x\"","{","{\"desc\":5}","{\"desc\":null}","{\"desc\":[]}","{\"desc\":{}}","{\"desc\":true}","{\"desc\":\"a\\u0000b\"}","not json"})
    { EXPECT_FALSE(ap(x,d,49,&rm)) << x; EXPECT_FALSE(rm); EXPECT_STREQ(d,""); }
  EXPECT_FALSE(ap(std::string(6,'[')+std::string(6,']'),d,49,&rm));
  EXPECT_FALSE(ap("{\"desc\":\"a\",\"x\":{\"y\":{\"z\":{\"w\":{}}}}}",d,49,&rm));
}
TEST(AnnounceParse, DescCleaningAndCut) {
  char d[49]; bool rm = false;
  ASSERT_TRUE(ap("{\"other\":1}",d,49,&rm)); EXPECT_STREQ(d,"");
  ASSERT_TRUE(ap("{\"desc\":\"a\\tb\\nc\\u0001d\"}",d,49,&rm)); EXPECT_STREQ(d,"a b c d");
  ASSERT_TRUE(ap("{\"desc\":\""+std::string(100,'a')+"\"}",d,49,&rm)); EXPECT_EQ(strlen(d),48u);          // cut, not refused
  std::string e; for (int i=0;i<30;i++) e += "\xC3\xA9"; ASSERT_TRUE(ap("{\"desc\":\""+e+"\"}",d,49,&rm)); EXPECT_EQ(strlen(d),48u);
  ASSERT_TRUE(ap("{\"desc\":\"a"+e+"\"}",d,49,&rm)); EXPECT_EQ(strlen(d),47u); EXPECT_EQ((unsigned char)d[46],0xA9u);  // never half a character
  char s[10]; ASSERT_TRUE(ap("{\"desc\":\"0123456789abc\"}",s,10,&rm)); EXPECT_STREQ(s,"012345678");          // the caller's smaller buffer wins
  char big[200]; ASSERT_TRUE(ap("{\"desc\":\""+std::string(100,'a')+"\"}",big,200,&rm)); EXPECT_EQ(strlen(big),48u);
  ASSERT_TRUE(ap("{\"desc\":\"x\",\"junk\":[1,{\"a\":2}]}",d,49,&rm)); EXPECT_STREQ(d,"x");
}

// ---- waiting table ----
TEST(WaitTable, DuplicateIdRefused) {
  BotMqttWaitTable w; EXPECT_EQ(w.add("aaaaaaaa",10,1),0); EXPECT_EQ(w.add("aaaaaaaa",11,1),-1); EXPECT_EQ(w.add("aaaaaaaa",11,2),-1); EXPECT_EQ(w.count(),1);
  EXPECT_EQ(w.add("aaaaaaab",12,1),1); EXPECT_EQ(w.match("aaaaaaaa",1,1000,5000),0); EXPECT_EQ(w.add("aaaaaaaa",13,1),0);   // free again once answered; lowest free slot
}
TEST(WaitTable, BadIds) {
  BotMqttWaitTable w; EXPECT_EQ(w.add(nullptr,1,1),-1); EXPECT_EQ(w.add("",1,1),-1); EXPECT_EQ(w.add("123456789",1,1),-1); EXPECT_EQ(w.count(),0);
  EXPECT_EQ(w.add("1234567",1,1),0); EXPECT_EQ(w.match(nullptr,1,1000,5000),-1); EXPECT_EQ(w.match("",1,1000,5000),-1); EXPECT_EQ(w.match("12345678",1,1000,5000),-1); EXPECT_EQ(w.match("123456",1,1000,5000),-1); EXPECT_EQ(w.match("1234567x",1,1000,5000),-1); EXPECT_EQ(w.count(),1);
  EXPECT_EQ(w.match("1234567",1,1000,5000),0);
}
TEST(WaitTable, MatchIsExactAndCaseSensitive) {
  BotMqttWaitTable w; w.add("8f2a00c1",1,1); EXPECT_EQ(w.match("8F2A00C1",1,1000,5000),-1); EXPECT_EQ(w.match("8f2a00c1 ",1,1000,5000),-1); EXPECT_EQ(w.match("8f2a00c1",1,1000,5000),0);
}
TEST(WaitTable, GenerationMismatch) {
  BotMqttWaitTable w; w.add("aaaaaaaa",1,5);
  EXPECT_EQ(w.match("aaaaaaaa",4,1000,5000),-1); EXPECT_EQ(w.match("aaaaaaaa",6,1000,5000),-1); EXPECT_EQ(w.match("aaaaaaaa",0,1000,5000),-1); EXPECT_EQ(w.count(),1);   // a wrong generation does not consume the slot
  EXPECT_EQ(w.match("aaaaaaaa",5,1000,5000),0); EXPECT_EQ(w.match("aaaaaaaa",5,1000,5000),-1);
}
TEST(WaitTable, MatchedAndExpiredNeverMatchAgain) {
  BotMqttWaitTable w; w.add("aaaaaaaa",100,1); w.add("bbbbbbbb",100,1);
  int e = w.expire(10000,5000); ASSERT_GE(e,0); const char* gone = e == 0 ? "aaaaaaaa" : "bbbbbbbb"; EXPECT_EQ(w.match(gone,1,1000,5000),-1);
  e = w.expire(10000,5000); ASSERT_GE(e,0); EXPECT_EQ(w.match("aaaaaaaa",1,1000,5000),-1); EXPECT_EQ(w.match("bbbbbbbb",1,1000,5000),-1); EXPECT_EQ(w.expire(10000,5000),-1); EXPECT_EQ(w.count(),0);
}
TEST(WaitTable, ExpiryOrderAndBoundary) {
  BotMqttWaitTable w; w.add("aaaaaaa1",2000,1); w.add("aaaaaaa2",1000,1); w.add("aaaaaaa3",3000,1);
  EXPECT_EQ(w.expire(5999,5000),-1);                       // age 4999 < wait, 3999, 2999
  EXPECT_EQ(w.expire(6000,5000),1);                        // slot 1 (sent at 1000) is exactly wait_ms old
  EXPECT_EQ(w.expire(6000,5000),-1);
  EXPECT_EQ(w.expire(9000,5000),0);                        // oldest of the rest first
  EXPECT_EQ(w.expire(9000,5000),2); EXPECT_EQ(w.expire(9000,5000),-1); EXPECT_EQ(w.count(),0);
  EXPECT_EQ(w.expire(1,5000),-1);
}
TEST(WaitTable, OldestFirstAcrossRollover) {
  BotMqttWaitTable w; w.add("aaaaaaa1",0xFFFFFFF0u,1); w.add("aaaaaaa2",0xFFFFFF00u,1);       // slot 1 is older
  EXPECT_EQ(w.expire(0x2000u,5000),1); EXPECT_EQ(w.expire(0x2000u,5000),0);
}
TEST(WaitTable, FullClearAndUser) {
  BotMqttWaitTable w; for (int i = 0; i < 4; i++) { char id[9]; botMqttMakeId(id,100+i); EXPECT_EQ(w.add(id,1,1),i); }
  EXPECT_EQ(w.count(),4); EXPECT_EQ(w.add("ffffffff",1,1),-1);
  w.setUser(2,7); EXPECT_EQ(w.userOf(2),7); EXPECT_EQ(w.userOf(1),0);
  w.setUser(-1,3); w.setUser(4,3); EXPECT_EQ(w.userOf(-1),0); EXPECT_EQ(w.userOf(4),0);
  w.clear(); EXPECT_EQ(w.count(),0); char id[9]; botMqttMakeId(id,100); EXPECT_EQ(w.match(id,1,1000,5000),-1); EXPECT_EQ(w.add("ffffffff",1,1),0); EXPECT_EQ(w.userOf(0),0);
}
TEST(WaitTable, UserIsFreedWithTheSlot) {
  BotMqttWaitTable w; w.add("aaaaaaaa",1,1); w.setUser(0,9); EXPECT_EQ(w.match("aaaaaaaa",1,1000,5000),0); w.setUser(0,5); EXPECT_EQ(w.userOf(0),0);    // free slot: no storage
  EXPECT_EQ(w.add("bbbbbbbb",1,1),0); EXPECT_EQ(w.userOf(0),0);
}

// ---- rate limit ----
TEST(RateLimit, WrapsWithTheClock) {
  BotMqttRate r; uint32_t t0 = 0xFFFFFFF0u; for (int i = 0; i < 30; i++) EXPECT_TRUE(r.allow(t0 + i*10)) << i;
  EXPECT_FALSE(r.allow(t0 + 400)); EXPECT_FALSE(r.allow(t0 + 59999)); EXPECT_TRUE(r.allow(t0 + 60000));         // the first stamp ages out, across the wrap
  EXPECT_FALSE(r.allow(t0 + 60001));
}
TEST(RateLimit, RefusedAttemptsAreNotRecorded) {
  BotMqttRate r; ASSERT_TRUE(r.allow(1000)); for (int i = 0; i < 29; i++) ASSERT_TRUE(r.allow(5000)); for (int i = 0; i < 100; i++) EXPECT_FALSE(r.allow(5001 + i));
  EXPECT_TRUE(r.allow(61000));                          // only the stamp from 1000 expired: exactly one slot again
  EXPECT_FALSE(r.allow(61000));
}
TEST(RateLimit, Sliding) {
  BotMqttRate r; for (int i = 0; i < 30; i++) ASSERT_TRUE(r.allow(i*1000)); EXPECT_FALSE(r.allow(29999)); EXPECT_TRUE(r.allow(60000)); EXPECT_FALSE(r.allow(60000)); EXPECT_TRUE(r.allow(61000));
}
TEST(Id, HexForm) {
  char id[9]; memset(id,'z',9); botMqttMakeId(id,0); EXPECT_STREQ(id,"00000000"); botMqttMakeId(id,0xFFFFFFFFu); EXPECT_STREQ(id,"ffffffff"); botMqttMakeId(id,0xABCDEF01u); EXPECT_STREQ(id,"abcdef01"); botMqttMakeId(id,0x0000000Au); EXPECT_STREQ(id,"0000000a");
  botMqttMakeId(nullptr,1);
}

// ---- announced descriptions as stored ----
// Stored descriptions need no JSON escaping: '"' -> '\'', '\\' -> '/', '<' -> '(' (control
// characters already become spaces), so the page counts them at one byte a byte.
TEST(DescClean, NothingLeftToEscape) {
  char d[49];
  size_t n = botMqttCleanDesc(d, sizeof d, "say \"hi\" <b>c:\\dir\\</b>\t\x7F" "end");
  EXPECT_EQ(n, strlen(d));
  EXPECT_STREQ(d, "say 'hi' (b>c:/dir/(/b>  end");
  EXPECT_TRUE(botMqttDescSafe(d));
  EXPECT_FALSE(botMqttDescSafe("a\"b")); EXPECT_FALSE(botMqttDescSafe("a\\b")); EXPECT_FALSE(botMqttDescSafe("a<b"));
  EXPECT_FALSE(botMqttDescSafe("a\nb")); EXPECT_FALSE(botMqttDescSafe("\x1f"));
  EXPECT_TRUE(botMqttDescSafe("caf\xC3\xA9 > 'x' / (y) &amp;")); EXPECT_TRUE(botMqttDescSafe(nullptr)); EXPECT_TRUE(botMqttDescSafe(""));
  // Every byte value: the cleaned text is safe, the same length (one byte per byte) and escapes to itself.
  for (int c = 1; c < 256; c++) {
    char in[4] = {'a', (char)c, 'z', 0}, out[8], esc[64];
    size_t n = botMqttCleanDesc(out, sizeof out, in);
    EXPECT_EQ(n, 3u) << c;
    EXPECT_TRUE(botMqttDescSafe(out)) << c;
    EXPECT_EQ(botJsonEscapedLen(out), n + 2) << c;
    EXPECT_EQ(botJsonEscape(esc, sizeof esc, out), n + 2) << c;
  }
  // Cut on a character boundary as before (48 bytes of 49).
  std::string q(60, '"'); EXPECT_EQ(botMqttCleanDesc(d, sizeof d, q.c_str()), 48u); EXPECT_EQ(std::string(d), std::string(48, '\''));
  EXPECT_EQ(botMqttCleanDesc(d, sizeof d, nullptr), 0u); EXPECT_STREQ(d, "");
}
TEST(DescClean, AnnouncementAndTableStoreTheCleanText) {
  char d[49]; bool rm = false;
  ASSERT_TRUE(ap("{\"desc\":\"tide \\\"now\\\" <b> c:\\\\x\"}", d, 49, &rm)); EXPECT_STREQ(d, "tide 'now' (b> c:/x");
  BotMqttCommands c;
  ASSERT_TRUE(c.set("tide", "tide \"now\" <b> c:\\x"));   // the table cleans whatever it is given
  EXPECT_STREQ(c.find("tide"), "tide 'now' (b> c:/x");
  EXPECT_TRUE(botMqttDescSafe(c.descAt(0)));
  // The same text goes into the published list unescaped.
  const char* names[] = {"tide"}; const char* descs[] = {c.descAt(0)}; const char* srcs[] = {"mqtt"};
  char b[200];
  ASSERT_GT(botMqttBuildCommandList(b, sizeof b, names, descs, srcs, 1), 0u);
  EXPECT_STREQ(b, "{\"commands\":[{\"name\":\"tide\",\"desc\":\"tide 'now' (b> c:/x\",\"source\":\"mqtt\"}]}");
}

// ---- announced commands ----
TEST(CommandsTable, NamesAndDescriptions) {
  BotMqttCommands c;
  EXPECT_FALSE(c.set(nullptr,"x")); EXPECT_FALSE(c.set("","x")); EXPECT_FALSE(c.set("Beer","x")); EXPECT_FALSE(c.set("be-er","x")); EXPECT_FALSE(c.set("be er","x")); EXPECT_FALSE(c.set("be_er","x")); EXPECT_FALSE(c.set("caf\xC3\xA9","x")); EXPECT_FALSE(c.set("!beer","x"));
  EXPECT_FALSE(c.set("abcdefghijklmnopq","x")); EXPECT_EQ(c.count(),0);
  EXPECT_TRUE(c.set("abcdefghijklmnop","x")); EXPECT_TRUE(c.set("0123456789","x")); EXPECT_EQ(c.count(),2);
  EXPECT_TRUE(c.set("nodesc",nullptr)); EXPECT_STREQ(c.find("nodesc"),"");
  EXPECT_TRUE(c.set("clean","a\tb\nc\x01" "d")); EXPECT_STREQ(c.find("clean"),"a b c d");
  EXPECT_TRUE(c.set("cut",std::string(100,'a').c_str())); EXPECT_EQ(strlen(c.find("cut")),48u);
  std::string e; for (int i=0;i<30;i++) e += "\xC3\xA9"; EXPECT_TRUE(c.set("utf",e.c_str())); EXPECT_EQ(strlen(c.find("utf")),48u);
  EXPECT_TRUE(c.set("utf2",("a"+e).c_str())); EXPECT_EQ(strlen(c.find("utf2")),47u);
  EXPECT_EQ(c.find(nullptr),nullptr); EXPECT_EQ(c.find(""),nullptr); EXPECT_EQ(c.find("Clean"),nullptr); EXPECT_EQ(c.find("clea"),nullptr); EXPECT_EQ(c.find("cleanx"),nullptr);
}
TEST(CommandsTable, ReplacementRemovalAndOrder) {
  BotMqttCommands c; c.set("a","1"); c.set("b","2"); c.set("c","3");
  EXPECT_TRUE(c.set("b","two")); EXPECT_EQ(c.count(),3); EXPECT_STREQ(c.nameAt(1),"b"); EXPECT_STREQ(c.descAt(1),"two");
  EXPECT_FALSE(c.remove("zz")); EXPECT_FALSE(c.remove(nullptr)); EXPECT_FALSE(c.remove("")); EXPECT_FALSE(c.remove("B"));
  EXPECT_TRUE(c.remove("a")); EXPECT_EQ(c.count(),2); EXPECT_STREQ(c.nameAt(0),"b"); EXPECT_STREQ(c.nameAt(1),"c"); EXPECT_STREQ(c.descAt(1),"3");
  EXPECT_FALSE(c.remove("a")); EXPECT_EQ(c.find("a"),nullptr);
  EXPECT_STREQ(c.nameAt(2),""); EXPECT_STREQ(c.descAt(2),""); EXPECT_STREQ(c.nameAt(-1),""); EXPECT_STREQ(c.descAt(-1),""); EXPECT_STREQ(c.nameAt(1000),""); EXPECT_STREQ(c.descAt(1000),"");
  c.clear(); EXPECT_EQ(c.count(),0); EXPECT_EQ(c.find("b"),nullptr); EXPECT_STREQ(c.nameAt(0),"");
}
TEST(CommandsTable, SixteenEntries) {
  BotMqttCommands c; char n[8]; for (int i=0;i<16;i++){ snprintf(n,8,"c%d",i); ASSERT_TRUE(c.set(n,"d")); }
  EXPECT_FALSE(c.set("extra","d")); EXPECT_EQ(c.count(),16);
  EXPECT_TRUE(c.set("c7","new")); EXPECT_STREQ(c.find("c7"),"new"); EXPECT_EQ(c.count(),16);   // replacing needs no free slot
  EXPECT_TRUE(c.remove("c3")); EXPECT_TRUE(c.set("extra","d")); EXPECT_EQ(c.count(),16); EXPECT_STREQ(c.nameAt(15),"extra"); EXPECT_FALSE(c.set("another","d"));
}

// ---- command list ----
TEST(CommandList, Shape) {
  const char* names[] = {"ping","beer"}; const char* descs[] = {"reply \"pong\"", nullptr}; const char* srcs[] = {"builtin", nullptr}; char b[300];
  size_t n = botMqttBuildCommandList(b,300,names,descs,srcs,2); ASSERT_GT(n,0u); EXPECT_EQ(strlen(b),n);
  EXPECT_STREQ(b,"{\"commands\":[{\"name\":\"ping\",\"desc\":\"reply \\\"pong\\\"\",\"source\":\"builtin\"},{\"name\":\"beer\",\"desc\":\"\",\"source\":\"\"}]}");
  JsonDocument d; ASSERT_FALSE(deserializeJson(d,b)); EXPECT_EQ(d["commands"].size(),2u); EXPECT_STREQ(d["commands"][0]["desc"],"reply \"pong\"");
}
TEST(CommandList, EmptyAndNulls) {
  char b[64]; EXPECT_EQ(botMqttBuildCommandList(b,64,nullptr,nullptr,nullptr,0),15u); EXPECT_STREQ(b,"{\"commands\":[]}");
  EXPECT_EQ(botMqttBuildCommandList(b,64,nullptr,nullptr,nullptr,3),15u); EXPECT_STREQ(b,"{\"commands\":[]}");
  const char* names[] = {"a"}; EXPECT_EQ(botMqttBuildCommandList(b,64,names,nullptr,nullptr,-1),15u); EXPECT_STREQ(b,"{\"commands\":[]}");
  const char* withNull[] = {nullptr,"b",""}; EXPECT_GT(botMqttBuildCommandList(b,64,withNull,nullptr,nullptr,3),15u);
  JsonDocument d; ASSERT_FALSE(deserializeJson(d,b)); EXPECT_EQ(d["commands"].size(),1u); EXPECT_STREQ(d["commands"][0]["name"],"b");   // entries without a name are skipped
  memset(b,'z',64); EXPECT_EQ(botMqttBuildCommandList(b,15,nullptr,nullptr,nullptr,0),0u); EXPECT_STREQ(b,"");
  EXPECT_EQ(botMqttBuildCommandList(b,16,nullptr,nullptr,nullptr,0),15u); EXPECT_EQ(botMqttBuildCommandList(b,0,nullptr,nullptr,nullptr,0),0u); EXPECT_EQ(botMqttBuildCommandList(nullptr,64,nullptr,nullptr,nullptr,0),0u);
}
TEST(CommandList, MoreWhenDropped) {
  const char* names[] = {"aaaa","bbbb","cccc"}; const char* descs[] = {"x","y","z"}; const char* srcs[] = {"custom","mqtt","builtin"};
  char full[300]; size_t fn = botMqttBuildCommandList(full,300,names,descs,srcs,3);
  JsonDocument d; ASSERT_FALSE(deserializeJson(d,full)); EXPECT_EQ(d["commands"].size(),3u); EXPECT_TRUE(d["more"].isNull());   // absent when complete
  EXPECT_EQ(strstr(full,"more"),nullptr);
  char b[300];
  for (size_t cap = 28; cap <= fn; cap++) {
    size_t k = botMqttBuildCommandList(b,cap,names,descs,srcs,3); ASSERT_GT(k,0u) << cap;
    JsonDocument e; ASSERT_FALSE(deserializeJson(e,b)) << cap << ": " << b;
    EXPECT_LT(e["commands"].size(),3u) << cap;
    EXPECT_TRUE(e["more"].as<bool>()) << cap << ": " << b;                                      // says it is incomplete
  }
  EXPECT_EQ(botMqttBuildCommandList(b,fn + 1,names,descs,srcs,3),fn);
  size_t k = botMqttBuildCommandList(b,20,names,descs,srcs,3); EXPECT_EQ(k,15u); EXPECT_STREQ(b,"{\"commands\":[]}");   // too small to say "more": still valid
}
TEST(CommandList, TruncationStaysValidJson) {
  std::vector<std::string> n, ds; std::vector<const char*> np, dp, sp;
  for (int i = 0; i < 16; i++) { n.push_back("cmd" + std::to_string(i)); ds.push_back("desc \"" + std::to_string(i) + "\" " + std::string(30,'d')); }
  for (int i = 0; i < 16; i++) { np.push_back(n[i].c_str()); dp.push_back(ds[i].c_str()); sp.push_back(i % 2 ? "mqtt" : "builtin"); }
  char full[4096]; size_t fn = botMqttBuildCommandList(full,4096,np.data(),dp.data(),sp.data(),16); ASSERT_GT(fn,0u);
  JsonDocument fd; ASSERT_FALSE(deserializeJson(fd,full)); ASSERT_EQ(fd["commands"].size(),16u);
  size_t last = 0; char b[4096];
  for (size_t cap = 1; cap <= fn + 2; cap++) {
    memset(b,'z',sizeof(b)); size_t k = botMqttBuildCommandList(b,cap,np.data(),dp.data(),sp.data(),16);
    ASSERT_EQ(b[cap < 4095 ? cap : 4095],'z') << cap;                           // nothing past the buffer
    if (cap < 16) { EXPECT_EQ(k,0u); EXPECT_STREQ(b,""); continue; }
    ASSERT_GT(k,0u) << cap; ASSERT_LT(k,cap); ASSERT_EQ(strlen(b),k);
    JsonDocument d; ASSERT_FALSE(deserializeJson(d,b)) << cap << ": " << b;
    size_t m = d["commands"].size(); ASSERT_LE(m,16u); EXPECT_GE(m,last); last = m;                      // more room never means fewer entries
    for (size_t i = 0; i < m; i++) { EXPECT_STREQ(d["commands"][i]["name"],n[i].c_str()); EXPECT_STREQ(d["commands"][i]["desc"],ds[i].c_str()); }   // always a leading run: dropped from the end
    if (cap > fn) { EXPECT_EQ(k,fn); }
    if (cap == fn) { EXPECT_LT(m,16u); }
  }
  EXPECT_EQ(last,16u);
}

// ---- 32-bit numbers, request ids and shortening, parse heap limits, late replies, the prefix ----
static std::string u32(uint32_t v) { char b[32]; size_t n = 0; EXPECT_TRUE(botMqttPutU32(b, sizeof(b), n, v)); return std::string(b, n); }
TEST(Unsigned32, PrintedAsUnsigned) {
  // The helper takes uint32_t and prints through unsigned long, so these hold on a 32-bit `long` too;
  // formatting through any signed 32-bit type would print them negative.
  EXPECT_EQ(u32(0u),"0"); EXPECT_EQ(u32(0x7FFFFFFFu),"2147483647"); EXPECT_EQ(u32(0x80000000u),"2147483648"); EXPECT_EQ(u32(0xFFFFFFFFu),"4294967295");
  char b[4]; size_t n = 0; EXPECT_FALSE(botMqttPutU32(b, 4, n, 0xFFFFFFFFu)); EXPECT_EQ(n,0u);
}
TEST(Unsigned32, InRequestAndStatus) {
  BotMqttRequest r = sampleReq(); char b[1024];
  for (uint32_t v : {0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFFu}) {
    r.unix = v; ASSERT_GT(botMqttBuildRequest(b,1024,r),0u); JsonDocument d; ASSERT_FALSE(deserializeJson(d,b)); EXPECT_EQ(d["unix"].as<uint32_t>(),v);
    EXPECT_NE(strstr(b,(std::string("\"unix\":")+std::to_string((unsigned long long)v)+",").c_str()),nullptr) << v;
    botMqttBuildStatus(b,200,true,"v","b",v); EXPECT_NE(strstr(b,(std::string("\"uptime_s\":")+std::to_string((unsigned long long)v)+"}").c_str()),nullptr) << v;
  }
}

TEST(RequestId, MustBeEightLowerHex) {
  char b[1024]; BotMqttRequest r = sampleReq();
  for (const char* id : {(const char*)nullptr,"","8f2a00c","8f2a00c10","8F2A00C1","8f2a00cg","8f2a 0c1","8f2a00c\"","--------"}) { r.id = id; memset(b,'z',16); EXPECT_EQ(botMqttBuildRequest(b,1024,r),0u) << (id ? id : "null"); EXPECT_STREQ(b,""); }
  r.id = "0123abcd"; EXPECT_GT(botMqttBuildRequest(b,1024,r),0u);
  r.prefix = nullptr; EXPECT_EQ(botMqttBuildRequest(b,1024,r),0u); r.prefix = ""; EXPECT_EQ(botMqttBuildRequest(b,1024,r),0u);
}
TEST(RequestId, ReplyToMatchesTheTopicBuilder) {
  char b[1024], t[BOT_MQTT_TOPIC_MAX + 1]; BotMqttRequest r = sampleReq();
  std::string p(BOT_MQTT_TOPIC_MAX - 15, 'p'); r.prefix = p.c_str(); ASSERT_GT(botMqttBuildRequest(b,1024,r),0u);       // reply_to is exactly 96 bytes
  ASSERT_TRUE(botMqttTopic(t,sizeof(t),r.prefix,"reply",r.id)); JsonDocument d; ASSERT_FALSE(deserializeJson(d,b)); EXPECT_STREQ(d["reply_to"],t); EXPECT_EQ(strlen(t),96u);
  p += "p"; r.prefix = p.c_str(); EXPECT_EQ(botMqttBuildRequest(b,1024,r),0u);                                          // 97: refused
}

TEST(RequestShorten, ExactAcrossAllArgsLengths) {
  // The cut is the longest slice that fits: one more byte (or character) would not fit.
  std::string n, p; char b[1100], c[1100];
  for (const char* unit : {"x", "\xC3\xA9", "\xE2\x82\xAC", "\xF0\x9F\xA4\x96", "\""}) {
    std::string a; for (int i = 0; i < 1200; i++) a += unit; BotMqttRequest r = bigReq(n,p,a);
    size_t k = botMqttBuildRequest(b,1100,r); ASSERT_GT(k,0u) << unit; ASSERT_LE(k,1000u);
    JsonDocument d; ASSERT_FALSE(deserializeJson(d,b)); std::string got = d["args"].as<const char*>(); ASSERT_EQ(got.size() % strlen(unit),0u) << unit; ASSERT_EQ(a.compare(0,got.size(),got),0);
    // with exactly one more character as the whole args, the result is cut to the same place: that character did not fit
    std::string more = a.substr(0, got.size() + strlen(unit)); BotMqttRequest r2 = bigReq(n,p,more);
    EXPECT_EQ(botMqttBuildRequest(c,1100,r2),k) << unit;
    EXPECT_GE(k + 6 * strlen(unit), 1000u) << unit;                                                                                   // within one character of the limit
  }
}
TEST(RequestShorten, ArgsCutAtEveryLimit) {
  std::string n(5,'n'), p(5,'p'); char b[1100];
  for (size_t alen : {0u,1u,2u,3u,500u,650u,700u,720u,760u,1000u,5000u}) {
    std::string a(alen,'x'); BotMqttRequest r = bigReq(n,p,a); size_t k = botMqttBuildRequest(b,1100,r); ASSERT_GT(k,0u) << alen; ASSERT_LE(k,1000u);
    JsonDocument d; ASSERT_FALSE(deserializeJson(d,b)) << alen; size_t got = strlen(d["args"]);
    if (got < alen) { EXPECT_EQ(k,1000u) << alen; } else { EXPECT_EQ(got,alen); }
  }
}

// Memory bound of the parsers. A device pool is 128 slots of 8 bytes = 1 KB and its limit 2 KB; the host's is
// 256 slots of 16 bytes = 4 KB, so the same "two pools" is 8 KB. The numbers are derived from the same
// macros the firmware uses, so on a 32-bit target these checks run against 2048 itself.
static std::string maxReply() { std::string d = "{\"text\":\"" + std::string(200,'a') + "\"}"; return d + std::string(BOT_MQTT_REPLY_IN_MAX - d.size(), ' '); }
TEST(ParseHeap, DeviceFigureAndScaling) {
  EXPECT_EQ((size_t)BOT_MQTT_PARSE_HEAP, (size_t)BOT_MQTT_PARSE_HEAP_DEVICE * (ARDUINOJSON_POOL_CAPACITY * 2u * sizeof(void*)) / BOT_MQTT_POOL_DEVICE);
  EXPECT_EQ((size_t)BOT_MQTT_PARSE_HEAP, 2u * BOT_MQTT_POOL_BYTES);                 // exactly two pools
  if (sizeof(void*) == 4) { EXPECT_EQ((size_t)BOT_MQTT_PARSE_HEAP, 2048u); }
}
TEST(ParseHeap, LargestValidReplyParsesUnderTheLimit) {
  std::string d = maxReply(); ASSERT_EQ(d.size(),512u); char t[201]; size_t peak = 0;
  ASSERT_TRUE(botMqttParseReplyLimited(d.data(),d.size(),t,201,BOT_MQTT_PARSE_HEAP,&peak)); EXPECT_EQ(strlen(t),200u);
  EXPECT_GT(peak,0u); EXPECT_LE(peak,(size_t)BOT_MQTT_PARSE_HEAP);
  EXPECT_TRUE(rp(d,t,201));
}
TEST(ParseHeap, TheLimitRefuses) {
  // Half the limit (one pool on the device) cannot hold even the largest valid reply: the allocator is what says no.
  std::string d = maxReply(); char t[201]; size_t peak = 99;
  EXPECT_FALSE(botMqttParseReplyLimited(d.data(),d.size(),t,201,BOT_MQTT_PARSE_HEAP / 2,&peak)); EXPECT_STREQ(t,""); EXPECT_LE(peak,(size_t)BOT_MQTT_PARSE_HEAP / 2);
  EXPECT_FALSE(botMqttParseReplyLimited(d.data(),d.size(),t,201,0,&peak)); EXPECT_LE(peak,0u);
  char a[49]; bool rm = true; EXPECT_FALSE(botMqttParseAnnounceLimited("{\"desc\":\"x\"}",12,a,49,&rm,BOT_MQTT_PARSE_HEAP / 2,&peak)); EXPECT_FALSE(rm); EXPECT_LE(peak,(size_t)BOT_MQTT_PARSE_HEAP / 2);
  EXPECT_TRUE(botMqttParseAnnounceLimited("{\"desc\":\"x\"}",12,a,49,&rm,BOT_MQTT_PARSE_HEAP,&peak)); EXPECT_STREQ(a,"x"); EXPECT_LE(peak,(size_t)BOT_MQTT_PARSE_HEAP);
}
TEST(ParseHeap, HostileDocumentsNeverExceedTheLimit) {
  std::vector<std::string> docs;
  std::string many = "{\"text\":\"a\""; for (int i = 0; many.size() < 500; i++) many += ",\"k" + std::to_string(i) + "\":0"; docs.push_back(many + "}");
  std::string strs = "{\"text\":\"a\""; for (int i = 0; strs.size() < 500; i++) strs += ",\"" + std::to_string(i) + "\":\"v\""; docs.push_back(strs + "}");
  std::string arr = "{\"text\":\"a\",\"x\":["; while (arr.size() < 508) arr += "0,"; docs.push_back(arr + "0]}");
  std::string nest = "{\"text\":\"a\",\"x\":[[[[0]]]]}"; docs.push_back(nest);
  std::string longkey = "{\"text\":\"a\",\"" + std::string(400,'k') + "\":1}"; docs.push_back(longkey);
  docs.push_back(maxReply());
  for (size_t limit : {(size_t)0, (size_t)64, (size_t)BOT_MQTT_PARSE_HEAP / 4, (size_t)BOT_MQTT_PARSE_HEAP / 2, (size_t)BOT_MQTT_PARSE_HEAP, (size_t)BOT_MQTT_PARSE_HEAP * 2})
    for (const std::string& d : docs) {
      ASSERT_LE(d.size(),512u); char t[201]; size_t peak = 0; bool ok = botMqttParseReplyLimited(d.data(),d.size(),t,201,limit,&peak);
      EXPECT_LE(peak,limit) << limit << " " << d.substr(0,30); if (limit <= BOT_MQTT_PARSE_HEAP / 2) { EXPECT_FALSE(ok); }
      char a[49]; bool rm = false; size_t p2 = 0; botMqttParseAnnounceLimited(d.data(),d.size() > 256 ? 256 : d.size(),a,49,&rm,limit,&p2); EXPECT_LE(p2,limit);
    }
  // Under the real limit these documents are parsed or refused; either way the bound above held.
}

TEST(AnnounceParse, KeyWithEmbeddedNulIsAnotherKey) {
  char d[49]; bool rm = false; const char p[] = "{\"desc\\u0000x\":\"hidden\",\"desc\":\"shown\"}";
  ASSERT_TRUE(botMqttParseAnnounce(p,sizeof(p)-1,d,49,&rm)); EXPECT_STREQ(d,"shown");
  const char q[] = "{\"desc\\u0000x\":\"hidden\"}"; ASSERT_TRUE(botMqttParseAnnounce(q,sizeof(q)-1,d,49,&rm)); EXPECT_STREQ(d,"");
}

TEST(WaitTable, LateReplyNeverMatches) {
  BotMqttWaitTable w; ASSERT_EQ(w.add("aaaaaaaa",1000,7),0);
  EXPECT_EQ(w.match("aaaaaaaa",7,1000,5000),0); ASSERT_EQ(w.add("aaaaaaaa",1000,7),0);                  // sent and answered at once
  EXPECT_EQ(w.match("aaaaaaaa",7,5999,5000),0); ASSERT_EQ(w.add("aaaaaaaa",1000,7),0);                  // 1 ms before the wait ends
  EXPECT_EQ(w.match("aaaaaaaa",7,6000,5000),-1); EXPECT_EQ(w.count(),1);                                // exactly the wait: late, slot kept
  EXPECT_EQ(w.match("aaaaaaaa",7,9000,5000),-1); EXPECT_EQ(w.count(),1);
  EXPECT_EQ(w.expire(6000,5000),0); EXPECT_EQ(w.count(),0); EXPECT_EQ(w.match("aaaaaaaa",7,6000,5000),-1); // expire() reports it as timed out, once
  EXPECT_EQ(w.expire(6000,5000),-1);
}
TEST(WaitTable, LateReplyAcrossTheWrap) {
  BotMqttWaitTable w; uint32_t sent = 0xFFFFF000u; ASSERT_EQ(w.add("bbbbbbbb",sent,1),0);
  uint32_t before = sent + 4999u, at = sent + 5000u;                                                    // before has wrapped past zero in the second case below
  EXPECT_EQ(w.match("bbbbbbbb",1,before,5000),0); ASSERT_EQ(w.add("bbbbbbbb",sent,1),0);
  EXPECT_LT(at,sent); EXPECT_EQ(w.match("bbbbbbbb",1,at,5000),-1); EXPECT_EQ(w.match("bbbbbbbb",1,at + 100,5000),-1);
  EXPECT_EQ(w.expire(at,5000),0); EXPECT_EQ(w.expire(at,5000),-1);
}
TEST(WaitTable, WrongGenerationNeverMatchesAndKeepsTheSlot) {
  BotMqttWaitTable w; ASSERT_EQ(w.add("cccccccc",100,3),0);
  for (uint32_t g : {0u,2u,4u,0xFFFFFFFFu}) EXPECT_EQ(w.match("cccccccc",g,200,5000),-1);
  EXPECT_EQ(w.count(),1); EXPECT_EQ(w.match("cccccccc",3,200,5000),0);
}

TEST(Prefix, Effective) {
  char b[BOT_MQTT_PREFIX_MAX + 1];
  EXPECT_EQ(botMqttEffectivePrefix(b, sizeof b, "meshcore/MeshBot", "Other"), 16u); EXPECT_STREQ(b, "meshcore/MeshBot");
  EXPECT_EQ(botMqttEffectivePrefix(b, sizeof b, "", "Mesh Bot!"), 16u);              EXPECT_STREQ(b, "meshcore/MeshBot");
  EXPECT_EQ(botMqttEffectivePrefix(b, sizeof b, nullptr, nullptr), 12u);             EXPECT_STREQ(b, "meshcore/bot");
  botMqttEffectivePrefix(b, sizeof b, "/bad", "Node1");                             EXPECT_STREQ(b, "meshcore/Node1");   // invalid: the default
  botMqttEffectivePrefix(b, sizeof b, "a+b", "Node1");                              EXPECT_STREQ(b, "meshcore/Node1");
  std::string p64(64, 'p');
  EXPECT_EQ(botMqttEffectivePrefix(b, sizeof b, p64.c_str(), "x"), 64u);            EXPECT_STREQ(b, p64.c_str());
  char small[8];
  EXPECT_EQ(botMqttEffectivePrefix(small, sizeof small, "meshcore/MeshBot", "x"), 0u); EXPECT_STREQ(small, "");
  EXPECT_EQ(botMqttEffectivePrefix(nullptr, 4, "a", "b"), 0u);
}

int main(int argc, char** argv) { ::testing::InitGoogleTest(&argc, argv); return RUN_ALL_TESTS(); }

// ---- replies that carry several messages ("more") ----
static bool rpart(const std::string& s, uint8_t part, char* t, size_t cap, uint8_t* parts = nullptr) {
  return botMqttParseReplyPartLimited(s.data(), s.size(), part, t, cap, BOT_MQTT_PARSE_HEAP, parts);
}
TEST(ReplyParts, TextAloneIsOnePart) {
  char t[201]; uint8_t n = 9;
  ASSERT_TRUE(rpart("{\"text\":\"one\"}", 0, t, 201, &n)); EXPECT_STREQ(t, "one"); EXPECT_EQ(n, 1);
  EXPECT_FALSE(rpart("{\"text\":\"one\"}", 1, t, 201, &n)); EXPECT_STREQ(t, ""); EXPECT_EQ(n, 1);
}
TEST(ReplyParts, MoreGivesEachPartCleanedAndCounted) {
  const std::string d = "{\"text\":\"one\",\"more\":[\" two\\n2 \",\"three\",\"four\"]}";
  char t[201]; uint8_t n = 0;
  ASSERT_TRUE(rpart(d, 0, t, 201, &n)); EXPECT_STREQ(t, "one"); EXPECT_EQ(n, 4);
  ASSERT_TRUE(rpart(d, 1, t, 201)); EXPECT_STREQ(t, "two 2");
  ASSERT_TRUE(rpart(d, 2, t, 201)); EXPECT_STREQ(t, "three");
  ASSERT_TRUE(rpart(d, 3, t, 201, &n)); EXPECT_STREQ(t, "four"); EXPECT_EQ(n, 4);
  EXPECT_FALSE(rpart(d, 4, t, 201)); EXPECT_STREQ(t, "");
  EXPECT_FALSE(rpart(d, 255, t, 201));
  ASSERT_TRUE(botMqttParseReply(d.data(), d.size(), t, 201)); EXPECT_STREQ(t, "one");   // the one-message parser gives the first
}
TEST(ReplyParts, ABadMoreRefusesTheWholeReply) {
  char t[201]; uint8_t n = 9;
  for (const char* x : {"{\"text\":\"a\",\"more\":[]}", "{\"text\":\"a\",\"more\":\"b\"}", "{\"text\":\"a\",\"more\":5}",
                        "{\"text\":\"a\",\"more\":{\"x\":\"b\"}}", "{\"text\":\"a\",\"more\":[\"b\",5]}", "{\"text\":\"a\",\"more\":[\"b\",\"  \"]}",
                        "{\"text\":\"a\",\"more\":[\"b\",\"c\",\"d\",\"e\"]}", "{\"text\":\"a\",\"more\":[[\"b\"]]}", "{\"more\":[\"b\"]}",
                        "{\"text\":\"\",\"more\":[\"b\"]}"}) {
    std::string s = x;
    EXPECT_FALSE(rpart(s, 0, t, 201, &n)) << x; EXPECT_STREQ(t, "") << x; EXPECT_EQ(n, 0) << x;
    EXPECT_FALSE(rpart(s, 1, t, 201)) << x;
  }
  std::string big = "{\"text\":\"a\",\"more\":[\"" + std::string(201, 'b') + "\"]}";
  EXPECT_FALSE(rpart(big, 0, t, 201)); EXPECT_FALSE(rpart(big, 1, t, 201));
  ASSERT_TRUE(rpart("{\"text\":\"a\",\"more\":null}", 0, t, 201, &n)); EXPECT_EQ(n, 1);   // null is the same as absent
}
TEST(ReplyParts, LargestReplyFitsTheParseLimitAndASmallBufferIsRefused) {
  // Four messages of 118 bytes: a 512-byte payload, the most a reply can carry.
  std::string p(118, 'x');
  std::string d = "{\"text\":\"" + p + "\",\"more\":[\"" + p + "\",\"" + p + "\",\"" + p + "\"]}";
  ASSERT_LE(d.size(), (size_t)BOT_MQTT_REPLY_IN_MAX);
  char t[201]; uint8_t n = 0; size_t peak = 0;
  for (uint8_t k = 0; k < 4; k++) {
    ASSERT_TRUE(botMqttParseReplyPartLimited(d.data(), d.size(), k, t, 201, BOT_MQTT_PARSE_HEAP, &n, &peak)) << (int)k;
    EXPECT_EQ(strlen(t), 118u); EXPECT_EQ(n, 4); EXPECT_LE(peak, (size_t)BOT_MQTT_PARSE_HEAP);
  }
  char small[50];
  EXPECT_FALSE(rpart(d, 2, small, sizeof(small))); EXPECT_STREQ(small, "");
  std::string over = d + std::string(BOT_MQTT_REPLY_IN_MAX, ' ');
  EXPECT_FALSE(rpart(over, 0, t, 201));
  EXPECT_FALSE(botMqttParseReplyPartLimited(nullptr, 5, 0, t, 201, BOT_MQTT_PARSE_HEAP));
  EXPECT_FALSE(botMqttParseReplyPartLimited(d.data(), d.size(), 0, nullptr, 201, BOT_MQTT_PARSE_HEAP));
  EXPECT_FALSE(botMqttParseReplyPartLimited(d.data(), d.size(), 1, t, 201, 16));   // too little parse memory: refused, not crashed
}
