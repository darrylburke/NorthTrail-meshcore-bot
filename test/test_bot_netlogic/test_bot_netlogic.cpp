// Host tests for BotNetLogic.h: parked-command table, steps, cache keys, joining watched-node
// entries, {fetch} values, URL normalisation, UTC offset and the web try holder.
#include <gtest/gtest.h>
#include <cstring>
#include <string>
#include <ArduinoJson.h>
#include "helpers/bot/BotNetLogic.h"

// ---- BotParkTable ----

TEST(Park, TwoSlotsThenFull) {
  BotParkTable t;
  EXPECT_TRUE(t.hasFree());
  int a = t.park(100), b = t.park(200);
  EXPECT_GE(a, 0); EXPECT_GE(b, 0); EXPECT_NE(a, b);
  EXPECT_EQ(t.park(300), -1);
  EXPECT_FALSE(t.hasFree());
  EXPECT_EQ(t.used(), 2);
  t.release(a);
  EXPECT_EQ(t.used(), 1);
  EXPECT_EQ(t.park(400), a);
}

TEST(Park, OneInflightAtATimeInArrivalOrder) {
  BotParkTable t;
  int a = t.park(0), b = t.park(1);
  EXPECT_EQ(t.nextToSubmit(), a);
  t.submitted(a, 7, 2);
  EXPECT_EQ(t.state(a), BPS_INFLIGHT);
  EXPECT_EQ(t.nextToSubmit(), -1);   // the worker is taken
  EXPECT_EQ(t.resultFor(7), a);
  t.requeue(a);                       // a has a second step: it goes behind b
  EXPECT_EQ(t.nextToSubmit(), b);
  t.submitted(b, 8, 3);
  t.finish(b);
  EXPECT_EQ(t.nextToSubmit(), a);
}

TEST(Park, StaleResultIsNeverAttributed) {
  BotParkTable t;
  int a = t.park(0);
  t.submitted(a, 5, 0);
  EXPECT_EQ(t.stepExpired(14999), -1);
  EXPECT_EQ(t.stepExpired(15000), a);
  t.finish(a);                        // expired: answered as a failure
  EXPECT_EQ(t.resultFor(5), -1);
  t.release(a);
  int b = t.park(20000);              // the same slot, a new command
  EXPECT_EQ(b, a);
  EXPECT_EQ(t.resultFor(5), -1);      // the late result of the old request matches nothing
  t.submitted(b, 6, 20000);
  EXPECT_EQ(t.resultFor(5), -1);
  EXPECT_EQ(t.resultFor(6), b);
  EXPECT_EQ(t.resultFor(0), -1);
}

// The per-step limit runs from the submission of each step, so a slow multi-step command is not
// cut short by the time its earlier steps took.
TEST(Park, FourStepsOfFourSecondsAllComplete) {
  BotParkTable t;
  int a = t.park(1000);
  uint32_t now = 1000;
  for (uint32_t step = 0; step < 4; step++) {
    ASSERT_EQ(t.nextToSubmit(), a);
    t.submitted(a, 10 + step, now);
    now += 4000;                      // the step takes 4 s
    EXPECT_EQ(t.stepExpired(now), -1) << step;
    EXPECT_EQ(t.ceilingExpired(now), -1) << step;
    ASSERT_EQ(t.resultFor(10 + step), a);
    if (step < 3) t.requeue(a); else t.finish(a);
  }
  EXPECT_EQ(t.state(a), BPS_DONE);
  EXPECT_EQ(now, 17000u);             // 16 s after parking: past the old 15 s limit
}

TEST(Park, TwoMultiStepCommandsAlternateAndBothComplete) {
  BotParkTable t;
  int a = t.park(0), b = t.park(10);
  int steps_left[BOT_PARK_SLOTS] = {0};
  steps_left[a] = 4; steps_left[b] = 4;
  uint32_t now = 10, seq = 1;
  int order[8], n = 0;
  while (n < 8) {
    int i = t.nextToSubmit();
    ASSERT_GE(i, 0);
    order[n++] = i;
    t.submitted(i, seq, now);
    now += 4000;
    ASSERT_EQ(t.stepExpired(now), -1);
    ASSERT_EQ(t.ceilingExpired(now), -1);   // 32 s in all: under the 45 s ceiling
    ASSERT_EQ(t.resultFor(seq), i);
    seq++;
    if (--steps_left[i] > 0) t.requeue(i); else t.finish(i);
  }
  for (int k = 0; k < 8; k++) EXPECT_EQ(order[k], k % 2 == 0 ? a : b) << k;   // they take turns
  EXPECT_EQ(t.state(a), BPS_DONE);
  EXPECT_EQ(t.state(b), BPS_DONE);
}

