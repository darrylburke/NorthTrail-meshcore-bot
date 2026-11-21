'use strict';
const test = require('node:test');
const assert = require('node:assert');
const fs = require('fs');
const os = require('os');
const path = require('path');
const { EventEmitter } = require('events');
const r = require('./responder.js');

// A stand-in for the MQTT client: records what is published, lets the test deliver messages.
class StubClient extends EventEmitter {
  constructor() { super(); this.published = []; this.subscribed = []; this.connected = true; this.ended = false; }
  subscribe(topic, opts, cb) { this.subscribed.push(topic); if (cb) cb(null, [{ topic, qos: 0 }]); }
  publish(topic, payload, opts, cb) { this.published.push({ topic, payload, opts }); if (typeof opts === 'function') opts(); else if (cb) cb(); }
  end(force, opts, cb) { this.ended = true; if (cb) cb(); }
}
function makeDeps() {
  const d = { out: [], err: [], exits: [], client: new StubClient(), tags: [] };
  d.createClient = (o, tag) => { d.tags.push(tag); return d.client; };
  d.exit = (c) => { d.exits.push(c); };
  d.now = () => new Date(2026, 0, 5, 9, 7, 0);
  Object.defineProperty(d, 'outFn', { value: (s) => d.out.push(s) });
  d.signals = new EventEmitter();
  const deps = { createClient: d.createClient, out: (s) => d.out.push(s), err: (s) => d.err.push(s), exit: d.exit, now: d.now, signals: d.signals, connectDeadline: 200 };
  return { d, deps };
}
const req = (extra = {}) => ({ id: '8f2a1c3e', cmd: 'beer', args: 'ipa', sender: 'Dee', max_len: 120, ...extra });

test('parseArgs defaults are neutral', () => {
  const o = r.parseArgs(['--cmd', 'a=b']);
  assert.strictEqual(o.host, 'localhost');
  assert.strictEqual(o.port, 1883);
  assert.strictEqual(o.prefix, 'meshcore/MeshBot');
  assert.strictEqual(o.maxLen, 100);
});

test('parseArgs reads every option', () => {
  const o = r.parseArgs(['--host', 'broker.example.org', '--port', '8883', '--tls', '--user', 'bot', '--pass-file', 'x', '--prefix', 'a/b',
    '--cmd', 'Beer=Hi', '--desc', 'beer=a drink', '--any', 'what?', '--announce', '--delay', '10', '--once', '--max-len', '50',
    '--sender', 'Zed', '--wait', '900', '--quiet']);
  assert.strictEqual(o.host, 'broker.example.org');
  assert.strictEqual(o.port, 8883);
  assert.ok(o.tls && o.announce && o.once && o.quiet);
  assert.strictEqual(o.user, 'bot');
  assert.strictEqual(o.passFile, 'x');
  assert.strictEqual(o.prefix, 'a/b');
  assert.strictEqual(o.cmds.get('beer'), 'Hi');
  assert.strictEqual(o.descs.get('beer'), 'a drink');
  assert.strictEqual(o.any, 'what?');
  assert.strictEqual(o.delay, 10);
  assert.strictEqual(o.maxLen, 50);
  assert.strictEqual(o.sender, 'Zed');
  assert.strictEqual(o.wait, 900);
});

test('parseArgs keeps = signs inside the reply', () => {
  assert.strictEqual(r.parseArgs(['--cmd', 'sum=1+1=2']).cmds.get('sum'), '1+1=2');
});

test('parseArgs rejects bad input', () => {
  const bad = [[], ['--cmd', 'nonsense'], ['--cmd', 'bad name=x'], ['--cmd', 'abcdefghijklmnopq=x'], ['--cmd', 'a=b', '--prefix', 'x/#'],
    ['--cmd', 'a=b', '--prefix', '/x'], ['--cmd', 'a=b', '--prefix', 'x/'], ['--cmd', 'a=b', '--prefix', 'x//y'], ['--cmd', 'a=b', '--prefix', 'x+'],
    ['--cmd', 'a=b', '--prefix', 'a'.repeat(65)], ['--cmd', 'a=b', '--port', '0'], ['--cmd', 'a=b', '--port', '12x'], ['--cmd', 'a=b', '--max-len', '201'],
    ['--cmd', 'a=b', '--wait', '5'], ['--cmd', 'a=b', '--bogus'], ['--cmd'], ['--desc', 'bad name=x', '--cmd', 'a=b']];
  for (const argv of bad) assert.throws(() => r.parseArgs(argv), r.UsageError, JSON.stringify(argv));
});

