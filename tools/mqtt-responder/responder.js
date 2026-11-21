#!/usr/bin/env node
'use strict';
/*
 * Test responder for the MeshCore bot's MQTT requests.
 *
 * The bot publishes a request for a command it does not know:
 *   <prefix>/request            {"id":"8f2a1c3e","reply_to":"<prefix>/reply/8f2a1c3e","cmd":"beer","args":"ipa",...}
 * and waits a few seconds for an answer on:
 *   <prefix>/reply/<id>         {"text":"..."}
 * A program may also announce commands on <prefix>/commands/<name> with {"desc":"..."}.
 *
 * This program answers the commands you name on the command line, and can pretend to be the
 * bot (--simulate) so a responder can be tested without a radio. Run with --help for the options.
 *
 * The request fields, the limits and the placeholder names below were checked against
 * src/helpers/bot/BotMqttMsg.h, BotMqttTopics.h and BotTemplate.h.
 */

const fs = require('fs');
const crypto = require('crypto');

// Limits of the firmware (BotMqttMsg.h).
const REPLY_PAYLOAD_MAX = 512;   // a larger reply payload is ignored by the bot
const REPLY_TEXT_MAX = 200;      // longest reply text the bot accepts (it refuses a longer one)
const ANNOUNCE_PAYLOAD_MAX = 256;
const ANNOUNCE_DESC_MAX = 48;    // the bot cuts a longer description
const REQUEST_MAX = 1000;      // largest request the bot publishes
const CONNECT_DEADLINE_MS = 10000;   // the simulator gives up when it cannot publish by then
const NAME_RE = /^[a-z0-9]{1,16}$/;   // a command name, and the id in a reply topic (BotMqttTopics.h)
const PREFIX_RE = /^[A-Za-z0-9_-]+(\/[A-Za-z0-9_-]+)*$/;   // botMqttPrefixValid, up to 64 bytes

const HELP = `
Usage:
  node responder.js [connection] --cmd NAME=REPLY [--cmd NAME=REPLY ...] [options]
  node responder.js [connection] --simulate "NAME ARGS" [options]

Connection:
  --host HOST            Broker host                         (default: localhost)
  --port PORT            Broker port                         (default: 1883)
  --tls                  Use TLS (mqtts)
  --user NAME            Username
  --pass-file FILE       File holding the password: the "xxx" of a line that starts
                         "password: xxx" if there is one, otherwise the first line.
  --pass PASSWORD        Password on the command line. It shows in 'ps' and in your shell
                         history: prefer --pass-file or the MQTT_PASS environment variable.
  --prefix TOPIC         Topic prefix, as set on the bot's page (default: meshcore/MeshBot)

  The password may also come from the MQTT_PASS environment variable. It is never printed.

Responder:
  --cmd NAME=REPLY       Answer command NAME with REPLY. Repeat for more commands.
                         NAME is 1 to 16 letters or digits, matched without regard to case.
                         REPLY may contain:
                           placeholders the BOT fills in:   {sender} {hops} {snr} {rssi}
                                                            {path} {args} {time} {date}
                                                            {channel} {uptime}
                           placeholders THIS PROGRAM fills: %args% %sender% %cmd% %now%
                                                            %field:NAME%  (any request field)
  --any REPLY            Answer every command not matched by --cmd with REPLY
  --desc NAME=TEXT       One-line description for a command, shown in the bot's help
                         (cut to 48 bytes by the bot)
  --announce             Publish each command and description (retained) so the bot lists
                         it in help; cleared again when this program exits
  --delay MS             Wait MS milliseconds before replying (to test the bot's wait time)
  --once                 Exit after the first reply
  --max-len N            Cut replies to N bytes when the request has no max_len (default 100)
                         A reply is never longer than 200 bytes, and is cut to the
                         request's max_len when that is smaller. With --simulate it is
                         the max_len put in the request.
  --raw-braces           Copy { and } in %args%, %sender%, %cmd% and %field:NAME% unchanged. By
                         default they become ( and ), so text typed on the mesh cannot make
                         the bot expand its own {placeholders}. Braces you write in the
                         REPLY itself are never changed.

Simulator (acts as the bot):
  --simulate "NAME ARGS" Publish one request for this command and print the reply
  --dm                   Simulate a direct message from a known contact (12-hex sender_key,
                         positions, empty channel). Default: a channel message (empty
                         sender_key, positions 0, channel "public").
  --sender NAME          Sender name to put in the simulated request (default: Tester)
  --wait MS              How long to wait for a reply           (default: 5000)

Other:
  --quiet                Print replies only
  --help                 Show this text

Exit codes (simulator): 0 reply received, 1 broker or publish error, 2 usage error,
  3 no reply within --wait, 4 a reply the bot would ignore, 5 not connected and the
  request not published within 10 seconds.

Examples:
  node responder.js --host broker.example.org --user bot --pass-file ./mqtt.txt \\
       --cmd "beer=Cheers {sender}, one %args% coming up" --desc "beer=order a drink" --announce

  node responder.js --host broker.example.org --user bot --pass-file ./mqtt.txt \\
       --simulate "beer ipa"
`;

