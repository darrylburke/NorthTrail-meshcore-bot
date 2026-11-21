// Host tests for BotConfig.h (defaults, JSON load/save with validation, botTzToPosix).
#include <gtest/gtest.h>
#include <cstring>
#include <string>
#include "helpers/bot/BotConfig.h"

static BotConfig load(const char* json){ BotConfig c; botConfigDefaults(c); JsonDocument d; deserializeJson(d,json); botConfigFromJson(d.as<JsonVariantConst>(),c); return c; }
TEST(Cfg, Defaults)            { BotConfig c; botConfigDefaults(c); EXPECT_EQ(c.num_channels,1); EXPECT_STREQ(c.channels[0],"#bot"); EXPECT_STREQ(c.channels[1],""); EXPECT_STREQ(c.scope,""); EXPECT_EQ(c.prefix,'!'); EXPECT_EQ(c.cooldown_s,10); EXPECT_EQ(c.max_per_min,6); EXPECT_EQ(c.advert_zero_hop_min,240); EXPECT_EQ(c.advert_flood_min,1440); EXPECT_STREQ(c.builtin[BB_8BALL].name,"8ball"); EXPECT_TRUE(c.builtin[BB_PING].enabled); EXPECT_STREQ(c.web_hash,""); }
TEST(Cfg, RoundTrip)           { BotConfig a; botConfigDefaults(a); strcpy(a.scope,"test-region"); a.lat=43.6534; a.num_custom=1; strcpy(a.custom[0].trigger,"hello"); a.custom[0].pfx=true; strcpy(a.custom[0].reply,"Hi {sender}"); a.custom[0].enabled=true; JsonDocument d; botConfigToJson(a,d.to<JsonObject>(),true); BotConfig b; botConfigDefaults(b); botConfigFromJson(d.as<JsonVariantConst>(),b); EXPECT_STREQ(b.scope,"test-region"); EXPECT_DOUBLE_EQ(b.lat,43.6534); EXPECT_EQ(b.num_custom,1); EXPECT_STREQ(b.custom[0].reply,"Hi {sender}"); }
TEST(Cfg, SecretsOmittedForWeb){ BotConfig a; botConfigDefaults(a); strcpy(a.web_hash,"abcd"); strcpy(a.web_salt,"ef"); JsonDocument d; botConfigToJson(a,d.to<JsonObject>(),false); std::string s; serializeJson(d,s); EXPECT_EQ(s.find("abcd"),std::string::npos); EXPECT_EQ(s.find("web_"),std::string::npos); }
TEST(Cfg, GarbageKeepsDefaults){ for(const char* j : {"", "{", "[]", "null", "{\"cooldown_s\":\"soon\",\"max_per_min\":0,\"lat\":999,\"prefix\":\"ab\",\"channel\":\"bot\"}"}) { BotConfig c=load(j); EXPECT_EQ(c.cooldown_s,10); EXPECT_EQ(c.max_per_min,6); EXPECT_DOUBLE_EQ(c.lat,0.0); EXPECT_EQ(c.prefix,'!'); EXPECT_EQ(c.num_channels,1); EXPECT_STREQ(c.channels[0],"#bot"); } }
TEST(Cfg, OverlongRejected)    { std::string j="{\"scope\":\""+std::string(200,'x')+"\"}"; BotConfig c=load(j.c_str()); EXPECT_STREQ(c.scope,""); }
TEST(Cfg, PartialUpdate)       { BotConfig c=load("{\"cooldown_s\":30}"); EXPECT_EQ(c.cooldown_s,30); EXPECT_EQ(c.max_per_min,6); }
TEST(Cfg, DuplicateNames)      { BotConfig c=load("{\"custom\":[{\"trigger\":\"ping\",\"reply\":\"x\",\"enabled\":true},{\"trigger\":\"Hello\",\"reply\":\"y\",\"enabled\":true}]}"); EXPECT_EQ(c.num_custom,1); EXPECT_STREQ(c.custom[0].trigger,"hello"); }
TEST(Cfg, SeventeenCustom)     { std::string j="{\"custom\":["; for(int i=0;i<17;i++){ j+=(i?",":""); j+="{\"trigger\":\"c"+std::to_string(i)+"\",\"reply\":\"r\",\"enabled\":true}"; } j+="]}"; EXPECT_EQ(load(j.c_str()).num_custom,16); }
TEST(Tz, Table)                { EXPECT_STREQ(botTzToPosix("America/Toronto"),"EST5EDT,M3.2.0,M11.1.0"); EXPECT_STREQ(botTzToPosix("CET-1CEST,M3.5.0,M10.5.0/3"),"CET-1CEST,M3.5.0,M10.5.0/3"); EXPECT_EQ(botTzToPosix("Mars/Olympus"),nullptr); }

// ---- extra: helpers ----
static BotConfig* heapDefaults(){ BotConfig* c=new BotConfig; botConfigDefaults(*c); return c; }
static void applyTo(BotConfig& c, const char* json, bool web=false){ JsonDocument d; deserializeJson(d,json); if(web) botConfigFromWebJson(d.as<JsonVariantConst>(),c); else botConfigFromJson(d.as<JsonVariantConst>(),c); }
static const char* HEX16="0123456789abcdef";
static const char* HEX64="0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

// ---- defaults / tz ----
TEST(Cfg, DefaultsAreMemcmpEqual){ BotConfig a,b; memset(&a,0xAA,sizeof a); memset(&b,0x55,sizeof b); botConfigDefaults(a); botConfigDefaults(b); EXPECT_EQ(memcmp(&a,&b,sizeof a),0); EXPECT_STREQ(a.tz,"America/Toronto"); EXPECT_STREQ(a.ignore,"bot"); EXPECT_EQ(a.num_custom,0); const char* ids[]={"ping","test","path","help","heard","mesh","dist","sun","roll","8ball","wx","aurora","alerts","nodes"}; for(int i=0;i<BB_COUNT;i++){ EXPECT_STREQ(a.builtin[i].name,ids[i]); EXPECT_TRUE(a.builtin[i].enabled);} }
TEST(Tz, AllTableEntries){ EXPECT_STREQ(botTzToPosix("America/St_Johns"),"NST3:30NDT,M3.2.0,M11.1.0"); EXPECT_STREQ(botTzToPosix("America/Halifax"),"AST4ADT,M3.2.0,M11.1.0"); EXPECT_STREQ(botTzToPosix("America/Winnipeg"),"CST6CDT,M3.2.0,M11.1.0"); EXPECT_STREQ(botTzToPosix("America/Regina"),"CST6"); EXPECT_STREQ(botTzToPosix("America/Edmonton"),"MST7MDT,M3.2.0,M11.1.0"); EXPECT_STREQ(botTzToPosix("America/Vancouver"),"PST8PDT,M3.2.0,M11.1.0"); EXPECT_STREQ(botTzToPosix("UTC"),"UTC0"); }
TEST(Tz, NullEmptyCaseAndPassThrough){ EXPECT_EQ(botTzToPosix(nullptr),nullptr); EXPECT_EQ(botTzToPosix(""),nullptr); EXPECT_EQ(botTzToPosix("america/toronto"),nullptr); EXPECT_EQ(botTzToPosix("utc"),nullptr); const char* p="EST5EDT"; EXPECT_EQ(botTzToPosix(p),p); }

// ---- strings ----
TEST(Cfg, ChannelRules){
  // the old single key, and the same rules for each element of the list
  EXPECT_STREQ(load("{\"channel\":\"#x\"}").channels[0],"#x");
  EXPECT_STREQ(load("{\"channel\":\"Public\"}").channels[0],"Public");
  for(const char* j : {"{\"channel\":\"#\"}","{\"channel\":\"\"}","{\"channel\":\"public\"}","{\"channel\":\"bot\"}","{\"channel\":\"Public2\"}","{\"channel\":5}","{\"channel\":\"#a\\nb\"}"}) { BotConfig c=load(j); EXPECT_STREQ(c.channels[0],"#bot") << j; EXPECT_EQ(c.num_channels,1) << j; }
  std::string c31="#"+std::string(30,'a'), c32="#"+std::string(31,'a'); EXPECT_STREQ(load(("{\"channel\":\""+c31+"\"}").c_str()).channels[0],c31.c_str()); EXPECT_STREQ(load(("{\"channel\":\""+c32+"\"}").c_str()).channels[0],"#bot");  // 31 chars fit, 32 do not
  for(const char* e : {"\"#\"","\"\"","\"public\"","\"bot\"","\"Public2\"","5","\"#a\\nb\"","null","[\"#x\"]"}) { BotConfig c=load((std::string("{\"channels\":[")+e+",\"#ok\"]}").c_str()); EXPECT_EQ(c.num_channels,1) << e; EXPECT_STREQ(c.channels[0],"#ok") << e; }
  BotConfig c=load(("{\"channels\":[\""+c32+"\",\""+c31+"\"]}").c_str()); EXPECT_EQ(c.num_channels,1); EXPECT_STREQ(c.channels[0],c31.c_str());
}
TEST(Cfg, StringBoundariesAndEmpty){
  std::string s30(30,'a'), s31(31,'a'); EXPECT_STREQ(load(("{\"scope\":\""+s30+"\"}").c_str()).scope,s30.c_str()); EXPECT_STREQ(load(("{\"scope\":\""+s31+"\"}").c_str()).scope,"");
  BotConfig c; botConfigDefaults(c); strcpy(c.scope,"test-region"); strcpy(c.help_link,"http://x"); strcpy(c.ignore,"zz"); applyTo(c,"{\"scope\":\"\",\"help_link\":\"\",\"ignore\":\"\",\"tz\":\"\"}"); EXPECT_STREQ(c.scope,""); EXPECT_STREQ(c.help_link,""); EXPECT_STREQ(c.ignore,""); EXPECT_STREQ(c.tz,"");
  EXPECT_STREQ(load(("{\"ignore\":\""+std::string(95,'i')+"\"}").c_str()).ignore,std::string(95,'i').c_str()); EXPECT_STREQ(load(("{\"ignore\":\""+std::string(96,'i')+"\"}").c_str()).ignore,"bot");
  EXPECT_STREQ(load(("{\"help_link\":\""+std::string(63,'h')+"\"}").c_str()).help_link,std::string(63,'h').c_str()); EXPECT_STREQ(load(("{\"help_link\":\""+std::string(64,'h')+"\"}").c_str()).help_link,"");
}
TEST(Cfg, ControlCharsRejected){
  for(const char* j : {"{\"scope\":\"a\\tb\"}","{\"scope\":\"a\\u0000b\"}","{\"scope\":\"a\\u007fb\"}","{\"scope\":\"a\\u001fb\"}","{\"ignore\":\"x\\ny\"}","{\"help_link\":\"a\\rb\"}"}) { BotConfig c=load(j); EXPECT_STREQ(c.scope,""); EXPECT_STREQ(c.ignore,"bot"); EXPECT_STREQ(c.help_link,""); }
  EXPECT_STREQ(load("{\"scope\":\"a b~\"}").scope,"a b~");
}
TEST(Cfg, StringsOnlyAcceptStrings){ for(const char* j : {"{\"scope\":5}","{\"scope\":true}","{\"scope\":null}","{\"scope\":[\"a\"]}","{\"scope\":{}}"}) EXPECT_STREQ(load(j).scope,"") << j; }
TEST(Cfg, ShorterValueLeavesNoStaleBytes){ BotConfig a; botConfigDefaults(a); applyTo(a,"{\"scope\":\"longerscope\"}"); applyTo(a,"{\"scope\":\"x\"}"); BotConfig b; botConfigDefaults(b); strcpy(b.scope,"x"); EXPECT_EQ(memcmp(&a,&b,sizeof a),0); }
TEST(Cfg, PrefixRules){
  EXPECT_EQ(load("{\"prefix\":\"?\"}").prefix,'?'); EXPECT_EQ(load("{\"prefix\":\"~\"}").prefix,'~');
  for(const char* j : {"{\"prefix\":\"\"}","{\"prefix\":\"ab\"}","{\"prefix\":\"a\"}","{\"prefix\":\"7\"}","{\"prefix\":\" \"}","{\"prefix\":\"\\u007f\"}","{\"prefix\":\"\\u00e9\"}","{\"prefix\":\"\\t\"}","{\"prefix\":33}","{\"prefix\":\"!!\"}"}) EXPECT_EQ(load(j).prefix,'!') << j;
}
TEST(Cfg, TzRules){
  EXPECT_STREQ(load("{\"tz\":\"America/Vancouver\"}").tz,"America/Vancouver"); EXPECT_STREQ(load("{\"tz\":\"CET-1CEST,M3.5.0,M10.5.0/3\"}").tz,"CET-1CEST,M3.5.0,M10.5.0/3"); EXPECT_STREQ(load("{\"tz\":\"\"}").tz,"");
  for(const char* j : {"{\"tz\":\"Mars/Olympus\"}","{\"tz\":\"utc\"}","{\"tz\":7}","{\"tz\":\"EST5\\tEDT\"}","{\"tz\":\"EST5EDT,M3.2.0,M11.1.0,aaaaaaaaaaaaaaaaaaaaaaaa\"}"}) EXPECT_STREQ(load(j).tz,"America/Toronto") << j;
}

// ---- numbers ----
TEST(Cfg, IntegerRanges){
  EXPECT_EQ(load("{\"cooldown_s\":0}").cooldown_s,0); EXPECT_EQ(load("{\"cooldown_s\":3600}").cooldown_s,3600);
  EXPECT_EQ(load("{\"max_per_min\":1}").max_per_min,1); EXPECT_EQ(load("{\"max_per_min\":30}").max_per_min,30);
  EXPECT_EQ(load("{\"advert_zero_hop_min\":0}").advert_zero_hop_min,0); EXPECT_EQ(load("{\"advert_zero_hop_min\":10080}").advert_zero_hop_min,10080);
  EXPECT_EQ(load("{\"advert_flood_min\":0}").advert_flood_min,0); EXPECT_EQ(load("{\"advert_flood_min\":10080}").advert_flood_min,10080);
  for(const char* j : {"{\"cooldown_s\":3601}","{\"cooldown_s\":-1}","{\"cooldown_s\":10.5}","{\"cooldown_s\":10.0}","{\"cooldown_s\":true}","{\"cooldown_s\":\"20\"}","{\"cooldown_s\":null}","{\"cooldown_s\":99999999999999}","{\"cooldown_s\":18446744073709551615}"}) EXPECT_EQ(load(j).cooldown_s,10) << j;
  for(const char* j : {"{\"max_per_min\":0}","{\"max_per_min\":31}","{\"max_per_min\":256}","{\"max_per_min\":-6}","{\"max_per_min\":6.5}"}) EXPECT_EQ(load(j).max_per_min,6) << j;
  for(const char* j : {"{\"advert_zero_hop_min\":10081}","{\"advert_zero_hop_min\":65536}","{\"advert_zero_hop_min\":-1}"}) EXPECT_EQ(load(j).advert_zero_hop_min,240) << j;
  for(const char* j : {"{\"advert_flood_min\":10081}","{\"advert_flood_min\":\"5\"}"}) EXPECT_EQ(load(j).advert_flood_min,1440) << j;
}
TEST(Cfg, LatLonRanges){
  BotConfig c=load("{\"lat\":-90,\"lon\":180}"); EXPECT_DOUBLE_EQ(c.lat,-90.0); EXPECT_DOUBLE_EQ(c.lon,180.0);
  c=load("{\"lat\":45.5,\"lon\":-75.75}"); EXPECT_DOUBLE_EQ(c.lat,45.5); EXPECT_DOUBLE_EQ(c.lon,-75.75);
  c=load("{\"lat\":90.0001,\"lon\":-180.0001}"); EXPECT_DOUBLE_EQ(c.lat,0.0); EXPECT_DOUBLE_EQ(c.lon,0.0);
  for(const char* j : {"{\"lat\":\"44\"}","{\"lat\":true}","{\"lat\":null}","{\"lat\":[1]}","{\"lat\":1e999}","{\"lon\":-181}"}) { c=load(j); EXPECT_DOUBLE_EQ(c.lat,0.0) << j; EXPECT_DOUBLE_EQ(c.lon,0.0) << j; }
}
TEST(Cfg, NanAndInfRejected){ BotConfig c; botConfigDefaults(c); JsonDocument d; d["lat"]=NAN; d["lon"]=INFINITY; botConfigFromJson(d.as<JsonVariantConst>(),c); EXPECT_DOUBLE_EQ(c.lat,0.0); EXPECT_DOUBLE_EQ(c.lon,0.0); }

