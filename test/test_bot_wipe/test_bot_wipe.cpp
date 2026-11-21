// Host tests for BotWipe.h: the wiping ArduinoJson allocator wipes exactly the bytes that were
// asked for. Every block comes from a guarded heap that puts guard bytes right after the block it
// handed out and checks them, and that the caller's bytes were wiped, when the block comes back.
// A wipe past the request (the device's old rounded "allocated size") damages the guard bytes,
// which is what corrupted the device's heap ("CORRUPT HEAP: Bad tail").
#include <gtest/gtest.h>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include "helpers/bot/BotWipe.h"

namespace {

const size_t GUARD = 16;
const uint8_t GUARD_BYTE = 0xA5;

struct Block { size_t n; bool guard_ok; bool wiped; };

struct GuardedHeap {
  static std::map<uint8_t*, size_t>& live() { static std::map<uint8_t*, size_t> m; return m; }
  static size_t& damaged() { static size_t d = 0; return d; }
  static size_t& unwiped() { static size_t u = 0; return u; }
  static size_t& takes() { static size_t t = 0; return t; }
  static size_t& fail_after() { static size_t f = SIZE_MAX; return f; }   // takes left before one fails

  static void reset() { live().clear(); damaged() = 0; unwiped() = 0; takes() = 0; fail_after() = SIZE_MAX; }

  static void* take(size_t n) {
    if (fail_after() == 0) return nullptr;
    if (fail_after() != SIZE_MAX) fail_after()--;
    uint8_t* b = (uint8_t*)malloc(n + GUARD);
    if (!b) return nullptr;
    memset(b, 0x5A, n);                 // stale contents, so a missing wipe shows
    memset(b + n, GUARD_BYTE, GUARD);
    live()[b] = n;
    takes()++;
    return b;
  }
  static void give(void* p) {
    uint8_t* b = (uint8_t*)p;
    auto it = live().find(b);
    ASSERT_NE(it, live().end()) << "freed a block it was not given";
    size_t n = it->second;
    for (size_t i = 0; i < GUARD; i++)
      if (b[n + i] != GUARD_BYTE) { damaged()++; break; }
    for (size_t i = 0; i < n; i++)   // the size header and the caller's bytes
      if (b[i] != 0) { unwiped()++; break; }
    live().erase(it);
    free(b);
  }
};

typedef BotWipingAllocatorOn<GuardedHeap> GuardedAlloc;
const size_t HEAD = GuardedAlloc::HEAD;

class Wipe : public ::testing::Test {
protected:
  void SetUp() override { GuardedHeap::reset(); }
  void TearDown() override {
    EXPECT_EQ(GuardedHeap::live().size(), 0u) << "blocks not freed";
    EXPECT_EQ(GuardedHeap::damaged(), 0u) << "a wipe went past the requested size";
    EXPECT_EQ(GuardedHeap::unwiped(), 0u) << "a block was freed without being wiped";
  }
  GuardedAlloc a;
};

}  // namespace

TEST_F(Wipe, BlockIsAlignedAndSized) {
  for (size_t n : {0u, 1u, 3u, 7u, 8u, 13u, 64u, 70u}) {
    void* p = a.allocate(n);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ((uintptr_t)p % 8, 0u) << n;   // malloc's alignment kept (it is at least 8 on the host)
    EXPECT_EQ(GuardedAlloc::requested(p), n);
    EXPECT_EQ(GuardedHeap::live().begin()->second, HEAD + n);   // exactly the request and the header
    memset(p, 0xEE, n);                     // the caller may use every byte it asked for
    a.deallocate(p);
  }
}

TEST_F(Wipe, ReallocateEdges) {
  void* p = a.reallocate(nullptr, 5);   // as allocate
  ASSERT_NE(p, nullptr);
  memcpy(p, "abcde", 5);
  void* q = a.reallocate(p, 11);        // grows: the old bytes move, the old block is wiped and freed
  ASSERT_NE(q, nullptr);
  EXPECT_EQ(memcmp(q, "abcde", 5), 0);
  EXPECT_EQ(GuardedHeap::live().size(), 1u);
  memset(q, 'z', 11);
  void* r = a.reallocate(q, 2);         // shrinks: only what fits is copied
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(memcmp(r, "zz", 2), 0);
  void* z = a.reallocate(r, 0);         // to nothing: a valid empty block
  ASSERT_NE(z, nullptr);
  EXPECT_EQ(GuardedAlloc::requested(z), 0u);
  a.deallocate(z);
  a.deallocate(nullptr);                // nothing happens
}