class UsageError extends Error {}

function parseArgs(argv) {
  const die = (msg) => { throw new UsageError(msg); };
  const o = { host: 'localhost', port: 1883, tls: false, prefix: 'meshcore/MeshBot',
              cmds: new Map(), descs: new Map(), any: null, announce: false, delay: 0, once: false,
              maxLen: 100, simulate: null, sender: 'Tester', wait: 5000, quiet: false, help: false, dm: false, rawBraces: false };
  const need = (i, name) => { if (i + 1 >= argv.length) die(name + ' needs a value'); return argv[i + 1]; };
  const pair = (text, name) => {
    const eq = text.indexOf('=');
    if (eq < 1) die(name + ' expects NAME=TEXT, got "' + text + '"');
    return [text.slice(0, eq).trim().toLowerCase(), text.slice(eq + 1)];
  };
  const int = (text, name, min, max) => {
    const n = /^\d+$/.test(text) ? Number(text) : NaN;
    if (!Number.isInteger(n) || n < min || n > max) die(name + ' must be a whole number from ' + min + ' to ' + max);
    return n;
  };
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    switch (a) {
      case '--help': case '-h': o.help = true; return o;
      case '--host': o.host = need(i, a); i++; break;
      case '--port': o.port = int(need(i, a), a, 1, 65535); i++; break;
      case '--tls': o.tls = true; break;
      case '--user': o.user = need(i, a); i++; break;
      case '--pass': o.pass = need(i, a); i++; break;
      case '--pass-file': o.passFile = need(i, a); i++; break;
      case '--prefix': o.prefix = need(i, a); i++; break;
      case '--cmd': { const [k, v] = pair(need(i, a), a); o.cmds.set(k, v); i++; break; }
      case '--desc': { const [k, v] = pair(need(i, a), a); o.descs.set(k, v); i++; break; }
      case '--any': o.any = need(i, a); i++; break;
      case '--announce': o.announce = true; break;
      case '--delay': o.delay = int(need(i, a), a, 0, 60000); i++; break;
      case '--once': o.once = true; break;
      case '--max-len': o.maxLen = int(need(i, a), a, 1, REPLY_TEXT_MAX); i++; break;
      case '--simulate': o.simulate = need(i, a); i++; break;
      case '--sender': o.sender = need(i, a); i++; break;
      case '--wait': o.wait = int(need(i, a), a, 100, 60000); i++; break;
      case '--quiet': o.quiet = true; break;
      case '--dm': o.dm = true; break;
      case '--raw-braces': o.rawBraces = true; break;
      default: die('unknown option ' + a);
    }
  }
  if (!validPrefix(o.prefix)) die('--prefix must be 1 to 64 letters, digits, _ - and / (no leading, trailing or doubled /, no wildcards)');
  if (o.simulate === null && o.cmds.size === 0 && o.any === null) die('give at least one --cmd NAME=REPLY, --any REPLY, or --simulate "NAME ARGS"');
  for (const name of o.cmds.keys()) if (!NAME_RE.test(name)) die('command name "' + name + '" must be 1 to 16 letters or digits');
  for (const name of o.descs.keys()) if (!NAME_RE.test(name)) die('command name "' + name + '" must be 1 to 16 letters or digits');
  return o;
}

