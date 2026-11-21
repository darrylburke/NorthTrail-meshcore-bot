// Host tests for BotReplies.h (reply formatters) and BotTemplate.h (botRender).
#include <gtest/gtest.h>
#include <cstring>
#include <string>
#include "helpers/bot/BotReplies.h"
#include "helpers/bot/BotTemplate.h"

static const uint8_t P2[] = {0xa5,0x3e,0x96,0xbc,0xad,0xb8,0x01,0x02,0x03};   // room for 3 hops of 3 bytes
static BotMsgInfo M(uint8_t hops){ return {"Dee",hops,3,P2,7.5f,-98,false}; }
static uint32_t fixed(uint32_t m){ return m>2 ? 2 : 0; }           // every die shows 3
static const char* look(const uint8_t* h,uint8_t n,void*){ return (n==3&&h[0]==0xa5) ? "Vincent Repeater" : nullptr; }

TEST(Fmt, Ping)          { char b[128]; botFmtPing(b,128,M(3)); EXPECT_STREQ(b,"@[Dee] 3 hops, SNR 7.5"); }
TEST(Fmt, PingPathListsEachRepeaterOnALine) {
  char b[128];
  size_t n = botFmtPingPath(b,128,M(2),look,nullptr,110); EXPECT_EQ(n, strlen(b));
  EXPECT_STREQ(b,"@[Dee] 2 hops, SNR 7.5\na53e96: Vincent Repeater\nbcadb8: ?");
  botFmtPingPath(b,128,M(1),nullptr,nullptr,110); EXPECT_STREQ(b,"@[Dee] 1 hop, SNR 7.5\na53e96: ?");
  BotMsgInfo d=M(2); d.direct=true; botFmtPingPath(b,128,d,look,nullptr,110); EXPECT_STREQ(b,"@[Dee] direct, SNR 7.5");
  botFmtPingPath(b,128,M(0),look,nullptr,110); EXPECT_STREQ(b,"@[Dee] 0 hops, SNR 7.5");
  char z[1]={'x'}; EXPECT_EQ(botFmtPingPath(z,0,M(2),look,nullptr,110),0u); EXPECT_EQ(z[0],'x');
}
TEST(Fmt, PingPathKeepsWholeLinesAndCountsTheRest) {
  static uint8_t path[64]; for (int i=0;i<64;i++) path[i]=(uint8_t)(0x10+i);
  BotMsgInfo m = {"Dee",23,2,path,-4.5f,-98,false};
  char b[208];
  for (size_t limit = 0; limit <= 207; limit++) {
    size_t n = botFmtPingPath(b,sizeof b,m,nullptr,nullptr,limit);
    std::string s(b);
    ASSERT_EQ(n, s.size());
    if (limit >= 24) ASSERT_LE(n, limit) << limit;   // the first line alone is 24 bytes
    // Every line after the first is "hhhh: ?" or, last, "+N more"; the lines shown and N make 23.
    int shown = 0, more = 0; size_t pos = s.find('\n');
    while (pos != std::string::npos) {
      size_t end = s.find('\n', pos + 1);
      std::string line = s.substr(pos + 1, end == std::string::npos ? std::string::npos : end - pos - 1);
      if (line[0] == '+') { more = atoi(line.c_str() + 1); ASSERT_EQ(end, std::string::npos); ASSERT_EQ(line, "+" + std::to_string(more) + " more"); }
      else { ASSERT_EQ(line.size(), 7u) << s; ASSERT_EQ(line.substr(4), ": ?"); shown++; }
      pos = end;
    }
    if (s.find('\n') != std::string::npos) ASSERT_EQ(shown + more, 23) << limit << " " << s;
  }
  botFmtPingPath(b,sizeof b,m,nullptr,nullptr,60);
  EXPECT_STREQ(b,"@[Dee] 23 hops, SNR -4.5\n1011: ?\n1213: ?\n1415: ?\n+20 more");
  char big[256]; botFmtPingPath(big,sizeof big,m,nullptr,nullptr,208);
  EXPECT_EQ(std::string(big).size(), 208u);
  EXPECT_EQ(std::string(big).substr(201), "3c3d: ?");   // all 23 fit in exactly 208
}
TEST(Fmt, PingSingular)  { char b[128]; botFmtPing(b,128,M(1)); EXPECT_STREQ(b,"@[Dee] 1 hop, SNR 7.5"); }
TEST(Fmt, SnrTrimmed)    { char b[128]; BotMsgInfo m=M(2); m.snr=-7.0f; botFmtPing(b,128,m); EXPECT_STREQ(b,"@[Dee] 2 hops, SNR -7"); m.snr=11.25f; botFmtPing(b,128,m); EXPECT_STREQ(b,"@[Dee] 2 hops, SNR 11.25"); }
TEST(Fmt, Test)          { char b[128]; botFmtTest(b,128,M(2)); EXPECT_STREQ(b,"@[Dee] ack | a53e96,bcadb8 (2 hops) | SNR 7.5 | RSSI -98"); }
TEST(Fmt, TestDirect)    { char b[128]; botFmtTest(b,128,M(0)); EXPECT_STREQ(b,"@[Dee] ack | Direct | SNR 7.5 | RSSI -98"); }
TEST(Fmt, Path)          { char b[128]; botFmtPath(b,128,M(2),look,nullptr); EXPECT_STREQ(b,"A53E96: Vincent Repeater / BCADB8: ?"); }
TEST(Fmt, Roll)          { char b[128]; botFmtRoll(b,128,"Dee","2d6",fixed); EXPECT_STREQ(b,"@[Dee] 2d6: 3+3 = 6"); botFmtRoll(b,128,"Dee","",fixed); EXPECT_STREQ(b,"@[Dee] 1d6: 3 = 3"); }
TEST(Fmt, RollBadInput)  { char b[128]; for(const char* a : {"99d6","2d1000","abc","0d6","2d"}) { botFmtRoll(b,128,"Dee",a,fixed); EXPECT_STREQ(b,"@[Dee] try: roll 2d6"); } }
TEST(Help, OnePage)      { const char* n[]={"ping","test","path"}; char b[128]; int pages; botFmtHelp(b,128,n,3,1,"example.org/bot",100,&pages); EXPECT_STREQ(b,"Cmds: ping test path | visit example.org/bot for more info"); EXPECT_EQ(pages,1); }
TEST(Help, NoLink)       { const char* n[]={"ping"}; char b[128]; int pages; botFmtHelp(b,128,n,1,1,"",100,&pages); EXPECT_STREQ(b,"Cmds: ping"); }
TEST(Help, Pages)        { const char* n[]={"aaaaaaaa","bbbbbbbb","cccccccc","dddddddd"}; char b[128]; int pages; size_t len=botFmtHelp(b,128,n,4,1,"",28,&pages); EXPECT_LE(len,28u); EXPECT_GT(pages,1); EXPECT_EQ(strncmp(b,"Cmds: ",6),0); len=botFmtHelp(b,128,n,4,2,"",28,&pages); EXPECT_LE(len,28u); EXPECT_EQ(strncmp(b,"Cmds 2/",7),0); EXPECT_EQ(botFmtHelp(b,128,n,4,pages+1,"",28,&pages),0u); }
TEST(Render, AllVars)    { BotVars v{"Dee","ipa","a53e96","#bot","12:04","2026-11-20","3d 4h",3,7.5f,-98}; char b[128]; botRender(b,128,"Hi {sender}, {hops} hops at {snr} dB ({rssi}) via {path} in {channel} {time} {date} up {uptime}: {args}",v); EXPECT_STREQ(b,"Hi Dee, 3 hops at 7.5 dB (-98) via a53e96 in #bot 12:04 2026-11-20 up 3d 4h: ipa"); }
TEST(Render, UnknownKept){ BotVars v{}; v.sender="Dee"; char b[64]; botRender(b,64,"{nope} { {sender}",v); EXPECT_STREQ(b,"{nope} { Dee"); }
TEST(Render, Bounded)    { BotVars v{}; v.sender="Dee"; char b[8]; size_t n=botRender(b,8,"{sender}{sender}{sender}{sender}",v); EXPECT_LT(n,8u); EXPECT_EQ(b[n],'\0'); }
TEST(Render, FetchFilled)     { BotVars v{}; v.sender="Dee"; v.fetch="4.2"; char b[64]; botRender(b,64,"{sender}: level {fetch} m ({fetch})",v); EXPECT_STREQ(b,"Dee: level 4.2 m (4.2)"); }
TEST(Render, FetchDefaultsEmpty) { BotVars v{}; char b[64]; botRender(b,64,"[{fetch}]",v); EXPECT_STREQ(b,"[]"); BotVars w; w.sender="x"; w.args=""; w.path=""; w.channel=""; w.time=""; w.date=""; w.uptime=""; w.hops=0; w.snr=0; w.rssi=0; botRender(b,64,"a{fetch}b",w); EXPECT_STREQ(b,"ab"); }
TEST(Render, FetchAllVarsInitStillEmpty) { BotVars v{"Dee","ipa","a53e96","#bot","12:04","2026-11-20","3d 4h",3,7.5f,-98}; char b[64]; botRender(b,64,"<{fetch}>",v); EXPECT_STREQ(b,"<>"); }
TEST(Render, FetchNullIsEmpty) { BotVars v{}; v.fetch=nullptr; char b[16]; botRender(b,16,"x{fetch}y",v); EXPECT_STREQ(b,"xy"); }
TEST(Render, FetchCaseSensitiveAndBounded) { BotVars v{}; v.fetch="0123456789"; char b[8]; size_t n=botRender(b,8,"{FETCH}{fetch}",v); EXPECT_STREQ(b,"{FETCH}"); EXPECT_EQ(n,7u); botRender(b,8,"{fetch}{fetch}",v); EXPECT_STREQ(b,"0123456"); }