test('parseArgs accepts a 64-byte prefix and --simulate alone', () => {
  assert.strictEqual(r.parseArgs(['--simulate', 'x', '--prefix', 'a'.repeat(64)]).prefix.length, 64);
});

test('readPassword: file, then --pass, then MQTT_PASS; never needs --pass', () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'resp-'));
  const f1 = path.join(dir, 'one'); fs.writeFileSync(f1, 'password: s3 cret \nuser: bot\n');
  const f3 = path.join(dir, 'three'); fs.writeFileSync(f3, 'hunter2\nnote: password: wrong\n');
  const f2 = path.join(dir, 'two'); fs.writeFileSync(f2, 'plainpass\nsecond\n');
  assert.strictEqual(r.readPassword({ passFile: f1 }, {}), 's3 cret');
  assert.strictEqual(r.readPassword({ passFile: f2 }, {}), 'plainpass');
  assert.strictEqual(r.readPassword({ passFile: f3 }, {}), 'hunter2');   // a later line only mentioning password: is ignored
  const f4 = path.join(dir, 'four'); fs.writeFileSync(f4, 'Broker login\nhost: h\nusername: bot\npassword: pw4\nprefix: a/b\n');
  assert.strictEqual(r.readPassword({ passFile: f4 }, {}), 'pw4');   // a labelled file with a heading line
  assert.strictEqual(r.readPassword({ pass: 'p' }, { MQTT_PASS: 'e' }), 'p');
  assert.strictEqual(r.readPassword({}, { MQTT_PASS: 'e' }), 'e');
  assert.strictEqual(r.readPassword({}, {}), undefined);
  assert.throws(() => r.readPassword({ passFile: path.join(dir, 'missing') }, {}), r.UsageError);
  fs.rmSync(dir, { recursive: true });
});

test('fillLocal fills the program placeholders and leaves the bot ones', () => {
  const now = new Date(2026, 0, 5, 9, 7);
  const out = r.fillLocal('%cmd% %args% by %sender% at %now% {sender} {args} %field:hops% %field:bot% %field:nope% %unknown% %field:%', { cmd: 'beer', args: 'ipa', sender: 'Dee', hops: 2, bot: 'B' }, now);
  assert.strictEqual(out, 'beer ipa by Dee at 09:07 {sender} {args} 2 B  %unknown% %field:%');
});

test('fillLocal handles missing and null fields', () => {
  assert.strictEqual(r.fillLocal('[%args%][%sender%][%field:x%]', { x: null }), '[][][]');
});

test('clampBytes never splits a UTF-8 character', () => {
  assert.strictEqual(r.clampBytes('abc', 3), 'abc');
  assert.strictEqual(r.clampBytes('abcd', 3), 'abc');
  assert.strictEqual(r.clampBytes('aéb', 2), 'a');        // é is 2 bytes
  assert.strictEqual(r.clampBytes('aéb', 3), 'aé');
  assert.strictEqual(r.clampBytes('\u{1F37A}x', 3), '');         // 4-byte emoji
  assert.strictEqual(r.clampBytes('\u{1F37A}x', 4), '\u{1F37A}');
  assert.strictEqual(r.clampBytes('abc', 0), '');
});

test('buildReply fills, cleans and builds the payload', () => {
  const b = r.buildReply('Cheers {sender}, %args%\n coming up ', req(), 100, new Date());
  assert.ok(b.ok);
  assert.strictEqual(b.text, 'Cheers {sender}, ipa  coming up');
  assert.deepStrictEqual(JSON.parse(b.payload), { text: b.text });
  assert.strictEqual(b.note, '');
});

test('buildReply cuts to max_len on a character boundary', () => {
  const b = r.buildReply('é'.repeat(50), req({ max_len: 11 }));
  assert.ok(b.ok);
  assert.strictEqual(Buffer.byteLength(b.text), 10);
  assert.match(b.note, /cut/);
});