function validPrefix(p) { return typeof p === 'string' && p.length >= 1 && p.length <= 64 && PREFIX_RE.test(p); }

function readPassword(o, env = process.env) {
  if (o.passFile) {
    let text;
    try { text = fs.readFileSync(o.passFile, 'utf8'); } catch (e) { throw new UsageError('cannot read ' + o.passFile + ': ' + e.code); }
    const lines = text.split(/\r?\n/);
    for (const line of lines) {   // a line that starts "password:" wins, wherever it is in the file
      const m = line.match(/^\s*password\s*:\s*(.+?)\s*$/i);
      if (m) return m[1];
    }
    return lines[0].trim();
  }
  if (o.pass !== undefined) return o.pass;
  if (env.MQTT_PASS) return env.MQTT_PASS;
  return undefined;
}

// Cut text to at most max bytes of UTF-8 without splitting a character.
function clampBytes(text, max) {
  const buf = Buffer.from(text, 'utf8');
  if (buf.length <= max) return text;
  let end = max;
  while (end > 0 && (buf[end] & 0xC0) === 0x80) end--;
  return buf.subarray(0, end).toString('utf8');
}

// Fills the %...% placeholders this program owns. {braces} are left for the bot.
// Values copied from the request have { and } turned into ( and ) unless rawBraces is set, so that
// text typed on the mesh cannot make the bot expand its own placeholders. Values are not scanned again.
function fillLocal(template, req, now = new Date(), rawBraces = false) {
  const safe = (v) => { const t = String(v); return rawBraces ? t : t.replace(/[{}]/g, (c) => (c === '{' ? '(' : ')')); };
  return template.replace(/%([a-z_]+)(?::([A-Za-z0-9_]+))?%/g, (whole, key, field) => {
    switch (key) {
      case 'args': return safe(req.args ?? '');
      case 'sender': return safe(req.sender ?? '');
      case 'cmd': return safe(req.cmd ?? '');
      case 'now': return String(now.getHours()).padStart(2, '0') + ':' + String(now.getMinutes()).padStart(2, '0');
      case 'field': return field && req[field] !== undefined && req[field] !== null ? safe(req[field]) : '';
      default: return whole;
    }
  });
}

// Control characters become spaces and the ends are trimmed, as the bot does with a reply.
function cleanText(text) {
  return text.replace(/[\u0000-\u001f\u007f]/g, ' ').replace(/^ +| +$/g, '');
}

// Builds the reply for a request. Returns { ok:true, text, payload, note } or { ok:false, why }.
// The text is cut (on a character boundary) to the request's max_len, else to fallbackMax, and
// never beyond 200 bytes; a payload over 512 bytes or an empty text is refused.
function buildReply(template, req, fallbackMax = 100, now = new Date(), rawBraces = false) {
  const limit = Math.min(Number.isInteger(req.max_len) && req.max_len > 0 ? req.max_len : fallbackMax, REPLY_TEXT_MAX);
  const full = cleanText(fillLocal(template, req, now, rawBraces));
  if (full === '') return { ok: false, why: 'the reply text is empty, the bot would ignore it' };
  const text = cleanText(clampBytes(full, limit));
  if (text === '') return { ok: false, why: 'the reply text is empty after cutting to ' + limit + ' bytes' };
  const payload = JSON.stringify({ text });
  if (Buffer.byteLength(payload) > REPLY_PAYLOAD_MAX) return { ok: false, why: 'the reply payload is over ' + REPLY_PAYLOAD_MAX + ' bytes, the bot would ignore it' };
  const cut = Buffer.byteLength(full) - Buffer.byteLength(text);
  return { ok: true, text, payload, note: cut > 0 ? 'cut ' + cut + ' byte(s) to fit ' + limit : '' };
}

