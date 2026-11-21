#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "BotText.h"

// Time helpers shared by the Kp and alert parsers (pure C/C++, no Arduino dependencies; host-testable).

static inline bool botIsLeapYear(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

// Days since 1970-01-01 for a proleptic Gregorian date (Hinnant days-from-civil); month 1-12.
static inline int64_t botDaysFromCivil(int y, int m, int d) {
  y -= m <= 2;
  int64_t era = (y >= 0 ? y : y - 399) / 400;
  int64_t yoe = y - era * 400;
  int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

static inline bool botIsoDigits(const char* s, int n, int& out) {
  int v = 0;
  for (int i = 0; i < n; i++) {
    if (s[i] < '0' || s[i] > '9') return false;
    v = v * 10 + (s[i] - '0');
  }
  out = v;
  return true;
}

// Parses "YYYY-MM-DDTHH:MM:SS" with an optional ".fraction" and an optional trailing "Z"
// (always UTC; fractions are ignored). Anything else, an impossible date or time, a year before
// 1970 or a result past 2106 (does not fit 32 bits) is false and leaves out unchanged.
// Decisions: no seconds-less form and no offsets; a leap second (60) is refused.
static inline bool botIsoToUnix(const char* iso, uint32_t& out) {
  if (!iso || strlen(iso) < 19) return false;
  int y, mo, d, h, mi, s;
  if (!botIsoDigits(iso, 4, y) || iso[4] != '-' || !botIsoDigits(iso + 5, 2, mo) || iso[7] != '-' ||
      !botIsoDigits(iso + 8, 2, d) || iso[10] != 'T' || !botIsoDigits(iso + 11, 2, h) || iso[13] != ':' ||
      !botIsoDigits(iso + 14, 2, mi) || iso[16] != ':' || !botIsoDigits(iso + 17, 2, s))
    return false;
  const char* p = iso + 19;
  if (*p == '.') {
    p++;
    if (*p < '0' || *p > '9') return false;
    while (*p >= '0' && *p <= '9') p++;
  }
  if (*p == 'Z') p++;
  if (*p != '\0') return false;
  static const int8_t dim[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (y < 1970 || mo < 1 || mo > 12 || h > 23 || mi > 59 || s > 59 || d < 1) return false;
  int maxd = dim[mo - 1] + ((mo == 2 && botIsLeapYear(y)) ? 1 : 0);
  if (d > maxd) return false;
  int64_t t = botDaysFromCivil(y, mo, d) * 86400 + h * 3600 + mi * 60 + s;
  if (t < 0 || t > (int64_t)0xFFFFFFFFu) return false;
  out = (uint32_t)t;
  return true;
}

// Local weekday and time as "Sat 14:00" for a unix time and a UTC offset in seconds. Returns the
// bytes written (cut on a UTF-8 boundary if cap is too small; "" when cap is 0).
static inline size_t botWeekdayHm(uint32_t unix_s, int32_t utc_offset_s, char* out, size_t cap) {
  if (!out || cap == 0) return 0;
  static const char* const names[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  int64_t local = (int64_t)unix_s + utc_offset_s;
  int64_t days = local >= 0 ? local / 86400 : -((-local + 86399) / 86400);
  int64_t sod = local - days * 86400;
  int wd = (int)(((days + 4) % 7 + 7) % 7);  // 1970-01-01 was a Thursday
  char t[16];
  snprintf(t, sizeof t, "%s %02d:%02d", names[wd], (int)(sod / 3600), (int)(sod % 3600 / 60));
  return botClamp(out, cap, t, cap - 1);
}
