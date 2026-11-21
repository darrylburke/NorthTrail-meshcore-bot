# MQTT responder (example)

A small command-line program that answers the bot's MQTT requests, and can also act as the bot so it can be tested without a radio.

When the bot receives a command it does not know, and MQTT is switched on, it publishes a JSON request to the broker. A program listening there may answer within the bot's wait time; the bot then sends the answer to the mesh. This tool is a working example of such a program: you give it the commands and replies on the command line.

## Install

Needs Node.js 18 or newer.

```
cd tools/mqtt-responder
npm install
npm test          # offline tests, no broker needed
node responder.js --help
```

The only dependency is `mqtt` (version 5).

## Options

Every option is also listed by `node responder.js --help`.

| Option | Meaning |
|---|---|
| `--host HOST` | Broker host (default `localhost`). |
| `--port PORT` | Broker port (default `1883`). |
| `--tls` | Use TLS (`mqtts`). |
| `--user NAME` | Username. |
| `--pass-file FILE` | File holding the password: the `xxx` of a line that starts `password: xxx` if the file has one, otherwise its first line. |
| `--pass PASSWORD` | Password on the command line. **It is visible in `ps` and in your shell history.** Prefer `--pass-file` or the `MQTT_PASS` environment variable. |
| `--prefix TOPIC` | Topic prefix, the one set on the bot's page (default `meshcore/MeshBot`). 1 to 64 letters, digits, `_`, `-` and `/`; no leading, trailing or doubled `/`. |
| `--cmd NAME=REPLY` | Answer command `NAME` with `REPLY`. Repeat for more. `NAME` is 1 to 16 letters or digits, matched without regard to case. |
| `--any REPLY` | Answer every command not matched by `--cmd`. |
| `--desc NAME=TEXT` | One-line description for a command, shown in the bot's `help` (the bot cuts it to 48 bytes). |
| `--announce` | Publish each `--cmd` with its description, retained, so the bot lists it in `help`. Cleared again when the program exits. |
| `--delay MS` | Wait before replying, to test the bot's wait time. |
| `--once` | Exit after the first reply. |
| `--max-len N` | Cut replies to N bytes when the request has no `max_len` (default 100, at most 200). With `--simulate` it is the `max_len` put in the simulated request. |
| `--raw-braces` | Copy `{` and `}` from the request unchanged into `%args%`, `%sender%`, `%cmd%` and `%field:NAME%` (see Placeholders). |
| `--simulate "NAME ARGS"` | Act as the bot: publish one request and print the reply. |
| `--dm` | Simulate a direct message from a known contact instead of a channel message (see the request fields). |
| `--sender NAME` | Sender name in the simulated request (default `Tester`). |
| `--wait MS` | How long the simulator waits for a reply (default 5000). |
| `--quiet` | Print replies only. |
| `--help` | Show the usage text. |

The simulator exits with 0 when it received a usable reply, 1 on a broker or publish error, 2 on a usage error, 3 when no reply came within `--wait`, 4 when a reply came that the bot would ignore, and 5 when it could not connect and publish within 10 seconds. The command name must be 1 to 16 letters or digits, and arguments that make the request exceed 1000 bytes are refused (the bot itself cuts them).

The password can also be given in the environment variable `MQTT_PASS`. The program never prints it.

### Examples

Answer one command and announce it in the bot's help:

```
node responder.js --host broker.example.org --user bot --pass-file ./mqtt.txt \
     --cmd "beer=Cheers {sender}, one %args% coming up" --desc "beer=order a drink" --announce
```

The same with the password from the environment:

```
MQTT_PASS='...' node responder.js --host broker.example.org --user bot --cmd "ping=pong" --once
```

Simulate a direct message instead of a channel message:

```
node responder.js --host broker.example.org --user bot --pass-file ./mqtt.txt --simulate "beer ipa" --dm
```