// ---- builtin ----
TEST(Cfg, BuiltinRenameAndEnable){
  BotConfig c=load("{\"builtin\":{\"ping\":{\"name\":\"Pong\"},\"8ball\":{\"enabled\":false},\"sun\":{\"name\":\"day\",\"enabled\":false}}}");
  EXPECT_STREQ(c.builtin[BB_PING].name,"pong"); EXPECT_TRUE(c.builtin[BB_PING].enabled); EXPECT_STREQ(c.builtin[BB_8BALL].name,"8ball"); EXPECT_FALSE(c.builtin[BB_8BALL].enabled); EXPECT_STREQ(c.builtin[BB_SUN].name,"day"); EXPECT_FALSE(c.builtin[BB_SUN].enabled);
}
TEST(Cfg, BuiltinInvalidMembersIgnored){
  for(const char* j : {"{\"builtin\":{\"ping\":{\"name\":\"\"}}}","{\"builtin\":{\"ping\":{\"name\":\"a b\"}}}","{\"builtin\":{\"ping\":{\"name\":\"abcdefghijklmnopq\"}}}","{\"builtin\":{\"ping\":{\"name\":5}}}","{\"builtin\":{\"ping\":{\"name\":\"!x\"}}}","{\"builtin\":{\"ping\":{\"name\":\"\\u00e9\"}}}","{\"builtin\":{\"ping\":\"pong\"}}","{\"builtin\":[1]}","{\"builtin\":{\"bogus\":{\"name\":\"zz\"}}}"}) { BotConfig c=load(j); EXPECT_STREQ(c.builtin[BB_PING].name,"ping") << j; }
  BotConfig c=load("{\"builtin\":{\"ping\":{\"enabled\":\"no\",\"name\":\"ok\"}}}"); EXPECT_TRUE(c.builtin[BB_PING].enabled); EXPECT_STREQ(c.builtin[BB_PING].name,"ok");
  EXPECT_STREQ(load("{\"builtin\":{\"ping\":{\"name\":\"abcdefghijklmnop\"}}}").builtin[BB_PING].name,"abcdefghijklmnop");  // 16 fits
}
TEST(Cfg, BuiltinCollisionRejected){
  BotConfig c=load("{\"builtin\":{\"ping\":{\"name\":\"TEST\"},\"sun\":{\"name\":\"moon\"}}}"); EXPECT_STREQ(c.builtin[BB_PING].name,"ping"); EXPECT_STREQ(c.builtin[BB_SUN].name,"moon");
  c=load("{\"builtin\":{\"ping\":{\"name\":\"same\"},\"test\":{\"name\":\"SAME\"}}}"); EXPECT_STREQ(c.builtin[BB_PING].name,"same"); EXPECT_STREQ(c.builtin[BB_TEST].name,"test");  // first wins
  EXPECT_STREQ(load("{\"builtin\":{\"ping\":{\"name\":\"ping\"}}}").builtin[BB_PING].name,"ping");  // own name is fine
}
TEST(Cfg, BuiltinSwapAccepted){ BotConfig c=load("{\"builtin\":{\"ping\":{\"name\":\"test\"},\"test\":{\"name\":\"ping\"}}}"); EXPECT_STREQ(c.builtin[BB_PING].name,"test"); EXPECT_STREQ(c.builtin[BB_TEST].name,"ping"); }
TEST(Cfg, BuiltinRenameVsExistingCustom){
  BotConfig* c=heapDefaults(); applyTo(*c,"{\"custom\":[{\"trigger\":\"foo\",\"reply\":\"x\"}]}");
  applyTo(*c,"{\"builtin\":{\"ping\":{\"name\":\"foo\"}}}"); EXPECT_STREQ(c->builtin[BB_PING].name,"ping");   // kept custom wins
  applyTo(*c,"{\"custom\":[],\"builtin\":{\"ping\":{\"name\":\"foo\"}}}"); EXPECT_STREQ(c->builtin[BB_PING].name,"foo"); EXPECT_EQ(c->num_custom,0);  // custom being cleared
  delete c;
}
TEST(Cfg, BuiltinProcessedBeforeCustom){
  // key order must not matter: custom listed first, the rename still takes effect first
  BotConfig c=load("{\"custom\":[{\"trigger\":\"zap\",\"reply\":\"x\"},{\"trigger\":\"ping\",\"reply\":\"y\"}],\"builtin\":{\"ping\":{\"name\":\"zap\"}}}");
  EXPECT_STREQ(c.builtin[BB_PING].name,"zap");   // custom list replaced, so no clash for the rename
  EXPECT_EQ(c.num_custom,1); EXPECT_STREQ(c.custom[0].trigger,"ping"); EXPECT_STREQ(c.custom[0].reply,"y");  // "zap" now duplicates the built-in and is skipped; "ping" is free
}

// ---- custom ----
TEST(Cfg, CustomElementRules){
  BotConfig c=load("{\"custom\":[{\"trigger\":\"a1\",\"reply\":\"r\"},{\"trigger\":\"b\",\"reply\":\"\"},{\"trigger\":\"\",\"reply\":\"r\"},{\"trigger\":\"has space\",\"reply\":\"r\"},{\"trigger\":\"abcdefghijklmnopq\",\"reply\":\"r\"},{\"reply\":\"r\"},{\"trigger\":\"nor\"},{\"trigger\":\"c\",\"reply\":\"r\",\"enabled\":\"yes\"},{\"trigger\":\"d\",\"reply\":\"r\",\"desc\":7},{\"trigger\":\"e\",\"reply\":\"a\\tb\"},5,null,\"x\",[1],{\"trigger\":\"ok16abcdefghijkl\",\"reply\":\"fine\",\"desc\":\"d\",\"enabled\":false}]}");
  ASSERT_EQ(c.num_custom,2); EXPECT_STREQ(c.custom[0].trigger,"a1"); EXPECT_STREQ(c.custom[0].desc,""); EXPECT_TRUE(c.custom[0].enabled);
  EXPECT_STREQ(c.custom[1].trigger,"ok16abcdefghijkl"); EXPECT_STREQ(c.custom[1].desc,"d"); EXPECT_FALSE(c.custom[1].enabled);
  EXPECT_EQ(c.custom[2].trigger[0],'\0');
}
TEST(Cfg, CustomLengths){
  std::string r160(160,'r'), r161(161,'r'), d48(48,'d'), d49(49,'d');
  BotConfig c=load(("{\"custom\":[{\"trigger\":\"a\",\"reply\":\""+r160+"\",\"desc\":\""+d48+"\"},{\"trigger\":\"b\",\"reply\":\""+r161+"\"},{\"trigger\":\"c\",\"reply\":\"x\",\"desc\":\""+d49+"\"}]}").c_str());
  ASSERT_EQ(c.num_custom,1); EXPECT_STREQ(c.custom[0].reply,r160.c_str()); EXPECT_STREQ(c.custom[0].desc,d48.c_str());
}
TEST(Cfg, CustomUtf8Allowed){ BotConfig c=load("{\"custom\":[{\"trigger\":\"cafe\",\"reply\":\"Caf\\u00e9 \\ud83e\\udded {sender}\",\"desc\":\"na\\u00efve\"}]}"); ASSERT_EQ(c.num_custom,1); EXPECT_STREQ(c.custom[0].reply,"Caf\xC3\xA9 \xF0\x9F\xA7\xAD {sender}"); EXPECT_STREQ(c.custom[0].desc,"na\xC3\xAFve"); }
TEST(Cfg, CustomReplacesListAndEmptyClears){
  BotConfig* c=heapDefaults(); applyTo(*c,"{\"custom\":[{\"trigger\":\"a\",\"reply\":\"1\"},{\"trigger\":\"b\",\"reply\":\"2\"}]}"); ASSERT_EQ(c->num_custom,2);
  applyTo(*c,"{\"custom\":[{\"trigger\":\"z\",\"reply\":\"9\"}]}"); ASSERT_EQ(c->num_custom,1); EXPECT_STREQ(c->custom[0].trigger,"z"); EXPECT_EQ(c->custom[1].trigger[0],'\0');
  applyTo(*c,"{\"custom\":\"nope\"}"); EXPECT_EQ(c->num_custom,1);
  applyTo(*c,"{\"cooldown_s\":5}"); EXPECT_EQ(c->num_custom,1);
  applyTo(*c,"{\"custom\":[]}"); EXPECT_EQ(c->num_custom,0);
  BotConfig* d=heapDefaults(); d->cooldown_s=5; EXPECT_EQ(memcmp(c,d,sizeof(BotConfig)),0);
  delete c; delete d;
}
TEST(Cfg, CustomDuplicateAmongThemselvesAndBuiltinsDisabled){
  BotConfig c=load("{\"builtin\":{\"sun\":{\"enabled\":false}},\"custom\":[{\"trigger\":\"A\",\"reply\":\"1\"},{\"trigger\":\"a\",\"reply\":\"2\"},{\"trigger\":\"SUN\",\"reply\":\"3\"},{\"trigger\":\"b\",\"reply\":\"4\"}]}");
  ASSERT_EQ(c.num_custom,2); EXPECT_STREQ(c.custom[0].reply,"1"); EXPECT_STREQ(c.custom[1].trigger,"b");
}
TEST(Cfg, CustomSkippedElementsDoNotCountTowardSixteen){ std::string j="{\"custom\":[{\"trigger\":\"\",\"reply\":\"x\"}"; for(int i=0;i<16;i++) j+=",{\"trigger\":\"c"+std::to_string(i)+"\",\"reply\":\"r\"}"; j+="]}"; BotConfig c=load(j.c_str()); EXPECT_EQ(c.num_custom,16); EXPECT_STREQ(c.custom[15].trigger,"c15"); }

// ---- web credentials ----
TEST(Cfg, WebSecretsLoadedWhenWellFormed){
  std::string j=std::string("{\"web_salt\":\"")+HEX16+"\",\"web_hash\":\""+HEX64+"\"}"; BotConfig c=load(j.c_str()); EXPECT_STREQ(c.web_salt,HEX16); EXPECT_STREQ(c.web_hash,HEX64);
  BotConfig* d=heapDefaults(); applyTo(*d,j.c_str()); applyTo(*d,"{\"web_salt\":\"\",\"web_hash\":\"\"}"); EXPECT_STREQ(d->web_salt,""); EXPECT_STREQ(d->web_hash,""); delete d;
}
TEST(Cfg, WebSecretsMalformedRejected){
  std::string good16=HEX16, good64=HEX64;
  std::string bad[]={ "{\"web_salt\":\"0123\",\"web_hash\":\""+good64+"\"}", "{\"web_salt\":\""+good16+"\",\"web_hash\":\"abcd\"}", "{\"web_salt\":\"0123456789ABCDEF\",\"web_hash\":\""+good64+"\"}", "{\"web_salt\":\""+good16+"\",\"web_hash\":\""+std::string(64,'g')+"\"}", "{\"web_salt\":\""+good16+"\",\"web_hash\":\"\"}", "{\"web_salt\":\"\",\"web_hash\":\""+good64+"\"}", "{\"web_salt\":5,\"web_hash\":\""+good64+"\"}", "{\"web_salt\":\""+good16+"\"}", "{\"web_hash\":\""+good64+"\"}", "{\"web_salt\":\""+good16+"0\",\"web_hash\":\""+good64+"\"}" };
  for(const std::string& j : bad){ BotConfig c=load(j.c_str()); EXPECT_STREQ(c.web_salt,"") << j; EXPECT_STREQ(c.web_hash,"") << j; }
}
TEST(Cfg, WebSecretsWrittenOnlyForStorage){
  BotConfig* a=heapDefaults(); strcpy(a->web_salt,HEX16); strcpy(a->web_hash,HEX64);
  JsonDocument d; botConfigToJson(*a,d.to<JsonObject>(),true); EXPECT_STREQ(d["web_salt"],HEX16); EXPECT_STREQ(d["web_hash"],HEX64);
  JsonDocument e; botConfigToJson(*a,e.to<JsonObject>(),false); EXPECT_TRUE(e["web_salt"].isNull()); EXPECT_TRUE(e["web_hash"].isNull());
  delete a;
}
TEST(Cfg, FromWebJsonNeverChangesSecrets){
  BotConfig* c=heapDefaults(); strcpy(c->web_salt,HEX16); strcpy(c->web_hash,HEX64);
  std::string other(64,'f');
  applyTo(*c,("{\"web_salt\":\"ffffffffffffffff\",\"web_hash\":\""+other+"\",\"cooldown_s\":77,\"scope\":\"x\"}").c_str(),true);
  EXPECT_STREQ(c->web_salt,HEX16); EXPECT_STREQ(c->web_hash,HEX64); EXPECT_EQ(c->cooldown_s,77); EXPECT_STREQ(c->scope,"x");
  applyTo(*c,"{\"web_salt\":\"\",\"web_hash\":\"\"}",true); EXPECT_STREQ(c->web_salt,HEX16); EXPECT_STREQ(c->web_hash,HEX64);
  applyTo(*c,("{\"web_salt\":\"ffffffffffffffff\",\"web_hash\":\""+other+"\"}").c_str(),false); EXPECT_STREQ(c->web_salt,"ffffffffffffffff");  // trusted path does load them
  delete c;
}
TEST(Cfg, FromWebJsonNullAndNonObject){ BotConfig* a=heapDefaults(); BotConfig* b=heapDefaults(); botConfigFromWebJson(JsonVariantConst(),*a); EXPECT_EQ(memcmp(a,b,sizeof(BotConfig)),0); applyTo(*a,"[1,2]",true); EXPECT_EQ(memcmp(a,b,sizeof(BotConfig)),0); delete a; delete b; }

