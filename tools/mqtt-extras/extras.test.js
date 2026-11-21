'use strict';
const test = require('node:test');
const assert = require('node:assert');
const fs = require('fs'), os = require('os'), path = require('path');
const g = require('./game.js');
const j = require('./joke.js');
const x = require('./extras.js');

const STORY = path.join(__dirname, 'story', 'zork1.z3');
const have = fs.existsSync(STORY);
const tmp = () => fs.mkdtempSync(path.join(os.tmpdir(), 'extras-'));
const bytes = (s) => Buffer.byteLength(s);
const req = (o) => JSON.stringify({ id: '0a1b2c3d', cmd: 'zork', args: 'look', sender: 'Ann', sender_key: '', max_len: 60, ...o });

test('page: fits whole, else cuts at a word with the marker and loses nothing', () => {
  assert.deepStrictEqual(g.page('short', 50), ['short', '']);
  const text = 'one two three four five six seven eight nine ten eleven twelve';
  let rest = text, got = [];
  while (rest) { const [p, r] = g.page(rest, 24); assert.ok(bytes(p) <= 24); got.push(p.replace(g.MORE, '')); rest = r; }
  assert.strictEqual(got.join(' '), text);
  assert.ok(g.page(text, 24)[0].endsWith(g.MORE));
});

test('messages break after a sentence only when that costs no extra message', () => {
  const t = 'This is the first sentence of the room. Here is a second one that runs on a bit.';
  const [parts, rest] = g.pages(t, 50);
  assert.deepStrictEqual(parts, ['This is the first sentence of the room.', 'Here is a second one that runs on a bit.']);
  assert.strictEqual(rest, '');
  const u = 'Short one. ' + 'A much longer second sentence that will need most of the space of two messages by itself, truly.';
  const [p2] = g.pages(u, 60);
  assert.strictEqual(p2.length, 2); assert.ok(p2.every((p) => bytes(p) <= 60)); assert.strictEqual(p2.join(' '), u);
});

test('game text and commands are cleaned', () => {
  assert.strictEqual(g.tidy('West of House\nYou are {here}.\n\n>'), 'West of House You are (here).');
  assert.strictEqual(g.cleanCommand('  OPEN\tMailbox\u0007 '), 'open mailbox');
  assert.strictEqual(g.cleanCommand('x'.repeat(200)).length, 60);
  assert.ok(bytes(g.HELP) <= 100);
});

test('parseArgs, readPassword, playerKey', () => {
  const o = x.parseArgs(['--host', 'h', '--prefix', 'a/b/']);
  assert.strictEqual(o.prefix, 'a/b'); assert.deepStrictEqual(o.commands, ['zork', 'dadjoke']);
  assert.deepStrictEqual(x.parseArgs(['--commands', 'DadJoke']).commands, ['dadjoke']);
  assert.throws(() => x.parseArgs(['--commands', 'zork,beer']), x.UsageError);
  assert.throws(() => x.parseArgs(['--prefix', 'a/#']), x.UsageError);
  assert.throws(() => x.parseArgs(['--bogus']), x.UsageError);
  const f = path.join(tmp(), 'p'); fs.writeFileSync(f, 'Heading\nusername: u\npassword: pw\n');
  assert.strictEqual(x.readPassword({ passFile: f }, {}), 'pw');
  assert.strictEqual(x.readPassword({}, { MQTT_PASS: 'e' }), 'e');
  assert.strictEqual(x.playerKey({ sender: 'Ann', sender_key: 'ABCDEF012345' }), 'k:abcdef012345');
  assert.strictEqual(x.playerKey({ sender: ' Ann ', sender_key: '' }), 'n:ann');
});

test('parseRequest: only the enabled commands, valid ids, limit from max_len', () => {
  const both = ['zork', 'dadjoke'];
  assert.strictEqual(x.parseRequest(both, 'not json'), null);
  assert.strictEqual(x.parseRequest(both, req({ cmd: 'beer' })), null);
  assert.strictEqual(x.parseRequest(['dadjoke'], req({})), null);
  assert.strictEqual(x.parseRequest(both, req({ id: 'XYZ' })), null);
  assert.deepStrictEqual(x.parseRequest(both, req({ cmd: 'DadJoke', args: '' })), { id: '0a1b2c3d', cmd: 'dadjoke', limit: 60, key: 'n:ann', args: '' });
  assert.strictEqual(x.parseRequest(both, req({ max_len: 9999 })).limit, 200);
  assert.strictEqual(x.parseRequest(both, req({ max_len: undefined })).limit, 100);
});

test('joke: tidy, cut, retry a long one, cut the shortest when none fits, empty when the site fails', async () => {
  assert.strictEqual(j.tidy('Why?\r\n\r\nBecause {sender}. '), 'Why? Because (sender).');
  assert.strictEqual(j.clampBytes('héllo wörld', 6), 'héllo');
  const seq = (...a) => { let i = 0; return async () => { const v = a[i++]; if (v instanceof Error) throw v; return v; }; };
  assert.strictEqual(await j.jokeThatFits(20, seq('x'.repeat(50), 'short one')), 'short one');
  assert.strictEqual(await j.jokeThatFits(20, seq('a'.repeat(50), 'b'.repeat(30), 'c'.repeat(40))), 'b'.repeat(20));
  assert.strictEqual(await j.jokeThatFits(20, seq(new Error('down'), new Error('down'), new Error('down'))), '');
  let seen;
  const fake = async (url, opt) => { seen = { url, opt }; return { ok: true, text: async () => 'Line one\nline two' }; };
  assert.strictEqual(await j.fetchJoke(fake), 'Line one line two');
  assert.strictEqual(seen.url, j.JOKE_URL); assert.strictEqual(seen.opt.headers.Accept, 'text/plain');
  await assert.rejects(j.fetchJoke(async () => ({ ok: false, status: 503 })));
});

