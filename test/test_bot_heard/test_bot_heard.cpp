// Host tests for BotHeard.h (BotHeardTable, BotMeshStats).
#include <gtest/gtest.h>
#include <cstdio>
#include <cstring>
#include "helpers/bot/BotHeard.h"

static const uint8_t K1[]={1,2,3,4}, K2[]={5,6,7,8};
TEST(Heard, CountsWithinWindow)  { BotHeardTable t; t.note(K1,"Dee",1000,0,7.5f); t.note(K2,"Rob",90000,2,-3.0f); EXPECT_EQ(t.count(90000,86400),1); EXPECT_EQ(t.count(90000,200000),2); }
TEST(Heard, SameKeyUpdates)      { BotHeardTable t; t.note(K1,"Dee",1000,0,7.5f); t.note(K1,"Dee",2000,0,9.0f); EXPECT_EQ(t.count(2000,86400),1); }
TEST(Heard, FormatDirectOnly)    { BotHeardTable t; t.note(K1,"Dee",1000,0,7.5f); t.note(K2,"Rob",1100,2,-3.0f); char b[128]; t.format(b,128,1200,100); EXPECT_STREQ(b,"Heard 2 in 24h. Direct: Dee 7.5"); }
TEST(Heard, FormatFitsMax)       { BotHeardTable t; uint8_t k[4]={0,0,0,0}; char n[16]; for(int i=0;i<40;i++){ k[0]=i; snprintf(n,16,"Node%02d",i); t.note(k,n,1000+i,0,1.0f);} char b[128]; EXPECT_LE(t.format(b,128,2000,60),60u); }
TEST(Heard, ReplacesOldestAt65)  { BotHeardTable t; uint8_t k[4]={0,0,0,0}; for(int i=0;i<65;i++){ k[0]=i; k[1]=i>>8; t.note(k,"n",1000+i,1,0);} EXPECT_EQ(t.count(2000,86400),64); }
TEST(Mesh, HourWindowAndAverage) { BotMeshStats m; m.notePacket(0,2); m.notePacket(60000,4); EXPECT_EQ(m.packetsLastHour(120000),2); EXPECT_NEAR(m.avgHops(120000),3.0,0.01); EXPECT_EQ(m.packetsLastHour(3700000),0); }
TEST(Mesh, SurvivesRollover)     { BotMeshStats m; m.notePacket(0xFFFFFF00u,1); m.notePacket(0x00000100u,1); EXPECT_EQ(m.packetsLastHour(0x00000200u),2); }

