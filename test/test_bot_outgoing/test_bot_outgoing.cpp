// Host tests for BotOutgoing.h: the reply mention, sanitising, the reply budget and the
// deferred-reply ring. The loop test feeds every reply format back through botParse.
#include <gtest/gtest.h>
#include <cstring>
#include <string>
#include <vector>
#include "helpers/bot/BotOutgoing.h"
#include "helpers/bot/BotCommandParser.h"
#include "helpers/bot/BotReplies.h"
#include "helpers/bot/BotTemplate.h"
#include "helpers/bot/BotHeard.h"

static const char* EMOJI = "\xF0\x9F\xA7\xAD";   // 4-byte UTF-8

// ---- budget ----
TEST(Budget, NodeNameTakesNameAndSeparator) {
  EXPECT_EQ(botReplyBudget("MeshBot"), 127u - 7u - 2u);
  EXPECT_EQ(botReplyBudget(""), 125u);
  EXPECT_EQ(botReplyBudget(nullptr), 125u);
  EXPECT_EQ(botReplyBudget(std::string(130, 'x').c_str()), 0u);
}

// ---- sanitising ----
TEST(Strip, RemovesControlBytes) {
  char b[32];
  EXPECT_EQ(botStripControls(b, sizeof(b), "De\ne\t\x7F!\x01"), 4u); EXPECT_STREQ(b, "Dee!");
  botStripControls(b, sizeof(b), nullptr); EXPECT_STREQ(b, "");
  std::string s = std::string("ok ") + EMOJI; botStripControls(b, sizeof(b), s.c_str()); EXPECT_EQ(std::string(b), s);
}
TEST(Strip, CutsOnCharacterBoundary) {
  char b[6];
  std::string s = std::string("ab") + EMOJI;   // 6 bytes, does not fit 5
  EXPECT_EQ(botStripControls(b, sizeof(b), s.c_str()), 2u); EXPECT_STREQ(b, "ab");
  char z[1] = {'x'}; EXPECT_EQ(botStripControls(z, 1, "abc"), 0u); EXPECT_EQ(z[0], '\0');
  EXPECT_EQ(botStripControls(nullptr, 0, "abc"), 0u);
}

// ---- mention ----
TEST(Mention, PrependedWhenMissing) {
  char b[128];
  EXPECT_EQ(botFinishReply(b, sizeof(b), "Heard 12 in 24h.", "Dee", 100, BRT_BUILTIN), 23u);
  EXPECT_STREQ(b, "@[Dee] Heard 12 in 24h.");
}
TEST(Mention, KeptWhenPresent) {
  char b[128];
  botFinishReply(b, sizeof(b), "@[Dee] 3 hops, SNR 7.5", "Dee", 100, BRT_BUILTIN); EXPECT_STREQ(b, "@[Dee] 3 hops, SNR 7.5");
  botFinishReply(b, sizeof(b), "@[Other] hi", "Dee", 100, BRT_BUILTIN); EXPECT_STREQ(b, "@[Other] hi");
}
TEST(Mention, EmptyReplyStaysSilent) {
  char b[16] = "junk";
  EXPECT_EQ(botFinishReply(b, sizeof(b), "", "Dee", 100, BRT_BUILTIN), 0u); EXPECT_STREQ(b, "");
  EXPECT_EQ(botFinishReply(b, sizeof(b), nullptr, "Dee", 100, BRT_BUILTIN), 0u); EXPECT_STREQ(b, "");
}
TEST(Mention, WholeResultClamped) {
  char b[128];
  std::string r(200, 'x');
  size_t n = botFinishReply(b, sizeof(b), r.c_str(), "Dee", 40, BRT_BUILTIN);
  EXPECT_EQ(n, 40u); EXPECT_EQ(strncmp(b, "@[Dee] xxx", 10), 0);
  EXPECT_EQ(std::string(b).substr(37), "\xE2\x80\xA6");   // ellipsis at the end
  std::string m = "@[Dee] " + r;
  n = botFinishReply(b, sizeof(b), m.c_str(), "Dee", 40, BRT_BUILTIN); EXPECT_EQ(n, 40u); EXPECT_EQ(strncmp(b, "@[Dee] ", 7), 0);
}
TEST(Mention, LongNameIsCutNotTheReply) {
  char b[128];
  std::string name(60, 'N');
  size_t n = botFinishReply(b, sizeof(b), "Heard 3 in 24h.", name.c_str(), 40, BRT_BUILTIN);
  EXPECT_LE(n, 40u);
  std::string s(b);
  EXPECT_EQ(s.substr(0, 2), "@[");
  EXPECT_NE(s.find("] Heard 3 in 24h."), std::string::npos);   // the reply survives whole
  EXPECT_EQ(botMentionNameLen(name.c_str(), 40), 40u - 4u - BOT_MENTION_MIN_BODY);
}
TEST(Mention, NameCutOnCharacterBoundary) {
  std::string name;
  for (int i = 0; i < 10; i++) name += EMOJI;   // 40 bytes
  size_t nl = botMentionNameLen(name.c_str(), 4 + BOT_MENTION_MIN_BODY + 6);
  EXPECT_EQ(nl, 4u);   // 6 bytes room, one whole emoji
  char b[128];
  botFinishReply(b, sizeof(b), "hi", name.c_str(), 4 + BOT_MENTION_MIN_BODY + 6, BRT_BUILTIN);
  EXPECT_EQ(std::string(b), "@[" + std::string(EMOJI) + "] hi");
}
TEST(Mention, BodyBudgetMatchesFinish) {
  // A formatter given the body budget is never cut once the mention is added.
  for (size_t mb = 10; mb <= 125; mb++) {
    const char* names[] = {"Dee", "A much longer sender name here", ""};
    for (const char* nm : names) {
      size_t body = botMentionBodyBudget(nm, mb);
      std::string r(body, 'b');
      char out[128];
      size_t n = botFinishReply(out, sizeof(out), r.c_str(), nm, mb, BRT_BUILTIN);
      EXPECT_LE(n, mb);
      if (body) EXPECT_EQ(std::string(out).substr(n - body), r) << mb << " " << nm;   // not cut
    }
  }
}
TEST(Mention, TinyLimits) {
  char b[16];
  for (size_t mb = 0; mb < 6; mb++) {
    size_t n = botFinishReply(b, sizeof(b), "hello", "Dee", mb, BRT_BUILTIN);
    EXPECT_LE(n, mb);
    if (n) EXPECT_EQ(strncmp(b, "@[", 2), 0);
  }
  EXPECT_EQ(botFinishReply(b, sizeof(b), "@[Dee] x", "Dee", 1, BRT_BUILTIN), 0u);   // "@" alone would not be a mention
  EXPECT_EQ(botFinishReply(b, 0, "hello", "Dee", 100, BRT_BUILTIN), 0u);
}