test('replyFor routes dadjoke to the joke source', async () => {
  const r = x.parseRequest(['dadjoke'], req({ cmd: 'dadjoke' }));
  assert.deepStrictEqual(await x.replyFor(r, { getJoke: async () => 'ha' }), ['ha']);
  assert.deepStrictEqual(await x.replyFor(r, { getJoke: async () => { throw new Error('down'); } }), []);
});

test('pages: several messages, the marker only when text is left over, payload kept within the limit', () => {
  const words = Array.from({ length: 60 }, (_, i) => 'word' + i).join(' ');
  const [all, none] = g.pages('short text', 40);
  assert.deepStrictEqual(all, ['short text']); assert.strictEqual(none, '');
  const [three, rest0] = g.pages(words.slice(0, 100), 40);
  assert.strictEqual(three.length, 3); assert.strictEqual(rest0, ''); assert.ok(three.every((p) => bytes(p) <= 40 && !p.endsWith(g.MORE)));
  assert.strictEqual(three.join(' '), words.slice(0, 100));
  const [four, rest] = g.pages(words, 40);
  assert.strictEqual(four.length, 4); assert.ok(rest); assert.ok(four[3].endsWith(g.MORE)); assert.ok(four.every((p) => bytes(p) <= 40));
  assert.strictEqual([...four.slice(0, 3), four[3].replace(g.MORE, ''), rest].join(' '), words);
  const [one, r1] = g.pages(words, 40, 1);
  assert.strictEqual(one.length, 1); assert.ok(one[0].endsWith(g.MORE) && r1);
  const quotes = Array.from({ length: 200 }, () => '"q"').join(' ');          // every quote doubles in JSON
  const [qs] = g.pages(quotes, 150);
  assert.ok(bytes(g.replyPayload(qs)) <= 512); assert.ok(qs.length < 4);
  assert.strictEqual(g.replyPayload(['a']), '{"text":"a"}');
  assert.strictEqual(g.replyPayload(['a', 'b', 'c']), '{"text":"a","more":["b","c"]}');
});

test('zork: start, play, page, save, reload, separate players, restart', { skip: !have && 'no story file (npm run fetch-story)' }, async () => {
  const story = fs.readFileSync(STORY), dir = tmp();
  const a = new g.Zork(story, dir);
  const whole = a.play('k:1', 'look', 105);                              // several messages, nothing left over
  assert.ok(whole.length > 1 && whole.length <= 4 && whole.every((p) => bytes(p) <= 105) && !whole[whole.length - 1].endsWith(g.MORE));
  assert.match(a.play('k:1', 'more', 105)[0], /Nothing more/);
  a.play('k:1', 'restart', 105);
  const [first] = a.play('k:1', 'look', 105, 1);
  assert.match(first, /^West of House/);
  assert.match(whole[0], /^ZORK I \(Infocom\)\. West of House|^West of House/);
  assert.ok(bytes(first) <= 105 && first.endsWith(g.MORE));
  assert.match(a.play('k:1', 'look more', 105, 1)[0], /mailbox/);              // a command ending in "more" is the next page
  assert.match(a.play('k:1', 'OPEN MAILBOX', 105, 1)[0], /leaflet/);
  assert.match(a.play('k:1', 'take leaflet', 105, 1)[0], /Taken/);
  assert.match(a.play('k:1', 'more', 105, 1)[0], /Nothing more/);
  assert.match(a.play('k:1', 'save', 105, 1)[0], /saved after every move/);
  assert.match(a.play('k:1', 'xyzzy {sender}', 105, 1)[0], /\(sender\)/);
  assert.match(a.play('k:2', 'inventory', 105, 1)[0], /empty-handed/);        // another player has their own game
  const b = new g.Zork(story, dir);                                      // a restart of the program
  assert.match(b.play('k:1', 'inventory', 105, 1)[0], /leaflet/);
  assert.match(b.play('k:1', 'restart', 105, 1)[0], /^New game\. ZORK I/);
  assert.match(b.play('k:1', 'inventory', 105, 1)[0], /empty-handed/);
  for (const f of fs.readdirSync(dir)) assert.match(f, /^[0-9a-f]{24}\.sav$/);
  const parts = await x.replyFor(x.parseRequest(['zork'], req({ max_len: 60 })), { zork: b });
  assert.ok(parts.length >= 1 && parts.length <= 4 && parts.every((p) => bytes(p) <= 60));
  assert.strictEqual((await x.replyFor(x.parseRequest(['zork'], req({ max_len: 60 })), { zork: b, parts: 1 })).length, 1);
});
