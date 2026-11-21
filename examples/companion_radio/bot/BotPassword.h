#pragma once

#include <stdint.h>

// Web password hashing for the bot: PBKDF2-HMAC-SHA256 (mbedtls), 10,000 iterations, 8-byte
// salt, 32-byte key as 64 lower-case hex characters (the /bot.json web_salt/web_hash format).
// Both functions are re-entrant: they touch no globals and keep all state on the caller's
// stack, so they may run on any task (e.g. the web server's) against copied salt and hash.
// One call takes on the order of a second on the ESP32-S3 (estimate; see the task 7 report).

#define BOT_PASSWORD_ITERATIONS 10000

// Hashes plain with salt into hash_hex (64 hex + NUL). False (hash_hex set to "") on failure.
bool botPasswordHash(const char* plain, const uint8_t salt[8], char hash_hex[65]);

// True when plain hashes to hash_hex under salt_hex (16 hex). Constant-time comparison.
// False for null input, a malformed salt or hash, or an empty stored hash.
bool botPasswordVerify(const char* plain, const char* salt_hex, const char* hash_hex);
