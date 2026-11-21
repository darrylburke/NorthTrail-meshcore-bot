#pragma once

// Small helpers shared by BotWeb.cpp and BotWebSetup.cpp (not part of any public interface).

#ifdef WITH_BOT

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <helpers/bot/BotWebRules.h>
#include "BotWipe.h"

namespace botweb {

struct Lock {
  SemaphoreHandle_t m;
  explicit Lock(SemaphoreHandle_t mux) : m(mux) { xSemaphoreTake(m, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(m); }
};

// Bounded JSON text writer; ok goes false (and stays) once something does not fit.
struct JW {
  char* b; size_t cap; size_t n; bool ok;
  JW(char* buf, size_t c) : b(buf), cap(c), n(0), ok(true) { if (cap) b[0] = 0; }
  void raw(const char* s) {
    size_t l = strlen(s);
    if (!ok || n + l + 1 > cap) { ok = false; return; }
    memcpy(b + n, s, l + 1);
    n += l;
  }
  void str(const char* s) {
    if (!ok) return;
    size_t w = botJsonEscape(b + n, cap - n, s);
    if (w == 0) { ok = false; if (n < cap) b[n] = 0; return; }
    n += w;
  }
  void key(const char* k) { raw("\""); raw(k); raw("\":"); }
  void kstr(const char* k, const char* v) { key(k); str(v); raw(","); }
  void kint(const char* k, long long v) { char t[24]; snprintf(t, sizeof(t), "%lld", v); key(k); raw(t); raw(","); }
  void kbool(const char* k, bool v) { key(k); raw(v ? "true," : "false,"); }
  void endObj() { if (ok && n && b[n - 1] == ',') b[--n] = 0; raw("}"); }   // drops a trailing comma
  void endArr() { if (ok && n && b[n - 1] == ',') b[--n] = 0; raw("]"); }
};

// ArduinoJson allocator that zeroes every block before freeing it, for documents that hold
// passwords (login, password change, setup, settings). Shared with BotStore (BotWipe.h).
using WipingAllocator = ::BotWipingAllocator;

inline void sendJson(AsyncWebServerRequest* r, int code, const char* body) {
  r->send(code, "application/json", body);
}

inline void sendErr(AsyncWebServerRequest* r, int code, const char* msg) {
  char buf[200];
  JW w(buf, sizeof(buf));
  w.raw("{\"error\":");
  w.str(msg);
  w.raw("}");
  sendJson(r, code, w.ok ? buf : "{\"error\":\"error\"}");
}

// A body that did not parse: "low memory" (503, worth retrying) or "bad request".
inline void sendParseErr(AsyncWebServerRequest* r, DeserializationError e) {
  if (e == DeserializationError::NoMemory) sendErr(r, 503, "low memory, try again");
  else sendErr(r, 400, "bad request");
}

inline void sendId(AsyncWebServerRequest* r, uint32_t id) {
  char body[32];
  snprintf(body, sizeof(body), "{\"id\":%u}", (unsigned)id);
  sendJson(r, 202, body);
}

}  // namespace botweb

#endif  // WITH_BOT