TEST(Park, ASlowStepExpiresOnlyThatStep) {
  BotParkTable t;
  int a = t.park(0);
  t.submitted(a, 1, 0);
  t.requeue(a);                       // step 1 answered at once
  t.submitted(a, 2, 1000);            // step 2 takes 16 s
  EXPECT_EQ(t.stepExpired(15999), -1);
  EXPECT_EQ(t.stepExpired(16000), a);
  EXPECT_EQ(t.ceilingExpired(16000), -1);
  t.requeue(a);                       // the step failed; the command goes on with the next one
  EXPECT_EQ(t.state(a), BPS_WAITING);
  EXPECT_EQ(t.stepExpired(40000), -1);    // waiting for its turn never expires on the step limit
  EXPECT_EQ(t.resultFor(2), -1);          // the slow step's late result is no longer its own
  t.submitted(a, 3, 17000);
  EXPECT_EQ(t.resultFor(3), a);
  t.finish(a);
  EXPECT_EQ(t.stepExpired(99999), -1);    // DONE never expires
  EXPECT_EQ(t.ceilingExpired(99999), -1);
}

TEST(Park, CeilingFreesASlotStuckWaiting) {
  BotParkTable t;
  int a = t.park(0), b = t.park(0);
  t.submitted(a, 1, 0);               // a holds the worker; b waits behind it
  EXPECT_EQ(t.stepExpired(30000), a);
  t.finish(a);
  t.release(a);
  EXPECT_EQ(t.state(b), BPS_WAITING);
  EXPECT_EQ(t.stepExpired(44999), -1);
  EXPECT_EQ(t.ceilingExpired(44999), -1);
  EXPECT_EQ(t.ceilingExpired(45000), b);
  t.finish(b);
  t.release(b);
  int c = t.park(0);
  t.submitted(c, 2, 44000);           // in flight, its own step only 1 s old
  EXPECT_EQ(t.stepExpired(45000), -1);
  EXPECT_EQ(t.ceilingExpired(45000), c);   // the ceiling ends it whatever its state
}

TEST(Park, TimersAreOrdered) {
  EXPECT_EQ(BOT_PARK_STEP_MS, 15000u);
  EXPECT_EQ(BOT_PARK_CEILING_MS, 45000u);
  EXPECT_EQ(BOT_TRY_TIMEOUT_MS, 50000u);
  EXPECT_GT(BOT_PARK_CEILING_MS, BOT_PARK_STEP_MS);
  EXPECT_GT(BOT_TRY_TIMEOUT_MS, BOT_PARK_CEILING_MS);   // a try outlives any parked command
}

TEST(Park, BothLimitsSurviveWrap) {
  BotParkTable t;
  int a = t.park(0xFFFFF000u);
  t.submitted(a, 1, 0xFFFFF000u);
  EXPECT_EQ(t.stepExpired(0x00001000u), -1);              // 8 s later, past the wrap
  EXPECT_EQ(t.stepExpired(0xFFFFF000u + 15000u), a);
  t.requeue(a);
  t.submitted(a, 2, 0x00002000u);
  EXPECT_EQ(t.ceilingExpired(0xFFFFF000u + 44999u), -1);
  EXPECT_EQ(t.stepExpired(0x00002000u + 14999u), -1);
  EXPECT_EQ(t.ceilingExpired(0xFFFFF000u + 45000u), a);
  EXPECT_EQ(t.stepExpired(0x00002000u + 15000u), a);
}

// A settings change starts a new generation: a command parked before it must not answer from,
// or write into the cache with, the old settings.
TEST(Park, OlderGenerationIsStale) {
  BotParkTable t;
  int a = t.park(0);
  EXPECT_FALSE(t.stale(a));
  t.newGeneration();
  EXPECT_TRUE(t.stale(a));
  int b = t.park(1);
  EXPECT_FALSE(t.stale(b));
  t.release(a);
  int c = t.park(2);                  // a reused slot takes the current generation
  EXPECT_EQ(c, a);
  EXPECT_FALSE(t.stale(c));
  EXPECT_FALSE(t.stale(-1));
  for (int k = 0; k < 3; k++) t.newGeneration();
  EXPECT_TRUE(t.stale(b));
  EXPECT_TRUE(t.stale(c));
}

