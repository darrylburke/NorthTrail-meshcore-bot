#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "BotText.h"

// The last steps before a reply goes on air (pure C/C++, no heap, host-testable): the
// reply budget, control-character stripping, the "@[sender] " mention that keeps two bots
// from answering each other, and the small ring that delays replies.

#define BOT_MAX_MSG 127                 // on-air text including "<node name>: "
#define BOT_MENTION_MIN_BODY 24         // reply bytes kept beside a very long sender name
#define BOT_REPLY_DELAY_MS 1500u        // a reply waits this long after the request
#define BOT_PART_GAP_MS 4000u           // between the messages of one multi-message broker reply
#define BOT_PING_PARTS_MAX 3            // a ping's route may take this many messages (one queue slot stays free)
#define BOT_KEY_BYTES 32                // a contact's public key

// Bytes a reply may use: BOT_MAX_MSG minus "<node_name>: ". 0 when the name leaves no room.
static inline size_t botReplyBudget(const char* node_name) {
  size_t n = node_name ? strlen(node_name) : 0;
  return n + 2 < BOT_MAX_MSG ? BOT_MAX_MSG - n - 2 : 0;
}

// Copies src without its control bytes (< 0x20 and 0x7F), cut on a UTF-8 character
// boundary to fit cap - 1. NUL-terminates when cap > 0; null src is "". Returns the length.
static inline size_t botStripControls(char* dst, size_t cap, const char* src) {
  if (!dst || cap == 0) return 0;
  size_t n = 0;
  for (const char* p = src ? src : ""; *p; p++) {
    uint8_t b = (uint8_t)*p;
    if (b < 0x20 || b == 0x7F) continue;
    if (n + 1 >= cap) {
      if (botIsUtf8Cont((char)b)) {   // the character in progress does not fit whole: drop it
        while (n > 0 && botIsUtf8Cont(dst[n - 1])) n--;
        if (n > 0 && ((uint8_t)dst[n - 1] & 0xC0) == 0xC0) n--;
      }
      break;
    }
    dst[n++] = (char)b;
  }
  dst[n] = '\0';
  return n;
}

static inline bool botStartsWithMention(const char* s) { return s && s[0] == '@' && s[1] == '['; }

// Bytes of the sender's name used inside "@[...] " when the reply may be max_bytes long: the
// whole name while BOT_MENTION_MIN_BODY bytes of reply still fit beside it, else the name is
// cut (on a character boundary) to leave them.
static inline size_t botMentionNameLen(const char* name, size_t max_bytes) {
  size_t n = name ? strlen(name) : 0;
  size_t reserve = 4 + BOT_MENTION_MIN_BODY;   // "@[" "] " and the kept reply bytes
  size_t avail = max_bytes > reserve ? max_bytes - reserve : 0;
  if (n > avail) {
    n = avail;
    while (n > 0 && botIsUtf8Cont(name[n])) n--;
  }
  return n;
}

// Bytes left for a reply that botFinishReply will prefix with the mention.
static inline size_t botMentionBodyBudget(const char* name, size_t max_bytes) {
  size_t used = 4 + botMentionNameLen(name, max_bytes);
  return max_bytes > used ? max_bytes - used : 0;
}

// Who wrote a reply's text. BRT_BUILTIN: one of the bot's own formatters, which may write the
// asker's mention themselves (roll, 8ball, dist). BRT_FOREIGN: text the bot did not write, from a
// custom command's template (whose {args} a stranger fills) or from the MQTT broker; it may begin
// "@[" with someone else's name, so it never stands in for the asker's mention.
enum BotReplyText : uint8_t { BRT_BUILTIN = 0, BRT_FOREIGN = 1 };