// ---- foreign text (broker replies, custom templates) always carries the asker's mention ----
TEST(Mention, ForeignTextNeverStandsInForTheMention) {
  char b[128];
  // A broker handler that echoes a stranger's "!say @[Alice] your node is being reported".
  EXPECT_EQ(botFinishReply(b, sizeof(b), "@[Alice] your node is being reported", "Mallory", 100, BRT_FOREIGN),
            strlen("@[Mallory] @[Alice] your node is being reported"));
  EXPECT_STREQ(b, "@[Mallory] @[Alice] your node is being reported");
  // A custom template "{args}" rendered with the same text.
  BotVars v = {"Mallory", "@[Alice] your node is being reported", "", "#bot", "", "", "", 0, 0.0f, 0};
  char r[128];
  botRender(r, sizeof(r), "{args}", v);
  botFinishReply(b, sizeof(b), r, "Mallory", 100, BRT_FOREIGN);
  EXPECT_STREQ(b, "@[Mallory] @[Alice] your node is being reported");
  // The built-in pass-through is unchanged (pinned above): only built-in text may lead with "@[".
  botFinishReply(b, sizeof(b), "@[Alice] hi", "Mallory", 100, BRT_BUILTIN); EXPECT_STREQ(b, "@[Alice] hi");
  // Text that does not begin "@[" is finished the same way from either source.
  char c[128];
  botFinishReply(b, sizeof(b), "tide 14:02", "Dee", 100, BRT_FOREIGN);
  botFinishReply(c, sizeof(c), "tide 14:02", "Dee", 100, BRT_BUILTIN);
  EXPECT_STREQ(b, c);
  EXPECT_STREQ(b, "@[Dee] tide 14:02");
}
TEST(Mention, ForeignTextIsClampedWithTheMention) {
  char b[128];
  std::string r = "@[Alice] " + std::string(200, 'x');   // a broker text can be 200 bytes
  for (size_t mb = 0; mb <= 125; mb++) {
    size_t n = botFinishReply(b, sizeof(b), r.c_str(), "Dee", mb, BRT_FOREIGN);
    EXPECT_LE(n, mb);
    EXPECT_EQ(n, strlen(b));
    if (mb < 5) { EXPECT_EQ(n, 0u) << mb; continue; }
    std::string want = "@[" + std::string("Dee").substr(0, botMentionNameLen("Dee", mb)) + "] ";   // cut when tiny
    EXPECT_EQ(std::string(b).rfind(want, 0), 0u) << mb << " " << b;   // the asker's mention first, always
    EXPECT_GT(n, want.size()) << mb;                                    // and some of the reply
  }
  size_t n = botFinishReply(b, sizeof(b), r.c_str(), "Dee", 40, BRT_FOREIGN);
  EXPECT_EQ(n, 40u);
  EXPECT_EQ(std::string(b).substr(0, 16), "@[Dee] @[Alice] ");
  EXPECT_EQ(std::string(b).substr(37), "\xE2\x80\xA6");   // cut with the ellipsis
  // A long sender name is cut, not the room for the reply.
  std::string name(60, 'N');
  n = botFinishReply(b, sizeof(b), r.c_str(), name.c_str(), 40, BRT_FOREIGN);
  EXPECT_LE(n, 40u);
  EXPECT_EQ(std::string(b).substr(0, 2 + botMentionNameLen(name.c_str(), 40) + 2),
            "@[" + name.substr(0, botMentionNameLen(name.c_str(), 40)) + "] ");
  // The body budget a broker request is given still fits whole beside the forced mention.
  size_t body = botMentionBodyBudget("Dee", 100);
  std::string exact = "@[" + std::string(body - 2, 'y');
  n = botFinishReply(b, sizeof(b), exact.c_str(), "Dee", 100, BRT_FOREIGN);
  EXPECT_EQ(std::string(b), "@[Dee] " + exact);
}