TEST(Park, DoneInArrivalOrderAndGuards) {
  BotParkTable t;
  int a = t.park(0), b = t.park(1);
  t.finish(b); t.finish(a);
  EXPECT_EQ(t.nextDone(), a);
  t.release(a);
  EXPECT_EQ(t.nextDone(), b);
  t.submitted(b, 3, 0);               // not WAITING: ignored
  EXPECT_EQ(t.state(b), BPS_DONE);
  t.requeue(b);                       // DONE is not requeued
  EXPECT_EQ(t.state(b), BPS_DONE);
  t.submitted(-1, 3, 0); t.finish(9); t.release(-4);   // out of range: no effect
  EXPECT_EQ(t.state(7), BPS_FREE);
  int c = t.park(2);
  t.submitted(c, 0, 0);               // 0 is not a sequence number
  EXPECT_EQ(t.state(c), BPS_WAITING);
}

// ---- the same command twice ----

static BotReplyDest D(uint8_t kind, const char* ch, uint8_t key0, uint32_t try_id) {
  BotReplyDest d; memset(&d, 0, sizeof d);
  d.kind = kind; d.key[0] = key0; d.try_id = try_id;
  if (ch) strncpy(d.channel, ch, sizeof d.channel - 1);
  return d;
}

TEST(Same, DestinationsCompareByKind) {
  EXPECT_TRUE(botDestSame(D(BD_CHANNEL, "#bot", 0, 0), D(BD_CHANNEL, "#bot", 9, 3)));   // only the channel counts
  EXPECT_FALSE(botDestSame(D(BD_CHANNEL, "#bot", 0, 0), D(BD_CHANNEL, "#other", 0, 0)));
  EXPECT_TRUE(botDestSame(D(BD_DIRECT, "x", 7, 0), D(BD_DIRECT, "y", 7, 1)));          // only the key counts
  EXPECT_FALSE(botDestSame(D(BD_DIRECT, nullptr, 7, 0), D(BD_DIRECT, nullptr, 8, 0)));
  EXPECT_TRUE(botDestSame(D(BD_TRY, nullptr, 0, 5), D(BD_TRY, nullptr, 0, 5)));
  EXPECT_FALSE(botDestSame(D(BD_TRY, nullptr, 0, 5), D(BD_TRY, nullptr, 0, 6)));
  EXPECT_FALSE(botDestSame(D(BD_CHANNEL, "#bot", 0, 0), D(BD_DIRECT, "#bot", 0, 0)));
}

TEST(Same, CommandArgsAndGeneration) {
  BotReplyDest c = D(BD_CHANNEL, "#bot", 0, 0);
  EXPECT_TRUE(botParkSameCommand(BNK_WX, "wx", "Kingston", c, 3, BNK_WX, "wx", "kingston", c, 3));   // args ignore ASCII case
  EXPECT_FALSE(botParkSameCommand(BNK_WX, "wx", "Kingston", c, 3, BNK_WX, "wx", "Ottawa", c, 3));
  EXPECT_FALSE(botParkSameCommand(BNK_WX, "wx", "", c, 3, BNK_AURORA, "wx", "", c, 3));
  EXPECT_FALSE(botParkSameCommand(BNK_CUSTOM, "beer", "", c, 3, BNK_CUSTOM, "tide", "", c, 3));
  EXPECT_FALSE(botParkSameCommand(BNK_WX, "wx", "", c, 3, BNK_WX, "wx", "", D(BD_CHANNEL, "#x", 0, 0), 3));
  EXPECT_FALSE(botParkSameCommand(BNK_WX, "wx", "", c, 3, BNK_WX, "wx", "", c, 4));   // a new generation is not a repeat
  EXPECT_TRUE(botParkSameCommand(BNK_NODES, "nodes", nullptr, c, 0, BNK_NODES, "nodes", "", c, 0));
}

// ---- watched nodes: room for the next entry ----

TEST(NodesFit, StopsWhenEvenTheShortestEntryCannotFit) {
  // The shortest entry is "<label>: online"; after the first it costs " | " too. The first entry
  // is always asked: botJoinAppend cuts it to fit so the reply has some text.
  EXPECT_TRUE(botWatchEntryMayFit(0, 9, false, "A"));     // "A: online" = 9
  EXPECT_TRUE(botWatchEntryMayFit(0, 8, false, "A"));     // the first entry is always asked (cut to fit)
  EXPECT_TRUE(botWatchEntryMayFit(9, 21, false, "B"));    // 9 + 3 + 9
  EXPECT_FALSE(botWatchEntryMayFit(9, 20, false, "B"));
  EXPECT_FALSE(botWatchEntryMayFit(0, 100, true, "A"));   // closed: an earlier entry was dropped
  EXPECT_TRUE(botWatchEntryMayFit(0, 100, false, nullptr));
}

// ---- steps ----

TEST(Steps, WxWithAndWithoutPlace) {
  EXPECT_EQ(botNetFirstStep(BNK_WX, false), 1);
  EXPECT_EQ(botNetFirstStep(BNK_WX, true), 0);
  EXPECT_EQ(botNetNextStep(BNK_WX, 0, true, 0), 1);
  EXPECT_EQ(botNetNextStep(BNK_WX, 0, false, 0), -1);
  EXPECT_EQ(botNetNextStep(BNK_WX, 1, true, 0), -1);
}