// ---- Extra edge cases ----
static const char* EMOJI = "\xF0\x9F\xA7\xAD";  // 4-byte compass

TEST(FmtSnr, QuarterValues) {
  char b[128]; BotMsgInfo m = M(1);
  const struct { float v; const char* s; } c[] = {{0.0f,"0"},{-0.0f,"0"},{-0.25f,"-0.25"},{0.5f,"0.5"},{7.75f,"7.75"},{-11.5f,"-11.5"},{20.0f,"20"}};
  for (auto& x : c) { m.snr = x.v; botFmtPing(b,128,m); EXPECT_EQ(std::string(b), std::string("@[Dee] 1 hop, SNR ") + x.s); }
}
TEST(FmtSnr, NaNAndHugeAreSafe) {
  char b[128]; BotMsgInfo m = M(1);
  m.snr = 0.0f/0.0f; botFmtPing(b,128,m); EXPECT_STREQ(b,"@[Dee] 1 hop, SNR 0");
  m.snr = 1e30f; botFmtPing(b,128,m); EXPECT_EQ(strncmp(b,"@[Dee] 1 hop, SNR ",18),0);
}
TEST(Fmt, PingZeroHopFloodAndDirect) {
  char b[128]; BotMsgInfo m = M(0); m.path = nullptr;
  botFmtPing(b,128,m); EXPECT_STREQ(b,"@[Dee] 0 hops, SNR 7.5");   // zero-hop flood
  m = M(3); m.direct = true; m.path = nullptr;
  botFmtPing(b,128,m); EXPECT_STREQ(b,"@[Dee] direct, SNR 7.5");   // came over a learned route
  m = M(0); m.direct = true;
  botFmtPing(b,128,m); EXPECT_STREQ(b,"@[Dee] direct, SNR 7.5");
}
TEST(Fmt, TestDirectFlagWithNullPath) {
  char b[128]; BotMsgInfo m = M(3); m.direct = true; m.path = nullptr;
  botFmtTest(b,128,m); EXPECT_STREQ(b,"@[Dee] ack | Direct | SNR 7.5 | RSSI -98");
}
TEST(Fmt, TestSingleHop) {
  char b[128]; BotMsgInfo m = M(1); m.hash_size = 1;
  botFmtTest(b,128,m); EXPECT_STREQ(b,"@[Dee] ack | a5 (1 hop) | SNR 7.5 | RSSI -98");
}
TEST(Fmt, NullSenderIsEmpty) {
  char b[128]; BotMsgInfo m = M(3); m.sender = nullptr;
  botFmtPing(b,128,m); EXPECT_STREQ(b,"@[] 3 hops, SNR 7.5");
  botFmtTest(b,128,m); EXPECT_EQ(strncmp(b,"@[] ack | ",10),0);
  botFmtRoll(b,128,nullptr,"2d6",fixed); EXPECT_STREQ(b,"@[] 2d6: 3+3 = 6");
  botFmtRoll(b,128,nullptr,"zz",fixed); EXPECT_STREQ(b,"@[] try: roll 2d6");
  botFmt8Ball(b,128,nullptr,[](uint32_t){return 0u;}); EXPECT_STREQ(b,"@[] It is certain.");
}
TEST(FmtPathHex, HashSizes) {
  char b[64]; const uint8_t p1[] = {0x0a,0xff}; const uint8_t p2[] = {0x0a,0x0b,0xff,0x01};
  BotMsgInfo m{"Dee",2,1,p1,0,0,false}; EXPECT_EQ(botFmtPathHex(b,64,m),5u); EXPECT_STREQ(b,"0a,ff");
  m = BotMsgInfo{"Dee",2,2,p2,0,0,false}; botFmtPathHex(b,64,m); EXPECT_STREQ(b,"0a0b,ff01");
  m = M(2); botFmtPathHex(b,64,m); EXPECT_STREQ(b,"a53e96,bcadb8");
}
TEST(FmtPathHex, DirectNeverReadsPath) {
  char b[64]; BotMsgInfo m{"Dee",0,3,nullptr,0,0,false};
  EXPECT_EQ(botFmtPathHex(b,64,m),6u); EXPECT_STREQ(b,"Direct");
  m.hops = 4; m.direct = true; botFmtPathHex(b,64,m); EXPECT_STREQ(b,"Direct");
}
TEST(FmtPathHex, InvalidHashSizeIsDirect) {
  char b[64]; BotMsgInfo m = M(2); m.hash_size = 0; botFmtPathHex(b,64,m); EXPECT_STREQ(b,"Direct");
  m.hash_size = 4; botFmtPathHex(b,64,m); EXPECT_STREQ(b,"Direct");
}
TEST(FmtPathHex, TightCapTruncates) {
  char b[8]; size_t n = botFmtPathHex(b,8,M(2)); EXPECT_EQ(n,7u); EXPECT_STREQ(b,"a53e96,");
}
TEST(FmtPath, DirectAndNullPath) {
  char b[64]; BotMsgInfo m{"Dee",0,3,nullptr,0,0,false};
  EXPECT_EQ(botFmtPath(b,64,m,look,nullptr),6u); EXPECT_STREQ(b,"Direct");
  m = M(3); m.direct = true; m.path = nullptr; botFmtPath(b,64,m,look,nullptr); EXPECT_STREQ(b,"Direct");
}
TEST(FmtPath, NullLookupAllUnknownAndHashSizeOne) {
  char b[64]; const uint8_t p[] = {0x0a,0xff};
  BotMsgInfo m{"Dee",2,1,p,0,0,false};
  botFmtPath(b,64,m,nullptr,nullptr); EXPECT_STREQ(b,"0A: ? / FF: ?");
}
static const char* ctxName(const uint8_t*, uint8_t n, void* ctx) { return n==2 ? (const char*)ctx : nullptr; }
TEST(FmtPath, PassesContextAndHashSize) {
  char b[64]; const uint8_t p[] = {0x01,0x02}; BotMsgInfo m{"Dee",1,2,p,0,0,false};
  botFmtPath(b,64,m,ctxName,(void*)"Hill"); EXPECT_STREQ(b,"0102: Hill");
}
TEST(FmtPath, TightCapNeverOverruns) {
  char b[10]; size_t n = botFmtPath(b,10,M(2),look,nullptr); EXPECT_LT(n,10u); EXPECT_EQ(b[n],'\0'); EXPECT_EQ(strncmp(b,"A53E96: V",9),0);
}
TEST(Fmt, TightCapAndEmojiSender) {
  char b[8]; size_t n = botFmtPing(b,8,M(3)); EXPECT_EQ(n,7u); EXPECT_STREQ(b,"@[Dee] ");
  std::string name = std::string("A") + EMOJI;
  BotMsgInfo m = M(3); m.sender = name.c_str();
  char c[7]; n = botFmtPing(c,7,m); EXPECT_EQ(n,3u); EXPECT_STREQ(c,"@[A");   // cut before the emoji, not inside it
  char d[64]; botFmtPing(d,64,m); EXPECT_EQ(std::string(d), "@[A" + std::string(EMOJI) + "] 3 hops, SNR 7.5");
}
TEST(Fmt, ZeroAndOneCap) {
  char b[4]; memset(b,0xAA,sizeof(b));
  EXPECT_EQ(botFmtPing(b,0,M(1)),0u); EXPECT_EQ((unsigned char)b[0],0xAAu);
  EXPECT_EQ(botFmtPing(b,1,M(1)),0u); EXPECT_EQ(b[0],'\0');
  EXPECT_EQ(botFmtTest(b,0,M(1)),0u); EXPECT_EQ(botFmtPathHex(b,0,M(1)),0u);
  EXPECT_EQ(botFmtPath(b,0,M(1),look,nullptr),0u); EXPECT_EQ(botFmtRoll(b,0,"D","",fixed),0u);
  EXPECT_EQ(botFmt8Ball(b,0,"D",fixed),0u);
  int pages = -1; const char* n[] = {"a"}; EXPECT_EQ(botFmtHelp(b,0,n,1,1,"",50,&pages),0u); EXPECT_EQ(pages,1);
}