// ---- null / unknown / round trip ----
TEST(Cfg, NullAndNonObjectChangeNothing){ BotConfig* a=heapDefaults(); BotConfig* b=heapDefaults(); botConfigFromJson(JsonVariantConst(),*a); EXPECT_EQ(memcmp(a,b,sizeof(BotConfig)),0); for(const char* j : {"5","\"str\"","true","[{\"cooldown_s\":1}]"}) { applyTo(*a,j); EXPECT_EQ(memcmp(a,b,sizeof(BotConfig)),0) << j; } delete a; delete b; }
TEST(Cfg, UnknownKeysIgnoredAndIndependent){ BotConfig c=load("{\"bogus\":{\"x\":[1]},\"cooldown_s\":\"bad\",\"max_per_min\":9,\"scope\":\"ok\"}"); EXPECT_EQ(c.cooldown_s,10); EXPECT_EQ(c.max_per_min,9); EXPECT_STREQ(c.scope,"ok"); }
TEST(Cfg, FullRoundTripMemcmp){
  BotConfig* a=heapDefaults();
  a->num_channels=1; strcpy(a->channels[0],"#mesh-test"); strcpy(a->scope,"test-region"); a->prefix='?'; a->cooldown_s=3600; a->max_per_min=30; strcpy(a->ignore,"bot,relay,rpt"); a->lat=45.4236; a->lon=-75.7009; strcpy(a->tz,"CET-1CEST,M3.5.0,M10.5.0/3"); strcpy(a->help_link,"https://example.org/bot"); a->advert_zero_hop_min=0; a->advert_flood_min=10080;
  strcpy(a->builtin[BB_PING].name,"test"); strcpy(a->builtin[BB_TEST].name,"ping"); a->builtin[BB_SUN].enabled=false; strcpy(a->builtin[BB_ROLL].name,"dice"); a->builtin[BB_8BALL].enabled=false;
  a->num_custom=16;
  for(int i=0;i<16;i++){ snprintf(a->custom[i].trigger,17,"cmd%d",i); snprintf(a->custom[i].reply,161,"R%d \xC3\xA9\xF0\x9F\xA7\xAD {sender} \xE2\x80\xA6 %s",i,std::string(100,'z').c_str()); snprintf(a->custom[i].desc,49,"d\xC3\xA9%d",i); a->custom[i].enabled=(i%3!=0); a->custom[i].pfx=(i%2==0); a->custom[i].bare=(i%2==1)||(i%4==0); }
  strcpy(a->web_salt,HEX16); strcpy(a->web_hash,HEX64);
  JsonDocument d; botConfigToJson(*a,d.to<JsonObject>(),true);
  BotConfig* b=heapDefaults(); botConfigFromJson(d.as<JsonVariantConst>(),*b);
  EXPECT_EQ(memcmp(a,b,sizeof(BotConfig)),0);
  // through text, as on disk. ArduinoJson parses short decimals as 32-bit floats, so lat/lon
  // only survive exactly here when they are float-exact (the cost is under a metre).
  a->lat=45.5; a->lon=-75.75;
  JsonDocument d3; botConfigToJson(*a,d3.to<JsonObject>(),true);
  std::string s; serializeJson(d3,s); JsonDocument d2; ASSERT_EQ(deserializeJson(d2,s),DeserializationError::Ok);
  BotConfig* e=heapDefaults(); botConfigFromJson(d2.as<JsonVariantConst>(),*e);
  EXPECT_EQ(memcmp(a,e,sizeof(BotConfig)),0);
  delete a; delete b; delete e;
}
TEST(Cfg, ToJsonCopiesStrings){ BotConfig* a=heapDefaults(); strcpy(a->scope,"abc"); JsonDocument d; botConfigToJson(*a,d.to<JsonObject>(),true); memset(a->scope,'Q',3); EXPECT_STREQ(d["scope"],"abc"); delete a; }
TEST(Cfg, ToJsonClampsCorruptNumCustom){ BotConfig* a=heapDefaults(); a->num_custom=200; JsonDocument d; botConfigToJson(*a,d.to<JsonObject>(),false); EXPECT_EQ(d["custom"].as<JsonArray>().size(),16u); delete a; }
TEST(Cfg, HostileDocumentChangesNothing){
  const char* j="{\"channel\":123,\"scope\":[],\"prefix\":5,\"cooldown_s\":true,\"max_per_min\":\"6\",\"ignore\":{},\"lat\":\"x\",\"lon\":null,\"tz\":false,\"help_link\":1.5,"
    "\"advert_zero_hop_min\":-1,\"advert_flood_min\":99999,\"builtin\":[1,2],\"custom\":\"nope\",\"web_salt\":1,\"web_hash\":[]}";
  BotConfig* a=heapDefaults(); BotConfig* b=heapDefaults(); applyTo(*a,j); EXPECT_EQ(memcmp(a,b,sizeof(BotConfig)),0);
  const char* k="{\"builtin\":{\"ping\":5,\"test\":{\"name\":7,\"enabled\":\"yes\"},\"path\":{\"name\":[],\"enabled\":1},\"help\":null,\"bogus\":{}},\"custom\":[1,\"x\",null,[],{\"trigger\":5,\"reply\":\"x\"},{\"trigger\":\"ok\"},{\"trigger\":\"ok\",\"reply\":{}},{\"trigger\":[],\"reply\":\"x\"}]}";
  applyTo(*a,k); EXPECT_EQ(memcmp(a,b,sizeof(BotConfig)),0);
  applyTo(*a,j,true); applyTo(*a,k,true); EXPECT_EQ(memcmp(a,b,sizeof(BotConfig)),0);
  delete a; delete b;
}

// ---- strict web entry point: whole custom list or nothing, with reasons ----
static void webStrict(BotConfig& c, const std::string& json, BotApplyReport& rep){ JsonDocument d; ASSERT_FALSE(deserializeJson(d,json)); botConfigFromWebJsonStrict(d.as<JsonVariantConst>(),c,rep); }
static BotConfig* withTwoCustom(){ BotConfig* c=heapDefaults(); applyTo(*c,"{\"custom\":[{\"trigger\":\"a\",\"reply\":\"1\"},{\"trigger\":\"b\",\"reply\":\"2\"}]}"); return c; }
static bool has(const char* s, const char* part){ return strstr(s,part)!=nullptr; }

TEST(Strict, ValidListReplaces){
  BotConfig* c=withTwoCustom(); BotApplyReport rep;
  webStrict(*c,"{\"custom\":[{\"trigger\":\"Fx\",\"reply\":\"sunny\",\"desc\":\"weather\",\"enabled\":false}]}",rep);
  EXPECT_STREQ(rep.custom,""); EXPECT_STREQ(rep.builtin,"");
  ASSERT_EQ(c->num_custom,1); EXPECT_STREQ(c->custom[0].trigger,"fx"); EXPECT_FALSE(c->custom[0].enabled); delete c;
}
TEST(Strict, OneBadElementRejectsTheWholeList){
  struct Case { const char* elem; const char* reason; } cases[] = {
    {"{\"trigger\":\"b\",\"reply\":\"line1\\nline2\"}", "reply"},                           // newline in the reply
    {"{\"trigger\":\"w?x\",\"reply\":\"r\"}", "trigger"},                                    // punctuation
    {"{\"trigger\":\"\",\"reply\":\"r\"}", "trigger"},
    {"{\"trigger\":\"ping\",\"reply\":\"r\"}", "built-in"},                                  // a built-in's name
    {"{\"trigger\":\"b\",\"reply\":\"\"}", "reply"},
    {"{\"trigger\":\"b\",\"reply\":\"r\",\"desc\":5}", "description"},                       // wrong type
    {"{\"trigger\":\"b\",\"reply\":\"r\",\"enabled\":\"yes\"}", "enabled"},
    {"{\"trigger\":\"b\",\"reply\":\"r\",\"desc\":\"tab\\there\"}", "description"},
    {"\"just text\"", "not a command"},
  };
  for (const Case& k : cases) {
    BotConfig* c=withTwoCustom(); BotApplyReport rep;
    webStrict(*c,std::string("{\"cooldown_s\":42,\"custom\":[{\"trigger\":\"ok\",\"reply\":\"fine\"},")+k.elem+"]}",rep);
    EXPECT_TRUE(has(rep.custom,"#2")) << k.elem << " -> " << rep.custom;
    EXPECT_TRUE(has(rep.custom,k.reason)) << k.elem << " -> " << rep.custom;
    ASSERT_EQ(c->num_custom,2) << k.elem; EXPECT_STREQ(c->custom[0].trigger,"a"); EXPECT_STREQ(c->custom[1].reply,"2");   // untouched
    EXPECT_EQ(c->cooldown_s,42);   // other fields still applied
    delete c;
  }
}
TEST(Strict, ReportNamesPositionAndTrigger){
  BotConfig* c=withTwoCustom(); BotApplyReport rep;
  webStrict(*c,"{\"custom\":[{\"trigger\":\"one\",\"reply\":\"1\"},{\"trigger\":\"two\",\"reply\":\"2\"},{\"trigger\":\"Sun\",\"reply\":\"3\"}]}",rep);
  EXPECT_TRUE(has(rep.custom,"#3 'sun'")) << rep.custom; EXPECT_TRUE(has(rep.custom,"built-in 'sun'")) << rep.custom; delete c;
  c=withTwoCustom(); webStrict(*c,"{\"custom\":[{\"trigger\":\"w\xC3\xA9x!\",\"reply\":\"1\"}]}",rep);
  EXPECT_TRUE(has(rep.custom,"#1 'w??x!'")) << rep.custom;   // non-ASCII shown as '?'
  EXPECT_EQ(c->num_custom,2); delete c;
}
TEST(Strict, Utf8ByteLimits){
  std::string emoji="\xF0\x9F\xA7\xAD";   // 4 bytes, one character
  std::string r160; for(int i=0;i<40;i++) r160+=emoji;   // 160 bytes: accepted
  std::string r164=r160+emoji;                          // 41 characters, 164 bytes: refused
  BotConfig* c=withTwoCustom(); BotApplyReport rep;
  webStrict(*c,"{\"custom\":[{\"trigger\":\"e\",\"reply\":\""+r160+"\"}]}",rep); EXPECT_STREQ(rep.custom,""); EXPECT_EQ(c->num_custom,1);
  webStrict(*c,"{\"custom\":[{\"trigger\":\"e\",\"reply\":\""+r164+"\"}]}",rep); EXPECT_TRUE(has(rep.custom,"160 bytes")) << rep.custom; EXPECT_EQ(c->num_custom,1); EXPECT_STREQ(c->custom[0].reply,r160.c_str());
  std::string d52; for(int i=0;i<13;i++) d52+=emoji;
  webStrict(*c,"{\"custom\":[{\"trigger\":\"e\",\"reply\":\"x\",\"desc\":\""+d52+"\"}]}",rep); EXPECT_TRUE(has(rep.custom,"48 bytes")) << rep.custom;
  delete c;
}
TEST(Strict, DuplicatesAndTooMany){
  BotConfig* c=withTwoCustom(); BotApplyReport rep;
  webStrict(*c,"{\"custom\":[{\"trigger\":\"x\",\"reply\":\"1\"},{\"trigger\":\"X\",\"reply\":\"2\"}]}",rep);
  EXPECT_TRUE(has(rep.custom,"#2 'x'")) << rep.custom; EXPECT_TRUE(has(rep.custom,"#1")) << rep.custom; EXPECT_EQ(c->num_custom,2);
  std::string j="{\"custom\":["; for(int i=0;i<17;i++){ j+=(i?",":""); j+="{\"trigger\":\"c"+std::to_string(i)+"\",\"reply\":\"r\"}"; } j+="]}";
  webStrict(*c,j,rep); EXPECT_TRUE(has(rep.custom,"16")) << rep.custom; EXPECT_EQ(c->num_custom,2);
  delete c;
}
TEST(Strict, EmptyListClears){ BotConfig* c=withTwoCustom(); BotApplyReport rep; webStrict(*c,"{\"custom\":[]}",rep); EXPECT_STREQ(rep.custom,""); EXPECT_EQ(c->num_custom,0); delete c; }
TEST(Strict, RefusedRenameIsReported){
  BotConfig* c=withTwoCustom(); BotApplyReport rep;
  webStrict(*c,"{\"builtin\":{\"ping\":{\"name\":\"test\"}}}",rep);
  EXPECT_STREQ(c->builtin[BB_PING].name,"ping"); EXPECT_TRUE(has(rep.builtin,"'ping'")) << rep.builtin; EXPECT_TRUE(has(rep.builtin,"built-in 'test'")) << rep.builtin;
  webStrict(*c,"{\"builtin\":{\"ping\":{\"name\":\"a\"}}}",rep);
  EXPECT_STREQ(c->builtin[BB_PING].name,"ping"); EXPECT_TRUE(has(rep.builtin,"custom command 'a'")) << rep.builtin;
  webStrict(*c,"{\"builtin\":{\"ping\":{\"name\":\"p-ng\"}}}",rep);
  EXPECT_STREQ(c->builtin[BB_PING].name,"ping"); EXPECT_TRUE(has(rep.builtin,"letters or digits")) << rep.builtin;
  webStrict(*c,"{\"builtin\":{\"ping\":{\"name\":\"pong\"},\"test\":{\"name\":\"ping\"}}}",rep);   // a chain is fine
  EXPECT_STREQ(rep.builtin,""); EXPECT_STREQ(c->builtin[BB_PING].name,"pong"); EXPECT_STREQ(c->builtin[BB_TEST].name,"ping");
  delete c;
}
TEST(Strict, RenameRecheckedWhenListRefused){
  // The rename to "a" would be fine if the new list replaced the old one, but the new list is
  // refused, so the old list (which has "a") stays and the rename must be refused too.
  BotConfig* c=withTwoCustom(); BotApplyReport rep;
  webStrict(*c,"{\"builtin\":{\"ping\":{\"name\":\"a\"}},\"custom\":[{\"trigger\":\"z\",\"reply\":\"\"}]}",rep);
  EXPECT_STREQ(c->builtin[BB_PING].name,"ping"); EXPECT_EQ(c->num_custom,2);
  EXPECT_TRUE(has(rep.custom,"#1")) << rep.custom; EXPECT_TRUE(has(rep.builtin,"custom command 'a'")) << rep.builtin;
  // And the list clashing with the new built-in name: the list is refused, the rename kept.
  webStrict(*c,"{\"builtin\":{\"ping\":{\"name\":\"zap\"}},\"custom\":[{\"trigger\":\"zap\",\"reply\":\"x\"}]}",rep);
  EXPECT_STREQ(c->builtin[BB_PING].name,"zap"); EXPECT_EQ(c->num_custom,2); EXPECT_TRUE(has(rep.custom,"built-in 'zap'")) << rep.custom;
  delete c;
}
TEST(Strict, SecretsNeverChanged){
  BotConfig* c=heapDefaults(); BotApplyReport rep;
  webStrict(*c,std::string("{\"web_salt\":\"")+HEX16+"\",\"web_hash\":\""+HEX64+"\"}",rep); EXPECT_STREQ(c->web_hash,""); delete c;
}

