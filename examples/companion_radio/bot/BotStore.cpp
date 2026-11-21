#ifdef WITH_BOT

#include "BotStore.h"
#include "BotWipe.h"

#include <stdlib.h>

static const char* const BOT_FILE = "/bot.json";
static const char* const BOT_TMP = "/bot.tmp";

enum BotFileState { BF_MISSING, BF_INVALID, BF_NO_MEMORY, BF_OK };

// Parses one file into c (on top of what c already holds) when it is a valid JSON object.
// BF_NO_MEMORY says nothing about the file itself, so it is never treated as damaged.
static BotFileState botStoreApplyFile(fs::FS& fs, const char* path, BotConfig& c) {
  if (!fs.exists(path)) return BF_MISSING;
  File f = fs.open(path, "r");
  if (!f) return BF_MISSING;
  JsonDocument doc(BotWipingAllocator::instance());   // holds the web hash and the broker password
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err == DeserializationError::NoMemory) return BF_NO_MEMORY;
  if (err || !doc.is<JsonObject>()) return BF_INVALID;
  botConfigFromJson(doc.as<JsonVariantConst>(), c);
  return BF_OK;
}

bool botStoreLoad(fs::FS& fs, BotConfig& c) {
  botConfigDefaults(c);
  BotFileState main_state = botStoreApplyFile(fs, BOT_FILE, c);
  if (main_state == BF_OK) return true;
  if (main_state == BF_NO_MEMORY) return false;   // the file may be fine: leave both files alone
  // /bot.json is missing or damaged. A save that was interrupted after writing /bot.tmp
  // leaves the good copy there: use it, and make it /bot.json so that the next save (which
  // starts by deleting /bot.tmp) cannot delete the only good copy.
  botConfigDefaults(c);
  if (botStoreApplyFile(fs, BOT_TMP, c) != BF_OK) {
    botConfigDefaults(c);
    return false;
  }
  if (fs.exists(BOT_FILE)) fs.remove(BOT_FILE);
  fs.rename(BOT_TMP, BOT_FILE);   // on failure the copy stays in /bot.tmp; c is loaded either way
  return true;
}

bool botStoreSave(fs::FS& fs, const BotConfig& c) {
  // Serialise into one heap buffer so the file gets a single write and a known length.
  // Low on memory, ArduinoJson leaves members out silently (the salt and hash, added last,
  // would go first): such a document is never written. The document and the buffer hold the
  // broker password, so both are wiped before they are freed (on every path).
  JsonDocument doc(BotWipingAllocator::instance());
  if (!botConfigToJsonChecked(c, doc, true)) return false;
  size_t len = measureJson(doc);
  if (len == 0) return false;
  char* buf = (char*)malloc(len + 1);
  if (!buf) return false;
  size_t n = serializeJson(doc, buf, len + 1);
  doc.clear();
  if (n != len) {   // serialisation came out short: write nothing
    botWipe(buf, len + 1);
    free(buf);
    return false;
  }

  bool ok = false;
  if (fs.exists(BOT_TMP)) fs.remove(BOT_TMP);
  File f = fs.open(BOT_TMP, "w");
  if (f) {
    size_t written = f.write((const uint8_t*)buf, n);
    f.close();
    ok = (n > 0 && written == n);
  }
  botWipe(buf, len + 1);
  free(buf);

  if (ok) {
    File chk = fs.open(BOT_TMP, "r");
    size_t sz = chk ? chk.size() : 0;
    if (chk) chk.close();
    ok = (sz != 0 && sz == n);
  }
  if (!ok) {
    if (fs.exists(BOT_TMP)) fs.remove(BOT_TMP);
    return false;
  }
  // SPIFFS rename fails when the target exists, so remove it first.
  if (fs.exists(BOT_FILE)) fs.remove(BOT_FILE);
  return fs.rename(BOT_TMP, BOT_FILE);
}

#endif  // WITH_BOT