static uint32_t maxRoll(uint32_t m) { return m - 1; }
static uint32_t lastMax = 0;
static uint32_t recRoll(uint32_t m) { lastMax = m; return 0; }
TEST(Roll, ParsesForms) {
  char b[128];
  botFmtRoll(b,128,"Dee","  2D6  ",fixed); EXPECT_STREQ(b,"@[Dee] 2d6: 3+3 = 6");
  botFmtRoll(b,128,"Dee","d20",maxRoll); EXPECT_STREQ(b,"@[Dee] 1d20: 20 = 20");
  botFmtRoll(b,128,"Dee","D100",recRoll); EXPECT_STREQ(b,"@[Dee] 1d100: 1 = 1"); EXPECT_EQ(lastMax,100u);
  botFmtRoll(b,128,"Dee",nullptr,fixed); EXPECT_STREQ(b,"@[Dee] 1d6: 3 = 3");
  botFmtRoll(b,128,"Dee","   ",fixed); EXPECT_STREQ(b,"@[Dee] 1d6: 3 = 3");
  botFmtRoll(b,128,"Dee","10d2",maxRoll); EXPECT_STREQ(b,"@[Dee] 10d2: 2+2+2+2+2+2+2+2+2+2 = 20");
}
TEST(Roll, Boundaries) {
  char b[128]; const char* bad[] = {"11d6","1d1","2d101","0d6","d0","d","2d6x","2d6 x","2 d6","2d 6","-1d6","2d6d3","6","99999999999d6","2d99999999999"};
  for (const char* a : bad) { botFmtRoll(b,128,"Dee",a,fixed); EXPECT_STREQ(b,"@[Dee] try: roll 2d6") << a; }
  botFmtRoll(b,128,"Dee","2d100",maxRoll); EXPECT_STREQ(b,"@[Dee] 2d100: 100+100 = 200");
  botFmtRoll(b,128,"Dee","1d2",maxRoll); EXPECT_STREQ(b,"@[Dee] 1d2: 2 = 2");
}
TEST(Roll, TightCap) {
  char b[12]; size_t n = botFmtRoll(b,12,"Dee","10d6",fixed); EXPECT_LT(n,12u); EXPECT_EQ(b[n],'\0'); EXPECT_EQ(strlen(b),n);
}