// ---- serialisation that reports a document that ran out of memory ----
class TinyAllocator : public ArduinoJson::Allocator {
public:
  explicit TinyAllocator(size_t budget) : left(budget) {}
  void* allocate(size_t n) override { if (n > left) return nullptr; left -= n; return malloc(n); }
  void deallocate(void* p) override { free(p); }
  void* reallocate(void* p, size_t n) override { if (n > left) return nullptr; left -= n; return realloc(p, n); }
  size_t left;
};
TEST(ToJson, CheckedReportsOverflow){
  BotConfig* a=heapDefaults(); a->num_custom=3;
  for(int i=0;i<3;i++){ snprintf(a->custom[i].trigger,17,"c%d",i); strcpy(a->custom[i].reply,"r"); }
  JsonDocument ok; EXPECT_TRUE(botConfigToJsonChecked(*a,ok,true));
  TinyAllocator tiny(600);
  JsonDocument small(&tiny); EXPECT_FALSE(botConfigToJsonChecked(*a,small,true));
  delete a;
}

// ---- milestone 2: new built-ins, lookups on custom commands, watched nodes ----
static const char* UUID_A="0A1b2C3d-4e5F-6a7B-8c9D-0e1F2a3B4c5D";
static const char* UUID_A_LOWER="0a1b2c3d-4e5f-6a7b-8c9d-0e1f2a3b4c5d";
static std::string node64(char ch){ return std::string(64,ch); }
static std::string wcmd(const std::string& extra){ return "{\"custom\":[{\"trigger\":\"q\",\"reply\":\"r\""+extra+"}]}"; }
static std::string wwatch(const std::string& elems){ return "{\"watch\":["+elems+"]}"; }

TEST(M2, DefaultsForNewSettings){
  BotConfig* c=heapDefaults();
  EXPECT_STREQ(c->builtin[BB_WX].name,"wx"); EXPECT_STREQ(c->builtin[BB_AURORA].name,"aurora"); EXPECT_STREQ(c->builtin[BB_ALERTS].name,"alerts"); EXPECT_STREQ(c->builtin[BB_NODES].name,"nodes");
  for(int i=BB_WX;i<BB_COUNT;i++) EXPECT_TRUE(c->builtin[i].enabled);
  EXPECT_EQ(BB_8BALL,9); EXPECT_EQ(BB_WX,10); EXPECT_EQ(c->watch_online_min,720); EXPECT_EQ(c->num_watch,0);
  EXPECT_STREQ(botBuiltinId(BB_NODES),"nodes");
  delete c;
}
TEST(M2, OldDocumentLoadsWithDefaults){
  const char* old="{\"channel\":\"#old\",\"cooldown_s\":20,\"builtin\":{\"ping\":{\"name\":\"pong\",\"enabled\":false},\"8ball\":{\"enabled\":true}},"
    "\"custom\":[{\"trigger\":\"hello\",\"reply\":\"Hi {sender}\",\"desc\":\"greet\",\"enabled\":true}],\"web_salt\":\"0123456789abcdef\",\"web_hash\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\"}";
  BotConfig* c=heapDefaults(); applyTo(*c,old);
  EXPECT_EQ(c->num_channels,1); EXPECT_STREQ(c->channels[0],"#old"); EXPECT_STREQ(c->builtin[BB_PING].name,"pong"); EXPECT_FALSE(c->builtin[BB_PING].enabled);
  ASSERT_EQ(c->num_custom,1); EXPECT_STREQ(c->custom[0].url,""); EXPECT_STREQ(c->custom[0].path,""); EXPECT_EQ(c->custom[0].cache_s,0);
  EXPECT_STREQ(c->builtin[BB_WX].name,"wx"); EXPECT_TRUE(c->builtin[BB_NODES].enabled); EXPECT_EQ(c->watch_online_min,720); EXPECT_EQ(c->num_watch,0);
  delete c;
}
TEST(M2, NewBuiltinsRenameAndDisable){
  BotConfig c=load("{\"builtin\":{\"wx\":{\"name\":\"Weather\"},\"aurora\":{\"enabled\":false},\"alerts\":{\"name\":\"wx\"},\"nodes\":{\"name\":\"net\"}}}");
  EXPECT_STREQ(c.builtin[BB_WX].name,"weather"); EXPECT_FALSE(c.builtin[BB_AURORA].enabled); EXPECT_STREQ(c.builtin[BB_ALERTS].name,"wx"); EXPECT_STREQ(c.builtin[BB_NODES].name,"net");
}
TEST(M2, StoredCustomClashingWithNewBuiltinIsSkipped){
  // A file written before "wx" became a built-in: the custom command is skipped, the built-in keeps its name.
  BotConfig c=load("{\"custom\":[{\"trigger\":\"wx\",\"reply\":\"old\"},{\"trigger\":\"Nodes\",\"reply\":\"old2\"},{\"trigger\":\"keep\",\"reply\":\"k\"}]}");
  EXPECT_STREQ(c.builtin[BB_WX].name,"wx"); EXPECT_STREQ(c.builtin[BB_NODES].name,"nodes");
  ASSERT_EQ(c.num_custom,1); EXPECT_STREQ(c.custom[0].trigger,"keep");
}
TEST(M2, CustomLookupAccepted){
  BotConfig c=load(wcmd(",\"url\":\"https://example.org/api?q={args}\",\"path\":\"main.temp\",\"cache_s\":300").c_str());
  ASSERT_EQ(c.num_custom,1); EXPECT_STREQ(c.custom[0].url,"https://example.org/api?q={args}"); EXPECT_STREQ(c.custom[0].path,"main.temp"); EXPECT_EQ(c.custom[0].cache_s,300);
  c=load(wcmd(",\"url\":\"http://example.org/a\",\"path\":\"a[0].b\",\"cache_s\":3600,\"reply\":\"x {fetch}\"").c_str()); EXPECT_EQ(c.num_custom,1);
  c=load(wcmd(",\"url\":\"\",\"path\":\"\",\"cache_s\":0").c_str()); EXPECT_EQ(c.num_custom,1);
  c=load(wcmd(",\"cache_s\":10").c_str()); ASSERT_EQ(c.num_custom,1); EXPECT_EQ(c.custom[0].cache_s,10);   // cache_s needs no url
}
TEST(M2, CustomLookupRejected){
  for(const char* extra : {
        ",\"url\":\"ftp://example.org/a\",\"path\":\"a\"",             // scheme
        ",\"url\":\"https://example.org/a?x={args}&y={args}\\u0000\",\"path\":\"a\"",
        ",\"url\":\"https://example.org/a?x=1\"",                       // url needs a path
        ",\"path\":\"a\"",                                              // path needs a url
        ",\"url\":\"https://example.org/a\",\"path\":\"a..b\"",         // bad path
        ",\"url\":\"https://example.org/a\",\"path\":\"a\",\"cache_s\":3601",
        ",\"url\":\"https://example.org/a\",\"path\":\"a\",\"cache_s\":-1",
        ",\"url\":\"https://example.org/a\",\"path\":\"a\",\"cache_s\":\"5\"",
        ",\"url\":5,\"path\":\"a\"",
        ",\"url\":\"https://example.org/a\",\"path\":7",
        ",\"reply\":\"{fetch}\"",                                       // {fetch} without a url
      }) { BotConfig c=load(wcmd(extra).c_str()); EXPECT_EQ(c.num_custom,0) << extra; }
  std::string u160="https://example.org/"+std::string(140,'a'), u161=u160+"a";
  EXPECT_EQ(load(wcmd(",\"url\":\""+u160+"\",\"path\":\"a\"").c_str()).num_custom,1);
  EXPECT_EQ(load(wcmd(",\"url\":\""+u161+"\",\"path\":\"a\"").c_str()).num_custom,0);
  std::string p48=std::string(32,'p')+"."+std::string(15,'q'), p49=p48+"q";
  EXPECT_EQ(load(wcmd(",\"url\":\"https://example.org/a\",\"path\":\""+p48+"\"").c_str()).num_custom,1);
  EXPECT_EQ(load(wcmd(",\"url\":\"https://example.org/a\",\"path\":\""+p49+"\"").c_str()).num_custom,0);
}
TEST(M2, StrictCustomLookupReasons){
  struct Case { const char* extra; const char* reason; } cases[] = {
    {",\"url\":\"ftp://example.org/a\",\"path\":\"a\"", "url: must start with http"},
    {",\"url\":\"https://example.org/a?x=1\"", "url needs a path"},
    {",\"path\":\"a\"", "path needs a url"},
    {",\"url\":\"https://example.org/a\",\"path\":\"a..b\"", "path"},
    {",\"url\":\"https://example.org/a\",\"path\":\"a\",\"cache_s\":3601", "cache_s"},
    {",\"reply\":\"x {fetch}\"", "{fetch} needs a url"},
    {",\"url\":\"https://example.org/{args}/a\",\"path\":\"a\"", "url: {args} is only allowed after the ?"},
    {",\"url\":7,\"path\":\"a\"", "url"},
  };
  for(const Case& k : cases){
    BotConfig* c=withTwoCustom(); BotApplyReport rep;
    webStrict(*c,"{\"custom\":[{\"trigger\":\"ok\",\"reply\":\"fine\"},{\"trigger\":\"q\",\"reply\":\"r\""+std::string(k.extra)+"}]}",rep);
    EXPECT_TRUE(has(rep.custom,"#2 'q'")) << k.extra << " -> " << rep.custom;
    EXPECT_TRUE(has(rep.custom,k.reason)) << k.extra << " -> " << rep.custom;
    ASSERT_EQ(c->num_custom,2); EXPECT_STREQ(c->custom[0].trigger,"a"); delete c;
  }
  BotConfig* c=withTwoCustom(); BotApplyReport rep;
  webStrict(*c,wcmd(",\"url\":\"https://example.org/a?q={args}\",\"path\":\"v\",\"cache_s\":60"),rep);
  EXPECT_STREQ(rep.custom,""); ASSERT_EQ(c->num_custom,1); EXPECT_STREQ(c->custom[0].path,"v"); EXPECT_EQ(c->custom[0].cache_s,60); delete c;
}
TEST(M2, WatchDefaultsAndAccepted){
  BotConfig c=load(("{\"watch_online_min\":5,"+wwatch("{\"label\":\"Hill\",\"kind\":\"observer\",\"id\":\""+std::string(UUID_A)+"\"},{\"label\":\"Fort\",\"kind\":\"node\",\"id\":\""+std::string(64,'a')+"\"}").substr(1)).c_str());
  EXPECT_EQ(c.watch_online_min,5); ASSERT_EQ(c.num_watch,2);
  EXPECT_STREQ(c.watch[0].label,"Hill"); EXPECT_EQ(c.watch[0].kind,BW_OBSERVER); EXPECT_STREQ(c.watch[0].id,UUID_A_LOWER);
  EXPECT_STREQ(c.watch[1].label,"Fort"); EXPECT_EQ(c.watch[1].kind,BW_NODE); EXPECT_STREQ(c.watch[1].id,std::string(64,'A').c_str());
  EXPECT_EQ(load("{\"watch_online_min\":10080}").watch_online_min,10080);
  for(const char* j : {"{\"watch_online_min\":4}","{\"watch_online_min\":10081}","{\"watch_online_min\":\"60\"}","{\"watch_online_min\":60.5}"}) EXPECT_EQ(load(j).watch_online_min,720) << j;
}
TEST(M2, WatchElementRulesLenientSkips){
  std::string good="{\"label\":\"ok\",\"kind\":\"node\",\"id\":\""+node64('1')+"\"}";
  std::string bad[]={
    "{\"label\":\"\",\"kind\":\"node\",\"id\":\""+node64('1')+"\"}",
    "{\"label\":\""+std::string(25,'l')+"\",\"kind\":\"node\",\"id\":\""+node64('1')+"\"}",
    "{\"label\":\"a\\tb\",\"kind\":\"node\",\"id\":\""+node64('1')+"\"}",
    "{\"label\":\"x\",\"kind\":\"relay\",\"id\":\""+node64('1')+"\"}",
    "{\"label\":\"x\",\"kind\":\"node\",\"id\":\""+std::string(63,'1')+"\"}",
    "{\"label\":\"x\",\"kind\":\"node\",\"id\":\""+std::string(64,'g')+"\"}",
    "{\"label\":\"x\",\"kind\":\"observer\",\"id\":\"0a1b2c3d-4e5f-6a7b-8c9d-0e1f2a3b4c5\"}",
    "{\"label\":\"x\",\"kind\":\"observer\",\"id\":\"0a1b2c3d04e5f-6a7b-8c9d-0e1f2a3b4c5d\"}",
    "{\"label\":\"x\",\"kind\":\"observer\",\"id\":\""+node64('1')+"\"}",
    "{\"label\":\"x\",\"kind\":\"node\",\"id\":\""+std::string(UUID_A)+"\"}",
    "{\"label\":5,\"kind\":\"node\",\"id\":\""+node64('1')+"\"}",
    "{\"kind\":\"node\",\"id\":\""+node64('1')+"\"}",
    "7"};
  for(const std::string& b : bad){ BotConfig c=load(wwatch(b+","+good).c_str()); ASSERT_EQ(c.num_watch,1) << b; EXPECT_STREQ(c.watch[0].label,"ok") << b; }
  EXPECT_EQ(load(wwatch("{\"label\":\""+std::string(24,'l')+"\",\"kind\":\"node\",\"id\":\""+node64('1')+"\"}").c_str()).num_watch,1);
  EXPECT_EQ(load(wwatch("{\"label\":\"A\",\"kind\":\"node\",\"id\":\""+node64('1')+"\"},{\"label\":\"B\",\"kind\":\"node\",\"id\":\""+node64('1')+"\"}").c_str()).num_watch,2);   // same id twice is allowed
}
TEST(M2, WatchLenientKeepsFirstFour){
  std::string e; for(int i=0;i<6;i++) e+=std::string(i?",":"")+"{\"label\":\"n"+std::to_string(i)+"\",\"kind\":\"node\",\"id\":\""+node64("0123456789abcdef"[i])+"\"}";
  BotConfig c=load(wwatch(e).c_str()); ASSERT_EQ(c.num_watch,4); EXPECT_STREQ(c.watch[3].label,"n3"); 
  BotConfig* d=heapDefaults(); applyTo(*d,wwatch(e).c_str()); applyTo(*d,"{\"watch\":[]}"); EXPECT_EQ(d->num_watch,0); EXPECT_EQ(d->watch[0].label[0],'\0');
  applyTo(*d,wwatch(e).c_str()); applyTo(*d,"{\"watch\":\"nope\"}"); EXPECT_EQ(d->num_watch,4);   // not an array: unchanged
  delete d;
}
TEST(M2, StrictWatchReasonsAndUntouched){
  struct Case { std::string elem; const char* reason; } cases[] = {
    {"{\"label\":\"\",\"kind\":\"node\",\"id\":\""+node64('1')+"\"}", "label"},
    {"{\"label\":\"x\",\"kind\":\"relay\",\"id\":\""+node64('1')+"\"}", "kind"},
    {"{\"label\":\"x\",\"kind\":\"node\",\"id\":\""+std::string(63,'1')+"\"}", "id"},
    {"{\"label\":\"x\",\"kind\":\"observer\",\"id\":\"not-a-uuid\"}", "id"},
    {"5", "not a watch entry"},
  };
  for(const Case& k : cases){
    BotConfig* c=heapDefaults(); applyTo(*c,wwatch("{\"label\":\"old\",\"kind\":\"node\",\"id\":\""+node64('2')+"\"}").c_str());
    BotApplyReport rep;
    webStrict(*c,"{\"cooldown_s\":42,\"watch_online_min\":99,"+wwatch("{\"label\":\"fine\",\"kind\":\"node\",\"id\":\""+node64('3')+"\"},"+k.elem).substr(1),rep);
    EXPECT_TRUE(has(rep.watch,"#2")) << k.elem << " -> " << rep.watch; EXPECT_TRUE(has(rep.watch,k.reason)) << k.elem << " -> " << rep.watch;
    ASSERT_EQ(c->num_watch,1); EXPECT_STREQ(c->watch[0].label,"old"); EXPECT_EQ(c->cooldown_s,42); EXPECT_EQ(c->watch_online_min,99);
    EXPECT_STREQ(rep.custom,""); delete c;
  }
  BotConfig* c=heapDefaults(); BotApplyReport rep;
  std::string e; for(int i=0;i<5;i++) e+=std::string(i?",":"")+"{\"label\":\"n"+std::to_string(i)+"\",\"kind\":\"node\",\"id\":\""+node64('4')+"\"}";
  webStrict(*c,wwatch(e),rep); EXPECT_TRUE(has(rep.watch,"#5")) << rep.watch; EXPECT_TRUE(has(rep.watch,"too many")) << rep.watch; EXPECT_EQ(c->num_watch,0);
  webStrict(*c,wwatch("{\"label\":\"Hill\",\"kind\":\"observer\",\"id\":\""+std::string(UUID_A)+"\"}"),rep); EXPECT_STREQ(rep.watch,""); ASSERT_EQ(c->num_watch,1); EXPECT_STREQ(c->watch[0].id,UUID_A_LOWER);
  webStrict(*c,"{\"watch\":[]}",rep); EXPECT_STREQ(rep.watch,""); EXPECT_EQ(c->num_watch,0);
  delete c;
}
TEST(M2, StrictLeavesWatchAndCustomIndependent){
  // a refused custom list does not stop a valid watch list, and the reverse
  BotConfig* c=heapDefaults(); BotApplyReport rep;
  webStrict(*c,"{\"custom\":[{\"trigger\":\"q\",\"reply\":\"{fetch}\"}],"+wwatch("{\"label\":\"A\",\"kind\":\"node\",\"id\":\""+node64('5')+"\"}").substr(1),rep);
  EXPECT_TRUE(has(rep.custom,"{fetch} needs a url")) << rep.custom; EXPECT_STREQ(rep.watch,""); EXPECT_EQ(c->num_custom,0); EXPECT_EQ(c->num_watch,1);
  delete c;
}
TEST(M2, StrictStillNeverTouchesSecrets){
  BotConfig* c=heapDefaults(); strcpy(c->web_salt,HEX16); strcpy(c->web_hash,HEX64); BotApplyReport rep;
  webStrict(*c,"{\"web_salt\":\"ffffffffffffffff\",\"web_hash\":\""+std::string(64,'f')+"\","+wwatch("{\"label\":\"A\",\"kind\":\"node\",\"id\":\""+node64('5')+"\"}").substr(1),rep);
  EXPECT_STREQ(c->web_salt,HEX16); EXPECT_STREQ(c->web_hash,HEX64); EXPECT_EQ(c->num_watch,1); delete c;
}
TEST(M2, FullRoundTripWithLookupsAndWatch){
  BotConfig* a=heapDefaults();
  a->num_custom=16;
  for(int i=0;i<16;i++){ snprintf(a->custom[i].trigger,17,"cmd%d",i); snprintf(a->custom[i].reply,161,(i%4!=3)?"R%d {fetch}":"R%d plain",i); snprintf(a->custom[i].desc,49,"d%d",i); a->custom[i].enabled=(i%2==0); a->custom[i].pfx=true; a->custom[i].bare=(i%3==0);
    if(i%4!=3){ snprintf(a->custom[i].url,161,"https://example.org/v%d?q={args}",i); snprintf(a->custom[i].path,49,"data[%d].v",i); a->custom[i].cache_s=(uint16_t)(i*200); } }
  
  a->num_watch=4; a->watch_online_min=1440;
  for(int i=0;i<4;i++){ snprintf(a->watch[i].label,25,"Site %d",i); a->watch[i].kind=(i%2)?BW_NODE:BW_OBSERVER;
    if(a->watch[i].kind==BW_NODE) strcpy(a->watch[i].id,node64("ABCDEF"[i]).c_str()); else strcpy(a->watch[i].id,UUID_A_LOWER); }
  strcpy(a->builtin[BB_WX].name,"weather"); a->builtin[BB_NODES].enabled=false;
  JsonDocument d; ASSERT_TRUE(botConfigToJsonChecked(*a,d,true));
  BotConfig* b=heapDefaults(); botConfigFromJson(d.as<JsonVariantConst>(),*b); EXPECT_EQ(memcmp(a,b,sizeof(BotConfig)),0);
  std::string s; serializeJson(d,s); JsonDocument d2; ASSERT_EQ(deserializeJson(d2,s),DeserializationError::Ok);
  BotConfig* e=heapDefaults(); botConfigFromJson(d2.as<JsonVariantConst>(),*e); EXPECT_EQ(memcmp(a,e,sizeof(BotConfig)),0);
  EXPECT_EQ(d["watch"].as<JsonArray>().size(),4u); EXPECT_STREQ(d["watch"][1]["kind"],"node"); EXPECT_STREQ(d["watch"][0]["kind"],"observer");
  delete a; delete b; delete e;
}
TEST(M2, ToJsonAlwaysWritesNewMembers){
  BotConfig* a=heapDefaults(); a->num_custom=1; strcpy(a->custom[0].trigger,"t"); a->custom[0].pfx=true; strcpy(a->custom[0].reply,"r");
  JsonDocument d; botConfigToJson(*a,d.to<JsonObject>(),false);
  EXPECT_TRUE(d["custom"][0]["url"].is<const char*>()); EXPECT_TRUE(d["custom"][0]["path"].is<const char*>()); EXPECT_EQ(d["custom"][0]["cache_s"].as<int>(),0);
  EXPECT_TRUE(d["watch"].is<JsonArray>()); EXPECT_EQ(d["watch"].as<JsonArray>().size(),0u); EXPECT_EQ(d["watch_online_min"].as<int>(),720);
  a->num_watch=200; JsonDocument e; botConfigToJson(*a,e.to<JsonObject>(),false); EXPECT_EQ(e["watch"].as<JsonArray>().size(),4u);   // corrupt count clamped
  delete a;
}