TEST(Steps, NodesOnePerEntryUpToFour) {
  EXPECT_EQ(botNetFirstStep(BNK_NODES, false), 0);
  EXPECT_EQ(botNetNextStep(BNK_NODES, 0, false, 3), 1);   // a failed entry does not stop the rest
  EXPECT_EQ(botNetNextStep(BNK_NODES, 1, true, 3), 2);
  EXPECT_EQ(botNetNextStep(BNK_NODES, 2, true, 3), -1);
  EXPECT_EQ(botNetNextStep(BNK_NODES, 3, true, 9), -1);   // at most 4
  EXPECT_EQ(botNetNextStep(BNK_NODES, 0, true, 0), -1);
  EXPECT_EQ(botNetNextStep(BNK_NODES, 0, true, -2), -1);
}

TEST(Steps, OthersOneStep) {
  for (uint8_t k : {BNK_AURORA, BNK_ALERTS, BNK_CUSTOM}) {
    EXPECT_EQ(botNetFirstStep(k, true), 0);
    EXPECT_EQ(botNetNextStep(k, 0, true, 4), -1);
  }
}

// ---- cache keys ----

TEST(CacheKey, LowerCasedArgs) {
  char k[48];
  ASSERT_TRUE(botNetCacheKey(k, sizeof k, 47, "wx", "Toronto ON"));
  EXPECT_STREQ(k, "wx|toronto on");
  ASSERT_TRUE(botNetCacheKey(k, sizeof k, 47, "aurora", ""));
  EXPECT_STREQ(k, "aurora|");
  ASSERT_TRUE(botNetCacheKey(k, sizeof k, 47, "+beer", nullptr));
  EXPECT_STREQ(k, "+beer|");
  ASSERT_TRUE(botNetCacheKey(k, sizeof k, 47, "wx", "\xC3\x89VORA"));   // bytes above ASCII untouched
  EXPECT_STREQ(k, "wx|\xC3\x89vora");
}

TEST(CacheKey, TooLongIsNotCut) {
  char k[48];
  std::string a(44, 'a');
  ASSERT_TRUE(botNetCacheKey(k, sizeof k, 47, "wx", a.c_str()));   // 2 + 1 + 44 = 47
  EXPECT_EQ(strlen(k), 47u);
  a += "b";
  EXPECT_FALSE(botNetCacheKey(k, sizeof k, 47, "wx", a.c_str()));
  EXPECT_STREQ(k, "");
  EXPECT_FALSE(botNetCacheKey(k, 8, 47, "wx", "toron"));            // cap 8 holds 7
  EXPECT_TRUE(botNetCacheKey(k, 8, 47, "wx", "toro"));
  EXPECT_STREQ(k, "wx|toro");
  EXPECT_FALSE(botNetCacheKey(nullptr, 8, 47, "wx", ""));
}

// ---- joining entries ----

TEST(Join, EntriesInOrder) {
  char b[128] = ""; size_t n = 0; bool closed = false;
  EXPECT_TRUE(botJoinAppend(b, sizeof b, n, 100, closed, "A: online"));
  EXPECT_TRUE(botJoinAppend(b, sizeof b, n, 100, closed, "B: offline 2h"));
  EXPECT_TRUE(botJoinAppend(b, sizeof b, n, 100, closed, "C: unavailable"));
  EXPECT_STREQ(b, "A: online | B: offline 2h | C: unavailable");
  EXPECT_EQ(n, strlen(b));
}

TEST(Join, DropsFromTheEndWithoutMark) {
  char b[128] = ""; size_t n = 0; bool closed = false;
  EXPECT_TRUE(botJoinAppend(b, sizeof b, n, 24, closed, "A: online"));       // 9
  EXPECT_TRUE(botJoinAppend(b, sizeof b, n, 24, closed, "B: online"));       // 9 + 3 + 9 = 21
  EXPECT_FALSE(botJoinAppend(b, sizeof b, n, 24, closed, "C: offline"));     // would be 34
  EXPECT_TRUE(closed);
  EXPECT_FALSE(botJoinAppend(b, sizeof b, n, 24, closed, "D"));              // fits, but after a drop
  EXPECT_STREQ(b, "A: online | B: online");
}

