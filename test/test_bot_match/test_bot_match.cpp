// Host tests for BotRegex.h (the bounded pattern matcher) and BotMatch.h (text rules).
#include <gtest/gtest.h>
#include <cstring>
#include <string>
#include "helpers/bot/BotMatch.h"

static bool rx(const char* pat, const char* text) {
  EXPECT_EQ(botRxCheck(pat), nullptr) << pat;
  BotRxMatch m;
  return botRxSearch(pat, text, m);
}
static std::string grp(const char* pat, const char* text, int g) {
  BotRxMatch m;
  if (!botRxSearch(pat, text, m) || !m.s[g]) return "<none>";
  return std::string(m.s[g], (size_t)(m.e[g] - m.s[g]));
}

TEST(Rx, LiteralsAnywhereAndCaseFolded) {
  EXPECT_TRUE(rx("weather", "what is the Weather like"));
  EXPECT_TRUE(rx("WEATHER", "weather"));
  EXPECT_FALSE(rx("weather", "whether"));
}
TEST(Rx, Anchors) {
  EXPECT_TRUE(rx("^good morning$", "Good Morning"));
  EXPECT_FALSE(rx("^good morning$", "good morning all"));
  EXPECT_FALSE(rx("^good", "a good day"));
  EXPECT_TRUE(rx("day$", "a good day"));
  EXPECT_TRUE(rx("^$", ""));
}
TEST(Rx, DotAndRepeats) {
  EXPECT_TRUE(rx("^a.c$", "abc"));
  EXPECT_FALSE(rx("^a.c$", "ac"));
  EXPECT_TRUE(rx("^ab*c$", "ac"));
  EXPECT_TRUE(rx("^ab*c$", "abbbc"));
  EXPECT_FALSE(rx("^ab+c$", "ac"));
  EXPECT_TRUE(rx("^ab?c$", "abc"));
  EXPECT_FALSE(rx("^ab?c$", "abbc"));
  EXPECT_TRUE(rx("^a.*c$", "a whole lot of c"));
  EXPECT_TRUE(rx("^.*$", ""));
}
TEST(Rx, ClassesAndSets) {
  EXPECT_TRUE(rx("^\\d+$", "12345"));
  EXPECT_FALSE(rx("^\\d+$", "12a45"));
  EXPECT_TRUE(rx("^\\w+\\s\\w+$", "hello world"));
  EXPECT_TRUE(rx("^[a-c]+$", "abcCBA"));
  EXPECT_FALSE(rx("^[a-c]+$", "abcd"));
  EXPECT_TRUE(rx("^[^,]+$", "no comma"));
  EXPECT_FALSE(rx("^[^,]+$", "a,b"));
  EXPECT_TRUE(rx("^[\\d.]+$", "10.5"));
  EXPECT_TRUE(rx("^[a\\-z]+$", "a-z"));
  EXPECT_FALSE(rx("^[a\\-z]+$", "b"));
  EXPECT_TRUE(rx("^what\\?$", "what?"));
  EXPECT_TRUE(rx("^\\D\\W\\S$", "a b"));
  EXPECT_FALSE(rx("^\\D\\W\\S$", "1 b"));
  EXPECT_FALSE(rx("^\\D\\W\\S$", "abc"));
  EXPECT_TRUE(rx("^\\D\\W\\S$", "a-c"));
}
TEST(Rx, WordBoundary) {
  EXPECT_TRUE(rx("\\bhi\\b", "oh hi there"));
  EXPECT_FALSE(rx("\\bhi\\b", "this"));
  EXPECT_TRUE(rx("\\bhi\\b", "hi"));
}
TEST(Rx, GroupsAlternativesAndCaptures) {
  EXPECT_TRUE(rx("^(hi|hello|hey)$", "HELLO"));
  EXPECT_FALSE(rx("^(hi|hello|hey)$", "howdy"));
  EXPECT_TRUE(rx("^cat|dog$", "catalog"));
  EXPECT_TRUE(rx("^cat|dog$", "hotdog"));
  EXPECT_EQ(grp("^weather (?:in |for )?(.+)$", "weather in North Bay", 1), "North Bay");
  EXPECT_EQ(grp("^weather (?:in |for )?(.+)$", "Weather Barrie", 1), "Barrie");
  EXPECT_EQ(grp("^(\\w+) (\\w+)$", "roll 2d6", 2), "2d6");
  EXPECT_EQ(grp("^(\\w+) (\\w+)$", "roll 2d6", 0), "roll 2d6");
  EXPECT_EQ(grp("^a(b)?c$", "ac", 1), "<none>");
  EXPECT_EQ(grp("^a(b)?c$", "abc", 1), "b");
  EXPECT_EQ(grp("^(ab)+$", "ababab", 1), "ab");
  EXPECT_EQ(grp("^(a|b)*c$", "abbac", 1), "a");
  EXPECT_EQ(grp("x(\\d+)", "aax42b", 0), "x42");
  EXPECT_EQ(grp("^((a)(b))$", "ab", 3), "b");
  EXPECT_TRUE(rx("^(?:ha)+!?$", "hahaha!"));
  EXPECT_FALSE(rx("^(?:ha)+!?$", "hah"));
  EXPECT_TRUE(rx("^(a*)*$", "aaa"));    // an empty round does not loop for ever
  EXPECT_TRUE(rx("^(a?)*b$", "b"));
}
TEST(Rx, NeverReadsPastTheEndOfTheText) {
  // An optional "any character" at the very end must not take the terminator and run on.
  char buf[16] = "x\0SECRETKEY";
  BotRxMatch m;
  ASSERT_TRUE(botRxSearch("x.?.*", buf, m));
  EXPECT_EQ(m.e[0], buf + 1);
  ASSERT_TRUE(botRxSearch("^x.?(.?.?.?)", buf, m));
  EXPECT_EQ(m.e[0], buf + 1); EXPECT_EQ(m.e[1], buf + 1);
  char hi[16] = "hi\0SECRET";
  ASSERT_TRUE(botRxSearch("^hi[^!]?\\D*$", hi, m));
  EXPECT_EQ(m.e[0], hi + 2);
  ASSERT_TRUE(botRxSearch("a\\S?\\W?", "a", m));
  EXPECT_EQ(m.e[0] - m.s[0], 1);
  BotMatchRule r; memset(&r, 0, sizeof r); r.kind = BMK_REGEX; r.enabled = true;
  strcpy(r.text, "^hi.?(.?.?.?.?.?.?.?.?.?)"); strcpy(r.run, "echo {1}");
  char out[64];
  EXPECT_EQ(botMatchFind(&r, 1, hi, out, sizeof out), 0);
  EXPECT_STREQ(out, "echo ");
}
TEST(Rx, UnknownEscapesAreRefused) {
  for (const char* p : {"\\B", "\\n", "\\t", "\\x41", "\\u0041", "\\0", "a\\z", "[\\b]", "[\\n]", "[a-\\w]", "[\\d-z]"})
    EXPECT_NE(botRxCheck(p), nullptr) << p;
  for (const char* p : {"\\.", "\\?", "\\\\", "[\\]\\-]", "[\\d\\s-]", "[-\\w]", "\\bhi\\b", "[a\\-z]"})
    EXPECT_EQ(botRxCheck(p), nullptr) << p;
}
TEST(Rx, EachStartingPointHasItsOwnStepBudget) {
  std::string t = std::string(140, 'a') + " weather";
  EXPECT_TRUE(rx("[a-z ]*weather", t.c_str()));
  EXPECT_TRUE(rx("weather$", t.c_str()));
}
TEST(Rx, CheckRefusesWhatIsNotSupported) {
  EXPECT_NE(botRxCheck(""), nullptr);
  EXPECT_NE(botRxCheck(nullptr), nullptr);
  EXPECT_NE(botRxCheck("a{2}"), nullptr);
  EXPECT_NE(botRxCheck("a*?"), nullptr);
  EXPECT_NE(botRxCheck("*a"), nullptr);
  EXPECT_NE(botRxCheck("a|*"), nullptr);
  EXPECT_NE(botRxCheck("(a"), nullptr);
  EXPECT_NE(botRxCheck("a)"), nullptr);
  EXPECT_NE(botRxCheck("[a"), nullptr);
  EXPECT_NE(botRxCheck("[]"), nullptr);
  EXPECT_NE(botRxCheck("[z-a]"), nullptr);
  EXPECT_NE(botRxCheck("a]"), nullptr);
  EXPECT_NE(botRxCheck("a\\"), nullptr);
  EXPECT_NE(botRxCheck("(?=a)"), nullptr);
  EXPECT_NE(botRxCheck("(a)\\1"), nullptr);
  EXPECT_NE(botRxCheck("^*"), nullptr);
  EXPECT_NE(botRxCheck("(((((a)))))"), nullptr);
  EXPECT_NE(botRxCheck("(a)(a)(a)(a)(a)(a)(a)(a)(a)(a)"), nullptr);
  EXPECT_EQ(botRxCheck("(a)(a)(a)(a)(a)(a)(a)(a)(a)"), nullptr);
  EXPECT_NE(botRxCheck(std::string(49, 'a').c_str()), nullptr);
  EXPECT_EQ(botRxCheck(std::string(48, 'a').c_str()), nullptr);
  EXPECT_EQ(botRxCheck("^(hi|hello)\\b[^!]*\\?$"), nullptr);
  EXPECT_EQ(botRxCheck("\\{\\}\\]\\["), nullptr);
}
TEST(Rx, CostlySearchesGiveUpInsteadOfHanging) {
  // Classic blow-ups: bounded by the step and depth limits, they end as "no match".
  std::string a(120, 'a');
  BotRxMatch m;
  EXPECT_FALSE(botRxSearch("^(a+)+$", (a + "!").c_str(), m));
  EXPECT_FALSE(botRxSearch("^(a|a)*$", (a + "!").c_str(), m));
  EXPECT_FALSE(botRxSearch("(.*)*x", a.c_str(), m));
  EXPECT_FALSE(botRxSearch("^(a|b)+$", std::string(120, 'b').c_str(), m));   // too deep: given up, never a crash
  EXPECT_EQ(m.s[0], nullptr);
  // An ordinary pattern over the longest message is well inside the limits.
  EXPECT_TRUE(rx("^a+$", a.c_str()));
  EXPECT_TRUE(rx("weather (in )?(.+)$", (std::string(90, 'x') + " weather in Barrie").c_str()));
}
TEST(Rx, EveryBytePatternIsSafeAfterCheck) {
  // Any two-to-four byte pattern from a hostile alphabet either fails the check or runs to an end.
  const char alpha[] = "a(|)[]^-\\*+?.$b";
  const size_t A = sizeof(alpha) - 1;
  BotRxMatch m;
  int ran = 0;
  for (size_t i = 0; i < A * A * A * A; i++) {
    char p[5] = { alpha[i % A], alpha[(i / A) % A], alpha[(i / A / A) % A], alpha[(i / A / A / A) % A], 0 };
    if (botRxCheck(p)) continue;
    botRxSearch(p, "ab-a]b[ba^", m);
    ran++;
  }
  EXPECT_GT(ran, 1000);
}