TEST_F(Wipe, FailedReallocationKeepsTheOldBlock) {
  void* p = a.allocate(9);
  ASSERT_NE(p, nullptr);
  memcpy(p, "123456789", 9);
  GuardedHeap::fail_after() = 0;
  EXPECT_EQ(a.reallocate(p, 40), nullptr);
  EXPECT_EQ(memcmp(p, "123456789", 9), 0);
  GuardedHeap::fail_after() = SIZE_MAX;
  a.deallocate(p);
}

TEST_F(Wipe, SizesThatWouldOverflowAreRefused) {
  EXPECT_EQ(a.allocate(SIZE_MAX), nullptr);
  EXPECT_EQ(a.allocate(SIZE_MAX - HEAD + 1), nullptr);
  EXPECT_EQ(GuardedHeap::takes(), 0u);
  void* p = a.allocate(4);
  EXPECT_EQ(a.reallocate(p, SIZE_MAX), nullptr);   // and the old block survives
  a.deallocate(p);
}

// A document like /bot.json passing through the allocator: strings of every length from 1 to 70
// (odd sizes, the string pool growing and shrinking), serialised, then destroyed.
TEST_F(Wipe, JsonDocumentThroughTheAllocator) {
  std::string in = "{";
  for (int i = 1; i <= 70; i++) {
    if (i > 1) in += ",";
    in += "\"k" + std::to_string(i) + "\":\"" + std::string((size_t)i, (char)('a' + i % 26)) + "\"";
  }
  in += ",\"list\":[";
  for (int i = 1; i <= 70; i += 3) in += (i > 1 ? ",\"" : "\"") + std::string((size_t)i, 'q') + "\"";
  in += "],\"pass\":\"secret-password\"}";
  std::string out;
  {
    JsonDocument doc(&a);
    ASSERT_EQ(deserializeJson(doc, in), DeserializationError::Ok);
    EXPECT_GT(GuardedHeap::takes(), 2u);   // several blocks, so reallocation was exercised
    doc["k3"] = "changed";                  // a new string after parsing
    doc["k70"].set(std::string(33, 'x'));
    serializeJson(doc, out);
    EXPECT_NE(out.find("\"k1\":\"b\""), std::string::npos);
    EXPECT_NE(out.find("secret-password"), std::string::npos);
    doc.shrinkToFit();                      // reallocates down to what is used
    std::string again;
    serializeJson(doc, again);
    EXPECT_EQ(again, out);
  }
  EXPECT_GT(GuardedHeap::takes(), 0u);
  // TearDown: every block was freed, wiped, and nothing past a request was touched.
}

TEST_F(Wipe, DefaultInstanceUsesTheHeap) {
  BotWipingAllocator* d = BotWipingAllocator::instance();
  ASSERT_EQ(d, BotWipingAllocator::instance());
  JsonDocument doc(d);
  ASSERT_EQ(deserializeJson(doc, "{\"a\":\"bcdefghijklmnopqrstuvwxyz\"}"), DeserializationError::Ok);
  EXPECT_STREQ(doc["a"].as<const char*>(), "bcdefghijklmnopqrstuvwxyz");
}

// The guard bytes do catch the device's old mistake: wiping a block to its rounded-up size.
TEST(WipeGuard, AWipePastTheRequestIsCaught) {
  GuardedHeap::reset();
  GuardedAlloc a;
  void* p = a.allocate(13);
  ASSERT_NE(p, nullptr);
  size_t rounded = (13 + 7) & ~(size_t)7;   // what a heap reports as the block's size
  botWipe(p, rounded);                      // the old deallocate's wipe
  a.deallocate(p);
  EXPECT_EQ(GuardedHeap::damaged(), 1u);
  GuardedHeap::reset();
}

TEST(WipeGuard, WipeZeroesThroughVolatile) {
  char b[6] = "abcde";
  botWipe(b, 3);
  EXPECT_EQ(b[0], 0); EXPECT_EQ(b[2], 0); EXPECT_EQ(b[3], 'd');
  botWipe(nullptr, 10);   // nothing happens
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