// Builds the retained announcement for a command. Returns { ok, payload } or { ok:false, why }.
function buildAnnouncement(desc) {
  const payload = JSON.stringify({ desc: clampBytes(cleanText(desc || ''), ANNOUNCE_DESC_MAX) });
  if (Buffer.byteLength(payload) > ANNOUNCE_PAYLOAD_MAX) return { ok: false, why: 'announcement over ' + ANNOUNCE_PAYLOAD_MAX + ' bytes' };
  return { ok: true, payload };
}

// What the bot would make of a reply payload (used by the simulator).
function checkReplyPayload(buf) {
  if (buf.length === 0 || buf.length > REPLY_PAYLOAD_MAX) return { ok: false, why: 'payload is ' + buf.length + ' bytes (the bot ignores 0 or over ' + REPLY_PAYLOAD_MAX + ')' };
  let body;
  try { body = JSON.parse(buf.toString('utf8')); } catch (e) { return { ok: false, why: 'payload is not JSON' }; }
  if (!body || typeof body !== 'object' || Array.isArray(body) || typeof body.text !== 'string') return { ok: false, why: 'payload is not an object with a string "text"' };
  const text = cleanText(body.text);
  const n = Buffer.byteLength(text);
  if (n === 0) return { ok: false, why: '"text" is empty' };
  if (n > REPLY_TEXT_MAX) return { ok: false, why: '"text" is ' + n + ' bytes, the bot refuses over ' + REPLY_TEXT_MAX };
  return { ok: true, text };
}

// A request shaped as botMqttBuildRequest writes it: same keys, same order, no others. Values as
// BotTask::askBroker fills them: a channel message has an empty sender_key and positions 0; a DM
// from a known contact has the first 6 bytes of the contact's key (12 hex) and its position.
// All coordinates are made-up. Throws UsageError for a bad command name or a request over 1000 bytes.
function buildSimulatedRequest(o, now = new Date(), idBytes = crypto.randomBytes(4)) {
  const id = idBytes.toString('hex');
  const pad = (n) => String(n).padStart(2, '0');
  const text = o.simulate.trim();
  const sp = text.search(/\s/);
  const cmd = (sp < 0 ? text : text.slice(0, sp)).replace(/^[!\/]/, '').toLowerCase();
  const args = sp < 0 ? '' : text.slice(sp + 1).trim();
  if (!NAME_RE.test(cmd)) throw new UsageError('--simulate needs a command name of 1 to 16 letters or digits, got "' + cmd + '"');
  const dm = !!o.dm;
  const req = {
    id, reply_to: o.prefix + '/reply/' + id, cmd, args, sender: o.sender,
    sender_key: dm ? 'a1b2c3d4e5f6' : '',
    sender_lat: dm ? 11.11111 : 0, sender_lon: dm ? -22.22222 : 0, dm, channel: dm ? '' : 'public',
    hops: 2, path: 'a5,3e', path_names: 'Hilltop,Ridge', snr: 7.5, rssi: -98,
    time: pad(now.getHours()) + ':' + pad(now.getMinutes()),
    date: now.getFullYear() + '-' + pad(now.getMonth() + 1) + '-' + pad(now.getDate()),
    tz: Intl.DateTimeFormat().resolvedOptions().timeZone || 'UTC',
    unix: Math.floor(now.getTime() / 1000), bot: 'SimBot', scope: '', uptime: '3h 12m',
    bot_lat: 12.34567, bot_lon: -45.6789, max_len: o.maxLen,
  };
  const size = Buffer.byteLength(JSON.stringify(req));
  if (size > REQUEST_MAX) throw new UsageError('the request would be ' + size + ' bytes; the bot sends at most ' + REQUEST_MAX + ' (shorten the arguments)');
  return req;
}