// Extra edge cases.
TEST(Heard, EmptyTable)          { BotHeardTable t; EXPECT_EQ(t.count(5000,86400),0); char b[64]; t.format(b,64,5000,60); EXPECT_STREQ(b,"Heard 0 in 24h."); }
TEST(Heard, NoDirectOmitsList)   { BotHeardTable t; t.note(K1,"Dee",1000,3,7.5f); char b[64]; t.format(b,64,1200,100); EXPECT_STREQ(b,"Heard 1 in 24h."); }
TEST(Heard, UpdateChangesAllFields) {
  BotHeardTable t; t.note(K1,"Dee",1000,3,7.5f); t.note(K1,"Dee2",2000,0,-1.25f);
  char b[128]; t.format(b,128,2100,100); EXPECT_STREQ(b,"Heard 1 in 24h. Direct: Dee2 -1.25");
}
TEST(Heard, MostRecentFirst)     { BotHeardTable t; t.note(K1,"Dee",1000,0,1.0f); t.note(K2,"Rob",1500,0,2.0f); char b[128]; t.format(b,128,2000,100); EXPECT_STREQ(b,"Heard 2 in 24h. Direct: Rob 2, Dee 1"); }
TEST(Heard, OutOfWindowNotListed) { BotHeardTable t; t.note(K1,"Old",1000,0,1.0f); t.note(K2,"New",100000,0,2.0f); char b[128]; t.format(b,128,100000,100); EXPECT_STREQ(b,"Heard 1 in 24h. Direct: New 2"); }
TEST(Heard, EmptyNameShowsHex)   { static const uint8_t K[]={0xAB,0x0C,9,9}; BotHeardTable t; t.note(K,"",1000,0,1.0f); t.note(K2,nullptr,1001,0,2.0f); char b[128]; t.format(b,128,1100,100); EXPECT_STREQ(b,"Heard 2 in 24h. Direct: 0506 2, AB0C 1"); }
TEST(Heard, NameTruncatedOnUtf8Boundary) {
  BotHeardTable t; char n[64]; memset(n,'a',22); n[22]='\0'; strcat(n,"\xC3\xA9\xC3\xA9");  // 22 ASCII + two 2-byte chars: second char would straddle byte 23
  t.note(K1,n,1000,0,0.0f); char b[128]; t.format(b,128,1100,100);
  char want[128]; memset(want,'a',22); want[22]='\0'; strcat(want,"\xC3\xA9"); char full[160]; snprintf(full,sizeof full,"Heard 1 in 24h. Direct: %s 0",want);
  // 22 + "é"(2) = 24 bytes > 23 usable, so the stored name is the 22 ASCII bytes only
  snprintf(full,sizeof full,"Heard 1 in 24h. Direct: %.22s 0",want); EXPECT_STREQ(b,full);
}
TEST(Heard, LongAsciiNameKeeps23)  { BotHeardTable t; t.note(K1,"abcdefghijklmnopqrstuvwxyz",1000,0,0.0f); char b[128]; t.format(b,128,1100,100); EXPECT_STREQ(b,"Heard 1 in 24h. Direct: abcdefghijklmnopqrstuvw 0"); }
TEST(Heard, FormatDropsNamesNotFit) {
  BotHeardTable t; t.note(K1,"Dee",1000,0,7.5f); t.note(K2,"Rob",1100,0,-3.0f); char b[128];
  // "Heard 2 in 24h. Direct: Rob -3" is 30 bytes; adding ", Dee 7.5" needs 39
  EXPECT_EQ(t.format(b,128,1200,38),30u); EXPECT_STREQ(b,"Heard 2 in 24h. Direct: Rob -3");
  EXPECT_EQ(t.format(b,128,1200,39),39u); EXPECT_STREQ(b,"Heard 2 in 24h. Direct: Rob -3, Dee 7.5");
  EXPECT_EQ(t.format(b,128,1200,29),15u); EXPECT_STREQ(b,"Heard 2 in 24h.");   // not even one name fits: no dangling "Direct:"
}
TEST(Heard, FormatRespectsCap)   { BotHeardTable t; t.note(K1,"Dee",1000,0,7.5f); t.note(K2,"Rob",1100,0,-3.0f); char b[40]; EXPECT_EQ(t.format(b,31,1200,500),30u); EXPECT_STREQ(b,"Heard 2 in 24h. Direct: Rob -3"); EXPECT_EQ(t.format(b,0,1200,500),0u); }
TEST(Heard, FormatTinyBuffer)    { BotHeardTable t; t.note(K1,"Dee",1000,0,7.5f); char b[8]; EXPECT_EQ(t.format(b,8,1200,500),7u); EXPECT_STREQ(b,"Heard 1"); }
TEST(Heard, SurvivesClockWrapSeconds) { BotHeardTable t; t.note(K1,"Dee",0xFFFFFF00u,0,1.0f); EXPECT_EQ(t.count(0x00000100u,86400),1); }
TEST(Heard, FullTableEvictsOldestNotFirstSlot) {
  BotHeardTable t; uint8_t k[4]={0,0,0,0};
  for (int i=0;i<64;i++){ k[0]=i; t.note(k,"n",2000-i,1,0); }   // slot 63 is the oldest
  uint8_t x[4]={200,0,0,0}; t.note(x,"x",3000,0,0);
  EXPECT_EQ(t.count(3000,999),1);   // only x is within 999 s
  EXPECT_EQ(t.count(3000,2000),64);
  EXPECT_EQ(t.count(3000,1062),64);        // key 63 (age 1063) was the one evicted; evicting key 0 would give 63
  k[0]=0; t.note(k,"n",3001,1,0);          // key 0 (newest of the old) still present: updates, no eviction
  EXPECT_EQ(t.count(3001,2000),64);
}
TEST(Mesh, EmptyIsZero)          { BotMeshStats m; EXPECT_EQ(m.packetsLastHour(1000),0); EXPECT_EQ(m.avgHops(1000),0.0f); }
TEST(Mesh, LongSilenceDropsOldBuckets) { BotMeshStats m; m.notePacket(1000,5); m.notePacket(5000,5); EXPECT_EQ(m.packetsLastHour(8000000),0); EXPECT_EQ(m.avgHops(8000000),0.0f); m.notePacket(8000000,1); EXPECT_EQ(m.packetsLastHour(8000100),1); EXPECT_NEAR(m.avgHops(8000100),1.0,0.01); }
TEST(Mesh, SameMinuteAccumulates){ BotMeshStats m; for(int i=0;i<10;i++) m.notePacket(1000+i*100,1); EXPECT_EQ(m.packetsLastHour(3000),10); }
TEST(Mesh, ManyBucketsSlideOut) { BotMeshStats m; for(int i=0;i<100;i++) m.notePacket((uint32_t)i*60000u,1); EXPECT_EQ(m.packetsLastHour(99u*60000u),60); }
TEST(Mesh, CountSaturates)       { BotMeshStats m; for(int i=0;i<70000;i++) m.notePacket(1000,255); EXPECT_EQ(m.packetsLastHour(1000),65535); EXPECT_GT(m.avgHops(1000),0.0f); }
TEST(Mesh, ThirtyDaySilenceThenPacket) { BotMeshStats m; const uint32_t d30=30u*86400000u; m.notePacket(1000,3); m.notePacket(1000+d30,1); EXPECT_EQ(m.packetsLastHour(1000+d30),1); EXPECT_NEAR(m.avgHops(1000+d30),1.0,0.01); }
TEST(Mesh, SilenceAcrossWrap)    { BotMeshStats m; m.notePacket(0xFFFFF000u,1); EXPECT_EQ(m.packetsLastHour(0x00400000u),0); }

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
