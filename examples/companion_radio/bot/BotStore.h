#pragma once

#include <FS.h>
#include <helpers/bot/BotConfig.h>

// Bot settings on the device file system (/bot.json). Loop task only.

// Fills c with defaults, then applies /bot.json when it can be read and parsed. When
// /bot.json is missing or damaged and /bot.tmp is valid (a save was cut between remove and
// rename), /bot.tmp is applied and renamed to /bot.json. Running out of memory while parsing
// changes no file. Returns true when a file was applied; false means c holds plain defaults.
bool botStoreLoad(fs::FS& fs, BotConfig& c);

// Writes /bot.tmp, closes it, checks its size, then replaces /bot.json with it. Returns false,
// leaving /bot.json untouched, when the document could not be built whole (low memory) or the
// temporary file could not be written.
bool botStoreSave(fs::FS& fs, const BotConfig& c);