TEST(Join, ExactFitAndFirstTooLong) {
  char b[128] = ""; size_t n = 0; bool closed = false;
  EXPECT_TRUE(botJoinAppend(b, sizeof b, n, 7, closed, "A: x"));
  EXPECT_FALSE(botJoinAppend(b, sizeof b, n, 7, closed, "B"));   // 4 + 3 + 1 = 8
  b[0] = '\0'; n = 0; closed = false;
  EXPECT_TRUE(botJoinAppend(b, sizeof b, n, 8, closed, "A: x"));
  EXPECT_TRUE(botJoinAppend(b, sizeof b, n, 8, closed, "B"));    // exactly 8
  EXPECT_STREQ(b, "A: x | B");
  b[0] = '\0'; n = 0; closed = false;
  EXPECT_FALSE(botJoinAppend(b, sizeof b, n, 10, closed, "Longlabel: offline"));
  EXPECT_EQ(n, strlen(b));
  EXPECT_LE(n, 10u);
  EXPECT_GT(n, 0u);                                               // still some text
  char small[6] = ""; n = 0; closed = false;
  botJoinAppend(small, sizeof small, n, 100, closed, "abcdefgh");  // bounded by cap too
  EXPECT_LE(strlen(small), 5u);
}

// ---- {fetch} ----

static JsonDocument J(const char* s) { JsonDocument d; deserializeJson(d, s); return d; }

TEST(Fetch, ValueKinds) {
  JsonDocument d = J("{\"a\":{\"s\":\"hi\",\"i\":42,\"f\":3.14159,\"b\":true,\"n\":null,\"o\":{},\"arr\":[1]}}");
  char v[61];
  ASSERT_TRUE(botFetchValue(d.as<JsonVariantConst>(), "a.s", v, sizeof v)); EXPECT_STREQ(v, "hi");
  ASSERT_TRUE(botFetchValue(d.as<JsonVariantConst>(), "a.i", v, sizeof v)); EXPECT_STREQ(v, "42");
  ASSERT_TRUE(botFetchValue(d.as<JsonVariantConst>(), "a.f", v, sizeof v)); EXPECT_STREQ(v, "3.14");
  ASSERT_TRUE(botFetchValue(d.as<JsonVariantConst>(), "a.b", v, sizeof v)); EXPECT_STREQ(v, "true");
  ASSERT_TRUE(botFetchValue(d.as<JsonVariantConst>(), "a.arr[0]", v, sizeof v)); EXPECT_STREQ(v, "1");
  EXPECT_FALSE(botFetchValue(d.as<JsonVariantConst>(), "a.n", v, sizeof v)); EXPECT_STREQ(v, "");
  EXPECT_FALSE(botFetchValue(d.as<JsonVariantConst>(), "a.o", v, sizeof v));
  EXPECT_FALSE(botFetchValue(d.as<JsonVariantConst>(), "a.arr", v, sizeof v));
  EXPECT_FALSE(botFetchValue(d.as<JsonVariantConst>(), "a.missing", v, sizeof v));
  EXPECT_FALSE(botFetchValue(d.as<JsonVariantConst>(), "a..s", v, sizeof v));
}

TEST(Fetch, LongStringCutOnCharacterAndControlsBecomeSpaces) {
  std::string s(58, 'x');
  s += "\xC3\xA9\xC3\xA9";   // two 2-byte characters: the cut at 60 keeps only the first
  JsonDocument d;
  d["v"] = s.c_str();
  d["c"] = "a\tb\nc\x7f" "d";
  char v[61];
  ASSERT_TRUE(botFetchValue(d.as<JsonVariantConst>(), "v", v, sizeof v));
  EXPECT_EQ(strlen(v), 60u);
  EXPECT_EQ(std::string(v), std::string(58, 'x') + "\xC3\xA9");
  ASSERT_TRUE(botFetchValue(d.as<JsonVariantConst>(), "c", v, sizeof v));
  EXPECT_STREQ(v, "a b c d");
  std::string big(5000, 'y');
  d["big"] = big.c_str();
  ASSERT_TRUE(botFetchValue(d.as<JsonVariantConst>(), "big", v, sizeof v));   // long values are cut, not refused
  EXPECT_EQ(strlen(v), 60u);
  char small[5];
  ASSERT_TRUE(botFetchValue(d.as<JsonVariantConst>(), "big", small, sizeof small));
  EXPECT_STREQ(small, "yyyy");
}

TEST(Fetch, FillReplacesEveryMark) {
  char b[64];
  const char pre[] = {'L', 'v', 'l', ' ', BOT_FETCH_MARK, ' ', '(', BOT_FETCH_MARK, ')', 0};
  botFetchFill(b, sizeof b, pre, "4.2");
  EXPECT_STREQ(b, "Lvl 4.2 (4.2)");
  botFetchFill(b, sizeof b, "no mark", "x");
  EXPECT_STREQ(b, "no mark");
  botFetchFill(b, sizeof b, pre, "");
  EXPECT_STREQ(b, "Lvl  ()");
  char s[6];
  size_t n = botFetchFill(s, sizeof s, pre, "123456");
  EXPECT_EQ(n, 5u);
  EXPECT_STREQ(s, "Lvl 1");
  botFetchFill(b, sizeof b, nullptr, "x");
  EXPECT_STREQ(b, "");
}