TEST(MatchRule, ExactIgnoresCaseAndOuterSpaces) {
  EXPECT_TRUE(botMatchExact("good morning", "  Good Morning "));
  EXPECT_TRUE(botMatchExact(" !gm ", "!GM"));
  EXPECT_FALSE(botMatchExact("good morning", "good  morning"));
  EXPECT_FALSE(botMatchExact("good morning", "good morning all"));
  EXPECT_FALSE(botMatchExact("", ""));
}
TEST(MatchRule, RunAndTextRules) {
  EXPECT_EQ(botMatchRunError("wx"), nullptr);
  EXPECT_EQ(botMatchRunError("wx {1}"), nullptr);
  EXPECT_EQ(botMatchRunError("zork look at {0}"), nullptr);
  EXPECT_NE(botMatchRunError(""), nullptr);
  EXPECT_NE(botMatchRunError("!wx"), nullptr);
  EXPECT_NE(botMatchRunError("{1}"), nullptr);
  EXPECT_NE(botMatchRunError("wx{1}"), nullptr);
  EXPECT_NE(botMatchRunError(" wx"), nullptr);
  EXPECT_NE(botMatchRunError("abcdefghijklmnopq"), nullptr);
  EXPECT_EQ(botMatchRunError("abcdefghijklmnop"), nullptr);
  EXPECT_EQ(botMatchTextError(BMK_EXACT, "hello"), nullptr);
  EXPECT_NE(botMatchTextError(BMK_EXACT, "   "), nullptr);
  EXPECT_NE(botMatchTextError(BMK_EXACT, " @[Dee] hi"), nullptr);
  EXPECT_NE(botMatchTextError(BMK_REGEX, "a{2}"), nullptr);
  EXPECT_EQ(botMatchTextError(BMK_REGEX, "^a+$"), nullptr);
}