// ---- refused keys without whole documents ----

// The comparison the web layer used to make with a whole "before" and "after" document.
static std::string refusedByDocuments(JsonObjectConst posted, const BotConfig& before, const BotConfig& after, const BotApplyReport& rep) {
  JsonDocument b, a;
  EXPECT_TRUE(botConfigToJsonChecked(before, b, false));
  EXPECT_TRUE(botConfigToJsonChecked(after, a, false));
  char refused[96] = "";
  for (JsonPairConst kv : posted) {
    // The old key "channel" is judged as the one-element list "channels" it stands for, and is
    // ignored when the document also carries "channels".
    bool legacy = strcmp(kv.key().c_str(), "channel") == 0;
    if (legacy && !posted["channels"].isNull()) continue;
    const char* dk = legacy ? "channels" : kv.key().c_str();
    JsonDocument one;
    JsonVariantConst pv = kv.value();
    if (legacy) { one.add(kv.value()); pv = one.as<JsonVariantConst>(); }
    JsonVariantConst bv = b[dk];
    if (bv.isNull() || a[dk] != bv || pv == bv) continue;
    if ((rep.match[0] && strcmp(kv.key().c_str(), "match") == 0) || (rep.custom[0] && strcmp(kv.key().c_str(), "custom") == 0) || (rep.builtin[0] && strcmp(kv.key().c_str(), "builtin") == 0) ||
        (rep.watch[0] && strcmp(kv.key().c_str(), "watch") == 0) || (rep.channels[0] && strcmp(dk, "channels") == 0))
      continue;
    size_t n = strlen(refused);
    snprintf(refused + n, sizeof(refused) - n, "%s%s", n ? ", " : "", kv.key().c_str());
  }
  return refused;
}

static BotConfig* richConfig() {
  BotConfig* c = heapDefaults();
  c->lat = 44.2312; c->lon = -76.486; strcpy(c->scope, "test-region"); strcpy(c->help_link, "https://example.org/h");
  applyTo(*c, "{\"custom\":[{\"trigger\":\"one\",\"reply\":\"1 {fetch}\",\"desc\":\"d\",\"enabled\":true,\"url\":\"https://example.org/a?q={args}\",\"path\":\"a.b[0]\",\"cache_s\":60},"
              "{\"trigger\":\"two\",\"reply\":\"2\",\"enabled\":false}],"
              "\"match\":[{\"kind\":\"exact\",\"text\":\"good morning\",\"run\":\"one\",\"enabled\":true},{\"kind\":\"regex\",\"text\":\"^w (.+)$\",\"run\":\"wx {1}\",\"enabled\":false}],"
              "\"watch\":[{\"label\":\"Obs\",\"kind\":\"observer\",\"id\":\"0a1b2c3d-4e5f-6a7b-8c9d-0e1f2a3b4c5d\"},"
              "{\"label\":\"Node\",\"kind\":\"node\",\"id\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\"}]}");
  return c;
}

TEST(RefusedKeys, MatchesWholeDocumentComparison) {
  BotConfig* base = richConfig();
  // What the page sends back unchanged: the settings document itself, then variations of it.
  JsonDocument full; ASSERT_TRUE(botConfigToJsonChecked(*base, full, false));
  std::string same; serializeJson(full, same);
  const std::string posts[] = {
    same,
    "{\"cooldown_s\":30,\"max_per_min\":99,\"lat\":44.2312,\"lon\":-76.486,\"tz\":\"Mars/Olympus\",\"node_name\":\"x\"}",
    "{\"channel\":\"bot\",\"scope\":\"ok-scope\",\"prefix\":\"ab\",\"ignore\":5,\"help_link\":\"\",\"advert_zero_hop_min\":-1,\"advert_flood_min\":20000,\"watch_online_min\":1}",
    "{\"lat\":44.23120000001,\"lon\":\"west\",\"cooldown_s\":10.0,\"max_per_min\":6}",
    "{\"lat\":91,\"lon\":-76.486}",
    "{\"builtin\":{\"ping\":{\"name\":\"test\"}}}",
    "{\"builtin\":{\"ping\":{\"name\":\"pong\",\"enabled\":false}}}",
    "{\"builtin\":7}",
    "{\"custom\":[{\"trigger\":\"bad trigger\",\"reply\":\"x\"}]}",
    "{\"custom\":\"none\"}",
    "{\"custom\":[{\"trigger\":\"one\",\"reply\":\"1 {fetch}\",\"desc\":\"d\",\"enabled\":true,\"url\":\"https://example.org/a?q={args}\",\"path\":\"a.b[0]\",\"cache_s\":60},{\"trigger\":\"two\",\"reply\":\"2\",\"enabled\":false}]}",
    "{\"custom\":[]}",
    "{\"watch\":[{\"label\":\"Obs\",\"kind\":\"observer\",\"id\":\"0A1B2C3D-4E5F-6A7B-8C9D-0E1F2A3B4C5D\"},{\"label\":\"Node\",\"kind\":\"node\",\"id\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\"}]}",
    "{\"watch\":[{\"label\":\"\",\"kind\":\"observer\",\"id\":\"x\"}]}",
    "{\"watch\":{}}",
    "{\"bare\":true}", "{\"bare\":false}", "{\"bare\":1}",
    "{\"match\":[{\"kind\":\"exact\",\"text\":\"good morning\",\"run\":\"one\",\"enabled\":true},{\"kind\":\"regex\",\"text\":\"^w (.+)$\",\"run\":\"wx {1}\",\"enabled\":false}]}",
    "{\"match\":[{\"kind\":\"regex\",\"text\":\"a{2}\",\"run\":\"wx\"}]}",
    "{\"match\":[]}", "{\"match\":7}",
    "{\"web_hash\":\"00\",\"web_salt\":\"11\",\"unknown\":1}",
    "{\"prefix\":\"?\",\"tz\":\"UTC\",\"channel\":\"Public\",\"scope\":\"\"}",
    "{\"tz\":\"America/Toronto\",\"ignore\":\"bot\",\"prefix\":\"!\",\"channel\":\"#bot\"}",
    "{\"channels\":[\"#bot\",\"#test\"],\"cooldown_s\":30}",
    "{\"channels\":[\"#bot\"]}",
    "{\"channels\":[]}",
    "{\"channels\":[\"#bot\",\"#BOT\"]}",
    "{\"channels\":[\"bot\"],\"channel\":\"#test\"}",
    "{\"channels\":\"#bot\"}",
    "{\"channels\":\"#test\",\"channel\":\"#test\"}",
    "{\"channels\":[\"#test\"],\"channel\":\"bot\"}",
    "{\"channel\":\"#test\"}",
    "{\"channel\":7}",
  };
  for (const std::string& p : posts) {
    JsonDocument d; ASSERT_FALSE(deserializeJson(d, p)) << p;
    BotConfig* after = new BotConfig; memcpy(after, base, sizeof *after);
    BotApplyReport rep; botConfigFromWebJsonStrict(d.as<JsonVariantConst>(), *after, rep);
    char got[96]; botCfgRefusedKeys(d.as<JsonObjectConst>(), *base, *after, rep, got, sizeof got);
    EXPECT_EQ(std::string(got), refusedByDocuments(d.as<JsonObjectConst>(), *base, *after, rep)) << p;
    delete after;
  }
  delete base;
}

TEST(RefusedKeys, NamesWhatWasNotTaken) {
  BotConfig* base = richConfig();
  JsonDocument d; ASSERT_FALSE(deserializeJson(d, "{\"cooldown_s\":30,\"max_per_min\":99,\"tz\":\"Mars/Olympus\",\"node_name\":\"x\",\"lat\":44.2312}"));
  BotConfig* after = new BotConfig; memcpy(after, base, sizeof *after);
  BotApplyReport rep; botConfigFromWebJsonStrict(d.as<JsonVariantConst>(), *after, rep);
  char got[96]; botCfgRefusedKeys(d.as<JsonObjectConst>(), *base, *after, rep, got, sizeof got);
  EXPECT_STREQ(got, "max_per_min, tz");   // cooldown_s was taken, lat unchanged, node_name is not a setting
  char small[8]; botCfgRefusedKeys(d.as<JsonObjectConst>(), *base, *after, rep, small, sizeof small);
  EXPECT_LE(strlen(small), 7u);           // bounded by the buffer
  delete after; delete base;
}

// ---- several channels ----
static std::string chans(const std::string& elems){ return "{\"channels\":["+elems+"]}"; }
static BotConfig* withTwoChannels(){ BotConfig* c=heapDefaults(); applyTo(*c,chans("\"#bot\",\"#test\"").c_str()); return c; }