// ---- no reply loop between two bots ----
static uint32_t fixedRnd(uint32_t m) { return m > 2 ? 2 : 0; }
static const char* look(const uint8_t*, uint8_t, void*) { return "Relay"; }

static void expectNoCommand(const std::string& built, const char* sender) {
  if (built.empty()) return;   // an empty reply is never sent
  char out[128];
  for (uint8_t src : {BRT_BUILTIN, BRT_FOREIGN}) {
    size_t n = botFinishReply(out, sizeof(out), built.c_str(), sender, 100, src);
    ASSERT_GT(n, 0u) << built;
    BotParsed p;
    EXPECT_FALSE(botParse(out, '!', true, p)) << out;     // as a direct message
    EXPECT_FALSE(botParse(out, '!', false, p)) << out;    // and as a channel body
    EXPECT_FALSE(botParse(out, '@', true, p)) << out;     // even with an unusual prefix
  }
}

TEST(NoLoop, EveryReplyFormatIsInert) {
  static const uint8_t path[] = {0xa5, 0x3e, 0x96, 0xbc, 0xad, 0xb8};
  BotMsgInfo m = {"Dee", 2, 3, path, 7.5f, -98, false};
  BotMsgInfo d = m; d.direct = true;
  char b[192];
  std::vector<std::string> all;
  botFmtPing(b, sizeof(b), m); all.push_back(b);
  botFmtPing(b, sizeof(b), d); all.push_back(b);
  botFmtTest(b, sizeof(b), m); all.push_back(b);
  botFmtTest(b, sizeof(b), d); all.push_back(b);
  botFmtPath(b, sizeof(b), m, look, nullptr); all.push_back(b);
  botFmtPath(b, sizeof(b), d, look, nullptr); all.push_back(b);
  botFmtRoll(b, sizeof(b), "Dee", "2d6", fixedRnd); all.push_back(b);
  botFmtRoll(b, sizeof(b), "Dee", "zz", fixedRnd); all.push_back(b);
  botFmt8Ball(b, sizeof(b), "Dee", fixedRnd); all.push_back(b);
  const char* names[] = {"ping", "test", "path", "help", "heard", "mesh", "dist", "sun", "roll", "8ball", "wx"};
  int pages = 0;
  botFmtHelp(b, sizeof(b), names, 11, 1, "example.org/bot", 60, &pages); all.push_back(b);
  botFmtHelp(b, sizeof(b), names, 11, 2, "", 30, &pages); all.push_back(b);
  BotHeardTable h;
  static const uint8_t k1[] = {1, 2, 3, 4}, k2[] = {5, 6, 7, 8};
  h.note(k1, "Relay", 100, 0, 6.5f); h.note(k2, "Far", 100, 3, 1.0f);
  h.format(b, sizeof(b), 110, 120); all.push_back(b);
  // The formats built in BotTask.cpp (mesh, dist, sun, help <cmd>), as printed there.
  all.push_back("Mesh: 19 pkts/hr, 7 nodes, avg 1.4 hops");
  all.push_back("@[Dee] 12.3 km NE of me");
  all.push_back("@[Dee] I don't know your location");
  all.push_back("@[Dee] I don't know my own location");
  all.push_back("Sun 07:48-16:42, moon Waxing Gibbous");
  all.push_back("No sunrise or sunset today, moon New");
  all.push_back("Clock not set yet");
  all.push_back("ping: hops and signal of your message");
  all.push_back("wx: no description");
  // Every built-in reply carries exactly one mention, at the start, once finished.
  for (const std::string& t : all) {
    char out[128];
    botFinishReply(out, sizeof(out), t.c_str(), "Dee", 100, BRT_BUILTIN);
    std::string o(out);
    EXPECT_EQ(o.rfind("@[Dee] ", 0), 0u) << o;
    EXPECT_EQ(o.find("@[", 1), std::string::npos) << o;
  }
  // Custom replies, including ones that start with a command word or the prefix.
  BotVars v = {"Dee", "", "a5,bc", "#bot", "12:00", "2026-11-20", "3h 2m", 2, 7.5f, -98};
  const char* customs[] = {"Hello {sender}", "ping", "!ping", "{args}", "  test it", "{hops} hops"};
  for (const char* t : customs) { botRender(b, sizeof(b), t, v); all.push_back(b); }
  v.args = "!ping"; botRender(b, sizeof(b), "{args}", v); all.push_back(b);

  // Without the mention, several of these would be commands to a peer bot.
  BotParsed p;
  EXPECT_TRUE(botParse("Sun 07:48-16:42, moon Full", '!', true, p));
  EXPECT_TRUE(botParse("Heard 12 in 24h.", '!', true, p));
  for (const std::string& s : all) {
    expectNoCommand(s, "Dee");
    expectNoCommand(s, "");
    expectNoCommand(s, std::string(60, 'Q').c_str());
  }
}

