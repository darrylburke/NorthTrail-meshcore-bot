#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <ArduinoJson.h>
#include "BotText.h"
#include "BotUrl.h"
#include "BotReplies.h"
#include "BotJsonPath.h"

// Weather and place look-up for the chat bot (Open-Meteo; pure C/C++ apart from ArduinoJson,
// no heap of its own, host-testable): request URLs, JSON filters, parsers and the reply wording.

struct BotWxNow {
  float temp, feels, wind_kmh;
  int rh;           // percent, -1 when unknown
  int wind_deg;     // 0-359, -1 when unknown
  int code;         // WMO weather code
  float tmin, tmax;
  int precip_pct;   // today's highest precipitation probability
  bool has_daily;   // tmin, tmax and precip_pct are valid
};

struct BotPlace { char name[40]; double lat, lon; };

// Decisions: false for a coordinate that is not finite or out of range, or a URL that does not
// fit; botGeoUrl also refuses an empty place and (via botUrlBuild) cuts the place to 60 bytes.
static inline bool botWxUrl(char* dst, size_t cap, double lat, double lon) {
  if (!dst || cap == 0) return false;
  dst[0] = '\0';
  char a[24], b[24];
  if (!botValidLatLon(lat, lon) || !botUrlFmtCoord(a, sizeof a, lat) || !botUrlFmtCoord(b, sizeof b, lon)) return false;
  int k = snprintf(dst, cap,
      "https://api.open-meteo.com/v1/forecast?latitude=%s&longitude=%s"
      "&current=temperature_2m,apparent_temperature,relative_humidity_2m,weather_code,wind_speed_10m,wind_direction_10m"
      "&daily=temperature_2m_max,temperature_2m_min,precipitation_probability_max&timezone=auto&forecast_days=1",
      a, b);
  if (k < 0 || (size_t)k >= cap) { dst[0] = '\0'; return false; }
  return true;
}

static inline bool botGeoUrl(char* dst, size_t cap, const char* place) {
  if (!dst || cap == 0) return false;
  dst[0] = '\0';
  if (!place || !*place) return false;
  return botUrlBuild(dst, cap,
      "https://geocoding-api.open-meteo.com/v1/search?name={args}&count=1&language=en&format=json", place);
}

// Filters keep only what the parsers read (use with DeserializationOption::Filter).
static inline void botWxFilter(JsonDocument& f) {
  f.clear();
  f["current"]["temperature_2m"] = true;
  f["current"]["apparent_temperature"] = true;
  f["current"]["relative_humidity_2m"] = true;
  f["current"]["weather_code"] = true;
  f["current"]["wind_speed_10m"] = true;
  f["current"]["wind_direction_10m"] = true;
  f["daily"]["temperature_2m_max"][0] = true;
  f["daily"]["temperature_2m_min"][0] = true;
  f["daily"]["precipitation_probability_max"][0] = true;
}

static inline void botGeoFilter(JsonDocument& f) {
  f.clear();
  f["results"][0]["name"] = true;
  f["results"][0]["latitude"] = true;
  f["results"][0]["longitude"] = true;
}

// Rounds half away from zero after clamping to +-lim; never returns a negative zero.
static inline long botWxRound(double v, long lim) {
  if (!(v == v)) return 0;
  if (v > (double)lim) v = (double)lim;
  if (v < -(double)lim) v = -(double)lim;
  long r = (long)floor((v < 0 ? -v : v) + 0.5);
  return v < 0 ? -r : r;
}

// A JSON number within [lo, hi] (so every later narrowing conversion is defined).
static inline bool botWxNumIn(JsonVariantConst v, double lo, double hi, double& out) {
  double d;
  if (!botJsonNum(v, d) || d < lo || d > hi) return false;
  out = d;
  return true;
}

// Needs current.temperature_2m (-100..70), weather_code (0..999) and wind_speed_10m (0..500) as
// JSON numbers in range; anything else fails the parse. Decisions: apparent_temperature (-100..70)
// defaults to the temperature, relative_humidity_2m (0..100) to -1 and wind_direction_10m
// (0..360) to -1 when missing, not a number or out of range; has_daily needs a number in range
// (temperatures -100..70, precipitation 0..100) as the first element of all three daily arrays.
static inline bool botWxParse(JsonVariantConst root, BotWxNow& out) {
  if (!root.is<JsonObjectConst>()) return false;
  JsonVariantConst cur = root["current"];
  if (!cur.is<JsonObjectConst>()) return false;
  double t, code, wind;
  if (!botWxNumIn(cur["temperature_2m"], -100, 70, t) || !botWxNumIn(cur["weather_code"], 0, 999, code) ||
      !botWxNumIn(cur["wind_speed_10m"], 0, 500, wind))
    return false;
  BotWxNow w;
  w.temp = (float)t;
  w.code = (int)botWxRound(code, 100000);
  w.wind_kmh = (float)wind;
  double v;
  w.feels = botWxNumIn(cur["apparent_temperature"], -100, 70, v) ? (float)v : w.temp;
  w.rh = botWxNumIn(cur["relative_humidity_2m"], 0, 100, v) ? (int)botWxRound(v, 100) : -1;
  w.wind_deg = botWxNumIn(cur["wind_direction_10m"], 0, 360, v) ? (int)botWxRound(v, 360) % 360 : -1;
  JsonVariantConst day = root["daily"];
  double hi, lo, pr;
  w.has_daily = botWxNumIn(day["temperature_2m_max"][0], -100, 70, hi) && botWxNumIn(day["temperature_2m_min"][0], -100, 70, lo) &&
                botWxNumIn(day["precipitation_probability_max"][0], 0, 100, pr);
  w.tmax = w.has_daily ? (float)hi : 0;
  w.tmin = w.has_daily ? (float)lo : 0;
  w.precip_pct = w.has_daily ? (int)botWxRound(pr, 100) : 0;
  out = w;
  return true;
}

