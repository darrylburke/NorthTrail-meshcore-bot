#ifdef WITH_BOT

#include "BotPassword.h"

#include <string.h>
#include "BotWipe.h"
#include <mbedtls/md.h>
#include <mbedtls/pkcs5.h>

static int botPwHexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

static bool botPwFromHex(const char* hex, uint8_t* out, size_t n) {
  if (!hex || strlen(hex) != 2 * n) return false;
  for (size_t i = 0; i < n; i++) {
    int hi = botPwHexVal(hex[2 * i]), lo = botPwHexVal(hex[2 * i + 1]);
    if (hi < 0 || lo < 0) return false;
    out[i] = (uint8_t)((hi << 4) | lo);
  }
  return true;
}

// PBKDF2-HMAC-SHA256 into key[32]; mbedtls 2.28 (this core) has only the md-context API.
static bool botPwDerive(const char* plain, const uint8_t salt[8], uint8_t key[32]) {
  const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (!info) return false;
  mbedtls_md_context_t ctx;
  mbedtls_md_init(&ctx);
  bool ok = mbedtls_md_setup(&ctx, info, 1) == 0 &&
            mbedtls_pkcs5_pbkdf2_hmac(&ctx, (const unsigned char*)plain, strlen(plain), salt, 8,
                                      BOT_PASSWORD_ITERATIONS, 32, key) == 0;
  mbedtls_md_free(&ctx);   // also zeroes the HMAC state
  return ok;
}

bool botPasswordHash(const char* plain, const uint8_t salt[8], char hash_hex[65]) {
  if (!hash_hex) return false;
  hash_hex[0] = '\0';
  if (!plain || !salt) return false;
  uint8_t key[32];
  if (!botPwDerive(plain, salt, key)) { botWipe(key, sizeof(key)); return false; }
  static const char digits[] = "0123456789abcdef";
  for (int i = 0; i < 32; i++) { hash_hex[2 * i] = digits[key[i] >> 4]; hash_hex[2 * i + 1] = digits[key[i] & 15]; }
  hash_hex[64] = '\0';
  botWipe(key, sizeof(key));
  return true;
}

bool botPasswordVerify(const char* plain, const char* salt_hex, const char* hash_hex) {
  if (!plain) return false;
  uint8_t salt[8], want[32], got[32] = {0};
  if (!botPwFromHex(salt_hex, salt, sizeof(salt)) || !botPwFromHex(hash_hex, want, sizeof(want))) return false;
  bool ok = botPwDerive(plain, salt, got);
  uint8_t diff = ok ? 0 : 1;
  for (int i = 0; i < 32; i++) diff |= (uint8_t)(got[i] ^ want[i]);   // constant time
  botWipe(got, sizeof(got));
  botWipe(want, sizeof(want));
  return diff == 0;
}

#endif  // WITH_BOT