// The text sent on air: a built-in reply as it is when it already begins "@[", otherwise
// "@[<name>] " followed by the reply (a foreign reply always, whatever it begins with); the whole
// is clamped to max_bytes (and cap - 1) with botClamp. A peer's parser refuses anything starting
// "@[", so a reply can never be taken as a command by another bot. Returns 0 and writes "" when
// there is nothing to send (empty reply, or too little room for a mention). name should already
// be free of control bytes.
static inline size_t botFinishReply(char* dst, size_t cap, const char* reply, const char* name, size_t max_bytes,
                                    uint8_t source) {
  if (!dst || cap == 0) return 0;
  dst[0] = '\0';
  if (!reply || !reply[0]) return 0;
  size_t limit = max_bytes < cap - 1 ? max_bytes : cap - 1;
  if (source == BRT_BUILTIN && botStartsWithMention(reply)) {
    size_t n = botClamp(dst, cap, reply, limit);
    if (n < 2 || !botStartsWithMention(dst)) { dst[0] = '\0'; return 0; }
    return n;
  }
  if (limit < 5) return 0;   // "@[] " plus at least one byte
  size_t nl = botMentionNameLen(name, limit);
  size_t pos = 0;
  dst[pos++] = '@'; dst[pos++] = '[';
  if (nl) memcpy(dst + pos, name, nl);
  pos += nl;
  dst[pos++] = ']'; dst[pos++] = ' ';
  // A template that itself begins with the asker's own mention ("@[{sender}] ...") is not doubled.
  if (strncmp(reply, dst, pos) == 0) reply += pos;
  if (!reply[0]) { dst[0] = '\0'; return 0; }
  size_t body = botClamp(dst + pos, cap - pos, reply, limit - pos);
  if (body == 0) { dst[0] = '\0'; return 0; }
  return pos + body;
}

// How many messages a ping may take: several only when no other reply is waiting to go out, so
// a long route never delays or pushes out an answer to someone else; otherwise one, cut "+N more".
static inline uint8_t botPingPartsAllowed(int queued) { return queued == 0 ? BOT_PING_PARTS_MAX : 1; }

// ---- deferred replies ----

// Where a reply goes: a channel by name, a contact by public key, or (BD_TRY) back to the web
// page that tried the command. A BD_TRY reply is never queued or transmitted: the bot hands it
// to its try holder at the one place replies are delivered.
enum BotDestKind : uint8_t { BD_CHANNEL = 0, BD_DIRECT = 1, BD_TRY = 2 };
struct BotReplyDest {
  uint8_t kind;                 // BotDestKind
  uint8_t key[BOT_KEY_BYTES];   // BD_DIRECT: the contact's public key
  char channel[32];             // BD_CHANNEL: its name
  uint32_t try_id;              // BD_TRY: the id the page gave the try
};

// One reply waiting to be sent (dest is BD_CHANNEL or BD_DIRECT).
struct BotQueuedReply {
  uint32_t due_ms;
  BotReplyDest dest;
  char text[128];
  char sender[24];              // for the log line written when it is sent
  char request[48];
};

// A FIFO of BotQueuedReply, sent strictly in order: the front is the next one out. The later
// messages of a multi-message reply are due later, and anything queued behind them waits its turn. due() compares wrap-safely (signed difference of millis() values).
class BotReplyQueue {
public:
  static const int SLOTS = 4;

  BotReplyQueue() : _head(0), _count(0) { memset(_q, 0, sizeof(_q)); }

  bool empty() const { return _count == 0; }
  bool full() const { return _count == SLOTS; }
  int size() const { return _count; }

  // A zeroed slot at the back, due at due_ms, for the caller to fill; nullptr when full.
  BotQueuedReply* push(uint32_t due_ms) {
    if (full()) return nullptr;
    BotQueuedReply* r = &_q[(_head + _count) % SLOTS];
    memset(r, 0, sizeof(*r));
    r->due_ms = due_ms;
    _count++;
    return r;
  }

  // The front entry once its time has come, else nullptr.
  BotQueuedReply* due(uint32_t now_ms) {
    if (empty()) return nullptr;
    BotQueuedReply* r = &_q[_head];
    return (int32_t)(now_ms - r->due_ms) >= 0 ? r : nullptr;
  }

  void pop() {
    if (empty()) return;
    memset(&_q[_head], 0, sizeof(_q[_head]));
    _head = (_head + 1) % SLOTS;
    _count--;
  }

private:
  BotQueuedReply _q[SLOTS];
  static_assert(BOT_PING_PARTS_MAX < SLOTS, "a ping leaves a queue slot free");
  int _head, _count;
};