// Fails when there is no results[0] with a non-empty name and in-range coordinates.
static inline bool botGeoParse(JsonVariantConst root, BotPlace& out) {
  if (!root.is<JsonObjectConst>()) return false;
  JsonVariantConst r = root["results"][0];
  if (!r.is<JsonObjectConst>()) return false;
  const char* name = botJsonStr(r["name"]);
  double lat, lon;
  if (!name || !botJsonNum(r["latitude"], lat) || !botJsonNum(r["longitude"], lon) || !botValidLatLon(lat, lon))
    return false;
  BotPlace p;
  botCopyClean(p.name, sizeof p.name, name);
  size_t a = 0, e = strlen(p.name);          // trim spaces (control characters became spaces)
  while (e > a && p.name[e - 1] == ' ') e--;
  while (a < e && p.name[a] == ' ') a++;
  if (e == a) return false;
  memmove(p.name, p.name + a, e - a);
  p.name[e - a] = '\0';
  p.lat = lat;
  p.lon = lon;
  out = p;
  return true;
}

// Text for a WMO weather code. An unknown code is written as "code <n>" into the caller's scratch
// buffer (cap >= 16 holds any int; a smaller one cuts it), so there is no shared state. Null
// scratch gives "code" for an unknown code.
static inline const char* botWmoText(int code, char* scratch, size_t cap) {
  switch (code) {
    case 0: return "clear";
    case 1: return "mostly clear";
    case 2: return "partly cloudy";
    case 3: return "overcast";
    case 45: case 48: return "fog";
    case 51: case 53: case 55: return "drizzle";
    case 56: case 57: return "freezing drizzle";
    case 61: return "light rain";
    case 63: return "rain";
    case 65: return "heavy rain";
    case 66: case 67: return "freezing rain";
    case 71: return "light snow";
    case 73: return "snow";
    case 75: return "heavy snow";
    case 77: return "snow grains";
    case 80: case 81: return "showers";
    case 82: return "heavy showers";
    case 85: case 86: return "snow showers";
    case 95: return "thunderstorm";
    case 96: case 99: return "thunderstorm, hail";
    default: break;
  }
  if (!scratch || cap == 0) return "code";
  snprintf(scratch, cap, "code %d", code);
  return scratch;
}

// Compass point for a bearing in degrees (any integer; wraps).
static inline const char* botWind8(int deg) {
  static const char* const names[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  long d = ((long)deg % 360 + 360) % 360;
  return names[((d * 2 + 45) / 90) & 7];
}

static inline void botWxTemp(BotOut& o, float t) {
  botOutInt(o, botWxRound(t, 1000));
  botOutAppend(o, "\xC2\xB0" "C");
}

// "Toronto: 6°C feels 4, clear, wind N 3 km/h, 79% RH | today 4/15°C, 0% precip" without the
// mention. Segments (place..condition, wind, humidity, today) are dropped from the right to fit
// max_bytes (and cap-1); if even the first does not fit it is cut with botClamp. Null or empty
// place prints "Here".
static inline size_t botWxFormat(char* dst, size_t cap, const char* place, const BotWxNow& w, size_t max_bytes) {
  if (!dst || cap == 0) return 0;
  dst[0] = '\0';
  size_t limit = max_bytes < cap - 1 ? max_bytes : cap - 1;
  size_t n = 0;
  char seg[128];
  BotOut o = botOutInit(seg, sizeof seg);
  botOutAppend(o, (place && *place) ? place : "Here");
  botOutAppend(o, ": ");
  botWxTemp(o, w.temp);
  long feels = botWxRound(w.feels, 1000);
  if (feels != botWxRound(w.temp, 1000)) { botOutAppend(o, " feels "); botOutInt(o, feels); }
  botOutAppend(o, ", ");
  char scratch[16];
  botOutAppend(o, botWmoText(w.code, scratch, sizeof scratch));
  if (!botAddSegment(dst, cap, n, limit, seg)) return n;

  o = botOutInit(seg, sizeof seg);
  long kmh = botWxRound(w.wind_kmh, 100000);
  if (!(w.wind_kmh >= 1.0f)) {   // under 1 km/h (also NaN and negative)
    botOutAppend(o, ", calm");
  } else {
    botOutAppend(o, ", wind ");
    if (w.wind_deg >= 0) { botOutAppend(o, botWind8(w.wind_deg)); botOutAppend(o, " "); }
    botOutInt(o, kmh);
    botOutAppend(o, " km/h");
  }
  if (!botAddSegment(dst, cap, n, limit, seg)) return n;

  if (w.rh >= 0) {
    o = botOutInit(seg, sizeof seg);
    botOutAppend(o, ", ");
    botOutInt(o, w.rh > 100 ? 100 : w.rh);
    botOutAppend(o, "% RH");
    if (!botAddSegment(dst, cap, n, limit, seg)) return n;
  }

  if (w.has_daily) {
    o = botOutInit(seg, sizeof seg);
    botOutAppend(o, " | today ");
    botOutInt(o, botWxRound(w.tmin, 1000));
    botOutAppend(o, "/");
    botWxTemp(o, w.tmax);
    botOutAppend(o, ", ");
    botOutInt(o, w.precip_pct < 0 ? 0 : (w.precip_pct > 100 ? 100 : w.precip_pct));
    botOutAppend(o, "% precip");
    botAddSegment(dst, cap, n, limit, seg);
  }
  return n;
}