TEST(Chan, DefaultIsOneEntry){
  BotConfig* c=heapDefaults(); EXPECT_EQ(BOT_MAX_CHANNELS,4); EXPECT_EQ(c->num_channels,1); EXPECT_STREQ(c->channels[0],"#bot");
  for(int i=1;i<BOT_MAX_CHANNELS;i++) for(int k=0;k<32;k++) EXPECT_EQ(c->channels[i][k],0);
  delete c;
}
TEST(Chan, OldFileWithOnlyChannelLoadsAsOneEntry){
  // The file a device in use holds: the single key "channel", no "channels".
  BotConfig* c=heapDefaults(); applyTo(*c,"{\"channel\":\"#test\",\"scope\":\"\",\"cooldown_s\":10}");
  ASSERT_EQ(c->num_channels,1); EXPECT_STREQ(c->channels[0],"#test"); EXPECT_STREQ(c->channels[1],"");
  // and saving it keeps it, as a list, without the old key
  JsonDocument d; ASSERT_TRUE(botConfigToJsonChecked(*c,d,true));
  EXPECT_TRUE(d["channel"].isNull()); ASSERT_TRUE(d["channels"].is<JsonArray>()); EXPECT_EQ(d["channels"].size(),1u); EXPECT_STREQ(d["channels"][0],"#test");
  std::string s; serializeJson(d,s); JsonDocument d2; ASSERT_FALSE(deserializeJson(d2,s));
  BotConfig* e=heapDefaults(); botConfigFromJson(d2.as<JsonVariantConst>(),*e); EXPECT_EQ(memcmp(c,e,sizeof(BotConfig)),0);
  delete c; delete e;
  // the old key over a longer list replaces it with that one entry
  c=withTwoChannels(); applyTo(*c,"{\"channel\":\"#bot\"}"); EXPECT_EQ(c->num_channels,1); EXPECT_STREQ(c->channels[1],""); delete c;
  // an invalid old key changes nothing
  c=withTwoChannels(); applyTo(*c,"{\"channel\":\"bot\"}"); EXPECT_EQ(c->num_channels,2); EXPECT_STREQ(c->channels[1],"#test"); delete c;
}
TEST(Chan, ChannelsWinsOverChannel){
  BotConfig c=load("{\"channel\":\"#old\",\"channels\":[\"#alpha\",\"Public\"]}"); ASSERT_EQ(c.num_channels,2); EXPECT_STREQ(c.channels[0],"#alpha"); EXPECT_STREQ(c.channels[1],"Public");
  c=load("{\"channels\":[\"#alpha\"],\"channel\":\"#old\"}"); ASSERT_EQ(c.num_channels,1); EXPECT_STREQ(c.channels[0],"#alpha");
  c=load("{\"channels\":\"#alpha\",\"channel\":\"#old\"}"); ASSERT_EQ(c.num_channels,1); EXPECT_STREQ(c.channels[0],"#bot");   // not a list: nothing changes, and channel is still ignored
  BotConfig* w=withTwoChannels(); BotApplyReport rep;
  webStrict(*w,"{\"channel\":\"#old\",\"channels\":[\"#alpha\"]}",rep); EXPECT_STREQ(rep.channels,""); ASSERT_EQ(w->num_channels,1); EXPECT_STREQ(w->channels[0],"#alpha");
  webStrict(*w,"{\"channel\":\"#old\",\"channels\":[]}",rep); EXPECT_STREQ(rep.channels,"empty list"); ASSERT_EQ(w->num_channels,1); EXPECT_STREQ(w->channels[0],"#alpha");
  delete w;
}
TEST(Chan, WebAcceptsTheOldKey){
  BotConfig* c=withTwoChannels(); BotApplyReport rep;
  webStrict(*c,"{\"channel\":\"#alpha\"}",rep); EXPECT_STREQ(rep.channels,""); ASSERT_EQ(c->num_channels,1); EXPECT_STREQ(c->channels[0],"#alpha"); EXPECT_STREQ(c->channels[1],"");
  webStrict(*c,"{\"channel\":\"alpha\",\"cooldown_s\":20}",rep); EXPECT_STREQ(rep.channels,"#1 'alpha': not a channel name"); EXPECT_STREQ(c->channels[0],"#alpha"); EXPECT_EQ(c->cooldown_s,20);
  delete c;
}
TEST(Chan, FourAcceptedFiveRefusedOrCut){
  std::string four="\"#a\",\"#b\",\"Public\",\"#d\"", five=four+",\"#e\"";
  BotConfig c=load(chans(four).c_str()); ASSERT_EQ(c.num_channels,4); EXPECT_STREQ(c.channels[2],"Public"); EXPECT_STREQ(c.channels[3],"#d");
  c=load(chans(five).c_str()); ASSERT_EQ(c.num_channels,4); EXPECT_STREQ(c.channels[3],"#d");   // lenient: the first four
  c=load(chans("\"bad\",\"#a\",\"#A\",\"#b\",5,\"#c\",\"#d\",\"#e\"").c_str()); ASSERT_EQ(c.num_channels,4);   // skipped ones do not count
  EXPECT_STREQ(c.channels[0],"#a"); EXPECT_STREQ(c.channels[1],"#b"); EXPECT_STREQ(c.channels[2],"#c"); EXPECT_STREQ(c.channels[3],"#d");
  BotConfig* w=withTwoChannels(); BotApplyReport rep;
  webStrict(*w,chans(four),rep); EXPECT_STREQ(rep.channels,""); EXPECT_EQ(w->num_channels,4);
  webStrict(*w,chans("\"#bot\",\"#test\""),rep); EXPECT_EQ(w->num_channels,2); EXPECT_STREQ(w->channels[2],""); EXPECT_STREQ(w->channels[3],"");   // shorter list: no stale rows
  webStrict(*w,chans(five),rep); EXPECT_STREQ(rep.channels,"#5: too many"); EXPECT_EQ(w->num_channels,2);
  delete w;
}
TEST(Chan, EmptyListRefusedOrDefaulted){
  BotConfig* c=withTwoChannels(); applyTo(*c,"{\"channels\":[]}"); ASSERT_EQ(c->num_channels,1); EXPECT_STREQ(c->channels[0],"#bot"); EXPECT_STREQ(c->channels[1],""); delete c;
  c=withTwoChannels(); applyTo(*c,chans("\"bot\",7,null").c_str()); ASSERT_EQ(c->num_channels,1); EXPECT_STREQ(c->channels[0],"#bot"); delete c;   // nothing valid left
  c=withTwoChannels(); BotApplyReport rep; webStrict(*c,"{\"channels\":[]}",rep); EXPECT_STREQ(rep.channels,"empty list"); EXPECT_EQ(c->num_channels,2); EXPECT_STREQ(c->channels[1],"#test"); delete c;
}
TEST(Chan, DuplicateIgnoringAsciiCaseRefused){
  BotConfig* c=withTwoChannels(); BotApplyReport rep;
  webStrict(*c,chans("\"#bot\",\"#BOT\""),rep); EXPECT_STREQ(rep.channels,"#2 '#BOT': duplicate"); EXPECT_EQ(c->num_channels,2); EXPECT_STREQ(c->channels[1],"#test");
  webStrict(*c,chans("\"Public\",\"#x\",\"Public\""),rep); EXPECT_STREQ(rep.channels,"#3 'Public': duplicate");
  webStrict(*c,chans("\"#caf\xC3\xA9\",\"#CAF\xC3\x89\""),rep); EXPECT_STREQ(rep.channels,""); EXPECT_EQ(c->num_channels,2);   // only ASCII is folded
  delete c;
  BotConfig l=load(chans("\"#Bot\",\"#bot\",\"#test\"").c_str()); ASSERT_EQ(l.num_channels,2); EXPECT_STREQ(l.channels[0],"#Bot"); EXPECT_STREQ(l.channels[1],"#test");
}
TEST(Chan, NotAChannelNameRefused){
  std::string c32="#"+std::string(31,'a');
  struct Case { std::string elem; const char* msg; } cases[] = {
    {"\"bot\"", "#2 'bot': not a channel name"},
    {"\"#\"", "#2 '#': not a channel name"},
    {"\"public\"", "#2 'public': not a channel name"},
    {"\"\"", "#2: not a channel name"},
    {"5", "#2: not a channel name"},
    {"null", "#2: not a channel name"},
    {"{\"name\":\"#x\"}", "#2: not a channel name"},
    {"\"#a\\tb\"", "#2 '#a?b': not a channel name"},
    {"\""+c32+"\"", nullptr},
  };
  for(const Case& k : cases){
    BotConfig* c=withTwoChannels(); BotConfig* before=new BotConfig; memcpy(before,c,sizeof *before); BotApplyReport rep;
    webStrict(*c,"{\"cooldown_s\":42,"+chans("\"#alpha\","+k.elem).substr(1),rep);
    if(k.msg) EXPECT_STREQ(rep.channels,k.msg) << k.elem; else { EXPECT_TRUE(has(rep.channels,"#2 '#aaaa")) << rep.channels; EXPECT_TRUE(has(rep.channels,"': not a channel name")) << rep.channels; }
    // the stored list is untouched, byte for byte; other fields still applied
    EXPECT_EQ(memcmp(c->channels,before->channels,sizeof c->channels),0) << k.elem; EXPECT_EQ(c->num_channels,2); EXPECT_EQ(c->cooldown_s,42);
    EXPECT_STREQ(rep.custom,""); EXPECT_STREQ(rep.watch,"");
    delete c; delete before;
  }
  BotConfig* c=withTwoChannels(); BotApplyReport rep; webStrict(*c,chans("\"bot\""),rep); EXPECT_STREQ(rep.channels,"#1 'bot': not a channel name"); delete c;
}
TEST(Chan, ValidListClearsAnEarlierReason){
  BotConfig* c=withTwoChannels(); BotApplyReport rep;
  webStrict(*c,chans("\"bot\""),rep); EXPECT_STRNE(rep.channels,"");
  webStrict(*c,chans("\"#alpha\",\"Public\",\"#test\""),rep); EXPECT_STREQ(rep.channels,""); ASSERT_EQ(c->num_channels,3); EXPECT_STREQ(c->channels[1],"Public");
  delete c;
}
TEST(Chan, OutputHasChannelsAndNoChannel){
  BotConfig* c=withTwoChannels(); JsonDocument d; botConfigToJson(*c,d.to<JsonObject>(),false);
  EXPECT_TRUE(d["channel"].isNull()); ASSERT_TRUE(d["channels"].is<JsonArray>()); ASSERT_EQ(d["channels"].size(),2u); EXPECT_STREQ(d["channels"][1],"#test");
  c->num_channels=200; JsonDocument e; botConfigToJson(*c,e.to<JsonObject>(),false); EXPECT_EQ(e["channels"].size(),4u);   // corrupt count clamped
  std::string s; serializeJson(d,s); EXPECT_EQ(s.find("\"channel\":"),std::string::npos);
  delete c;
}
TEST(Chan, FourChannelsRoundTripMemcmp){
  BotConfig* a=heapDefaults(); a->num_channels=4;
  strcpy(a->channels[0],"#alpha"); strcpy(a->channels[1],"Public"); strcpy(a->channels[2],"#te\"st\\"); strcpy(a->channels[3],("#"+std::string(30,'z')).c_str());
  JsonDocument d; ASSERT_TRUE(botConfigToJsonChecked(*a,d,true));
  BotConfig* b=heapDefaults(); botConfigFromJson(d.as<JsonVariantConst>(),*b); EXPECT_EQ(memcmp(a,b,sizeof(BotConfig)),0);
  std::string s; serializeJson(d,s); JsonDocument d2; ASSERT_EQ(deserializeJson(d2,s),DeserializationError::Ok);
  BotConfig* e=heapDefaults(); botConfigFromJson(d2.as<JsonVariantConst>(),*e); EXPECT_EQ(memcmp(a,e,sizeof(BotConfig)),0);
  // and through the strict web loader, as the page sends the list back
  BotConfig* w=heapDefaults(); BotApplyReport rep; JsonDocument p; p["channels"]=d2["channels"]; botConfigFromWebJsonStrict(p.as<JsonVariantConst>(),*w,rep);
  EXPECT_STREQ(rep.channels,""); EXPECT_EQ(memcmp(w->channels,a->channels,sizeof a->channels),0); EXPECT_EQ(w->num_channels,4);
  delete a; delete b; delete e; delete w;
}
TEST(Chan, HasChannelIsExactAndCaseSensitive){
  BotConfig* c=withTwoChannels();
  EXPECT_TRUE(botConfigHasChannel(*c,"#bot")); EXPECT_TRUE(botConfigHasChannel(*c,"#test"));
  EXPECT_FALSE(botConfigHasChannel(*c,"#Bot")); EXPECT_FALSE(botConfigHasChannel(*c,"#TEST")); EXPECT_FALSE(botConfigHasChannel(*c,"#bo"));
  EXPECT_FALSE(botConfigHasChannel(*c,"#bot ")); EXPECT_FALSE(botConfigHasChannel(*c,"Public")); EXPECT_FALSE(botConfigHasChannel(*c,""));
  EXPECT_FALSE(botConfigHasChannel(*c,nullptr));
  strcpy(c->channels[2],"#alpha");   // beyond num_channels: not listened on
  EXPECT_FALSE(botConfigHasChannel(*c,"#alpha"));
  c->num_channels=200; EXPECT_TRUE(botConfigHasChannel(*c,"#alpha")); EXPECT_FALSE(botConfigHasChannel(*c,""));   // corrupt count clamped, empty rows never match
  delete c;
}
TEST(Chan, RefusedKeysForChannels){
  struct Case { const char* post; const char* refused; } cases[] = {
    {"{\"channels\":[\"#bot\",\"#test\"]}", ""},              // unchanged
    {"{\"channels\":[\"#alpha\"]}", ""},                      // taken
    {"{\"channels\":[]}", ""},                                // refused, but rep.channels says why
    {"{\"channels\":\"#bot\"}", "channels"},                  // not a list
    {"{\"channel\":\"#alpha\"}", ""},                         // old key, taken
    {"{\"channel\":5}", ""},                                  // old key refused, rep.channels says why
    {"{\"channels\":[\"#alpha\"],\"channel\":\"bad\"}", ""},  // the old key is ignored beside the new one
    {"{\"channels\":[\"#bot\",\"#test\"],\"channel\":\"#bot\"}", ""},
  };
  for(const Case& k : cases){
    BotConfig* base=withTwoChannels(); BotConfig* after=new BotConfig; memcpy(after,base,sizeof *after);
    JsonDocument d; ASSERT_FALSE(deserializeJson(d,k.post));
    BotApplyReport rep; botConfigFromWebJsonStrict(d.as<JsonVariantConst>(),*after,rep);
    char got[96]; botCfgRefusedKeys(d.as<JsonObjectConst>(),*base,*after,rep,got,sizeof got);
    EXPECT_STREQ(got,k.refused) << k.post;
    EXPECT_EQ(std::string(got),refusedByDocuments(d.as<JsonObjectConst>(),*base,*after,rep)) << k.post;
    delete base; delete after;
  }
  // a refused old key from an older page: the reason is in rep.channels, nothing else reported
  BotConfig* base=withTwoChannels(); BotConfig* after=new BotConfig; memcpy(after,base,sizeof *after);
  JsonDocument d; ASSERT_FALSE(deserializeJson(d,"{\"channel\":\"bot\",\"scope\":\"x\"}"));
  BotApplyReport rep; botConfigFromWebJsonStrict(d.as<JsonVariantConst>(),*after,rep);
  EXPECT_STREQ(rep.channels,"#1 'bot': not a channel name"); EXPECT_EQ(after->num_channels,2);
  delete base; delete after;
}

