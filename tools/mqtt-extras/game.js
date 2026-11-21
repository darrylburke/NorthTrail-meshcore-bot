'use strict';
// Runs a version-3 Z-machine story (Zork I) one command at a time, with one saved game per
// player and replies cut into messages that fit the mesh (several per reply, then pages). No MQTT in this file.

const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const JSZM = require('./vendor/jszm.js');

const MORE = ' [more]';
const HELP = 'Zork I: zork look, zork open mailbox, zork n. zork more = next page. zork restart = new game.';
const GAME_OVER = /Type RESTART, RESTORE, or QUIT/i;

function byteLen(s) { return Buffer.byteLength(s, 'utf8'); }

// The game's text as one line: prompts and layout removed, braces made harmless for the bot.
function tidy(text) {
  return text.replace(/[{}]/g, (c) => (c === '{' ? '(' : ')'))
    .replace(/[^\x20-\x7e\n]/g, ' ')
    .replace(/\n\s*>\s*$/, '')
    .replace(/\s*\n\s*/g, ' ')
    .replace(/ {2,}/g, ' ')
    .trim();
}

// The opening text without its copyright banner (the banner alone is three pages).
function intro(text) {
  const t = tidy(text);
  const m = t.match(/Serial number \d+\s*(.*)$/);
  return 'ZORK I (Infocom). ' + (m ? m[1] : t);
}

// What a player may send to the game: printable ASCII, lower case, at most 60 characters.
function cleanCommand(args) {
  return String(args ?? '').replace(/[^\x20-\x7e]/g, ' ').replace(/\s+/g, ' ').trim().toLowerCase().slice(0, 60);
}

// Where to cut ASCII text so the head is at most room bytes: at a space in the last half, else
// exactly at room. With sentence set, after a sentence instead when one ends in the last 40%.
function cutPoint(text, room, sentence = false) {
  const max = Math.min(text.length, room);
  const win = text.slice(0, max + 1);
  if (sentence) {
    const stop = Math.max(win.lastIndexOf('. '), win.lastIndexOf('! '), win.lastIndexOf('? '), win.lastIndexOf('" '));
    if (stop + 1 >= max * 0.6) return stop + 1;
  }
  const space = win.lastIndexOf(' ');
  return space > max * 0.5 ? space : max;
}

// Splits text into [page, rest]. A page is at most limit bytes and ends with MORE when there is a
// rest.
function page(text, limit) {
  if (byteLen(text) <= limit) return [text, ''];
  const cut = cutPoint(text, Math.max(limit - byteLen(MORE), 1));
  return [text.slice(0, cut).trimEnd() + MORE, text.slice(cut).trimStart()];
}

// Splits text into up to maxParts messages of at most limit bytes each, cut at spaces (or after a
// sentence when that needs no more messages). Returns
// [parts, rest]: when text needs more messages than that, the last one ends with MORE and rest holds
// what is left. Fewer messages are used if the reply document would pass payloadMax bytes.
function pages(text, limit, maxParts = 4, payloadMax = 512) {
  for (let n = Math.max(1, maxParts); ; n--) {
    const parts = [];
    let rest = text;
    while (rest && parts.length < n) {
      if (parts.length === n - 1) { const [p, r] = page(rest, limit); parts.push(p); rest = r; break; }
      if (byteLen(rest) <= limit) { parts.push(rest); rest = ''; break; }
      // A cut after a sentence reads better, but only when it does not cost an extra message.
      let cut = cutPoint(rest, limit, true);
      const left = Math.ceil(byteLen(rest) / limit) - 1;
      if (byteLen(rest.slice(cut).trimStart()) > left * (limit - 10)) cut = cutPoint(rest, limit);
      parts.push(rest.slice(0, cut).trimEnd()); rest = rest.slice(cut).trimStart();
    }
    if (n === 1 || byteLen(replyPayload(parts)) <= payloadMax) return [parts, rest];
  }
}

// The reply document the bot expects: the first message in "text", any others in "more".
function replyPayload(parts) {
  return JSON.stringify(parts.length > 1 ? { text: parts[0], more: parts.slice(1) } : { text: parts[0] });
}

