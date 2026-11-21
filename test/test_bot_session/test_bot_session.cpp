// Host tests for BotSession.h (BotSessions: web login sessions).
#include <gtest/gtest.h>
#include <cstring>
#include "helpers/bot/BotSession.h"

static const uint8_t R[16]={1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
TEST(Session, CreateAndCheck)   { BotSessions s; char t[33]; s.create(R,1000,t); EXPECT_EQ(strlen(t),32u); EXPECT_TRUE(s.check(t,2000)); }
TEST(Session, WrongToken)       { BotSessions s; char t[33]; s.create(R,1000,t); EXPECT_FALSE(s.check("00000000000000000000000000000000",2000)); EXPECT_FALSE(s.check("",2000)); EXPECT_FALSE(s.check(nullptr,2000)); }
TEST(Session, IdleExpiry)       { BotSessions s; char t[33]; s.create(R,0,t); EXPECT_TRUE(s.check(t,19*60000u)); EXPECT_TRUE(s.check(t,38*60000u)); EXPECT_FALSE(s.check(t,38*60000u+20*60000u+1)); }
TEST(Session, Revoke)           { BotSessions s; char t[33]; s.create(R,0,t); s.revoke(t); EXPECT_FALSE(s.check(t,1)); }
TEST(Session, FifthReplacesOld) { BotSessions s; char t[5][33]; uint8_t r[16]; for(int i=0;i<5;i++){ memset(r,i+1,16); s.create(r,i*1000,t[i]); } EXPECT_FALSE(s.check(t[0],6000)); EXPECT_TRUE(s.check(t[4],6000)); }
TEST(Session, SurvivesRollover) { BotSessions s; char t[33]; s.create(R,0xFFFFFF00u,t); EXPECT_TRUE(s.check(t,0x00000100u)); }

// Extra edge cases.
TEST(Session, TokenIsLowerHexOfRandom) {
  BotSessions s; char t[33]; s.create(R,0,t);
  EXPECT_STREQ(t, "0102030405060708090a0b0c0d0e0f10");
}
TEST(Session, ExactlyTwentyMinutesStillValid) {
  BotSessions s; char t[33]; s.create(R,0,t);
  EXPECT_TRUE(s.check(t,20*60000u)); EXPECT_FALSE(s.check(t,40*60000u+1));
}
TEST(Session, WrongLengthAndCaseRejected) {
  BotSessions s; char t[33]; s.create(R,0,t);
  char longer[40]; snprintf(longer, sizeof(longer), "%s0", t);
  EXPECT_FALSE(s.check(longer,1));
  char shorter[33]; memcpy(shorter, t, 31); shorter[31] = 0;
  EXPECT_FALSE(s.check(shorter,1));
  EXPECT_FALSE(s.check("0102030405060708090A0B0C0D0E0F10",1));   // tokens are lower-case
  EXPECT_TRUE(s.check(t,1));
}
TEST(Session, RevokeOnlyThatOne) {
  BotSessions s; char a[33], b[33]; uint8_t r2[16]; memset(r2,0xAB,16);
  s.create(R,0,a); s.create(r2,0,b); s.revoke(a);
  EXPECT_FALSE(s.check(a,1)); EXPECT_TRUE(s.check(b,1));
  s.revoke(nullptr); s.revoke(""); EXPECT_TRUE(s.check(b,1));
}
TEST(Session, RevokeAllExcept) {
  BotSessions s; char t[3][33]; uint8_t r[16];
  for(int i=0;i<3;i++){ memset(r,0x10+i,16); s.create(r,0,t[i]); }
  s.revokeAllExcept(t[1]);
  EXPECT_FALSE(s.check(t[0],1)); EXPECT_TRUE(s.check(t[1],1)); EXPECT_FALSE(s.check(t[2],1));
  s.revokeAllExcept(nullptr);
  EXPECT_FALSE(s.check(t[1],1));
}
TEST(Session, ExpiredSlotReusedBeforeLive) {
  BotSessions s; char t[5][33]; uint8_t r[16];
  memset(r,1,16); s.create(r,0,t[0]);                    // will expire
  for(int i=1;i<4;i++){ memset(r,i+1,16); s.create(r,30*60000u,t[i]); }
  memset(r,9,16); s.create(r,30*60000u+5,t[4]);         // takes the expired slot
  for(int i=1;i<5;i++) EXPECT_TRUE(s.check(t[i],30*60000u+10));
}
TEST(Session, CheckRefreshKeepsActiveOne) {
  BotSessions s; char t[5][33]; uint8_t r[16];
  for(int i=0;i<4;i++){ memset(r,i+1,16); s.create(r,i*1000,t[i]); }
  EXPECT_TRUE(s.check(t[0],5000));                       // t[0] now most recently used
  memset(r,7,16); s.create(r,6000,t[4]);                 // replaces t[1], the least recently used
  EXPECT_TRUE(s.check(t[0],7000)); EXPECT_FALSE(s.check(t[1],7000)); EXPECT_TRUE(s.check(t[4],7000));
}

TEST(Session, CheckWithoutTouchKeepsIdleTimer) {
  BotSessions s; char t[33]; s.create(R,0,t);
  EXPECT_TRUE(s.check(t,19*60000u,false));               // valid, but the timer is not refreshed
  EXPECT_TRUE(s.check(t,20*60000u,false));
  EXPECT_FALSE(s.check(t,20*60000u+1,false));            // 20 minutes after the last refresh
  EXPECT_FALSE(s.check(t,20*60000u+2));                  // and gone for good
}
TEST(Session, TouchAfterPollsRefreshes) {
  BotSessions s; char t[33]; s.create(R,0,t);
  EXPECT_TRUE(s.check(t,10*60000u,false));
  EXPECT_TRUE(s.check(t,15*60000u));                     // a POST refreshes
  EXPECT_TRUE(s.check(t,34*60000u,false));
  EXPECT_FALSE(s.check(t,35*60000u+1,false));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