test('buildReply uses the fallback when the request has no max_len, never over 200', () => {
  const noLen = { id: 'aa', cmd: 'x', args: '', sender: 's' };
  assert.strictEqual(r.buildReply('x'.repeat(300), noLen, 40).text.length, 40);
  assert.strictEqual(r.buildReply('x'.repeat(300), req({ max_len: 5000 }), 40).text.length, 200);
  assert.strictEqual(r.buildReply('x'.repeat(300), noLen, 200).text.length, 200);
  assert.strictEqual(r.buildReply('x'.repeat(300), req({ max_len: 0 }), 40).text.length, 40);
  assert.strictEqual(r.buildReply('x'.repeat(300), req({ max_len: '9' }), 40).text.length, 40);
});

test('buildReply refuses an empty text', () => {
  assert.strictEqual(r.buildReply('   ', req()).ok, false);
  assert.strictEqual(r.buildReply('%args%', req({ args: '' })).ok, false);
  assert.strictEqual(r.buildReply('\n\t', req()).ok, false);
});

test('buildReply payload stays within 512 bytes even with heavy escaping', () => {
  const b = r.buildReply('"'.repeat(300), req({ max_len: 200 }));
  assert.ok(b.ok);
  assert.ok(Buffer.byteLength(b.payload) <= 512);
  assert.strictEqual(b.text.length, 200);
});

test('buildReply keeps control characters out of the text', () => {
  const b = r.buildReply('a\u0000b\u007fc', req());
  assert.strictEqual(b.text, 'a b c');
});

test('buildAnnouncement cuts the description to 48 bytes and cleans it', () => {
  const a = r.buildAnnouncement('order a drink');
  assert.deepStrictEqual(JSON.parse(a.payload), { desc: 'order a drink' });
  const long = r.buildAnnouncement('é'.repeat(40));
  assert.strictEqual(Buffer.byteLength(JSON.parse(long.payload).desc), 48);
  assert.deepStrictEqual(JSON.parse(r.buildAnnouncement(undefined).payload), { desc: '' });
  assert.strictEqual(JSON.parse(r.buildAnnouncement('a\nb').payload).desc, 'a b');
  assert.ok(Buffer.byteLength(r.buildAnnouncement('"'.repeat(100)).payload) <= 256);
});

test('checkReplyPayload mirrors the bot', () => {
  const ok = (s) => r.checkReplyPayload(Buffer.from(s));
  assert.deepStrictEqual(ok('{"text":" hi\\n"}'), { ok: true, text: 'hi' });
  assert.strictEqual(ok('').ok, false);
  assert.strictEqual(ok('nope').ok, false);
  assert.strictEqual(ok('[]').ok, false);
  assert.strictEqual(ok('{"text":5}').ok, false);
  assert.strictEqual(ok('{"text":" "}').ok, false);
  assert.strictEqual(ok(JSON.stringify({ text: 'x'.repeat(201) })).ok, false);
  assert.strictEqual(ok(JSON.stringify({ text: 'x'.repeat(200) })).ok, true);
  assert.strictEqual(ok(JSON.stringify({ text: 'x', pad: 'y'.repeat(500) })).ok, false);
});

const REQUEST_KEYS = ['id', 'reply_to', 'cmd', 'args', 'sender', 'sender_key', 'sender_lat', 'sender_lon', 'dm', 'channel', 'hops', 'path',
  'path_names', 'snr', 'rssi', 'time', 'date', 'tz', 'unix', 'bot', 'scope', 'uptime', 'bot_lat', 'bot_lon', 'max_len'];