static BotMatchRule R(uint8_t kind, const char* text, const char* run, bool on = true) {
  BotMatchRule r;
  memset(&r, 0, sizeof(r));
  r.kind = kind; r.enabled = on;
  strcpy(r.text, text); strcpy(r.run, run);
  return r;
}
TEST(MatchRule, FindFirstEnabledRuleAndFillsPlaceholders) {
  BotMatchRule rules[4] = { R(BMK_EXACT, "good morning", "gm", false), R(BMK_EXACT, "good morning", "hello"),
                            R(BMK_REGEX, "^weather (?:in )?(.+)$", "wx {1}"), R(BMK_REGEX, "morning", "late {0}!") };
  char out[64];
  EXPECT_EQ(botMatchFind(rules, 4, " Good morning", out, sizeof(out)), 1);
  EXPECT_STREQ(out, "hello");
  EXPECT_EQ(botMatchFind(rules, 4, "weather in Barrie", out, sizeof(out)), 2);
  EXPECT_STREQ(out, "wx Barrie");
  EXPECT_EQ(botMatchFind(rules, 4, "what a Morning", out, sizeof(out)), 3);
  EXPECT_STREQ(out, "late Morning!");
  EXPECT_EQ(botMatchFind(rules, 4, "nothing here", out, sizeof(out)), -1);
  EXPECT_EQ(botMatchFind(rules, 4, "@[Dee] good morning", out, sizeof(out)), -1);   // a bot's reply
  EXPECT_EQ(botMatchFind(rules, 4, "  @[Dee] weather in x", out, sizeof(out)), -1);
  EXPECT_EQ(botMatchFind(rules, 4, "", out, sizeof(out)), -1);
  EXPECT_EQ(botMatchFind(rules, 0, "good morning", out, sizeof(out)), -1);
  EXPECT_EQ(botMatchFind(nullptr, 4, "good morning", out, sizeof(out)), -1);
}
TEST(MatchRule, ExpansionIsCutToTheBuffer) {
  BotMatchRule rules[1] = { R(BMK_REGEX, "^say (.+)$", "echo {1} {1} {9}{x}") };
  char out[16];
  EXPECT_EQ(botMatchFind(rules, 1, "say abcdefgh", out, sizeof(out)), 0);
  EXPECT_STREQ(out, "echo abcdefgh a");
  char big[64];
  EXPECT_EQ(botMatchFind(rules, 1, "say hi", big, sizeof(big)), 0);
  EXPECT_STREQ(big, "echo hi hi {x}");
  BotMatchRule ex[1] = { R(BMK_EXACT, "ping me", "ping {0}") };
  EXPECT_EQ(botMatchFind(ex, 1, "  Ping Me ", big, sizeof(big)), 0);
  EXPECT_STREQ(big, "ping Ping Me");
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