// ---- deferred-reply ring ----
TEST(Queue, FifoWithDelay) {
  BotReplyQueue q;
  EXPECT_TRUE(q.empty());
  BotQueuedReply* a = q.push(1000); ASSERT_NE(a, nullptr); strcpy(a->text, "a");
  BotQueuedReply* b = q.push(1100); ASSERT_NE(b, nullptr); strcpy(b->text, "b");
  EXPECT_EQ(q.size(), 2);
  EXPECT_EQ(q.due(999), nullptr);
  ASSERT_NE(q.due(1000), nullptr); EXPECT_STREQ(q.due(1000)->text, "a");
  q.pop();
  EXPECT_EQ(q.due(1099), nullptr);
  ASSERT_NE(q.due(5000), nullptr); EXPECT_STREQ(q.due(5000)->text, "b");
  q.pop();
  EXPECT_TRUE(q.empty()); EXPECT_EQ(q.due(5000), nullptr);
  q.pop();   // harmless when empty
  EXPECT_EQ(q.size(), 0);
}
TEST(Queue, FullRefusesAndSlotsAreZeroed) {
  BotReplyQueue q;
  for (int i = 0; i < BotReplyQueue::SLOTS; i++) { BotQueuedReply* r = q.push(0); ASSERT_NE(r, nullptr); strcpy(r->text, "x"); r->dest.kind = BD_DIRECT; }
  EXPECT_TRUE(q.full());
  EXPECT_EQ(q.push(0), nullptr);
  q.pop();
  BotQueuedReply* r = q.push(0); ASSERT_NE(r, nullptr); EXPECT_STREQ(r->text, ""); EXPECT_EQ(r->dest.kind, BD_CHANNEL);
}
TEST(Queue, DueSurvivesMillisWrap) {
  BotReplyQueue q;
  q.push(0xFFFFFF00u + BOT_REPLY_DELAY_MS);   // queued just before the wrap
  uint32_t due = 0xFFFFFF00u + BOT_REPLY_DELAY_MS;
  EXPECT_EQ(q.due(0xFFFFFF00u), nullptr);
  EXPECT_NE(q.due(due), nullptr);
  EXPECT_NE(q.due(due + 10), nullptr);
}

TEST(PingParts, SeveralOnlyWhenNothingElseIsQueued) {
  EXPECT_EQ(botPingPartsAllowed(0), BOT_PING_PARTS_MAX);
  for (int q = 1; q <= (int)BotReplyQueue::SLOTS; q++) EXPECT_EQ(botPingPartsAllowed(q), 1);
  EXPECT_LT(BOT_PING_PARTS_MAX, (int)BotReplyQueue::SLOTS);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

TEST(Mention, ForeignTextWithTheAskersOwnMentionIsNotDoubled) {
  char out[160];
  size_t n = botFinishReply(out, sizeof(out), "@[Dee] hello", "Dee", 120, BRT_FOREIGN);
  EXPECT_STREQ("@[Dee] hello", out);
  EXPECT_EQ(strlen(out), n);
  // someone else's mention still gets the asker's in front
  botFinishReply(out, sizeof(out), "@[Bob] hello", "Dee", 120, BRT_FOREIGN);
  EXPECT_STREQ("@[Dee] @[Bob] hello", out);
  // nothing after the asker's own mention: nothing to send
  EXPECT_EQ(0u, botFinishReply(out, sizeof(out), "@[Dee] ", "Dee", 120, BRT_FOREIGN));
  EXPECT_STREQ("", out);
}