function createClient(o, tag) {
  const mqtt = require('mqtt');
  const url = (o.tls ? 'mqtts://' : 'mqtt://') + o.host + ':' + o.port;
  return mqtt.connect(url, {
    username: o.user, password: readPassword(o),
    clientId: 'bot-' + tag + '-' + crypto.randomBytes(4).toString('hex'),
    clean: true, reconnectPeriod: 3000, connectTimeout: 10000,
  });
}

function defaultDeps() {
  return { createClient, out: (s) => console.log(s), err: (s) => console.error(s), exit: (c) => process.exit(c), now: () => new Date(), signals: process, connectDeadline: CONNECT_DEADLINE_MS };
}

function wireErrors(client, deps) {
  client.on('error', (e) => {
    deps.err('mqtt error: ' + e.message);
    if (/not authori[sz]ed|bad user/i.test(e.message)) deps.exit(1);
  });
}

function runResponder(o, deps = defaultDeps()) {
  const log = (s) => { if (!o.quiet) deps.out(s); };
  const client = deps.createClient(o, 'responder');
  wireErrors(client, deps);
  const reqTopic = o.prefix + '/request';
  let announced = [];

  client.on('connect', () => {
    log('connected to ' + o.host + ':' + o.port + (o.user ? ' as ' + o.user : ''));
    client.subscribe(reqTopic, { qos: 0 }, (err, granted) => {
      if (err) return deps.err('subscribe failed: ' + err.message);
      if (granted && granted[0] && granted[0].qos === 128) deps.err('warning: the broker refused the subscription to ' + reqTopic + ' (check the access rules)');
      log('listening on ' + reqTopic);
      log('answering: ' + [...o.cmds.keys()].join(', ') + (o.any !== null ? (o.cmds.size ? ', ' : '') + '(anything else)' : ''));
    });
    if (o.announce) {
      announced = [];
      for (const name of o.cmds.keys()) {
        const a = buildAnnouncement(o.descs.get(name));
        if (!a.ok) { deps.err('not announcing ' + name + ': ' + a.why); continue; }
        client.publish(o.prefix + '/commands/' + name, a.payload, { retain: true, qos: 1 });
        announced.push(name);
      }
      log('announced ' + announced.length + ' command(s) under ' + o.prefix + '/commands/');
    }
  });
  client.on('reconnect', () => log('reconnecting...'));

  client.on('message', (topic, payload) => {
    if (topic !== reqTopic) return;
    let req;
    try { req = JSON.parse(payload.toString('utf8')); } catch (e) { return log('ignored: request is not JSON'); }
    if (!req || typeof req !== 'object' || Array.isArray(req) || typeof req.cmd !== 'string') return log('ignored: request has no "cmd"');
    if (typeof req.id !== 'string' || !NAME_RE.test(req.id)) return log('ignored: request id is not usable as a topic level');
    // The reply topic is built from our own prefix, whatever reply_to says.
    const replyTopic = o.prefix + '/reply/' + req.id;

    const name = req.cmd.toLowerCase();
    const template = o.cmds.has(name) ? o.cmds.get(name) : o.any;
    log('request  ' + JSON.stringify({ id: req.id, cmd: req.cmd, args: req.args, sender: req.sender, dm: req.dm, channel: req.channel, hops: req.hops }));
    if (template === null || template === undefined) return log('         no --cmd for "' + name + '", not answering');

    const r = buildReply(template, req, o.maxLen, deps.now(), o.rawBraces);
    if (!r.ok) return deps.err('not replying to ' + req.id + ': ' + r.why);
    if (r.note) log('         ' + r.note);
    const send = () => {
      client.publish(replyTopic, r.payload, { qos: 0 }, (err) => {
        if (err) return deps.err('publish failed: ' + err.message);
        deps.out((o.quiet ? '' : 'reply    ') + r.text);
        if (o.once) shutdown(0);
      });
    };
    if (o.delay > 0) setTimeout(send, o.delay); else send();
  });

  // Clears the retained announcements, then exits. Safe to call twice; the 2 s guard is cancelled.
  let closing = false;
  function shutdown(code) {
    if (closing) return;
    closing = true;
    let finished = false, guard = null;
    const done = () => { if (finished) return; finished = true; clearTimeout(guard); client.end(false, {}, () => deps.exit(code)); };
    if (announced.length && client.connected) {
      let left = announced.length;
      for (const name of announced) client.publish(o.prefix + '/commands/' + name, '', { retain: true, qos: 1 }, () => { if (--left === 0) done(); });
      if (!finished) guard = setTimeout(done, 2000);
    } else done();
  }
  if (deps.signals) for (const sig of ['SIGINT', 'SIGTERM']) deps.signals.on(sig, () => shutdown(0));
  return { client, shutdown };
}