// ---- URL form ----

TEST(Url, AddsSlashBeforeQueryAndDropsFragment) {
  char u[128];
  ASSERT_TRUE(botFetchNormalizeUrl(u, sizeof u, "https://api.example.org?q=1")); EXPECT_STREQ(u, "https://api.example.org/?q=1");
  ASSERT_TRUE(botFetchNormalizeUrl(u, sizeof u, "http://h:8080?q=a%3Ab")); EXPECT_STREQ(u, "http://h:8080/?q=a%3Ab");
  ASSERT_TRUE(botFetchNormalizeUrl(u, sizeof u, "https://h")); EXPECT_STREQ(u, "https://h/");
  ASSERT_TRUE(botFetchNormalizeUrl(u, sizeof u, "https://h/p?q=1")); EXPECT_STREQ(u, "https://h/p?q=1");
  ASSERT_TRUE(botFetchNormalizeUrl(u, sizeof u, "https://h/p?q=1#frag")); EXPECT_STREQ(u, "https://h/p?q=1");
  ASSERT_TRUE(botFetchNormalizeUrl(u, sizeof u, "https://h#frag")); EXPECT_STREQ(u, "https://h/");
}

TEST(Url, RefusesOtherSchemesEmptyHostAndOverflow) {
  char u[16];
  EXPECT_FALSE(botFetchNormalizeUrl(u, sizeof u, "ftp://h/")); EXPECT_STREQ(u, "");
  EXPECT_FALSE(botFetchNormalizeUrl(u, sizeof u, "https:///p"));
  EXPECT_FALSE(botFetchNormalizeUrl(u, sizeof u, "https://?q"));
  EXPECT_FALSE(botFetchNormalizeUrl(u, sizeof u, nullptr));
  EXPECT_TRUE(botFetchNormalizeUrl(u, sizeof u, "http://abcdefg"));     // 14 + "/" = 15 fits
  EXPECT_FALSE(botFetchNormalizeUrl(u, sizeof u, "http://abcdefgh"));   // 16 does not
}

// ---- UTC offset ----

TEST(Offset, FromBrokenDownLocalTime) {
  uint32_t now = 1795209600u;   // 2026-11-20T21:20:00Z
  struct tm lt = {};
  lt.tm_year = 126; lt.tm_mon = 10; lt.tm_mday = 20; lt.tm_hour = 16; lt.tm_min = 20; lt.tm_sec = 0;   // EST
  EXPECT_EQ(botUtcOffset(lt, now), -5 * 3600);
  lt.tm_mday = 21; lt.tm_hour = 2; lt.tm_min = 50;   // India, next day
  EXPECT_EQ(botUtcOffset(lt, now), 5 * 3600 + 1800);
  lt.tm_mday = 20; lt.tm_hour = 21; lt.tm_min = 20;
  EXPECT_EQ(botUtcOffset(lt, now), 0);
  lt.tm_year = 70; lt.tm_mon = 0; lt.tm_mday = 1;   // nonsense: ignored
  EXPECT_EQ(botUtcOffset(lt, now), 0);
}

// ---- try holder ----

TEST(Try, OneAtATimeAndTakeOnce) {
  BotTryHolder h;
  uint32_t id = 0; char out[128];
  EXPECT_FALSE(h.take(id, out, sizeof out));
  EXPECT_TRUE(h.begin(5, 1000));
  EXPECT_TRUE(h.pending());
  EXPECT_FALSE(h.begin(6, 1001));            // one at a time
  EXPECT_FALSE(h.take(id, out, sizeof out)); // not done yet
  EXPECT_FALSE(h.complete(6, "wrong try"));  // only the pending try's own reply
  EXPECT_TRUE(h.complete(5, "@[web] pong"));
  EXPECT_FALSE(h.pending());
  EXPECT_TRUE(h.take(id, out, sizeof out));
  EXPECT_EQ(id, 5u); EXPECT_STREQ(out, "@[web] pong");
  EXPECT_FALSE(h.take(id, out, sizeof out)); // once
  EXPECT_FALSE(h.complete(5, "late"));
}

