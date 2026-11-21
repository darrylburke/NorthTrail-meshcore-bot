// Host tests for BotLimits.h (BotCooldown, BotRateCap, botIsIgnored).
#include <gtest/gtest.h>
#include <cstdio>
#include "helpers/bot/BotLimits.h"

TEST(Cooldown, BlocksThenAllows)   { BotCooldown c; EXPECT_TRUE(c.allow("Dee",1000,10000)); EXPECT_FALSE(c.allow("Dee",5000,10000)); EXPECT_TRUE(c.allow("Dee",11000,10000)); }
TEST(Cooldown, PerSender)          { BotCooldown c; EXPECT_TRUE(c.allow("Dee",1000,10000)); EXPECT_TRUE(c.allow("Rob",1001,10000)); }
TEST(Cooldown, CaseInsensitive)    { BotCooldown c; EXPECT_TRUE(c.allow("Dee",1000,10000)); EXPECT_FALSE(c.allow("DEE",2000,10000)); }
TEST(Cooldown, SurvivesRollover)   { BotCooldown c; EXPECT_TRUE(c.allow("Dee",0xFFFFF000u,10000)); EXPECT_FALSE(c.allow("Dee",0x00000100u,10000)); EXPECT_TRUE(c.allow("Dee",0x00003000u,10000)); }
TEST(Cooldown, SeventeenSenders)   { BotCooldown c; char n[8]; for(int i=0;i<17;i++){ snprintf(n,8,"s%d",i); EXPECT_TRUE(c.allow(n,1000+i,10000)); } }
TEST(RateCap, SixPerMinute)        { BotRateCap r; for(int i=0;i<6;i++) EXPECT_TRUE(r.allow(1000+i*100,6)); EXPECT_FALSE(r.allow(2000,6)); EXPECT_TRUE(r.allow(61001,6)); }
TEST(RateCap, StopsBotPingPong)    { BotRateCap r; int sent=0; for(uint32_t t=0;t<60000;t+=500) if(r.allow(t,6)) sent++; EXPECT_EQ(sent,6); }
TEST(RateCap, SurvivesRollover)    { BotRateCap r; for(int i=0;i<6;i++) EXPECT_TRUE(r.allow(0xFFFFFF00u+i,6)); EXPECT_FALSE(r.allow(0x00000010u,6)); }
TEST(Ignore, OwnNameAndPatterns)   { EXPECT_TRUE(botIsIgnored("MeshBot","meshbot","")); EXPECT_TRUE(botIsIgnored("rr-Bot \xF0\x9F\x97\xBF","Me","bot")); EXPECT_FALSE(botIsIgnored("Dee","Me","bot,relay")); EXPECT_TRUE(botIsIgnored("MyRelay","Me","bot, relay")); }

