#pragma once

// The wipe and the wiping ArduinoJson allocator now live in the pure header (host-tested in
// test/test_bot_wipe); this include keeps the device code's "BotWipe.h" working.

#ifdef WITH_BOT

#include <helpers/bot/BotWipe.h>

#endif  // WITH_BOT