TEST(Try, SilentAndTimeout) {
  BotTryHolder h;
  uint32_t id = 0; char out[128] = "x";
  h.begin(1, 0);
  h.complete(1, "");
  ASSERT_TRUE(h.take(id, out, sizeof out)); EXPECT_STREQ(out, "");
  h.begin(2, 0xFFFFF000u);
  h.tick(0xFFFFF000u + BOT_TRY_TIMEOUT_MS - 1);
  EXPECT_TRUE(h.pending());
  h.tick(0xFFFFF000u + BOT_TRY_TIMEOUT_MS);  // across the millis() wrap
  EXPECT_FALSE(h.pending());
  ASSERT_TRUE(h.take(id, out, sizeof out)); EXPECT_EQ(id, 2u); EXPECT_STREQ(out, "");
  EXPECT_FALSE(h.complete(2, "late reply"));  // a reply after the timeout is dropped
}

TEST(Try, UntakenResultReplacedAndBounded) {
  BotTryHolder h;
  uint32_t id = 0; char out[8];
  h.begin(1, 0); h.complete(1, "first");
  EXPECT_TRUE(h.begin(2, 5));                // a finished, untaken result is dropped
  std::string longer(300, 'z');
  h.complete(2, longer.c_str());
  ASSERT_TRUE(h.take(id, out, sizeof out));
  EXPECT_EQ(id, 2u);
  EXPECT_LE(strlen(out), 7u);
}

// ---- reply routing and the cap ----

TEST(Route, ParkedCountsTheCapExactlyOnce) {
  BotRateCap cap;
  for (uint8_t kind : {BD_CHANNEL, BD_DIRECT}) {
    BotRateCap c;
    ASSERT_TRUE(botNetMayStart(kind, c, 1000, 1));
    EXPECT_EQ(botNetAfterStart(kind, BNS_PARKED, c, 1000, 1), BNA_PARKED);
    EXPECT_FALSE(c.wouldAllow(1001, 1));                 // counted at parking
    // The result is delivered without counting again, so it still goes out.
    EXPECT_EQ(botDeliverRoute(kind, true, false, false, c, 2000, 1), BDR_QUEUE);
    EXPECT_FALSE(botNetMayStart(kind, c, 2001, 1));      // and the next command is held back
  }
  (void)cap;
}

TEST(Route, CapRefusesBeforeLookingUp) {
  BotRateCap c;
  ASSERT_TRUE(c.allow(0, 1));
  EXPECT_FALSE(botNetMayStart(BD_CHANNEL, c, 10, 1));
  EXPECT_FALSE(botNetMayStart(BD_DIRECT, c, 10, 1));
  EXPECT_TRUE(botNetMayStart(BD_TRY, c, 10, 1));       // a try is never limited
  EXPECT_FALSE(botNetMayStart(BD_CHANNEL, c, 10, 0));
}

TEST(Route, BusyChannelSilentDirectTold) {
  BotRateCap c;
  EXPECT_EQ(botNetAfterStart(BD_CHANNEL, BNS_BUSY, c, 0, 1), BNA_SILENT);
  EXPECT_TRUE(c.wouldAllow(1, 1));                     // busy on a channel uses nothing
  EXPECT_EQ(botNetAfterStart(BD_DIRECT, BNS_BUSY, c, 0, 1), BNA_DELIVER_BUSY);
  EXPECT_EQ(botNetAfterStart(BD_TRY, BNS_BUSY, c, 0, 1), BNA_DELIVER_BUSY);
  EXPECT_TRUE(c.wouldAllow(1, 1));                     // counted only when delivered
  EXPECT_EQ(botNetAfterStart(BD_DIRECT, BNS_REPLY, c, 0, 1), BNA_DELIVER);
  EXPECT_EQ(botDeliverRoute(BD_DIRECT, true, false, true, c, 2, 1), BDR_QUEUE);
  EXPECT_FALSE(c.wouldAllow(3, 1));
}

TEST(Route, RepeatIsDroppedSilentlyWithoutTheCap) {
  for (uint8_t kind : {BD_CHANNEL, BD_DIRECT, BD_TRY}) {
    BotRateCap c;
    EXPECT_EQ(botNetAfterStart(kind, BNS_DUPLICATE, c, 0, 1), BNA_SILENT) << (int)kind;
    EXPECT_TRUE(c.wouldAllow(1, 1));
  }
}

TEST(Route, TryNeverQueuedAndNeverCounted) {
  BotRateCap c;
  EXPECT_EQ(botNetAfterStart(BD_TRY, BNS_PARKED, c, 0, 1), BNA_PARKED);
  EXPECT_TRUE(c.wouldAllow(1, 1));
  for (bool text : {false, true})
    for (bool full : {false, true})
      for (bool count : {false, true}) EXPECT_EQ(botDeliverRoute(BD_TRY, text, full, count, c, 5, 0), BDR_HOLDER);
  EXPECT_TRUE(c.wouldAllow(6, 1));
}