static const char* const ANS[20] = {"It is certain.","It is decidedly so.","Without a doubt.","Yes definitely.","You may rely on it.","As I see it, yes.","Most likely.","Outlook good.","Yes.","Signs point to yes.","Reply hazy, try again.","Ask again later.","Better not tell you now.","Cannot predict now.","Concentrate and ask again.","Don't count on it.","My reply is no.","My sources say no.","Outlook not so good.","Very doubtful."};
static uint32_t g_idx = 0;
static uint32_t idxRnd(uint32_t m) { EXPECT_EQ(m,20u); return g_idx; }
TEST(EightBall, AllTwentyAnswers) {
  char b[64];
  for (uint32_t i = 0; i < 20; i++) { g_idx = i; botFmt8Ball(b,64,"Dee",idxRnd); EXPECT_EQ(std::string(b), std::string("@[Dee] ") + ANS[i]); }
}
TEST(EightBall, OutOfRangeRndStaysInTable) {
  char b[64]; g_idx = 25; botFmt8Ball(b,64,"Dee",[](uint32_t){ return 25u; }); EXPECT_EQ(std::string(b), std::string("@[Dee] ") + ANS[5]);
}
TEST(EightBall, TightCap) { char b[10]; size_t n = botFmt8Ball(b,10,"Dee",[](uint32_t){return 0u;}); EXPECT_EQ(n,9u); EXPECT_STREQ(b,"@[Dee] It"); }