// ---- MQTT broker settings ----
static std::string webStr(BotConfig& c, const std::string& json, BotApplyReport* repOut=nullptr){ JsonDocument d; EXPECT_FALSE(deserializeJson(d,json)); BotApplyReport rep; botConfigFromWebJsonStrict(d.as<JsonVariantConst>(),c,rep); if(repOut) *repOut=rep; return json; }
static std::string refusedFor(const BotConfig& base, const std::string& post){
  BotConfig* after=new BotConfig; memcpy(after,&base,sizeof *after);
  JsonDocument d; EXPECT_FALSE(deserializeJson(d,post));
  BotApplyReport rep; botConfigFromWebJsonStrict(d.as<JsonVariantConst>(),*after,rep);
  char got[96]; botCfgRefusedKeys(d.as<JsonObjectConst>(),base,*after,rep,got,sizeof got);
  delete after; return got;
}

TEST(Mqtt, Defaults){
  BotConfig* c=heapDefaults();
  EXPECT_FALSE(c->mqtt_enabled); EXPECT_STREQ(c->mqtt_host,""); EXPECT_EQ(c->mqtt_port,1883); EXPECT_STREQ(c->mqtt_user,""); EXPECT_STREQ(c->mqtt_pass,""); EXPECT_STREQ(c->mqtt_prefix,""); EXPECT_EQ(c->mqtt_wait_s,5);
  EXPECT_FALSE(botConfigMqttUsable(*c));
  delete c;
}
TEST(Mqtt, OldDocumentLoadsWithDefaults){
  BotConfig* c=heapDefaults(); applyTo(*c,"{\"channels\":[\"#x\"],\"cooldown_s\":30,\"web_salt\":\"0123456789abcdef\",\"web_hash\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\"}");
  EXPECT_EQ(c->cooldown_s,30); EXPECT_FALSE(c->mqtt_enabled); EXPECT_STREQ(c->mqtt_host,""); EXPECT_EQ(c->mqtt_port,1883); EXPECT_EQ(c->mqtt_wait_s,5); EXPECT_STREQ(c->mqtt_pass,"");
  delete c;
}
TEST(Mqtt, EnabledOnlyBoolean){
  EXPECT_TRUE(load("{\"mqtt_enabled\":true}").mqtt_enabled);
  for(const char* j : {"{\"mqtt_enabled\":1}","{\"mqtt_enabled\":\"true\"}","{\"mqtt_enabled\":null}","{\"mqtt_enabled\":[true]}"}) EXPECT_FALSE(load(j).mqtt_enabled) << j;
  BotConfig* c=heapDefaults(); c->mqtt_enabled=true; applyTo(*c,"{\"mqtt_enabled\":false}"); EXPECT_FALSE(c->mqtt_enabled); delete c;
}
TEST(Mqtt, HostRules){
  for(const char* h : {"broker.example.org","192.168.1.10","a-b.c","X9","a"}) EXPECT_STREQ(load((std::string("{\"mqtt_host\":\"")+h+"\"}").c_str()).mqtt_host,h) << h;
  std::string h64(64,'a'), h65(65,'a');
  EXPECT_STREQ(load(("{\"mqtt_host\":\""+h64+"\"}").c_str()).mqtt_host,h64.c_str());
  EXPECT_STREQ(load(("{\"mqtt_host\":\""+h65+"\"}").c_str()).mqtt_host,"");
  for(const char* h : {"a b","a_b","a/b","a:1","http://x","a\\u00e9","a\\tb","a\\u0000b"}) { BotConfig c=load((std::string("{\"mqtt_host\":\"")+h+"\"}").c_str()); EXPECT_STREQ(c.mqtt_host,"") << h; }
  for(const char* j : {"{\"mqtt_host\":5}","{\"mqtt_host\":null}","{\"mqtt_host\":true}"}) EXPECT_STREQ(load(j).mqtt_host,"") << j;
  BotConfig* c=heapDefaults(); applyTo(*c,"{\"mqtt_host\":\"Broker.Example.org\"}"); EXPECT_STREQ(c->mqtt_host,"Broker.Example.org");   // not lower-cased
  applyTo(*c,"{\"mqtt_host\":\"bad host\"}"); EXPECT_STREQ(c->mqtt_host,"Broker.Example.org");
  applyTo(*c,"{\"mqtt_host\":\"\"}"); EXPECT_STREQ(c->mqtt_host,"");
  delete c;
}
TEST(Mqtt, PortRules){
  EXPECT_EQ(load("{\"mqtt_port\":1}").mqtt_port,1); EXPECT_EQ(load("{\"mqtt_port\":65535}").mqtt_port,65535); EXPECT_EQ(load("{\"mqtt_port\":8883}").mqtt_port,8883);
  for(const char* j : {"{\"mqtt_port\":0}","{\"mqtt_port\":65536}","{\"mqtt_port\":-1}","{\"mqtt_port\":1883.0}","{\"mqtt_port\":\"1884\"}","{\"mqtt_port\":null}","{\"mqtt_port\":true}"}) EXPECT_EQ(load(j).mqtt_port,1883) << j;
}
TEST(Mqtt, WaitRules){
  EXPECT_EQ(load("{\"mqtt_wait_s\":1}").mqtt_wait_s,1); EXPECT_EQ(load("{\"mqtt_wait_s\":15}").mqtt_wait_s,15);
  for(const char* j : {"{\"mqtt_wait_s\":0}","{\"mqtt_wait_s\":16}","{\"mqtt_wait_s\":-1}","{\"mqtt_wait_s\":5.5}","{\"mqtt_wait_s\":\"9\"}","{\"mqtt_wait_s\":null}"}) EXPECT_EQ(load(j).mqtt_wait_s,5) << j;
}
TEST(Mqtt, UserAndPassRules){
  std::string u32(32,'u'), u33(33,'u'), p64(64,'p'), p65(65,'p');
  EXPECT_STREQ(load("{\"mqtt_user\":\"bot\"}").mqtt_user,"bot");
  EXPECT_STREQ(load(("{\"mqtt_user\":\""+u32+"\"}").c_str()).mqtt_user,u32.c_str()); EXPECT_STREQ(load(("{\"mqtt_user\":\""+u33+"\"}").c_str()).mqtt_user,"");
  EXPECT_STREQ(load(("{\"mqtt_pass\":\""+p64+"\"}").c_str()).mqtt_pass,p64.c_str()); EXPECT_STREQ(load(("{\"mqtt_pass\":\""+p65+"\"}").c_str()).mqtt_pass,"");
  EXPECT_STREQ(load("{\"mqtt_pass\":\"s3cret!~Value\\\"\\\\\"}").mqtt_pass,"s3cret!~Value\"\\");
  for(const char* f : {"mqtt_user","mqtt_pass"})
    for(const char* v : {"a b","a\\tb","a\\u0000b","a\\u007fb","a\\u00e9","\\u00e9"}) {
      BotConfig c=load((std::string("{\"")+f+"\":\""+v+"\"}").c_str());
      EXPECT_STREQ(c.mqtt_user,"") << f << v; EXPECT_STREQ(c.mqtt_pass,"") << f << v;
    }
  for(const char* j : {"{\"mqtt_user\":5}","{\"mqtt_pass\":5}","{\"mqtt_pass\":null}","{\"mqtt_pass\":true}","{\"mqtt_pass\":[\"a\"]}"}) { BotConfig c=load(j); EXPECT_STREQ(c.mqtt_user,"") << j; EXPECT_STREQ(c.mqtt_pass,"") << j; }
  BotConfig* c=heapDefaults(); strcpy(c->mqtt_user,"bot"); strcpy(c->mqtt_pass,"s3cretValue"); applyTo(*c,"{\"mqtt_user\":\"\",\"mqtt_pass\":\"\"}"); EXPECT_STREQ(c->mqtt_user,""); EXPECT_STREQ(c->mqtt_pass,""); delete c;
}
TEST(Mqtt, PrefixRules){
  EXPECT_STREQ(load("{\"mqtt_prefix\":\"meshcore/MeshBot\"}").mqtt_prefix,"meshcore/MeshBot");
  EXPECT_STREQ(load("{\"mqtt_prefix\":\"a\"}").mqtt_prefix,"a");
  std::string p64(64,'a'), p65(65,'a');
  EXPECT_STREQ(load(("{\"mqtt_prefix\":\""+p64+"\"}").c_str()).mqtt_prefix,p64.c_str()); EXPECT_STREQ(load(("{\"mqtt_prefix\":\""+p65+"\"}").c_str()).mqtt_prefix,"");
  for(const char* v : {"/a","a/","a//b","a b","#","a/#","a/+","a\\u00e9","a\\tb","\\u0000"}) EXPECT_STREQ(load((std::string("{\"mqtt_prefix\":\"")+v+"\"}").c_str()).mqtt_prefix,"") << v;
  BotConfig* c=heapDefaults(); applyTo(*c,"{\"mqtt_prefix\":\"x/y\"}"); applyTo(*c,"{\"mqtt_prefix\":\"/bad\"}"); EXPECT_STREQ(c->mqtt_prefix,"x/y");
  applyTo(*c,"{\"mqtt_prefix\":\"\"}"); EXPECT_STREQ(c->mqtt_prefix,"");   // empty = use the default
  applyTo(*c,"{\"mqtt_prefix\":5}"); EXPECT_STREQ(c->mqtt_prefix,""); delete c;
}
TEST(Mqtt, UsableNeedsEnabledAndHost){
  BotConfig* c=heapDefaults();
  EXPECT_FALSE(botConfigMqttUsable(*c));
  c->mqtt_enabled=true; EXPECT_FALSE(botConfigMqttUsable(*c));
  strcpy(c->mqtt_host,"broker.example.org"); EXPECT_TRUE(botConfigMqttUsable(*c));
  c->mqtt_enabled=false; EXPECT_FALSE(botConfigMqttUsable(*c));
  delete c;
}
TEST(Mqtt, WebOutputNeverHasThePassword){
  BotConfig* c=heapDefaults(); strcpy(c->mqtt_pass,"s3cretValue"); strcpy(c->mqtt_host,"broker.example.org"); strcpy(c->mqtt_user,"bot"); c->mqtt_enabled=true; c->mqtt_port=8883; c->mqtt_wait_s=9; strcpy(c->mqtt_prefix,"meshcore/MeshBot");
  JsonDocument w; ASSERT_TRUE(botConfigToJsonChecked(*c,w,false));
  std::string s; serializeJson(w,s);
  EXPECT_EQ(s.find("s3cretValue"),std::string::npos); EXPECT_FALSE(w["mqtt_pass"].is<const char*>()); EXPECT_TRUE(w["mqtt_pass"].isNull());
  EXPECT_TRUE(w["mqtt_pass_set"].is<bool>()); EXPECT_TRUE(w["mqtt_pass_set"].as<bool>());
  EXPECT_EQ(w["mqtt_enabled"].as<bool>(),true); EXPECT_STREQ(w["mqtt_host"],"broker.example.org"); EXPECT_EQ(w["mqtt_port"].as<int>(),8883); EXPECT_STREQ(w["mqtt_user"],"bot"); EXPECT_STREQ(w["mqtt_prefix"],"meshcore/MeshBot"); EXPECT_EQ(w["mqtt_wait_s"].as<int>(),9);
  JsonDocument st; botConfigToJson(*c,st.to<JsonObject>(),true);
  EXPECT_STREQ(st["mqtt_pass"],"s3cretValue"); EXPECT_TRUE(st["mqtt_pass_set"].isNull());
  c->mqtt_pass[0]='\0'; JsonDocument w2; ASSERT_TRUE(botConfigToJsonChecked(*c,w2,false)); EXPECT_FALSE(w2["mqtt_pass_set"].as<bool>()); EXPECT_TRUE(w2["mqtt_pass_set"].is<bool>());
  JsonDocument st2; botConfigToJson(*c,st2.to<JsonObject>(),true); EXPECT_STREQ(st2["mqtt_pass"],"");
  delete c;
}
TEST(Mqtt, WebPasswordRule){
  BotConfig* c=heapDefaults(); strcpy(c->mqtt_pass,"s3cretValue");
  webStr(*c,"{\"mqtt_host\":\"broker.example.org\"}"); EXPECT_STREQ(c->mqtt_pass,"s3cretValue");        // absent: kept
  webStr(*c,"{\"mqtt_pass\":null}"); EXPECT_STREQ(c->mqtt_pass,"s3cretValue");                         // null: kept
  webStr(*c,"{\"mqtt_pass\":5}"); EXPECT_STREQ(c->mqtt_pass,"s3cretValue");                            // not a string: kept
  webStr(*c,"{\"mqtt_pass\":\"bad pass\"}"); EXPECT_STREQ(c->mqtt_pass,"s3cretValue");                 // invalid: kept
  webStr(*c,"{\"mqtt_pass_set\":false}"); EXPECT_STREQ(c->mqtt_pass,"s3cretValue");                    // ignored
  webStr(*c,"{\"mqtt_pass\":\"newValue2\"}"); EXPECT_STREQ(c->mqtt_pass,"newValue2");                 // replaced
  webStr(*c,"{\"mqtt_pass\":\"\"}"); EXPECT_STREQ(c->mqtt_pass,"");                                    // cleared
  applyTo(*c,"{\"mqtt_pass\":\"viaWeb\"}",true); EXPECT_STREQ(c->mqtt_pass,"viaWeb");                  // the lenient web loader too
  applyTo(*c,"{\"mqtt_pass\":\"\"}",true); EXPECT_STREQ(c->mqtt_pass,"");
  delete c;
}
TEST(Mqtt, WebLoadersStillCannotTouchSaltOrHash){
  BotConfig* c=heapDefaults(); strcpy(c->web_salt,HEX16); strcpy(c->web_hash,HEX64); BotConfig* b=new BotConfig; memcpy(b,c,sizeof *b);
  webStr(*c,"{\"mqtt_pass\":\"x\",\"web_salt\":\"\",\"web_hash\":\"\"}"); EXPECT_STREQ(c->web_salt,HEX16); EXPECT_STREQ(c->web_hash,HEX64);
  strcpy(b->mqtt_pass,"x"); EXPECT_EQ(memcmp(b,c,sizeof *b),0);
  delete c; delete b;
}
TEST(Mqtt, StorageRoundTripMemcmp){
  BotConfig* a=heapDefaults(); a->mqtt_enabled=true; strcpy(a->mqtt_host,"192.168.1.10"); a->mqtt_port=8883; strcpy(a->mqtt_user,"bot"); strcpy(a->mqtt_pass,"s3cret\"\\Value~"); strcpy(a->mqtt_prefix,"meshcore/MeshBot"); a->mqtt_wait_s=15;
  JsonDocument d; ASSERT_TRUE(botConfigToJsonChecked(*a,d,true)); std::string s; serializeJson(d,s);
  JsonDocument d2; ASSERT_FALSE(deserializeJson(d2,s));
  BotConfig* b=heapDefaults(); botConfigFromJson(d2.as<JsonVariantConst>(),*b);
  EXPECT_EQ(memcmp(a,b,sizeof *a),0);
  // the stored password reaches the file loader only
  BotConfig* w=heapDefaults(); botConfigFromWebJson(d2.as<JsonVariantConst>(),*w); EXPECT_STREQ(w->mqtt_pass,"s3cret\"\\Value~");
  delete a; delete b; delete w;
}
TEST(Mqtt, StrictWebLoaderListsUnchanged){
  BotConfig* c=withTwoChannels(); BotApplyReport rep;
  webStr(*c,"{\"channels\":[\"#a\",\"bad\"],\"mqtt_port\":8883}",&rep);
  EXPECT_STREQ(rep.channels,"#2 'bad': not a channel name"); EXPECT_EQ(c->num_channels,2); EXPECT_EQ(c->mqtt_port,8883);
  delete c;
}
TEST(Mqtt, RefusedKeysReportNewSettings){
  BotConfig* base=heapDefaults(); strcpy(base->mqtt_pass,"s3cretValue"); strcpy(base->mqtt_host,"broker.example.org");
  struct Case { const char* post; const char* refused; };
  const Case cases[]={
    {"{\"mqtt_enabled\":true,\"mqtt_host\":\"other.example.org\",\"mqtt_port\":8883,\"mqtt_user\":\"bot\",\"mqtt_prefix\":\"a/b\",\"mqtt_wait_s\":9}",""},  // all taken
    {"{\"mqtt_enabled\":1}","mqtt_enabled"},
    {"{\"mqtt_host\":\"bad host\"}","mqtt_host"},
    {"{\"mqtt_host\":\"broker.example.org\"}",""},                  // same as stored
    {"{\"mqtt_port\":0,\"mqtt_wait_s\":16}","mqtt_port, mqtt_wait_s"},
    {"{\"mqtt_port\":1883,\"mqtt_wait_s\":5}",""},
    {"{\"mqtt_user\":\"a b\",\"mqtt_prefix\":\"/x\"}","mqtt_user, mqtt_prefix"},
    {"{\"mqtt_prefix\":\"\"}",""},                                  // empty equals stored empty
    {"{\"mqtt_pass\":\"newValue2\"}",""},                           // replaced
    {"{\"mqtt_pass\":\"\"}",""},                                    // cleared
    {"{\"mqtt_pass\":\"s3cretValue\"}",""},                         // same as stored
    {"{\"mqtt_pass\":\"bad pass\"}","mqtt_pass"},                   // refused, stored one kept
    {"{\"mqtt_pass\":5}","mqtt_pass"},
    {"{\"mqtt_pass\":null}",""},                                    // null means keep: nothing was asked
    {"{\"mqtt_pass_set\":false}",""},                               // ignored, never reported
    {"{\"mqtt_pass_set\":true,\"mqtt_pass\":\"bad pass\"}","mqtt_pass"},
  };
  for(const Case& k : cases) EXPECT_EQ(refusedFor(*base,k.post),k.refused) << k.post;
  // a posted password that fits is never reported when the stored one already equals it, also with no stored one
  BotConfig* none=heapDefaults(); EXPECT_EQ(refusedFor(*none,"{\"mqtt_pass\":\"\"}"),""); EXPECT_EQ(refusedFor(*none,"{\"mqtt_pass\":\"x\"}"),"");
  delete base; delete none;
}
TEST(Mqtt, RefusedKeysMatchWholeDocumentsForNonSecretKeys){
  BotConfig* base=heapDefaults(); strcpy(base->mqtt_host,"broker.example.org"); base->mqtt_enabled=true;
  JsonDocument full; ASSERT_TRUE(botConfigToJsonChecked(*base,full,false)); std::string same; serializeJson(full,same);
  const std::string posts[]={ same, "{\"mqtt_enabled\":false,\"mqtt_host\":\"x y\",\"mqtt_port\":70000,\"mqtt_user\":\"u\",\"mqtt_prefix\":\"bad/\",\"mqtt_wait_s\":0}",
    "{\"mqtt_enabled\":true,\"mqtt_host\":\"broker.example.org\",\"mqtt_port\":1883,\"mqtt_wait_s\":5,\"mqtt_prefix\":\"\"}", "{\"mqtt_enabled\":\"yes\",\"mqtt_port\":\"1\"}" };
  for(const std::string& p : posts){
    JsonDocument d; ASSERT_FALSE(deserializeJson(d,p)); BotConfig* after=new BotConfig; memcpy(after,base,sizeof *after);
    BotApplyReport rep; botConfigFromWebJsonStrict(d.as<JsonVariantConst>(),*after,rep);
    char got[96]; botCfgRefusedKeys(d.as<JsonObjectConst>(),*base,*after,rep,got,sizeof got);
    EXPECT_EQ(std::string(got),refusedByDocuments(d.as<JsonObjectConst>(),*base,*after,rep)) << p;
    delete after;
  }
  delete base;
}

