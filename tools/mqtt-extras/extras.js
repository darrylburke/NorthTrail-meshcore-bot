#!/usr/bin/env node
'use strict';
// Extra commands for the MeshCore bot, answered over MQTT by one program:
//   zork     plays Zork I, one saved game per sender
//   dadjoke  a random joke from icanhazdadjoke.com

const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const { Zork, replyPayload } = require('./game.js');
const { jokeThatFits } = require('./joke.js');

const REPLY_TEXT_MAX = 200;      // the bot refuses longer reply texts
const DEFAULT_MAX = 100;
const COMMANDS = { zork: 'play Zork: zork look', dadjoke: 'a random dad joke' };   // name -> help text

const HELP = `
Usage:
  node extras.js --host HOST --user NAME --pass-file FILE --prefix TOPIC [options]

  --host HOST        Broker host                          (default: localhost)
  --port PORT        Broker port                          (default: 1883)
  --tls              Use TLS (mqtts)
  --user NAME        Username
  --pass-file FILE   File holding the password: the "xxx" of a line that starts
                     "password: xxx" if there is one, otherwise the first line.
                     The MQTT_PASS environment variable also works.
  --prefix TOPIC     Topic prefix, as set on the bot's page (default: meshcore/MeshBot)
  --commands LIST    Which commands to answer             (default: zork,dadjoke)
  --story FILE       Zork game file                       (default: story/zork1.z3)
  --saves DIR        Where saved Zork games are kept      (default: saves)
  --parts N          Most messages one Zork reply may use, 1 to 4 (default: 4).
                     Use 1 with bot firmware that sends only one message per reply.
  --no-announce      Do not list the commands in the bot's help
  --quiet            Print errors only
  --help             Show this text

Get the Zork game file once with:  npm run fetch-story
People send, with the bot's prefix:  !dadjoke   !zork   !zork open mailbox   !zork more   !zork restart
`;

class UsageError extends Error {}