function runSimulator(o, deps = defaultDeps()) {
  const req = buildSimulatedRequest(o, deps.now());
  if (!req.cmd) throw new UsageError('--simulate needs a command name');
  const client = deps.createClient(o, 'simulator');
  wireErrors(client, deps);
  const replyTopic = o.prefix + '/reply/' + req.id;
  let timer = null;
  const finish = (code) => { clearTimeout(timer); clearTimeout(deadline); client.end(false, {}, () => deps.exit(code)); };
  // Against an unreachable broker the client retries for ever; give up when nothing was published in time.
  const deadline = setTimeout(() => { deps.err('could not connect and publish within ' + (deps.connectDeadline ?? CONNECT_DEADLINE_MS) / 1000 + ' seconds (is the broker reachable?)'); finish(5); }, deps.connectDeadline ?? CONNECT_DEADLINE_MS);
  client.on('connect', () => {
    client.subscribe(replyTopic, { qos: 0 }, (err, granted) => {
      if (err) { deps.err('subscribe failed: ' + err.message); return deps.exit(1); }
      if (granted && granted[0] && granted[0].qos === 128) deps.err('warning: the broker refused the subscription to ' + replyTopic);
      client.publish(o.prefix + '/request', JSON.stringify(req), { qos: 0 }, (e) => {
        if (e) { deps.err('publish failed: ' + e.message); return deps.exit(1); }
        clearTimeout(deadline);
        if (!o.quiet) deps.out('request  ' + o.prefix + '/request  ' + JSON.stringify({ id: req.id, cmd: req.cmd, args: req.args, sender: req.sender }));
        timer = setTimeout(() => { deps.out('no reply within ' + o.wait + ' ms (the bot would stay silent)'); finish(3); }, o.wait);
      });
    });
  });
  client.on('message', (topic, payload) => {
    if (topic !== replyTopic) return;
    const c = checkReplyPayload(payload);
    if (!c.ok) { deps.out('reply the bot would ignore: ' + c.why); return finish(4); }
    deps.out((o.quiet ? '' : 'reply    ') + c.text);
    finish(0);
  });
  return { client, request: req };
}

function main(argv, deps = defaultDeps()) {
  let o;
  try {
    o = parseArgs(argv);
    if (o.help) { deps.out(HELP); return deps.exit(0); }
    // Fail early on an unreadable password file, before connecting.
    if (o.passFile) readPassword(o);
    return o.simulate !== null ? runSimulator(o, deps) : runResponder(o, deps);
  } catch (e) {
    if (!(e instanceof UsageError)) throw e;
    deps.err('error: ' + e.message + '\n       run with --help for usage');
    return deps.exit(2);
  }
}

module.exports = { HELP, UsageError, parseArgs, readPassword, clampBytes, fillLocal, cleanText, buildReply, buildAnnouncement,
                   checkReplyPayload, buildSimulatedRequest, runResponder, runSimulator, main, validPrefix };

if (require.main === module) main(process.argv.slice(2));