TEST(Help, EmptyList) {
  char b[64]; int pages = 0;
  EXPECT_EQ(botFmtHelp(b,64,nullptr,0,1,"",50,&pages),5u); EXPECT_STREQ(b,"Cmds:"); EXPECT_EQ(pages,1);
  botFmtHelp(b,64,nullptr,0,1,"x.org",50,&pages); EXPECT_STREQ(b,"Cmds: | visit x.org for more info"); EXPECT_EQ(pages,1);
  botFmtHelp(b,64,nullptr,0,1,nullptr,50,nullptr); EXPECT_STREQ(b,"Cmds:");
}
TEST(Help, OutOfRangeSetsTotalAndWritesEmpty) {
  const char* n[] = {"ping","test"}; char b[64]; int pages = 0;
  strcpy(b,"junk"); EXPECT_EQ(botFmtHelp(b,64,n,2,0,"",50,&pages),0u); EXPECT_STREQ(b,""); EXPECT_EQ(pages,1);
  strcpy(b,"junk"); EXPECT_EQ(botFmtHelp(b,64,n,2,-3,"",50,&pages),0u); EXPECT_STREQ(b,"");
  strcpy(b,"junk"); EXPECT_EQ(botFmtHelp(b,64,n,2,2,"",50,&pages),0u); EXPECT_STREQ(b,""); EXPECT_EQ(pages,1);
}
TEST(Help, LinkOnPageOneOnly) {
  const char* n[] = {"aaaaaaaa","bbbbbbbb","cccccccc","dddddddd"}; char b[128]; int pages;
  // The link part " | visit x.org for more info" is 28 bytes. Limit 44: "Cmds:" + one name (9) + link = 42;
  // a second name would make 51, so the rest go to page 2, which has no link.
  botFmtHelp(b,128,n,4,1,"x.org",44,&pages);
  EXPECT_STREQ(b,"Cmds: aaaaaaaa | visit x.org for more info"); EXPECT_EQ(pages,2);
  botFmtHelp(b,128,n,4,2,"x.org",44,&pages); EXPECT_STREQ(b,"Cmds 2/2: bbbbbbbb cccccccc dddddddd");
}
TEST(Help, LittleRoomForLinkDropsLinkSoNamesStayOnPageOne) {
  const char* n[] = {"ping","test"}; char b[128]; int pages;
  botFmtHelp(b,128,n,2,1,"example.org/bot",30,&pages);   // the 38-byte link part does not fit beside "Cmds:"
  EXPECT_STREQ(b,"Cmds: ping test"); EXPECT_EQ(pages,1);
}
TEST(Help, LinkLongerThanLimitOmitted) {
  const char* n[] = {"ping","test"}; char b[128]; int pages;
  size_t len = botFmtHelp(b,128,n,2,1,"example.org/very/long",20,&pages);
  EXPECT_LE(len,20u); EXPECT_STREQ(b,"Cmds: ping test"); EXPECT_EQ(pages,1);
  len = botFmtHelp(b,128,nullptr,0,1,"example.org/very/long",20,&pages);   // no names at all
  EXPECT_STREQ(b,"Cmds:"); EXPECT_EQ(pages,1);
  len = botFmtHelp(b,128,nullptr,0,1,"example.org/very/long",14,&pages);   // "Cmds:" + the 44-byte link part needs 49 > 14: link left out
  EXPECT_LE(len,14u); EXPECT_STREQ(b,"Cmds:");
}
TEST(Help, OwnerLinkWording) {
  // The exact wording asked for: the list, then "visit <address> for more info".
  const char* n[] = {"ping","test"}; char b[128]; int pages;
  botFmtHelp(b,128,n,2,1,"https://example.org/mesh",100,&pages);
  EXPECT_STREQ(b,"Cmds: ping test | visit https://example.org/mesh for more info"); EXPECT_EQ(pages,1);
}
TEST(Help, LinkKeptWhenItFitsBesideFirstName) {
  const char* n[] = {"ping"}; char b[128]; int pages;
  botFmtHelp(b,128,n,1,1,"x.org",38,&pages); EXPECT_STREQ(b,"Cmds: ping | visit x.org for more info");   // exactly 38 bytes
  botFmtHelp(b,128,n,1,1,"x.org",37,&pages); EXPECT_STREQ(b,"Cmds: ping");
}
TEST(Help, MaxBytesSmallerThanCap) {
  const char* n[] = {"aaaa","bbbb","cccc"}; char b[128]; int pages;
  size_t len = botFmtHelp(b,128,n,3,1,"",16,&pages); EXPECT_LE(len,16u); EXPECT_STREQ(b,"Cmds: aaaa bbbb"); EXPECT_EQ(pages,2);
  len = botFmtHelp(b,128,n,3,2,"",16,&pages); EXPECT_STREQ(b,"Cmds 2/2: cccc");
}
TEST(Help, TinyLimitNeverExceeds) {
  const char* n[] = {"ping"}; char b[128]; int pages;
  EXPECT_EQ(botFmtHelp(b,128,n,1,1,"",3,&pages),3u); EXPECT_STREQ(b,"Cmd");
}
TEST(Help, SweepEveryPageWithinLimitAndNamesAgree) {
  const char* n[] = {"ping","test","path","roll","8ball","help","wx","sun","moon","dist","heard","uptime"};
  const char* links[] = {"", "x.org", "example.org/bot/commands"};
  for (const char* link : links) for (size_t mx = 6; mx <= 60; mx++) {
    char b[128]; int pages = 0;
    botFmtHelp(b,128,n,12,1,link,mx,&pages);
    ASSERT_GE(pages,1);
    std::string all;
    for (int p = 1; p <= pages; p++) {
      size_t len = botFmtHelp(b,128,n,12,p,link,mx,&pages);
      EXPECT_LE(len,mx) << "mx=" << mx << " page=" << p; EXPECT_EQ(strlen(b),len);
      std::string pre = p==1 ? "Cmds:" : "Cmds " + std::to_string(p) + "/" + std::to_string(pages) + ":";
      EXPECT_EQ(std::string(b).compare(0,pre.size(),pre),0) << b;
      if (link[0] && p != 1) EXPECT_EQ(std::string(b).find("visit "), std::string::npos);
      all += std::string(b) + "|";
    }
    EXPECT_EQ(botFmtHelp(b,128,n,12,pages+1,link,mx,&pages),0u);
    if (mx >= 26) for (const char* x : n) EXPECT_NE(all.find(std::string(" ") + x), std::string::npos) << x << " mx=" << mx;  // all fit somewhere
  }
}
TEST(Help, OversizedNameSkipped) {
  std::string big(100,'x'); const char* n[] = {"ping",big.c_str(),"test"}; char b[128]; int pages;
  botFmtHelp(b,128,n,3,1,"",30,&pages); EXPECT_STREQ(b,"Cmds: ping test"); EXPECT_EQ(pages,1);
  const char* m[] = {big.c_str()}; botFmtHelp(b,128,m,1,1,"",30,&pages); EXPECT_STREQ(b,"Cmds:"); EXPECT_EQ(pages,1);
}
TEST(Help, CapBoundsPage) {
  const char* n[] = {"ping","test","path"}; char b[20]; int pages;
  size_t len = botFmtHelp(b,20,n,3,1,"",100,&pages);   // cap-1 = 19 bytes
  EXPECT_LE(len,19u); EXPECT_EQ(strlen(b),len); EXPECT_STREQ(b,"Cmds: ping test"); EXPECT_EQ(pages,2);
}
TEST(Help, ManyPagesCoverEveryNameOnce) {
  const char* n[] = {"aa","bb","cc","dd","ee","ff","gg","hh","ii","jj","kk","ll"}; char b[64]; int pages = 0;
  botFmtHelp(b,64,n,12,1,"",14,&pages); EXPECT_GE(pages,10);   // two-digit page numbers
  std::string all; int p = 1; botFmtHelp(b,64,n,12,pages,"",14,&pages);
  for (p = 1; p <= pages; p++) { size_t l = botFmtHelp(b,64,n,12,p,"",14,&pages); EXPECT_LE(l,14u); all += b; all += "|"; }
  for (const char* x : n) { size_t first = all.find(x); ASSERT_NE(first,std::string::npos); EXPECT_EQ(all.find(x,first+1),std::string::npos); }
  botFmtHelp(b,64,n,12,pages,"",14,&pages); EXPECT_EQ(strncmp(b,"Cmds ",5),0);
}

