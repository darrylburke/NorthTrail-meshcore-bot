#pragma once

#include <math.h>
#include <stdint.h>

// Location and astronomy maths for the chat bot (pure C/C++, no Arduino dependencies; host-testable).

static const double BOT_PI = 3.14159265358979323846;

static inline double botDeg2Rad(double d) { return d * (BOT_PI / 180.0); }

// Great-circle distance in km (haversine, Earth radius 6371.0 km). Longitude differences wrap
// naturally, so the short way across the antimeridian is used.
static inline double botDistanceKm(double lat1, double lon1, double lat2, double lon2) {
  double p1 = botDeg2Rad(lat1), p2 = botDeg2Rad(lat2);
  double dp = p2 - p1, dl = botDeg2Rad(lon2 - lon1);
  double s1 = sin(dp / 2), s2 = sin(dl / 2);
  double a = s1 * s1 + cos(p1) * cos(p2) * s2 * s2;
  if (a > 1.0) a = 1.0;  // rounding guard for antipodes
  return 2.0 * 6371.0 * asin(sqrt(a));
}

// Initial bearing from point 1 to point 2 as one of 8 compass points (45-degree sectors
// centred on the points). Identical points give atan2(0,0) = 0, so "N".
static inline const char* botBearing8(double lat1, double lon1, double lat2, double lon2) {
  static const char* const names[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  double p1 = botDeg2Rad(lat1), p2 = botDeg2Rad(lat2), dl = botDeg2Rad(lon2 - lon1);
  double y = sin(dl) * cos(p2);
  double x = cos(p1) * sin(p2) - sin(p1) * cos(p2) * cos(dl);
  double deg = atan2(y, x) * (180.0 / BOT_PI);
  if (deg < 0) deg += 360.0;
  int idx = (int)floor(deg / 45.0 + 0.5) & 7;  // 360 -> 8 -> 0
  return names[idx];
}

// Day of year (1-based) for a day number counted from 1970-01-01 (Hinnant civil-from-days).
static inline int botDayOfYear(uint32_t days_since_epoch) {
  int64_t z = (int64_t)days_since_epoch + 719468;
  int64_t era = z / 146097;
  int64_t doe = z - era * 146097;
  int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int64_t doy_mar = doe - (365 * yoe + yoe / 4 - yoe / 100);  // 0-based, year starts 1 March
  int64_t y = yoe + era * 400;
  if ((5 * doy_mar + 2) / 153 >= 10) y++;  // Jan/Feb belong to the next calendar year
  // Days from 1970-01-01 to y-01-01, so the answer is the plain difference.
  int64_t ym = y - 1, e2 = ym / 400, yo2 = ym - e2 * 400;
  int64_t jan1 = e2 * 146097 + (yo2 * 365 + yo2 / 4 - yo2 / 100 + 306) - 719468;
  return (int)((int64_t)days_since_epoch - jan1 + 1);
}

// Sunrise and sunset as unix times for the UTC calendar day containing utc_now (NOAA general
// solar position equations, zenith 90.833 degrees, solar noon of that day). Times are true
// unix times and are NOT wrapped into the day: for western longitudes sunset can land after
// 24:00 UTC, for eastern ones sunrise before 00:00. Returns false in polar day or night
// (rise_utc/set_utc untouched). lon is degrees east.
static inline bool botSunTimes(double lat, double lon, uint32_t utc_now, uint32_t& rise_utc, uint32_t& set_utc) {
  uint32_t day = utc_now / 86400u;
  double day0 = (double)day * 86400.0;
  int doy = botDayOfYear(day);
  double g = 2.0 * BOT_PI / 365.0 * (doy - 1);  // fractional year at noon (hour 12)
  double eqtime = 229.18 * (0.000075 + 0.001868 * cos(g) - 0.032077 * sin(g)
                            - 0.014615 * cos(2 * g) - 0.040849 * sin(2 * g));
  double decl = 0.006918 - 0.399912 * cos(g) + 0.070257 * sin(g) - 0.006758 * cos(2 * g)
                + 0.000907 * sin(2 * g) - 0.002697 * cos(3 * g) + 0.00148 * sin(3 * g);
  double la = botDeg2Rad(lat);
  double c = cos(botDeg2Rad(90.833)) / (cos(la) * cos(decl)) - tan(la) * tan(decl);
  if (!(c >= -1.0 && c <= 1.0)) return false;  // also rejects NaN (lat = +-90)
  double ha = acos(c) * (180.0 / BOT_PI);
  double rise_min = 720.0 - 4.0 * (lon + ha) - eqtime;
  double set_min = 720.0 - 4.0 * (lon - ha) - eqtime;
  double r = floor(day0 + rise_min * 60.0 + 0.5), st = floor(day0 + set_min * 60.0 + 0.5);
  rise_utc = r < 0 ? 0u : (uint32_t)r;  // clock near epoch (unset): clamp, a negative cast is UB
  set_utc = st < 0 ? 0u : (uint32_t)st;
  return true;
}

// Moon phase name from a 29.530588853-day cycle anchored at the new moon of 2000-01-06 18:14
// UTC (unix 947182440). Eight equal segments centred on the named phases, so "New" covers the
// last and first sixteenth of the cycle. Times before the anchor work (fmod fixed to >= 0).
static inline const char* botMoonPhase(uint32_t utc_now) {
  static const char* const names[8] = {"New", "Waxing Crescent", "First Quarter", "Waxing Gibbous",
                                       "Full", "Waning Gibbous", "Last Quarter", "Waning Crescent"};
  const double period = 29.530588853 * 86400.0;
  double t = (double)utc_now - 947182440.0;
  double f = fmod(t, period) / period;
  if (f < 0) f += 1.0;
  return names[(int)floor(f * 8.0 + 0.5) & 7];
}
