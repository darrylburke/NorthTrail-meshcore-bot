'use strict';
// Fetches a dad joke from icanhazdadjoke.com that fits a mesh message. No MQTT in this file.

const JOKE_URL = 'https://icanhazdadjoke.com/';
const USER_AGENT = 'meshcore-bot-extras (MQTT responder)';
const FETCH_MS = 1300;           // per try; three tries stay inside the bot's 5 second wait
const TRIES = 3;

// A joke as one line: control characters and line breaks become spaces, braces are made harmless
// for the bot (it fills {placeholders} in replies).
function tidy(text) {
  return String(text).replace(/[{}]/g, (c) => (c === '{' ? '(' : ')'))
    .replace(/[\u0000-\u001f\u007f]+/g, ' ').replace(/ {2,}/g, ' ').trim();
}

// Cuts text to at most max bytes of UTF-8 without splitting a character.
function clampBytes(text, max) {
  const buf = Buffer.from(text, 'utf8');
  if (buf.length <= max) return text;
  let end = max;
  while (end > 0 && (buf[end] & 0xC0) === 0x80) end--;
  return buf.subarray(0, end).toString('utf8').trimEnd();
}

async function fetchJoke(fetchFn = fetch) {
  const res = await fetchFn(JOKE_URL, { headers: { Accept: 'text/plain', 'User-Agent': USER_AGENT }, signal: AbortSignal.timeout(FETCH_MS) });
  if (!res.ok) throw new Error('HTTP ' + res.status);
  return tidy(await res.text());
}

// A joke of at most limit bytes: asks again (up to TRIES times) when one is too long, and cuts the
// shortest one seen if none fits. Returns '' when the site gave nothing.
async function jokeThatFits(limit, getJoke = fetchJoke) {
  let shortest = '';
  for (let i = 0; i < TRIES; i++) {
    let j = '';
    try { j = await getJoke(); } catch (e) { continue; }
    if (!j) continue;
    if (Buffer.byteLength(j) <= limit) return j;
    if (!shortest || Buffer.byteLength(j) < Buffer.byteLength(shortest)) shortest = j;
  }
  return shortest ? clampBytes(shortest, limit) : '';
}

module.exports = { tidy, clampBytes, fetchJoke, jokeThatFits, JOKE_URL };
