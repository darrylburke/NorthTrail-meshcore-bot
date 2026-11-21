// Host tests for BotText.h (botClamp, botSplitSender) and BotCommandParser.h (botParse).
#include <gtest/gtest.h>
#include <cstring>
#include <string>
#include "helpers/bot/BotText.h"
#include "helpers/bot/BotCommandParser.h"

TEST(BotClamp, ShortTextUnchanged)      { char b[32]; EXPECT_EQ(botClamp(b,32,"hello",20),5u); EXPECT_STREQ(b,"hello"); }
TEST(BotClamp, CutAddsEllipsisWithinMax){ char b[32]; size_t n=botClamp(b,32,"abcdefghijklmnop",10); EXPECT_EQ(n,10u); EXPECT_STREQ(b,"abcdefg\xE2\x80\xA6"); }
TEST(BotClamp, NeverSplitsEmoji)        { char b[32]; botClamp(b,32,"ab\xF0\x9F\xA7\xADzzzzzz",7); EXPECT_STREQ(b,"ab\xE2\x80\xA6"); }
TEST(BotClamp, RespectsDstCap)          { char b[6]; size_t n=botClamp(b,6,"abcdefghij",50); EXPECT_LT(n,6u); EXPECT_EQ(b[n],'\0'); }
TEST(BotSplit, NameWithEmoji)           { char n[32]; const char* body; ASSERT_TRUE(botSplitSender("MeshNode\xF0\x9F\xA7\xAD: !ping",n,32,&body)); EXPECT_STREQ(body,"!ping"); }
TEST(BotSplit, UsesFirstSeparator)      { char n[32]; const char* body; ASSERT_TRUE(botSplitSender("Dee: note: hi",n,32,&body)); EXPECT_STREQ(n,"Dee"); EXPECT_STREQ(body,"note: hi"); }
TEST(BotSplit, NoSeparator)             { char n[32]; const char* body; EXPECT_FALSE(botSplitSender("hello",n,32,&body)); }
TEST(BotParse, Basic)                   { BotParsed p; ASSERT_TRUE(botParse("!ping",'!',false,p)); EXPECT_STREQ(p.cmd,"ping"); EXPECT_STREQ(p.args,""); }
TEST(BotParse, PrefixedFlag)            { BotParsed p; ASSERT_TRUE(botParse("!ping",'!',false,p)); EXPECT_TRUE(p.prefixed); ASSERT_TRUE(botParse("  !ping x",'!',true,p)); EXPECT_TRUE(p.prefixed); ASSERT_TRUE(botParse("ping x",'!',true,p)); EXPECT_FALSE(p.prefixed); EXPECT_STREQ(p.cmd,"ping"); EXPECT_STREQ(p.args,"x"); p.prefixed = true; EXPECT_FALSE(botParse("ping",'!',false,p)); EXPECT_TRUE(p.prefixed); }
TEST(BotParse, UpperCaseAndArgs)        { BotParsed p; ASSERT_TRUE(botParse("  !WX  Ottawa ",'!',false,p)); EXPECT_STREQ(p.cmd,"wx"); EXPECT_STREQ(p.args,"Ottawa "); }
TEST(BotParse, ChannelNeedsPrefix)      { BotParsed p; EXPECT_FALSE(botParse("ping",'!',false,p)); }
TEST(BotParse, DmPrefixOptional)        { BotParsed p; EXPECT_TRUE(botParse("ping",'!',true,p)); EXPECT_TRUE(botParse("!ping",'!',true,p)); }
TEST(BotParse, RejectsMentionReply)     { BotParsed p; EXPECT_FALSE(botParse("@[Dee] 3 hops",'!',true,p)); }
TEST(BotParse, RejectsBareAndDoubled)   { BotParsed p; EXPECT_FALSE(botParse("!",'!',false,p)); EXPECT_FALSE(botParse("!!",'!',false,p)); EXPECT_FALSE(botParse("! ping",'!',false,p)); }
TEST(BotParse, RejectsOverlongWord)     { BotParsed p; EXPECT_FALSE(botParse("!abcdefghijklmnopq",'!',false,p)); }
TEST(BotParse, LongArgsKept)            { std::string s="!say "+std::string(150,'x'); BotParsed p; ASSERT_TRUE(botParse(s.c_str(),'!',false,p)); EXPECT_EQ(strlen(p.args),150u); }