// ---- moving the broker clears its password (security ruling) ----

// Applies post to a copy of the stored settings with the strict web loader, then asks the rule.
static bool movedClears(const BotConfig& stored, const char* post, BotConfig& after){
  memcpy(&after,&stored,sizeof after);
  JsonDocument d; EXPECT_FALSE(deserializeJson(d,post)) << post;
  BotApplyReport rep; botConfigFromWebJsonStrict(d.as<JsonVariantConst>(),after,rep);
  return botCfgBrokerMoveClearsPass(stored,after,d["mqtt_pass"]);
}
TEST(MqttMove, HostOrPortChangeClearsThePassword){
  BotConfig* st=heapDefaults(); st->mqtt_enabled=true; strcpy(st->mqtt_host,"broker.example.org"); strcpy(st->mqtt_user,"bot"); strcpy(st->mqtt_pass,"s3cretValue");
  BotConfig* af=new BotConfig;
  EXPECT_TRUE(movedClears(*st,"{\"mqtt_host\":\"other.example.org\"}",*af));                         // host changed
  EXPECT_TRUE(movedClears(*st,"{\"mqtt_port\":1884}",*af));                                          // port changed
  EXPECT_TRUE(movedClears(*st,"{\"mqtt_host\":\"other.example.org\",\"mqtt_port\":1884}",*af));      // both
  EXPECT_TRUE(movedClears(*st,"{\"mqtt_host\":\"\"}",*af));                                          // host removed
  EXPECT_TRUE(movedClears(*st,"{\"mqtt_host\":\"Broker.example.org\"}",*af));                        // compared exactly as stored
  EXPECT_TRUE(movedClears(*st,"{\"mqtt_host\":\"other.example.org\",\"mqtt_pass\":null}",*af));      // null: not set
  EXPECT_TRUE(movedClears(*st,"{\"mqtt_host\":\"other.example.org\",\"mqtt_pass\":5}",*af));         // not a string
  EXPECT_TRUE(movedClears(*st,"{\"mqtt_host\":\"other.example.org\",\"mqtt_pass\":\"bad pass\"}",*af));   // refused string
  EXPECT_STREQ(af->mqtt_pass,"s3cretValue");   // the loader kept it: the rule is what clears it
  delete st; delete af;
}
TEST(MqttMove, KeptWhenNotMovedOrSetInTheSameRequest){
  BotConfig* st=heapDefaults(); st->mqtt_enabled=true; strcpy(st->mqtt_host,"broker.example.org"); strcpy(st->mqtt_pass,"s3cretValue");
  BotConfig* af=new BotConfig;
  EXPECT_FALSE(movedClears(*st,"{\"mqtt_host\":\"broker.example.org\",\"mqtt_port\":1883}",*af));    // both unchanged
  EXPECT_FALSE(movedClears(*st,"{\"mqtt_user\":\"other\",\"mqtt_prefix\":\"meshcore/MeshBot\",\"mqtt_wait_s\":9,\"mqtt_enabled\":false}",*af));
  EXPECT_FALSE(movedClears(*st,"{\"mqtt_host\":\"bad host\"}",*af));                                // refused: not moved
  EXPECT_FALSE(movedClears(*st,"{\"mqtt_port\":0}",*af));
  EXPECT_FALSE(movedClears(*st,"{\"scope\":\"ca\"}",*af));
  EXPECT_FALSE(movedClears(*st,"{\"mqtt_host\":\"other.example.org\",\"mqtt_pass\":\"newValue2\"}",*af));   // supplied
  EXPECT_STREQ(af->mqtt_pass,"newValue2");
  EXPECT_FALSE(movedClears(*st,"{\"mqtt_host\":\"other.example.org\",\"mqtt_pass\":\"s3cretValue\"}",*af)); // supplied, the same
  EXPECT_FALSE(movedClears(*st,"{\"mqtt_host\":\"other.example.org\",\"mqtt_pass\":\"\"}",*af));     // supplied as "": cleared by the loader
  EXPECT_STREQ(af->mqtt_pass,"");
  st->mqtt_pass[0]='\0';
  EXPECT_FALSE(movedClears(*st,"{\"mqtt_host\":\"other.example.org\"}",*af));                       // nothing stored: nothing to clear
  delete st; delete af;
}
TEST(MqttMove, MessageWords){
  EXPECT_STREQ(BOT_MQTT_PASS_CLEARED_MSG,"broker changed: password cleared, enter it again");
}

TEST(CfgMatch, RulesLoadRoundTripAndRefuse) {
  BotConfig* c = heapDefaults();
  EXPECT_FALSE(c->bare); EXPECT_EQ(c->num_match, 0);
  applyTo(*c, "{\"bare\":true,\"match\":[{\"kind\":\"exact\",\"text\":\"good morning\",\"run\":\"gm\"},"
              "{\"kind\":\"regex\",\"text\":\"^weather (.+)$\",\"run\":\"wx {1}\",\"enabled\":false},"
              "{\"kind\":\"regex\",\"text\":\"(\",\"run\":\"wx\"},{\"kind\":\"glob\",\"text\":\"x\",\"run\":\"wx\"},"
              "{\"kind\":\"exact\",\"text\":\"x\",\"run\":\"!wx\"},{\"kind\":\"exact\",\"text\":\"@[a] x\",\"run\":\"wx\"},7]}");
  EXPECT_TRUE(c->bare);
  ASSERT_EQ(c->num_match, 2);   // the file loader skips the bad ones
  EXPECT_EQ(c->match[0].kind, BMK_EXACT); EXPECT_TRUE(c->match[0].enabled); EXPECT_STREQ(c->match[0].text, "good morning"); EXPECT_STREQ(c->match[0].run, "gm");
  EXPECT_EQ(c->match[1].kind, BMK_REGEX); EXPECT_FALSE(c->match[1].enabled); EXPECT_STREQ(c->match[1].run, "wx {1}");
  JsonDocument d; ASSERT_TRUE(botConfigToJsonChecked(*c, d, true));
  BotConfig* r = heapDefaults(); botConfigFromJson(d.as<JsonVariantConst>(), *r);
  EXPECT_EQ(memcmp(c, r, sizeof *c), 0);
  // The web path takes a list whole or not at all, and says why.
  BotApplyReport rep;
  JsonDocument p; deserializeJson(p, "{\"match\":[{\"kind\":\"exact\",\"text\":\"hi\",\"run\":\"ping\"},{\"kind\":\"regex\",\"text\":\"a{2}\",\"run\":\"wx\"}]}");
  botConfigFromWebJsonStrict(p.as<JsonVariantConst>(), *c, rep);
  EXPECT_EQ(c->num_match, 2); EXPECT_STREQ(c->match[0].run, "gm");
  EXPECT_EQ(std::string(rep.match).rfind("#2: ", 0), 0u) << rep.match;
  std::string nine = "{\"match\":[";
  for (int i = 0; i < 9; i++) nine += std::string(i ? "," : "") + "{\"kind\":\"exact\",\"text\":\"t" + std::to_string(i) + "\",\"run\":\"ping\"}";
  nine += "]}";
  deserializeJson(p, nine); botConfigFromWebJsonStrict(p.as<JsonVariantConst>(), *c, rep);
  EXPECT_EQ(c->num_match, 2); EXPECT_STREQ(rep.match, "#9: at most 8 text rules");
  deserializeJson(p, "{\"match\":[]}"); botConfigFromWebJsonStrict(p.as<JsonVariantConst>(), *c, rep);
  EXPECT_EQ(c->num_match, 0); EXPECT_STREQ(rep.match, "");
  delete c; delete r;
}

TEST(CfgForms, EachCommandSaysWhichFormsItAnswersTo) {
  BotConfig* c = heapDefaults();
  EXPECT_TRUE(c->builtin[BB_PING].pfx); EXPECT_FALSE(c->builtin[BB_PING].bare);
  applyTo(*c, "{\"builtin\":{\"ping\":{\"bare\":true},\"test\":{\"pfx\":false,\"bare\":true},\"path\":{\"pfx\":false},\"help\":{\"pfx\":false,\"bare\":false}},"
              "\"custom\":[{\"trigger\":\"a\",\"reply\":\"x\"},{\"trigger\":\"b\",\"reply\":\"x\",\"pfx\":false,\"bare\":true},"
              "{\"trigger\":\"c\",\"reply\":\"x\",\"pfx\":false},{\"trigger\":\"d\",\"reply\":\"x\",\"bare\":\"yes\"}]}");
  EXPECT_TRUE(c->builtin[BB_PING].pfx);  EXPECT_TRUE(c->builtin[BB_PING].bare);
  EXPECT_FALSE(c->builtin[BB_TEST].pfx); EXPECT_TRUE(c->builtin[BB_TEST].bare);
  EXPECT_TRUE(c->builtin[BB_PATH].pfx);  EXPECT_FALSE(c->builtin[BB_PATH].bare);   // neither form: not taken
  EXPECT_TRUE(c->builtin[BB_HELP].pfx);
  ASSERT_EQ(c->num_custom, 2);   // "c" (neither form) and "d" (not a boolean) are skipped
  EXPECT_TRUE(c->custom[0].pfx); EXPECT_FALSE(c->custom[0].bare);
  EXPECT_FALSE(c->custom[1].pfx); EXPECT_TRUE(c->custom[1].bare);
  JsonDocument d; ASSERT_TRUE(botConfigToJsonChecked(*c, d, true));
  BotConfig* r = heapDefaults(); botConfigFromJson(d.as<JsonVariantConst>(), *r);
  EXPECT_EQ(memcmp(c, r, sizeof *c), 0);
  BotApplyReport rep; JsonDocument p;
  deserializeJson(p, "{\"builtin\":{\"ping\":{\"pfx\":false,\"bare\":false}}}");
  botConfigFromWebJsonStrict(p.as<JsonVariantConst>(), *c, rep);
  EXPECT_TRUE(c->builtin[BB_PING].pfx); EXPECT_NE(rep.builtin[0], '\0');
  deserializeJson(p, "{\"custom\":[{\"trigger\":\"a\",\"reply\":\"x\",\"pfx\":false}]}");
  botConfigFromWebJsonStrict(p.as<JsonVariantConst>(), *c, rep);
  EXPECT_EQ(c->num_custom, 2); EXPECT_NE(rep.custom[0], '\0');
  delete c; delete r;
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