Test it from a second terminal, with no radio and no bot (the simulator publishes a request shaped like the bot's and prints the reply):

```
node responder.js --host broker.example.org --user bot --pass-file ./mqtt.txt --simulate "beer ipa"
```

A different topic prefix, TLS, a catch-all and a delayed reply:

```
node responder.js --host broker.example.org --port 8883 --tls --user bot --pass-file ./mqtt.txt \
     --prefix meshcore/OtherBot --any "No such command, {sender}" --delay 2000 --max-len 120 --quiet
```

## Message format

Topics, all under the prefix:

| Topic | Direction | Content |
|---|---|---|
| `<prefix>/request` | bot publishes | the request (below) |
| `<prefix>/reply/<id>` | program publishes | `{"text":"..."}` |
| `<prefix>/commands/<name>` | program publishes, retained | `{"desc":"..."}`; an empty payload removes the command |
| `<prefix>/status` | bot publishes | its online/offline state |
| `<prefix>/commandlist` | bot publishes, retained, QoS 0 | `{"commands":[...]}`, ending with `"more":true` when it did not all fit |

### The request

In this order. Values shown are examples.

| Field | Type | Meaning |
|---|---|---|
| `id` | string | 8 lower-case hex characters; the reply topic ends with it |
| `reply_to` | string | `<prefix>/reply/<id>`, where to answer |
| `cmd` | string | the command name, lower case, without the prefix character |
| `args` | string | the rest of the line (cut by the bot if the request would be too long) |
| `sender` | string | the sender's name |
| `sender_key` | string | the first 6 bytes of the sender's public key, 12 hex characters; empty on a channel message and for a direct message from an unknown contact |
| `sender_lat`, `sender_lon` | number | the sender's position, 5 decimals; 0 unless the message is a direct message from a known contact with a position |
| `dm` | boolean | true for a direct message, false for a channel message |
| `channel` | string | the channel name; empty on a direct message |
| `hops` | number | hops the message travelled; 0 when it came direct |
| `path` | string | the path's hop ids (may be emptied by the bot to fit) |
| `path_names` | string | names for those hops (may be emptied by the bot to fit) |
| `snr` | number | signal to noise, dB |
| `rssi` | number | signal strength, dBm |
| `time`, `date`, `tz` | string | the bot's local time, date and time zone |
| `unix` | number | Unix time in seconds; 0 when the bot's clock is not set |
| `bot` | string | the bot's name |
| `scope` | string | the bot's region scope, if any |
| `uptime` | string | the bot's uptime |
| `bot_lat`, `bot_lon` | number | the position set on the bot's page, 5 decimals |
| `max_len` | number | how many bytes of reply text fit in one mesh message |

The simulator fills these as the bot does: by default a channel message (`dm` false, `channel` `public`, empty `sender_key`, positions 0), with `--dm` a direct message from a known contact (`dm` true, empty `channel`, a 12-hex key and a position). All coordinates in the simulator are made up. A request is at most 1000 bytes (the bot empties `path_names`, then `path`, then cuts `args` to fit). `cmd`, `args` and `sender` are text typed by anyone on the mesh: never run them as a command, put them in a file name, a URL or a query, or trust them in any way.

### The reply

Publish `{"text":"..."}` on the `reply_to` topic (this tool builds the topic from the prefix and `id` itself). The bot ignores the reply when:

- it arrives after the bot's wait time (1 to 15 seconds, set on the bot's page);
- the payload is over 512 bytes;
- it is not a JSON object with a string `text`;
- the text, with control characters turned into spaces and the ends trimmed, is empty or over 200 bytes.

A reply may carry up to three further messages: `{"text":"first","more":["second","third"]}`. The bot sends each as a mesh message of its own, a few seconds apart, each with its mention and each counted against the replies-per-minute cap. Every string follows the rule for `text`, the whole payload must still be within 512 bytes, and a `more` that is not an array of 1 to 3 such strings makes the bot ignore the whole reply. The page's Try box shows only the first message, marked `[1/N]`. This tool itself always sends a single message.

This tool therefore cleans the text, cuts it on a UTF-8 character boundary to the request's `max_len` (or `--max-len`, never over 200 bytes), and refuses with a message on the screen an empty text or a payload over 512 bytes. In the simulator, a reply that the bot would ignore is reported as such.

The bot then renders the text once with its own placeholders, adds its own mention of the sender at the start, and keeps the whole message within one mesh message, so `max_len` is already what is left for your text. Because the bot expands `{...}` after your cut, a text with placeholders may be shorter or longer than you expect on the air. The mention is always added, even when your text itself begins with `@[...]`, so a reply can never pass for one addressed to someone other than the asker.