TEST(Route, DropsWithoutUsingTheCap) {
  BotRateCap c;
  EXPECT_EQ(botDeliverRoute(BD_CHANNEL, false, false, true, c, 0, 1), BDR_DROP);   // nothing to send
  EXPECT_EQ(botDeliverRoute(BD_CHANNEL, true, true, true, c, 0, 1), BDR_DROP);     // ring full
  EXPECT_TRUE(c.wouldAllow(1, 1));
  EXPECT_EQ(botDeliverRoute(7, true, false, true, c, 0, 1), BDR_DROP);             // unknown kind
  EXPECT_EQ(botDeliverRoute(BD_CHANNEL, true, false, true, c, 0, 1), BDR_QUEUE);
  EXPECT_EQ(botDeliverRoute(BD_CHANNEL, true, false, true, c, 1, 1), BDR_DROP);    // cap refuses
}

// A reply that waited is not sent to a channel removed from the bot's list meanwhile (m7).
static BotConfig g_route_cfg;   // never on the stack
TEST(Route, RemovedChannelDropsTheWaitingReply) {
  botConfigDefaults(g_route_cfg);   // channels: "#bot"
  BotReplyDest d;
  memset(&d, 0, sizeof(d));
  d.kind = BD_CHANNEL;
  strcpy(d.channel, "#bot");
  EXPECT_TRUE(botReplyDestServed(d, g_route_cfg));
  strcpy(g_route_cfg.channels[0], "#test");   // the owner swapped the channel during the wait
  EXPECT_FALSE(botReplyDestServed(d, g_route_cfg));
  g_route_cfg.num_channels = 2;
  strcpy(g_route_cfg.channels[1], "#bot");
  EXPECT_TRUE(botReplyDestServed(d, g_route_cfg));
  g_route_cfg.num_channels = 1;               // only the first row counts
  EXPECT_FALSE(botReplyDestServed(d, g_route_cfg));
  d.channel[0] = '\0';
  EXPECT_FALSE(botReplyDestServed(d, g_route_cfg));
  d.kind = BD_DIRECT;                         // a direct message or a try has no channel to lose
  EXPECT_TRUE(botReplyDestServed(d, g_route_cfg));
  d.kind = BD_TRY;
  EXPECT_TRUE(botReplyDestServed(d, g_route_cfg));

  // Routed: dropped without the cap being used or the capped flag set, for a look-up's result
  // (counted when parked) and a broker's reply (counted at delivery) alike.
  for (bool count : {false, true}) {
    BotRateCap c;
    bool capped = true;
    EXPECT_EQ(botDeliverRoute(BD_CHANNEL, true, false, count, c, 0, 1, &capped, false), BDR_DROP);
    EXPECT_FALSE(capped);
    EXPECT_TRUE(c.wouldAllow(1, 1));
    EXPECT_EQ(botDeliverRoute(BD_CHANNEL, true, false, count, c, 0, 1, &capped, true), BDR_QUEUE);
  }
  BotRateCap c;
  EXPECT_EQ(botDeliverRoute(BD_TRY, true, false, true, c, 0, 1, nullptr, false), BDR_HOLDER);
}

TEST(Try, BrokerSourceKeptWithAReplyOnly) {
  BotTryHolder h;
  uint32_t id = 0; char out[128]; bool broker = false;
  h.begin(1, 0); h.complete(1, "@[web] tide 2.1 m", true);
  ASSERT_TRUE(h.take(id, out, sizeof out, &broker)); EXPECT_TRUE(broker);
  h.begin(2, 0); h.complete(2, "", true);                     // no reply: nothing to attribute
  ASSERT_TRUE(h.take(id, out, sizeof out, &broker)); EXPECT_FALSE(broker);
  h.begin(3, 0); h.complete(3, "@[web] pong");
  ASSERT_TRUE(h.take(id, out, sizeof out, &broker)); EXPECT_FALSE(broker);
  h.begin(4, 0); h.complete(4, "@[web] x", true);
  EXPECT_TRUE(h.begin(5, 1));                                 // an untaken result is dropped, flag too
  h.complete(5, "@[web] y");
  ASSERT_TRUE(h.take(id, out, sizeof out, &broker)); EXPECT_EQ(id, 5u); EXPECT_FALSE(broker);
  h.begin(6, 0); h.tick(BOT_TRY_TIMEOUT_MS);
  broker = true;
  ASSERT_TRUE(h.take(id, out, sizeof out, &broker)); EXPECT_FALSE(broker);   // timed out
  h.begin(7, 0); h.complete(7, "@[web] z", true);
  EXPECT_TRUE(h.take(id, out, sizeof out));                  // the flag is optional
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