function parseArgs(argv) {
  const o = { host: 'localhost', port: 1883, tls: false, user: undefined, passFile: undefined, prefix: 'meshcore/MeshBot',
              commands: Object.keys(COMMANDS), story: path.join(__dirname, 'story', 'zork1.z3'), saves: path.join(__dirname, 'saves'),
              announce: true, quiet: false, help: false, parts: 4 };
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    const need = () => { if (i + 1 >= argv.length) throw new UsageError(a + ' needs a value'); return argv[++i]; };
    switch (a) {
      case '--host': o.host = need(); break;
      case '--port': o.port = Number(need()); if (!Number.isInteger(o.port) || o.port < 1 || o.port > 65535) throw new UsageError('--port must be 1 to 65535'); break;
      case '--tls': o.tls = true; break;
      case '--user': o.user = need(); break;
      case '--pass-file': o.passFile = need(); break;
      case '--prefix': o.prefix = need().replace(/\/+$/, ''); break;
      case '--commands': {
        o.commands = [...new Set(need().toLowerCase().split(',').map((s) => s.trim()).filter(Boolean))];
        const bad = o.commands.filter((c) => !(c in COMMANDS));
        if (bad.length || !o.commands.length) throw new UsageError('--commands takes a list from: ' + Object.keys(COMMANDS).join(','));
        break;
      }
      case '--story': o.story = need(); break;
      case '--saves': o.saves = need(); break;
      case '--parts': o.parts = Number(need()); if (!Number.isInteger(o.parts) || o.parts < 1 || o.parts > 4) throw new UsageError('--parts must be 1 to 4'); break;
      case '--no-announce': o.announce = false; break;
      case '--quiet': o.quiet = true; break;
      case '--help': case '-h': o.help = true; break;
      default: throw new UsageError('unknown option ' + a);
    }
  }
  if (!o.prefix || /[#+\s]/.test(o.prefix)) throw new UsageError('--prefix must be a plain topic without # + or spaces');
  return o;
}

function readPassword(o, env = process.env) {
  if (o.passFile) {
    let text;
    try { text = fs.readFileSync(o.passFile, 'utf8'); } catch (e) { throw new UsageError('cannot read ' + o.passFile + ': ' + e.code); }
    const lines = text.split(/\r?\n/);
    for (const line of lines) {
      const m = line.match(/^\s*password\s*:\s*(.+?)\s*$/i);
      if (m) return m[1];
    }
    return lines[0].trim();
  }
  return env.MQTT_PASS || undefined;
}

// Who is asking: the contact's key in a direct message, otherwise the sender's name.
function playerKey(req) {
  if (typeof req.sender_key === 'string' && req.sender_key) return 'k:' + req.sender_key.toLowerCase();
  return 'n:' + String(req.sender ?? '').trim().toLowerCase();
}

// A request for one of the commands in names, or null.
function parseRequest(names, raw) {
  let req;
  try { req = JSON.parse(raw); } catch (e) { return null; }
  if (!req || typeof req !== 'object' || typeof req.cmd !== 'string') return null;
  const cmd = req.cmd.toLowerCase();
  if (!names.includes(cmd)) return null;
  if (typeof req.id !== 'string' || !/^[0-9a-f]{8}$/.test(req.id)) return null;
  const limit = Math.min(Number.isInteger(req.max_len) && req.max_len >= 20 ? req.max_len : DEFAULT_MAX, REPLY_TEXT_MAX);
  return { id: req.id, cmd, limit, key: playerKey(req), args: String(req.args ?? '') };
}

// The messages to send for a parsed request (an empty array means do not answer). ctx: { zork, getJoke, parts }.
async function replyFor(r, ctx) {
  if (r.cmd === 'zork') return ctx.zork.play(r.key, r.args, r.limit, ctx.parts ?? 4);
  if (r.cmd === 'dadjoke') { const j = await (ctx.getJoke ? jokeThatFits(r.limit, ctx.getJoke) : jokeThatFits(r.limit)); return j ? [j] : []; }
  return [];
}

function main(argv) {
  let o, password;
  try { o = parseArgs(argv); if (!o.help) password = readPassword(o); } catch (e) { console.error(e.message + '\n' + HELP); process.exit(2); }
  if (o.help) { console.log(HELP); return; }
  const ctx = { parts: o.parts };
  if (o.commands.includes('zork')) {
    let story;
    try { story = fs.readFileSync(o.story); } catch (e) { console.error('cannot read the game file ' + o.story + ' (run: npm run fetch-story, or use --commands dadjoke)'); process.exit(2); }
    ctx.zork = new Zork(story, o.saves);
  }
  const log = (s) => { if (!o.quiet) console.log(s); };

  const mqtt = require('mqtt');
  const client = mqtt.connect((o.tls ? 'mqtts://' : 'mqtt://') + o.host + ':' + o.port, {
    username: o.user, password, clientId: 'bot-extras-' + crypto.randomBytes(4).toString('hex'),
    clean: true, reconnectPeriod: 3000, connectTimeout: 10000,
  });
  const reqTopic = o.prefix + '/request';
  const annTopic = (name) => o.prefix + '/commands/' + name;

  client.on('error', (e) => {
    console.error('mqtt error: ' + e.message);
    if (/not authori[sz]ed|bad user/i.test(e.message)) process.exit(1);
  });
  client.on('connect', () => {
    log('connected to ' + o.host + ':' + o.port + (o.user ? ' as ' + o.user : ''));
    client.subscribe(reqTopic, { qos: 0 }, (err) => {
      if (err) return console.error('subscribe failed: ' + err.message);
      log('answering ' + o.commands.join(', ') + ' on ' + reqTopic);
    });
    announce();
  });
  // Announced again every 10 minutes, so the bot's help recovers if another copy of this program
  // cleared the announcements when it stopped, or the broker lost them.
  const announce = () => { if (o.announce && client.connected) for (const c of o.commands) client.publish(annTopic(c), JSON.stringify({ desc: COMMANDS[c] }), { qos: 1, retain: true }); };
  setInterval(announce, 10 * 60 * 1000).unref();
  client.on('message', (topic, buf) => {
    if (topic !== reqTopic || buf.length > 1000) return;
    const r = parseRequest(o.commands, buf.toString('utf8'));
    if (!r) return;
    replyFor(r, ctx).then((parts) => {
      if (!parts.length) return console.error(r.cmd + ': nothing to send (not answering)');
      client.publish(o.prefix + '/reply/' + r.id, replyPayload(parts), { qos: 0 });
      log('[' + r.key.slice(0, 12) + '] ' + r.cmd + ' ' + r.args + '  =>  ' + parts.join('  //  '));
    }).catch((e) => console.error(r.cmd + ' error: ' + e.message));
  });

  let stopping = false;
  const stop = () => {
    if (stopping) return; stopping = true;
    const end = () => client.end(false, {}, () => process.exit(0));
    if (o.announce && client.connected) {
      let left = o.commands.length;
      for (const c of o.commands) client.publish(annTopic(c), '', { qos: 1, retain: true }, () => { if (--left === 0) end(); });
    } else end();
    setTimeout(() => process.exit(0), 3000).unref();
  };
  process.on('SIGINT', stop); process.on('SIGTERM', stop);
}

module.exports = { parseArgs, readPassword, playerKey, parseRequest, replyFor, UsageError, HELP, COMMANDS };
if (require.main === module) main(process.argv.slice(2));