In a reply, the bot's `{args}` is the request's `args` cut to 47 bytes and `{path}` is the path cut to 39 bytes (both on a character boundary): a reply is one mesh message, so longer is never needed. Use `%args%` or `%field:path%` for the request's own, longer values.

### Placeholders

| Placeholder | Filled by | Value |
|---|---|---|
| `{sender}` `{hops}` `{snr}` `{rssi}` `{path}` `{args}` `{time}` `{date}` `{channel}` `{uptime}` | the bot | from the message, when it sends the reply |
| `{fetch}` | the bot | empty for a reply from the broker |
| `%args%` | this program | request `args` |
| `%sender%` | this program | request `sender` |
| `%cmd%` | this program | request `cmd` |
| `%now%` | this program | the local time of the machine running the tool, `HH:MM` |
| `%field:NAME%` | this program | any request field, e.g. `%field:hops%`, `%field:bot%` (empty if absent) |

The bot renders the whole reply, including text you copied in, with its own `{...}` placeholders, and `args` and `sender` are typed by anyone on the mesh. So that a stranger cannot make the bot expand `{sender}`, `{path}` and the rest, values copied by `%args%`, `%sender%`, `%cmd%` and `%field:NAME%` have `{` and `}` replaced with `(` and `)`. Braces you write in the reply itself (such as `{sender}` in a `--cmd` reply) are never changed. `--raw-braces` turns the replacement off. Placeholders are filled once; a value that looks like a placeholder is not expanded again by this tool.

### Announcements

`--announce` publishes `{"desc":"..."}`, retained, on `<prefix>/commands/<name>`. The name is 1 to 16 characters of `a-z` and `0-9`. An announcement over 256 bytes is ignored by the bot; the description is cut to 48 bytes (this tool does the same, on a character boundary). Announced commands appear in the bot's `help` only while the bot is connected to the broker (they are forgotten when it is not, and the broker sends them again when it reconnects). The bot stores a description with `"` replaced by `'`, `\` by `/`, `<` by `(` and control characters by spaces; that is how `help <name>`, the bot's page and its published command list show it. On exit the tool publishes an empty retained payload to remove them.

## What the bot does before it asks the broker

- A command is offered to the broker only if the bot has no such command itself. A command the owner has switched off is never offered.
- In a direct message the bot offers a command only when the sender typed the command prefix (for example `!beer`).
- The bot waits for a reply for the wait time set on its page, from 1 to 15 seconds. A reply that arrives later is ignored. With `--delay` you can test that.
- At most 4 requests wait at once; a fifth command is not asked.
- At most 30 requests are published per minute; beyond that the command is not asked.
- If the same command with the same arguments from the same destination is already waiting, it is not asked again.
- The bot's own reply cap is counted when the reply is sent, so commands nobody answers do not use it.

## Broker access example (Mosquitto)

A user that can only use the bot's topics. In `mosquitto.conf`:

```
password_file /etc/mosquitto/passwd
acl_file /etc/mosquitto/acl
```

Create the user:

```
mosquitto_passwd -c /etc/mosquitto/passwd bot
```

Then give it access only under the prefix, in `/etc/mosquitto/acl`:

```
user bot
topic readwrite meshcore/MeshBot/#
```

Use one user for the bot and a separate one for the responder if you can; the responder then needs only read on `request` and `commands`, and write on `reply/#` and `commands/#`.

## Security

- The connection is plain TCP unless you use `--tls`, and the bot's own connection is plain TCP. The password and every request cross the network in the clear. Use a broker on your own network.
- Whoever can publish under the prefix can make the bot speak, by answering requests or by announcing commands. Keep the broker's access rules tight (see above).
- Use `--pass-file` (kept readable only by you) or `MQTT_PASS`. A password given with `--pass` is visible to other users of the machine.
- Treat `cmd`, `args` and `sender` as untrusted text.
- `sender` and `args` are typed by strangers and are not guaranteed to be valid UTF-8 (the bot copies the bytes it received). Decode them leniently, so a malformed byte becomes U+FFFD instead of an error. In Node, `JSON.parse(payload.toString('utf8'))` already does this. In Python, `json.loads(payload.decode('utf-8', errors='replace'))`.
- A hostile or stalled broker (one that stops answering, or keeps the connection busy) can stop the bot from applying a new MQTT setting until it is restarted, while its local commands keep working; the page then says "cannot restart: reboot the bot".