TEST(Render, NullFieldsAndNullTemplate) {
  BotVars v{}; char b[64];
  botRender(b,64,"[{sender}|{args}|{path}|{channel}|{time}|{date}|{uptime}]",v); EXPECT_STREQ(b,"[||||||]");
  EXPECT_EQ(botRender(b,64,nullptr,v),0u); EXPECT_STREQ(b,"");
}
TEST(Render, NumbersAndSnr) {
  BotVars v{}; v.hops = 0; v.snr = -0.25f; v.rssi = -105; char b[64];
  botRender(b,64,"{hops} {snr} {rssi}",v); EXPECT_STREQ(b,"0 -0.25 -105");
  v.snr = -0.0f; botRender(b,64,"{snr}",v); EXPECT_STREQ(b,"0");
}
TEST(Render, CaseSensitiveAndMalformedKept) {
  BotVars v{}; v.sender = "Dee"; char b[64];
  botRender(b,64,"{Sender}{}{ sender}{sender",v); EXPECT_STREQ(b,"{Sender}{}{ sender}{sender");
  botRender(b,64,"{{sender}} {",v); EXPECT_STREQ(b,"{Dee} {");
}
TEST(Render, TightCapAndEmoji) {
  BotVars v{}; std::string name = std::string("A") + EMOJI; v.sender = name.c_str(); char b[4];
  size_t n = botRender(b,4,"{sender}",v); EXPECT_EQ(n,1u); EXPECT_STREQ(b,"A");   // would split the emoji
  char c[8]; n = botRender(c,8,"hello world",v); EXPECT_EQ(n,7u); EXPECT_STREQ(c,"hello w");
  char z[2] = {'x',0}; EXPECT_EQ(botRender(z,0,"abc",v),0u); EXPECT_EQ(z[0],'x');
  EXPECT_EQ(botRender(z,1,"abc",v),0u); EXPECT_EQ(z[0],'\0');
}
TEST(Render, LongArgsTruncatedNotOverrun) {
  std::string a(300,'q'); BotVars v{}; v.args = a.c_str(); char b[40]; memset(b,0xAA,sizeof(b));
  size_t n = botRender(b,32,"say {args}",v); EXPECT_EQ(n,31u); EXPECT_EQ(b[31],'\0'); EXPECT_EQ((unsigned char)b[32],0xAAu);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