// One running game.
class Session {
  constructor(story) {
    this.out = '';
    this.saved = null;
    this.toRestore = null;
    this.over = false;
    const vm = new JSZM(Uint8Array.from(story));
    const self = this;
    vm.print = function* (text) { self.out += text; };
    vm.read = function* () { return yield 'read'; };
    vm.save = function* (buf) { self.saved = Uint8Array.from(buf); return true; };
    vm.restore = function* () { const b = self.toRestore; self.toRestore = null; return b; };
    this.gen = vm.run();
    this.intro = this.step(undefined);
  }

  // Feeds one line to the game and returns what it printed.
  step(line) {
    if (this.over) return '';
    this.out = '';
    let r;
    try { r = this.gen.next(line); } catch (e) { this.over = true; return this.out + ' [the game stopped: ' + e.message + ']'; }
    if (r.done) this.over = true;
    return this.out;
  }

  // The game position as bytes, or null when it could not be saved.
  snapshot() {
    this.saved = null;
    this.step('save');
    return this.saved;
  }

  // Loads a position made by snapshot(). Returns false when the game refused it.
  load(buf) {
    this.toRestore = Uint8Array.from(buf);
    this.step('restore');
    return this.toRestore === null && !this.over;
  }
}

// All players' games. dir holds one save file per player.
class Zork {
  constructor(story, dir, maxLive = 50) {
    this.story = story;
    this.dir = dir;
    this.maxLive = maxLive;
    this.live = new Map();      // player key -> { session, pending }
    fs.mkdirSync(dir, { recursive: true, mode: 0o700 });
  }

  file(key) { return path.join(this.dir, crypto.createHash('sha256').update(key).digest('hex').slice(0, 24) + '.sav'); }

  forget(key) {
    this.live.delete(key);
    try { fs.unlinkSync(this.file(key)); } catch (e) { /* no save yet */ }
  }

  // The player's entry, started or reloaded as needed. fresh is true for a brand-new game.
  open(key) {
    let e = this.live.get(key);
    if (e) { this.live.delete(key); this.live.set(key, e); return { e, fresh: false }; }
    e = { session: new Session(this.story), pending: '' };
    let fresh = true;
    try {
      const buf = fs.readFileSync(this.file(key));
      if (e.session.load(buf)) fresh = false; else e = { session: new Session(this.story), pending: '' };
    } catch (err) { /* no save: a new game */ }
    this.live.set(key, e);
    while (this.live.size > this.maxLive) this.live.delete(this.live.keys().next().value);
    return { e, fresh };
  }

  persist(key, e) {
    const buf = e.session.snapshot();
    if (!buf) return;
    const f = this.file(key), tmp = f + '.tmp';
    fs.writeFileSync(tmp, buf, { mode: 0o600 });
    fs.renameSync(tmp, f);
  }

  // Answers one request from a player: an array of 1 to maxParts messages of at most limit bytes each.
  play(key, args, limit, maxParts = 4) {
    const cmd = cleanCommand(args);
    const send = (e, text) => { const [parts, rest] = pages(text, limit, e ? maxParts : 1); if (e) e.pending = rest; return parts; };

    if (cmd === 'help' || cmd === '?') return send(null, HELP);
    if (cmd === 'restart') {
      this.forget(key);
      const { e } = this.open(key);
      this.persist(key, e);
      return send(e, 'New game. ' + intro(e.session.intro));
    }
    // "more" is not a word the game knows, so "read leaflet more" can only mean the next page.
    if (cmd === 'more' || cmd.endsWith(' more')) {
      const e = this.live.get(key);
      if (!e || !e.pending) return send(null, 'Nothing more to show. Try: zork look');
      return send(e, e.pending);
    }
    if (/^(save|restore|quit|q|script|unscript)$/.test(cmd)) {
      return send(null, 'Your game is saved after every move. zork restart starts a new one.');
    }

    const { e, fresh } = this.open(key);
    if (cmd === '') {
      if (!fresh) return send(e, HELP);
      this.persist(key, e);
      return send(e, intro(e.session.intro));
    }
    let text = tidy(e.session.step(cmd));
    if (e.session.over || GAME_OVER.test(text)) {
      text = text.replace(/Would you like to restart.*$/i, '').trim();
      this.forget(key);
      return send(null, (text + ' Game over. Your next command starts a new game.').trim());
    }
    this.persist(key, e);
    return send(e, text || 'Nothing happens.');
  }
}

module.exports = { Zork, Session, tidy, intro, cleanCommand, page, pages, replyPayload, HELP, MORE };
