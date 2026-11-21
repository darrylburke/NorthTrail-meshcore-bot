#pragma once

// The bot's HTTP(S) worker: one FreeRTOS task that performs one GET at a time, so the radio loop
// never waits on DNS, a TLS handshake or a slow server.
//
// THREAD RULE: begin(), busy(), submit(), poll() and the status getters run on the Arduino loop
// task only. The worker task shares exactly three things with it, and two atomic flags hand them
// back and forth; no mutex is used and none is held across a network call:
//   - _url and _req_seq: written by the loop in submit() while the worker is idle (_pending
//     false), read by the worker while _pending is true;
//   - the body buffer and the result fields (_len, _status, _outcome, _ms, _dns_ms, _conn_ms,
//     _xchg_ms, _res_seq): written by
//     the worker while _pending is true, read by the loop only after the worker has set _done
//     and then cleared _pending;
//   - _pending (loop sets it in submit(), the worker clears it when finished) and _done (the
//     worker sets it before clearing _pending, the loop clears it in poll()).
// The worker never touches bot state. It speaks a minimal HTTP/1.0 GET itself (BotHttp.h): no
// HTTPClient, no String, nothing the peer can grow. Its stack high-water mark is read by the loop with
// uxTaskGetStackHighWaterMark, which needs nothing shared.

#ifdef WITH_BOT

#include <Arduino.h>
#include <atomic>

#define BOT_FETCH_URL_MAX 319          // bytes of a URL
#define BOT_FETCH_BODY_MAX 16384       // bytes of a response body (one buffer, allocated in begin())
#define BOT_FETCH_STACK 12288          // worker stack (see BotFetch.cpp)
#define BOT_FETCH_CONNECT_MS 4000u     // TCP connect (and the TLS write timeout)
#define BOT_FETCH_HANDSHAKE_S 8u       // TLS handshake (measured on the device: 4 to 5 s for a whole request)
#define BOT_FETCH_TOTAL_MS 10000u      // from the connection attempt to the last body byte (a connect plus
                                       // handshake that uses it up ends the request before it is sent)
#define BOT_FETCH_MIN_HEAP 55000u      // free heap needed before a TLS request
#define BOT_FETCH_MIN_BLOCK 20000u     // largest free block needed before a TLS request

enum BotFetchOutcome : uint8_t {
  BFO_NONE,        // nothing done yet
  BFO_OK,          // status 200 and the whole body read
  BFO_HTTP,        // a status other than 200 (http_status holds it)
  BFO_TIMEOUT,     // the 10 s from the connection attempt ran out (BOT_FETCH_TOTAL_MS)
  BFO_TOO_LARGE,   // the body is over BOT_FETCH_BODY_MAX
  BFO_LOW_MEMORY,  // not enough heap for TLS: refused before connecting
  BFO_NO_WIFI,     // the station is not connected
  BFO_ERROR,       // the request could not be sent, or the connection closed early
  BFO_BAD_URL,     // the URL could not be split into host, port and path
  BFO_DNS,         // the host name did not resolve
  BFO_CONNECT,     // TCP connect or TLS handshake (including certificate verification) failed
  BFO_BAD_HEADERS, // not HTTP, a header line over 256 bytes, headers over 2 KB, or a compressed body
  BFO_CHUNKED,     // a chunked (or other non-identity) transfer encoding
  BFO_NO_CERTS,    // https asked for but no certificate bundle is embedded
};

struct BotFetchResult {
  bool done;          // false: nothing new (still running, or nothing was submitted)
  bool ok;            // outcome == BFO_OK
  int http_status;    // the HTTP status when a status line was read, else 0
  const char* body;   // NUL-terminated; valid until the next submit()
  size_t len;
  uint32_t seq;       // the sequence number submit() gave this request
  uint8_t outcome;    // BotFetchOutcome
  uint32_t ms;        // how long the request took in all
  uint32_t dns_ms;    // of which: resolving the host name
  uint32_t conn_ms;   //   TCP connect plus (https) the TLS handshake
  uint32_t xchg_ms;   //   sending the request and reading the reply
};

class BotFetch {
public:
  BotFetch();
  bool begin();                                    // allocates the body buffer, creates the task; false if it could not
  bool ready() const { return _task != nullptr; }
  bool busy() const;                               // a request is running or its result has not been polled
  // Copies the URL (normalised: see botFetchNormalizeUrl) and wakes the worker. False when busy,
  // not ready, not http:// or https://, or longer than BOT_FETCH_URL_MAX. *seq gets the request's
  // sequence number (never 0).
  bool submit(const char* url, uint32_t* seq = nullptr);
  BotFetchResult poll();                           // done=true once per finished request
  uint32_t stackFreeMin() const;                   // worker stack never used so far (bytes), 0 if no task
  static const char* outcomeText(uint8_t outcome);
  static const char* outcomeCode(uint8_t outcome);   // at most 6 characters (the status line)

private:
  static void taskEntry(void* arg);
  void work();       // worker task: one request

  TaskHandle_t _task;
  uint32_t _seq;     // loop only: last sequence number given out

  // Shared, see the thread rule above.
  char _url[BOT_FETCH_URL_MAX + 1];
  uint32_t _req_seq;
  char* _body;
  size_t _len;
  int _status;
  uint8_t _outcome;
  uint32_t _ms;
  uint32_t _dns_ms, _conn_ms, _xchg_ms;
  uint32_t _res_seq;
  std::atomic<bool> _pending;
  std::atomic<bool> _done;
};

#endif  // WITH_BOT