// Extra edge cases (guard canary past the logical buffer to catch overruns).
TEST(BotClamp, ZeroCapWritesNothing) {
  char b[4]; memset(b, 0xAA, sizeof(b));
  EXPECT_EQ(botClamp(b, 0, "abc", 10), 0u);
  EXPECT_EQ((unsigned char)b[0], 0xAAu);
}
TEST(BotClamp, TinyMaxHasNoEllipsis) {
  char b[16]; size_t n;
  n = botClamp(b, 16, "abcdef", 2); EXPECT_EQ(n, 2u); EXPECT_STREQ(b, "ab");
  n = botClamp(b, 16, "abcdef", 0); EXPECT_EQ(n, 0u); EXPECT_STREQ(b, "");
  n = botClamp(b, 16, "\xE2\x80\xA6" "abc", 2); EXPECT_EQ(n, 0u); EXPECT_STREQ(b, ""); // no split of the 3-byte char
}
TEST(BotClamp, ExactFitNotCut) { char b[16]; EXPECT_EQ(botClamp(b,16,"abcde",5),5u); EXPECT_STREQ(b,"abcde"); }
TEST(BotClamp, DstCapSmallerThanMaxWins) {
  char b[5]; size_t n = botClamp(b, 5, "abcdefgh", 100);
  EXPECT_LE(n, 4u); EXPECT_EQ(b[n], '\0');
}
TEST(BotClamp, NullTextIsEmpty) { char b[4]; EXPECT_EQ(botClamp(b,4,nullptr,3),0u); EXPECT_STREQ(b,""); }
TEST(BotSplit, EmptyNameRejected)  { char n[8]; const char* body; EXPECT_FALSE(botSplitSender(": hi",n,8,&body)); }
TEST(BotSplit, ColonWithoutSpace)  { char n[8]; const char* body; EXPECT_FALSE(botSplitSender("a:b",n,8,&body)); }
TEST(BotSplit, EmptyBodyOk)        { char n[8]; const char* body; ASSERT_TRUE(botSplitSender("Dee: ",n,8,&body)); EXPECT_STREQ(n,"Dee"); EXPECT_STREQ(body,""); }
TEST(BotSplit, SmallNameCapTruncatesAtCharBoundary) {
  char n[5]; const char* body;
  ASSERT_TRUE(botSplitSender("ab\xF0\x9F\xA7\xAD" "cd: x",n,5,&body));
  EXPECT_STREQ(n,"ab"); // emoji (4 bytes) would not fit in 4 chars
  EXPECT_STREQ(body,"x");
  char m[4]; ASSERT_TRUE(botSplitSender("abcdef: x",m,4,&body)); EXPECT_STREQ(m,"abc");
}
TEST(BotSplit, ZeroNameCapStillSplits) { const char* body; ASSERT_TRUE(botSplitSender("Dee: hi",nullptr,0,&body)); EXPECT_STREQ(body,"hi"); }
TEST(BotParse, SixteenCharWordAccepted) { BotParsed p; ASSERT_TRUE(botParse("!abcdefghijklmnop x",'!',false,p)); EXPECT_STREQ(p.cmd,"abcdefghijklmnop"); EXPECT_STREQ(p.args,"x"); }
TEST(BotParse, DigitsAllowed)           { BotParsed p; ASSERT_TRUE(botParse("!b2 go",'!',false,p)); EXPECT_STREQ(p.cmd,"b2"); }
TEST(BotParse, EmptyAndSpacesOnly)      { BotParsed p; EXPECT_FALSE(botParse("",'!',true,p)); EXPECT_FALSE(botParse("   ",'!',true,p)); EXPECT_FALSE(botParse(nullptr,'!',true,p)); }
TEST(BotParse, DmRejectsDoubledAndSpaced){ BotParsed p; EXPECT_FALSE(botParse("!!ping",'!',true,p)); EXPECT_FALSE(botParse("! ping",'!',true,p)); }
TEST(BotParse, MentionAfterSpacesRejected){ BotParsed p; EXPECT_FALSE(botParse("  @[x] !ping",'!',false,p)); }
TEST(BotParse, NonAlnumStopsWord)       { BotParsed p; ASSERT_TRUE(botParse("!ping,now",'!',false,p)); EXPECT_STREQ(p.cmd,"ping"); EXPECT_STREQ(p.args,",now"); }
TEST(BotParse, NonAsciiNotAWord)        { BotParsed p; EXPECT_FALSE(botParse("!\xC3\xA9" "x",'!',false,p)); }

// Every limit from 0 to 20 over multi-byte and malformed text: never longer than the limit,
// always NUL-terminated, and the kept prefix ends where the input has a character start, so
// the cut itself never leaves a partial sequence (one the input already had may remain).
TEST(BotClamp, SweepNeverSplitsOrOverruns) {
  const std::string inputs[] = {
    "a\xF0\x9F\x98\x80" "b\xF0\x9F\x98\x80\xF0\x9F\x98\x80" "c\xE2\x82\xAC\xC3\xA9" "d\xF0\x9F\xA7\xAD\xF0\x9F\xA7\xAD",
    "\xF0\x9F\x98\x80\xF0\x9F\x98\x80\xF0\x9F\x98\x80\xF0\x9F\x98\x80\xF0\x9F\x98\x80\xF0\x9F\x98\x80",
    "\x80" "abcdefghijklmnopqrstuvwxyz",        // lone continuation byte first
    "abc\x80" "def\x80\x80ghijklmnopqrstu",       // stray continuation bytes inside
    "abcdefghijklmnopqrstuvw\xF0\x9F",            // truncated lead sequence at the end
    "abcdefghijklmnopq\xE2",                       // lone lead byte at the end
    "short",
  };
  for (const std::string& in : inputs) {
    for (size_t max = 0; max <= 20; max++) {
      char b[64]; memset(b, 0x55, sizeof(b));
      size_t n = botClamp(b, sizeof(b), in.c_str(), max);
      ASSERT_LE(n, max);
      ASSERT_EQ(b[n], '\0');
      ASSERT_EQ(strlen(b), n);
      std::string out(b, n);
      if (out == in) continue;
      std::string kept = out;
      if (kept.size() >= 3 && kept.compare(kept.size() - 3, 3, "\xE2\x80\xA6") == 0) kept.resize(kept.size() - 3);
      ASSERT_EQ(in.compare(0, kept.size(), kept), 0) << max;   // a prefix of the input
      ASSERT_LT(kept.size(), in.size());
      if (!kept.empty()) EXPECT_FALSE(botIsUtf8Cont(in[kept.size()])) << "cut inside a character at max " << max;
    }
  }
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