test('request keys equal those botMqttRequestTry writes, in order', (t) => {
  const header = path.join(__dirname, '..', '..', 'src', 'helpers', 'bot', 'BotMqttMsg.h');
  if (!fs.existsSync(header)) return t.skip('firmware header not found: ' + header);
  const text = fs.readFileSync(header, 'utf8');
  const a = text.indexOf('botMqttRequestTry(char*');
  const b = text.indexOf('botMqttBuildRequest(char*', a);
  assert.ok(a > 0 && b > a, 'request builder not found in the header');
  const keys = [...new Set([...text.slice(a, b).matchAll(/\{?,?\\"([a-z_]+)\\":/g)].map((m) => m[1]))];
  assert.deepStrictEqual(Object.keys(r.buildSimulatedRequest(r.parseArgs(['--simulate', 'x']))), keys);
  assert.deepStrictEqual(keys, REQUEST_KEYS);
});

test('simulated request has the firmware keys in order and an 8 hex id', () => {
  const o = r.parseArgs(['--simulate', '!Beer  ipa  ale', '--sender', 'Dee', '--max-len', '90']);
  const q = r.buildSimulatedRequest(o, new Date(2026, 0, 5, 9, 7, 3), Buffer.from('0a1b2c3d', 'hex'));
  assert.deepStrictEqual(Object.keys(q), REQUEST_KEYS);
  assert.strictEqual(q.id, '0a1b2c3d');
  assert.match(q.id, /^[0-9a-f]{8}$/);
  assert.strictEqual(q.reply_to, 'meshcore/MeshBot/reply/0a1b2c3d');
  assert.strictEqual(q.cmd, 'beer');
  assert.strictEqual(q.args, 'ipa  ale');
  assert.strictEqual(q.sender, 'Dee');
  assert.strictEqual(q.time, '09:07');
  assert.strictEqual(q.date, '2026-01-05');
  assert.strictEqual(q.max_len, 90);
  assert.strictEqual(typeof q.dm, 'boolean');
  for (const k of ['sender_lat', 'sender_lon', 'bot_lat', 'bot_lon', 'snr', 'hops', 'rssi', 'unix']) assert.strictEqual(typeof q[k], 'number', k);
  assert.ok(Buffer.byteLength(JSON.stringify(q)) <= 1000);
});

test('simulated request id is random hex by default', () => {
  const o = r.parseArgs(['--simulate', 'x']);
  assert.match(r.buildSimulatedRequest(o).id, /^[0-9a-f]{8}$/);
});

test('runResponder answers a request on the reply topic with the stub client', () => {
  const { d, deps } = makeDeps();
  const o = r.parseArgs(['--cmd', 'beer=Cheers {sender}, %args% at %now%', '--any', 'unknown %cmd%']);
  r.runResponder(o, deps);
  d.client.emit('connect');
  assert.deepStrictEqual(d.client.subscribed, ['meshcore/MeshBot/request']);
  d.client.emit('message', 'meshcore/MeshBot/request', Buffer.from(JSON.stringify(req({ cmd: 'BEER' }))));
  const p = d.client.published.at(-1);
  assert.strictEqual(p.topic, 'meshcore/MeshBot/reply/8f2a1c3e');
  assert.deepStrictEqual(JSON.parse(p.payload), { text: 'Cheers {sender}, ipa at 09:07' });
  d.client.emit('message', 'meshcore/MeshBot/request', Buffer.from(JSON.stringify(req({ cmd: 'wine', id: 'ab12cd34' }))));
  assert.deepStrictEqual(JSON.parse(d.client.published.at(-1).payload), { text: 'unknown wine' });
});

test('runResponder ignores bad requests and other topics, and never trusts reply_to', () => {
  const { d, deps } = makeDeps();
  r.runResponder(r.parseArgs(['--cmd', 'beer=x']), deps);
  d.client.emit('connect');
  const send = (t, b) => d.client.emit('message', t, Buffer.from(b));
  send('meshcore/MeshBot/request', 'not json');
  send('meshcore/MeshBot/request', '[]');
  send('meshcore/MeshBot/request', JSON.stringify({ id: '8f2a1c3e' }));
  send('meshcore/MeshBot/request', JSON.stringify(req({ id: 'a/b' })));
  send('meshcore/MeshBot/request', JSON.stringify(req({ id: 5 })));
  send('meshcore/MeshBot/request', JSON.stringify(req({ cmd: 'unmatched' })));
  send('meshcore/MeshBot/other', JSON.stringify(req()));
  assert.strictEqual(d.client.published.length, 0);
  send('meshcore/MeshBot/request', JSON.stringify(req({ reply_to: 'other/place' })));
  assert.strictEqual(d.client.published.at(-1).topic, 'meshcore/MeshBot/reply/8f2a1c3e');
});

test('runResponder reports a reply it must refuse instead of sending', () => {
  const { d, deps } = makeDeps();
  r.runResponder(r.parseArgs(['--cmd', 'beer=%args%']), deps);
  d.client.emit('connect');
  d.client.emit('message', 'meshcore/MeshBot/request', Buffer.from(JSON.stringify(req({ args: '' }))));
  assert.strictEqual(d.client.published.length, 0);
  assert.match(d.err.join('\n'), /not replying.*empty/);
});

test('runResponder with --announce publishes retained commands and clears them on shutdown', () => {
  const { d, deps } = makeDeps();
  const o = r.parseArgs(['--cmd', 'beer=x', '--cmd', 'tea=y', '--desc', 'beer=order a drink', '--announce']);
  const h = r.runResponder(o, deps);
  d.client.emit('connect');
  const [a, b] = d.client.published;
  assert.strictEqual(a.topic, 'meshcore/MeshBot/commands/beer');
  assert.deepStrictEqual(JSON.parse(a.payload), { desc: 'order a drink' });
  assert.strictEqual(a.opts.retain, true);
  assert.strictEqual(b.topic, 'meshcore/MeshBot/commands/tea');
  h.shutdown(0);
  const cleared = d.client.published.slice(2);
  assert.deepStrictEqual(cleared.map((p) => [p.topic, p.payload]), [['meshcore/MeshBot/commands/beer', ''], ['meshcore/MeshBot/commands/tea', '']]);
  assert.deepStrictEqual(d.exits, [0]);
});

test('runResponder --once exits after the first reply', () => {
  const { d, deps } = makeDeps();
  r.runResponder(r.parseArgs(['--cmd', 'beer=x', '--once']), deps);
  d.client.emit('connect');
  d.client.emit('message', 'meshcore/MeshBot/request', Buffer.from(JSON.stringify(req())));
  assert.deepStrictEqual(d.exits, [0]);
});

test('runSimulator publishes a firmware-shaped request and prints the reply', () => {
  const { d, deps } = makeDeps();
  const o = r.parseArgs(['--simulate', 'beer ipa', '--prefix', 'x/y']);
  const { request } = r.runSimulator(o, deps);
  d.client.emit('connect');
  assert.deepStrictEqual(d.client.subscribed, ['x/y/reply/' + request.id]);
  const p = d.client.published[0];
  assert.strictEqual(p.topic, 'x/y/request');
  assert.deepStrictEqual(Object.keys(JSON.parse(p.payload)), REQUEST_KEYS);
  d.client.emit('message', 'x/y/reply/' + request.id, Buffer.from('{"text":"Cheers"}'));
  assert.ok(d.out.includes('reply    Cheers'));
  assert.deepStrictEqual(d.exits, [0]);
});

test('runSimulator flags a reply the bot would ignore', () => {
  const { d, deps } = makeDeps();
  const { request } = r.runSimulator(r.parseArgs(['--simulate', 'beer']), deps);
  d.client.emit('connect');
  d.client.emit('message', 'meshcore/MeshBot/reply/' + request.id, Buffer.from(JSON.stringify({ text: 'x'.repeat(250) })));
  assert.match(d.out.join('\n'), /would ignore.*refuses over 200/);
  assert.deepStrictEqual(d.exits, [4]);
});

test('main: --help prints usage and exits 0; usage errors exit 2', () => {
  let { d, deps } = makeDeps();
  r.main(['--help'], deps);
  assert.match(d.out[0], /Usage:/);
  assert.deepStrictEqual(d.exits, [0]);
  ({ d, deps } = makeDeps());
  r.main(['--nope'], deps);
  assert.deepStrictEqual(d.exits, [2]);
  assert.match(d.err[0], /unknown option/);
  assert.deepStrictEqual(d.tags, []);
});

test('the help text names no personal data and documents the safe password routes', () => {
  assert.doesNotMatch(r.HELP, new RegExp(['North' + 'Trail', 'champ' + 'ion', 'dar' + 'ryl', '\\/ho' + 'me\\/'].join('|'), 'i'));
  assert.match(r.HELP, /MQTT_PASS/);
  assert.match(r.HELP, /--pass-file/);
});

test('simulated channel request matches askBroker: empty key, positions 0, a channel', () => {
  const q = r.buildSimulatedRequest(r.parseArgs(['--simulate', 'x']));
  assert.strictEqual(q.dm, false);
  assert.strictEqual(q.sender_key, '');
  assert.strictEqual(q.sender_lat, 0);
  assert.strictEqual(q.sender_lon, 0);
  assert.strictEqual(q.channel, 'public');
});

test('simulated --dm request: 12-hex key, a position, no channel', () => {
  const q = r.buildSimulatedRequest(r.parseArgs(['--simulate', 'x', '--dm']));
  assert.strictEqual(q.dm, true);
  assert.match(q.sender_key, /^[0-9a-f]{12}$/);
  assert.notStrictEqual(q.sender_lat, 0);
  assert.strictEqual(q.channel, '');
  assert.deepStrictEqual(Object.keys(q), REQUEST_KEYS);
});

test('simulator checks the command name and the request size', () => {
  assert.throws(() => r.buildSimulatedRequest(r.parseArgs(['--simulate', 'a_b'])), r.UsageError);
  assert.throws(() => r.buildSimulatedRequest(r.parseArgs(['--simulate', 'x ' + 'y'.repeat(1000)])), /1000/);
  const ok = r.buildSimulatedRequest(r.parseArgs(['--simulate', 'x ' + 'y'.repeat(500)]));
  assert.ok(Buffer.byteLength(JSON.stringify(ok)) <= 1000);
  const { d, deps } = makeDeps();
  r.main(['--simulate', 'x ' + 'y'.repeat(1000)], deps);
  assert.deepStrictEqual(d.exits, [2]);
  assert.deepStrictEqual(d.tags, []);
});

test('no real-looking coordinates in the simulated request', () => {
  for (const dm of [false, true]) {
    const q = r.buildSimulatedRequest({ ...r.parseArgs(['--simulate', 'x']), dm });
    for (const k of ['sender_lat', 'sender_lon', 'bot_lat', 'bot_lon']) assert.ok([0, 11.11111, -22.22222, 12.34567, -45.6789].includes(q[k]), k);
  }
});

test('braces copied from the request become parentheses; the template keeps its own', () => {
  const q = req({ args: '{sender} {hops}', sender: '{x}', hops: '{y}' });
  assert.strictEqual(r.fillLocal('{sender}: %args% %sender% %field:hops%', q), '{sender}: (sender) (hops) (x) (y)');
  assert.strictEqual(r.fillLocal('%cmd%', { cmd: 'a{b}' }), 'a(b)');
  assert.strictEqual(r.buildReply('{sender} %args%', q).text, '{sender} (sender) (hops)');
});

test('--raw-braces copies braces unchanged', () => {
  const o = r.parseArgs(['--cmd', 'a=b', '--raw-braces']);
  assert.strictEqual(o.rawBraces, true);
  assert.strictEqual(r.parseArgs(['--cmd', 'a=b']).rawBraces, false);
  assert.strictEqual(r.fillLocal('%args%', { args: '{sender}' }, new Date(), true), '{sender}');
  assert.strictEqual(r.buildReply('%args%', req({ args: '{hops}' }), 100, new Date(), true).text, '{hops}');
});

test('placeholders are expanded once, not recursively', () => {
  assert.strictEqual(r.fillLocal('%args%', { args: '%sender%', sender: 'Dee' }), '%sender%');
  assert.strictEqual(r.fillLocal('%field:args%', { args: '%now%' }, new Date(2026, 0, 1, 1, 1), true), '%now%');
});

test('responder applies --raw-braces from the options', () => {
  for (const raw of [false, true]) {
    const { d, deps } = makeDeps();
    r.runResponder(r.parseArgs(['--cmd', 'beer={sender} %args%'].concat(raw ? ['--raw-braces'] : [])), deps);
    d.client.emit('connect');
    d.client.emit('message', 'meshcore/MeshBot/request', Buffer.from(JSON.stringify(req({ args: '{hops}' }))));
    assert.strictEqual(JSON.parse(d.client.published.at(-1).payload).text, raw ? '{sender} {hops}' : '{sender} (hops)');
  }
});

test('SIGINT and SIGTERM clear the announcements and exit once', () => {
  for (const sig of ['SIGINT', 'SIGTERM']) {
    const { d, deps } = makeDeps();
    r.runResponder(r.parseArgs(['--cmd', 'beer=x', '--announce']), deps);
    d.client.emit('connect');
    assert.strictEqual(d.signals.listenerCount(sig), 1);
    d.signals.emit(sig);
    d.signals.emit(sig);   // a second signal changes nothing
    const cleared = d.client.published.filter((p) => p.payload === '');
    assert.deepStrictEqual(cleared.map((p) => p.topic), ['meshcore/MeshBot/commands/beer']);
    assert.deepStrictEqual(d.exits, [0]);
    assert.ok(d.client.ended);
  }
});

test('shutdown with no acknowledgement exits once, after its 2 s guard, even when called twice', async () => {
  const { d, deps } = makeDeps();
  d.client.publish = function (topic, payload, opts, cb) { this.published.push({ topic, payload, opts }); };   // acks never come
  const h = r.runResponder(r.parseArgs(['--cmd', 'beer=x', '--announce']), deps);
  d.client.emit('connect');
  h.shutdown(0);
  assert.deepStrictEqual(d.exits, []);   // waiting for the ack or the guard
  h.shutdown(0);
  assert.deepStrictEqual(d.exits, []);
  // the acknowledgements never come, so only the 2 s guard ends it
  await new Promise((res) => setTimeout(res, 2100));
  assert.deepStrictEqual(d.exits, [0]);
});

test('--delay holds the reply back', async () => {
  const { d, deps } = makeDeps();
  r.runResponder(r.parseArgs(['--cmd', 'beer=x', '--delay', '60']), deps);
  d.client.emit('connect');
  d.client.emit('message', 'meshcore/MeshBot/request', Buffer.from(JSON.stringify(req())));
  assert.strictEqual(d.client.published.length, 0);
  await new Promise((res) => setTimeout(res, 150));
  assert.strictEqual(d.client.published.length, 1);
});

test('--any with --once answers an unmatched command, then exits', () => {
  const { d, deps } = makeDeps();
  r.runResponder(r.parseArgs(['--any', 'sorry %cmd%', '--once']), deps);
  d.client.emit('connect');
  d.client.emit('message', 'meshcore/MeshBot/request', Buffer.from(JSON.stringify(req({ cmd: 'zzz' }))));
  assert.strictEqual(JSON.parse(d.client.published.at(-1).payload).text, 'sorry zzz');
  assert.deepStrictEqual(d.exits, [0]);
});

test('simulator exits 3 when no reply comes', async () => {
  const { d, deps } = makeDeps();
  r.runSimulator(r.parseArgs(['--simulate', 'x', '--wait', '100']), deps);
  d.client.emit('connect');
  await new Promise((res) => setTimeout(res, 250));
  assert.deepStrictEqual(d.exits, [3]);
  assert.match(d.out.join('\n'), /no reply within 100 ms/);
});

test('simulator exits 5 when it cannot publish before the connect deadline', async () => {
  const { d, deps } = makeDeps();
  r.runSimulator(r.parseArgs(['--simulate', 'x']), deps);   // 'connect' never fires
  await new Promise((res) => setTimeout(res, 350));
  assert.deepStrictEqual(d.exits, [5]);
  assert.match(d.err.join('\n'), /could not connect/);
  assert.strictEqual(d.client.published.length, 0);
});

test('the password is never printed', () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'resp-'));
  const f = path.join(dir, 'pw'); fs.writeFileSync(f, 'S3CRET-VALUE\n');
  const { d, deps } = makeDeps();
  r.main(['--pass-file', f, '--pass', 'S3CRET-ARG', '--cmd', 'beer=x', '--announce'], deps);
  d.client.emit('connect');
  d.client.emit('message', 'meshcore/MeshBot/request', Buffer.from(JSON.stringify(req())));
  d.client.emit('error', new Error('connection refused'));
  const seen = JSON.stringify([d.out, d.err, d.client.published]);
  assert.doesNotMatch(seen, /S3CRET/);
  assert.match(r.HELP, /never printed/);
  fs.rmSync(dir, { recursive: true });
  const { d: d2, deps: deps2 } = makeDeps();
  r.main(['--pass-file', path.join(dir, 'gone'), '--cmd', 'a=b'], deps2);
  assert.deepStrictEqual(d2.exits, [2]);
});
