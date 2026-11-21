#ifdef WITH_BOT

// "Try a command" on the web page: POST /api/try queues the text, the loop task runs it through
// BotTask::tryCommand() (nothing is transmitted), and GET /api/try reads the reply back.
// See the thread model in BotWeb.h: the handlers only read the try record and use the single
// request slot, both under _mux; BotTask is called only from loop().

#include "BotWeb.h"
#include "BotTask.h"
#include "BotWebInternal.h"

using namespace botweb;

// ---- loop task ----

// Hands the queued text to the bot. When the bot is still running an earlier try (one a later
// POST replaced here), this one is answered "busy" at once.
void BotWeb::applyTryReq() {
  bool started = _bot->tryCommand(_pend_try, _pend_id);
  if (started) return;
  Lock l(_mux);
  if (_try_id == _pend_id) {
    _try_done = true;
    _try_busy = true;
    _try_broker = false;
    _try_reply[0] = 0;
  }
}

// Picks up a finished try. One whose id is no longer the most recent (replaced by a newer
// POST) is dropped.
void BotWeb::pollTry() {
  uint32_t id = 0;
  char text[TRY_REPLY];
  bool broker = false;
  if (!_bot->takeTryResult(id, text, sizeof(text), &broker)) return;
  Lock l(_mux);
  if (id != _try_id || _try_done) return;
  StrHelper::strncpy(_try_reply, text, sizeof(_try_reply));
  _try_busy = false;
  _try_broker = broker;
  _try_done = true;
}

// ---- async_tcp task ----

// {"id":n,"done":false} while it runs, then {"id":n,"done":true,"reply":"..."} ("" = the bot
// would stay silent), with "busy":true when the bot could not take it and "broker":true when the
// MQTT broker answered it. {"id":0,...} before the first try.
void BotWeb::writeTry(char* dst, size_t cap) {
  JW w(dst, cap);
  w.raw("{");
  w.kint("id", _try_id);
  w.kbool("done", _try_done);
  if (_try_done) {
    if (_try_busy) w.kbool("busy", true);
    if (_try_broker) w.kbool("broker", true);
    w.kstr("reply", _try_reply);
  }
  w.endObj();
  if (!w.ok) snprintf(dst, cap, "{\"id\":%u,\"done\":false}", (unsigned)_try_id);   // cannot happen: TRY_JSON_CAP
}

void BotWeb::handleTry(AsyncWebServerRequest* r) {
  if (!r->_tempObject) { sendErr(r, 400, "bad request"); return; }
  if (!authed(r, nullptr, true)) { sendErr(r, 401, "please sign in"); return; }
  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, (const char*)r->_tempObject);
  if (e) { sendParseErr(r, e); return; }
  const char* text = botJsonStr(doc["text"]);
  if (!botTryTextOk(text)) { sendErr(r, 400, "the command must be 1 to 120 characters, on one line"); return; }
  uint32_t id;
  {
    Lock l(_mux);
    if (_pend_kind != K_NONE) { sendErr(r, 503, "busy, try again"); return; }
    StrHelper::strncpy(_pend_try, text, sizeof(_pend_try));
    id = enqueue(K_TRY);
  }
  sendId(r, id);
}

// A polling read: it does not refresh the session's idle timer.
void BotWeb::handleTryGet(AsyncWebServerRequest* r) {
  if (!authed(r, nullptr, false)) { sendErr(r, 401, "please sign in"); return; }
  char body[TRY_JSON_CAP];
  {
    Lock l(_mux);
    writeTry(body, sizeof(body));
  }
  sendJson(r, 200, body);
}

#endif  // WITH_BOT