// Extra edge cases.
TEST(Cooldown, ZeroCooldownAlwaysAllows) { BotCooldown c; EXPECT_TRUE(c.allow("Dee",1000,0)); EXPECT_TRUE(c.allow("Dee",1000,0)); EXPECT_TRUE(c.allow("Dee",1001,0)); }
TEST(Cooldown, ExactBoundaryAllows)      { BotCooldown c; EXPECT_TRUE(c.allow("Dee",1000,10000)); EXPECT_FALSE(c.allow("Dee",10999,10000)); EXPECT_TRUE(c.allow("Dee",11000,10000)); }
TEST(Cooldown, BlockedAttemptDoesNotExtend) { BotCooldown c; EXPECT_TRUE(c.allow("Dee",1000,10000)); EXPECT_FALSE(c.allow("Dee",9000,10000)); EXPECT_TRUE(c.allow("Dee",11000,10000)); }
TEST(Cooldown, NullAndEmptySender)       { BotCooldown c; EXPECT_TRUE(c.allow(nullptr,1000,10000)); EXPECT_FALSE(c.allow("",2000,10000)); }
TEST(Cooldown, FullTableEvictsOldest) {
  BotCooldown c; char n[8];
  for (int i = 0; i < 16; i++) { snprintf(n,8,"s%d",i); EXPECT_TRUE(c.allow(n,1000+i,100000)); }
  EXPECT_TRUE(c.allow("extra",2000,100000));   // evicts s0 (oldest)
  EXPECT_TRUE(c.allow("s0",2001,100000));      // s0 forgotten -> allowed again (evicts s1)
  EXPECT_FALSE(c.allow("s15",2002,100000));    // s15 still tracked
}
TEST(Cooldown, EvictionSurvivesRollover) {
  BotCooldown c; char n[8];
  for (int i = 0; i < 16; i++) { snprintf(n,8,"s%d",i); EXPECT_TRUE(c.allow(n,0xFFFFFFF0u+i,100000)); }
  EXPECT_TRUE(c.allow("extra",0x10u,100000));  // oldest by wrapped age is s0
  EXPECT_TRUE(c.allow("s0",0x11u,100000));
  EXPECT_FALSE(c.allow("s15",0x12u,100000));
}
TEST(RateCap, WouldAllowMatchesAndRecordsNothing) {
  BotRateCap r;
  for (int i = 0; i < 3; i++) { EXPECT_TRUE(r.wouldAllow(1000 + i, 3)); EXPECT_TRUE(r.wouldAllow(1000 + i, 3)); EXPECT_TRUE(r.allow(1000 + i, 3)); }
  EXPECT_FALSE(r.wouldAllow(2000, 3)); EXPECT_FALSE(r.allow(2000, 3));
  EXPECT_TRUE(r.wouldAllow(61000, 3));   // the first one has left the window
  EXPECT_FALSE(r.wouldAllow(61000, 0));
  BotRateCap q; for (int i = 0; i < 50; i++) EXPECT_TRUE(q.wouldAllow(5, 1));   // never records
  EXPECT_TRUE(q.allow(5, 1)); EXPECT_FALSE(q.wouldAllow(6, 1));
}
TEST(RateCap, ZeroAlwaysRefuses)         { BotRateCap r; EXPECT_FALSE(r.allow(1000,0)); EXPECT_FALSE(r.allow(900000,0)); }
TEST(RateCap, WindowIsExactlySixtySeconds) { BotRateCap r; EXPECT_TRUE(r.allow(1000,1)); EXPECT_FALSE(r.allow(60999,1)); EXPECT_TRUE(r.allow(61000,1)); }
TEST(RateCap, RefusedDoesNotConsumeSlot) { BotRateCap r; EXPECT_TRUE(r.allow(1000,1)); for (int i=0;i<10;i++) EXPECT_FALSE(r.allow(2000+i,1)); EXPECT_TRUE(r.allow(61000,1)); }
TEST(RateCap, CapsAtThirty)              { BotRateCap r; int sent=0; for (int i=0;i<100;i++) if (r.allow(1000+i,255)) sent++; EXPECT_EQ(sent,30); }
TEST(RateCap, SlidingWindowReleasesOneAtATime) {
  BotRateCap r; EXPECT_TRUE(r.allow(0,2)); EXPECT_TRUE(r.allow(30000,2)); EXPECT_FALSE(r.allow(59999,2));
  EXPECT_TRUE(r.allow(60000,2)); EXPECT_FALSE(r.allow(60001,2)); EXPECT_TRUE(r.allow(90000,2));
}
TEST(Ignore, OwnNameExactOnly)           { EXPECT_FALSE(botIsIgnored("MeshBot2","MeshBot","")); EXPECT_FALSE(botIsIgnored("Dee","","")); }
TEST(Ignore, NullArgsSafe)               { EXPECT_FALSE(botIsIgnored(nullptr,nullptr,nullptr)); EXPECT_FALSE(botIsIgnored("Dee",nullptr,nullptr)); EXPECT_FALSE(botIsIgnored(nullptr,"Me","bot")); EXPECT_TRUE(botIsIgnored("Dee","Dee",nullptr)); }
TEST(Ignore, EmptySenderEmptyOwnNotMatched) { EXPECT_FALSE(botIsIgnored("","","bot")); EXPECT_FALSE(botIsIgnored("","",",, ,")); }
TEST(Ignore, EmptyPatternsMatchNothing)  { EXPECT_FALSE(botIsIgnored("Dee","Me",",")); EXPECT_FALSE(botIsIgnored("Dee","Me","  ,  , ,")); EXPECT_FALSE(botIsIgnored("Dee","Me","")); }
TEST(Ignore, PatternsTrimmedAndFolded)   { EXPECT_TRUE(botIsIgnored("Dee-RELAY","Me","  foo ,  relay  ")); EXPECT_TRUE(botIsIgnored("xFOOx","Me","foo")); EXPECT_FALSE(botIsIgnored("Dee","Me","foo, bar")); }
TEST(Ignore, PatternWithInnerSpace)      { EXPECT_TRUE(botIsIgnored("my bot 2","Me","my bot")); EXPECT_FALSE(botIsIgnored("mybot","Me","my bot")); }
TEST(Ignore, NonAsciiNotFolded)          { EXPECT_TRUE(botIsIgnored("A\xC3\x89Z","Me","\xC3\x89")); EXPECT_FALSE(botIsIgnored("A\xC3\xA9Z","Me","\xC3\x89")); }
TEST(Ignore, PatternLongerThanSender)    { EXPECT_FALSE(botIsIgnored("ab","Me","abc")); }

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
