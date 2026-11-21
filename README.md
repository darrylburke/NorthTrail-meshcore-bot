# MeshCore chat bot for the Xiao S3 WIO

A chat bot built into the [MeshCore](https://github.com/meshcore-dev/MeshCore) WiFi companion
firmware. The node stays a normal companion radio that the MeshCore app can connect to, and it
also answers commands sent to it over the mesh: signal checks, who is around, weather, your own
canned replies, and anything an outside program cares to answer over MQTT.

```
You:  !ping
Bot:  @[You] 2 hops, SNR 7.5

You:  !wx
Bot:  @[You] Here: 6°C feels 2, fog, wind S 20 km/h, 100% RH | today 4/14°C, 0% precip
```

It runs unattended with nothing attached but power. Everything is configured from a web page the
node serves on your local network.

This repository is a fork of [agessaman/MeshCore](https://github.com/agessaman/MeshCore)
(`observer-firmware`, v1.18.0), which is itself a fork of MeshCore. The original MeshCore README is
kept as [README-MeshCore.md](README-MeshCore.md). All of the bot is behind one build flag,
`WITH_BOT`; without it the firmware builds exactly as before.

## Contents

- [What it does](#what-it-does)
- [Hardware](#hardware)
- [Build and flash](#build-and-flash)
- [First-time setup](#first-time-setup)
- [Using the bot](#using-the-bot)
- [Built-in commands](#built-in-commands)
- [Custom commands](#custom-commands)
- [Text rules](#text-rules)
- [The web page](#the-web-page)
- [Settings](#settings)
- [MQTT: commands answered by your own programs](#mqtt-commands-answered-by-your-own-programs)
- [Example programs](#example-programs)
- [Serial commands](#serial-commands)
- [Security and privacy](#security-and-privacy)
- [How the code is organised](#how-the-code-is-organised)
- [Tests](#tests)
- [Limits and known gaps](#limits-and-known-gaps)
- [Licence](#licence)

## What it does

- Answers commands on up to four channels and in direct messages.
- Fourteen built-in commands, each of which can be renamed or switched off.
- Up to sixteen custom commands with fill-in placeholders, optionally fetching one value from a
  JSON web address.
- Hands any command it does not know to an MQTT broker, so a program on another machine can answer
  it. Two examples are included: a dad-joke command and a playable Zork I.
- Limits itself: a cooldown per sender, a cap on replies per minute, and an ignore list so two
  bots cannot set each other off.
- A password-protected web page for settings, live status, a log of recent commands, and a box
  to try a command without transmitting anything.
- Keeps the node reachable: it advertises on a schedule and accepts new contacts automatically.

## Hardware

- Seeed Studio XIAO ESP32-S3 with the Wio-SX1262 LoRa board ("Xiao S3 WIO").
- A WiFi network with internet access, for the web page, the clock, the internet commands and MQTT.

The bot's logic is plain C++ with no hardware dependencies, but the device code has only been
built and run on this board. Other ESP32-S3 boards would need their own build environment.

## Build and flash

You need [PlatformIO](https://platformio.org/install/cli).

```sh
git clone <this repository>
cd <its folder>
pio run -e Xiao_S3_WIO_companion_radio_wifi_bot -t upload --upload-port /dev/ttyACM0
```

- The build environment is `Xiao_S3_WIO_companion_radio_wifi_bot`. It is the stock WiFi companion
  build plus `WITH_BOT=1`, with no WiFi name or password compiled in.
- To stamp your own version string: `PLATFORMIO_BUILD_FLAGS='-DFIRMWARE_VERSION=\"my-version\"'`.
- If the board does not show up as a serial port, hold BOOT while plugging it in.
- **Back up the node first** if it is already in use. Flashing replaces the firmware only, but if
  you erase the flash you lose the node's identity, contacts and channels.

Size on this board: about 47% of flash and 54% of RAM at build time. With the bot connected to a
broker roughly 79 KB of heap stays free.

## First-time setup

1. **Fresh node, no WiFi saved.** The node opens a temporary access point named `MeshBot-XXXX`
   (the last part comes from its public key). Join it and open the setup page it serves. Enter
   your WiFi name and password, a password for the web page, and the bot's name and location.
   The node restarts and joins your network. The access point is open, exists only while the node
   has no usable WiFi, and closes after 15 minutes without use.
2. **Node that already has WiFi saved.** There is no setup page. Set the web password over USB
   serial with `set bot.webpwd <password>` (see [Serial commands](#serial-commands)).
3. **Open the web page** at `http://meshbot-xxxx.local/` or the node's IP address. Both are
   printed on the serial console at boot. Log in and set the channels, scope, location and time
   zone.
4. **Give the node its channels.** The bot only hears channels the node itself has. Add them with
   the MeshCore app; the web page marks any listed channel the node does not have.

There is no default password.

## Using the bot

- **Each command answers to exact text you choose:** its name with the prefix (`!ping`), without
  it (`ping`), or both. On the web page the box beside each built-in and custom command shows
  that text, for example `!ping, ping`. The default is the prefixed form only.
- **On a channel**, a command is answered only in a form listed for it. Listing the bare word
  means a chat line that happens to start with it is answered too.
- **In a direct message**, the bot's own commands are answered in either form.
- **A command answered over MQTT** needs the prefix, unless "also answer commands from the broker
  written without the prefix" is on and a program has announced the name. Ordinary chat sent to
  the bot is never forwarded to an outside program.
- **Text rules** let the bot answer messages that are not written as commands at all. See
  [Text rules](#text-rules).
- **Replies** go back where the command came from: the same channel, or a direct message. Every
  reply starts with a mention of the asker, `@[name]`. The bot never treats a message starting
  with `@[` as a command, which keeps bots from answering each other.
- **One mesh message per reply.** Text that does not fit is cut. The exception is an MQTT reply
  that asks for several messages (up to four).
- **Silence means no.** An unknown command, a failed look-up on a channel, a sender in cooldown or
  a full minute all produce nothing. In a direct message a failed internet command says so.
- **Order of look-up:** text rules first; then built-in command, custom command, MQTT.
- A direct message only arrives if the bot knows the sender, so the two nodes must have heard each
  other's adverts first.

## Built-in commands

| Command | Reply | Needs internet |
|---|---|---|
| `ping` | Hop count and signal of your message, then the route it took: one repeater to a line, by name where known | no |
| `test` | Path, SNR and RSSI of your message | no |
| `path` | The repeaters your message came through, by name where known | no |
| `help` | The command list and the help link; `help 2` continues; `help <command>` gives one line | no |
| `heard` | Nodes heard in the last 24 hours | no |
| `mesh` | Traffic summary for the last hour | no |
| `dist` | How far and in which direction you are from the bot | no |
| `sun` | Sunrise, sunset and moon phase at the bot's location | no (needs the clock) |
| `roll` | Dice, e.g. `roll 2d6`; up to ten dice of a hundred sides | no |
| `8ball` | A magic 8-ball answer | no |
| `wx [place]` | Current weather and today's forecast, at the bot's location or a named place | yes |
| `aurora` | Kp index and a northern-lights outlook | yes |
| `alerts` | Active weather alerts for the bot's location (Environment Canada) | yes |
| `nodes` | Online or offline for up to four watched nodes or observers | yes |

Notes:

- `ping` lists each repeater as `a53e: Name`, or `a53e: ?` when the bot has no name for it.
  Only whole lines are sent. A route that does not fit one message carries on in further
  messages (`route, part 2` and so on, about 4 seconds apart, up to three in all, each counted
  against the per-minute cap). That happens only when no other reply is waiting to go out;
  otherwise, and for a route too long even for three messages, the last line reads `+N more`. A direct or
  zero-hop message has no list.
- `heard` and `mesh` use counters kept in memory; they start again after a reboot.
- `dist` needs your position. In a direct message the bot uses your contact's advertised position.
  On a channel it matches your name against its contacts, and says the position is unknown when
  there is no match or more than one.
- `alerts` covers Canada only. `nodes` reads the MeshCore Canada analyzer.
- Data sources, none of which need a key: Open-Meteo (weather and place look-up), NOAA SWPC
  (Kp forecast), Environment Canada (`api.weather.gc.ca`), and `live.meshcore.ca`. HTTPS servers
  are verified against a certificate bundle built into the firmware.

## Custom commands

Up to sixteen, edited on the web page. Each has a trigger word, a reply, a one-line description
for `help`, and an on/off switch.

The reply may contain placeholders the bot fills in from the message it is answering:

| Placeholder | Value |
|---|---|
| `{sender}` | The sender's name |
| `{hops}` | Hop count |
| `{snr}`, `{rssi}` | Signal of the received message |
| `{path}` | The repeaters it came through |
| `{args}` | The rest of the line after the command |
| `{time}`, `{date}` | Local time and date |
| `{channel}` | The channel name |
| `{uptime}` | How long the bot has been running |
| `{fetch}` | A value fetched from the web (see below) |

Example: trigger `hello`, reply `Hi {sender}, heard you at {snr} dB`.

**Live look-ups.** A custom command may also have a web address (`http` or `https`, GET only),
the path of one value inside the JSON it returns, and how many seconds to reuse the last answer.
The value is available as `{fetch}`. `{args}` may be used inside the address and is URL-encoded.
A slow or failed look-up sends nothing.

Custom replies always get the asker's mention in front. A reply that already begins with
`@[{sender}]` is not doubled.

## Text rules

Up to eight, edited on the web page. A rule says which message to look for and which command to
run when it arrives, exactly as if that command had been sent with the prefix. The command can
be a built-in, a custom command or one answered over MQTT.

| Kind | Matches | Example text | Example command |
|---|---|---|---|
| Exact text | The whole message, ignoring capitals and spaces at the ends. It may include the prefix | `good morning` | `hello` |
| Pattern | A regular expression found anywhere in the message. Use `^` and `$` for the start and end | `^weather (?:in )?(.+)$` | `wx {1}` |

- In the command, `{0}` is the text that matched and `{1}` to `{9}` are a pattern's groups. The
  command name itself must be written out, so a stranger's text can never choose which command
  runs.
- Rules are tried from the top before the message is read as a command, and the first that fits
  wins. A message starting with `@[` (a bot's reply) never matches.
- Patterns support `.` `^` `$` `\b` `\d` `\w` `\s`, sets such as `[a-z]` and `[^,]`, groups
  `( )` and `(?: )`, alternatives `a|b`, and the repeats `*` `+` `?`. Capitals are ignored. Not
  supported: `{n,m}` repeats, lazy repeats such as `*?`, look-around, back-references, and
  letter escapes such as `\n` or `\B`. The page refuses a pattern the bot cannot run and says why.
- A pattern is matched against text strangers send, so the work is bounded: a search that takes
  too many steps or nests more than 16 deep is given up and counts as no match. Keep patterns
  simple; a repeated group such as `(\w+ )+` stops matching beyond about a dozen rounds, so
  prefer `.*` or a set such as `[a-z ]*`.
- A text or pattern is at most 48 bytes, and so is the command.

## The web page

Served over plain HTTP on port 80, on your local network only.

- **Status:** uptime, WiFi, clock, nodes heard, free memory, and the MQTT connection.
- **Try a command:** runs a command as if it had arrived in a direct message and shows the reply.
  Nothing is transmitted. Text rules are tried too. To try an MQTT command, type it with the
  prefix; the page shows only the first message of a multi-message reply, marked `[1/N]`.
- **Settings,** the built-in commands (rename or switch off), custom commands, text rules,
  watched nodes and MQTT.
- **Advertise now,** and a change-password form.
- **Recent:** the last commands and replies, kept in memory only.
- **Node:** name, channels and default scope, read-only (set these in the MeshCore app).

## Settings

Stored on the node in `/bot.json`.

| Setting | Default | Range or meaning |
|---|---|---|
| Channels | `#bot` | 1 to 4, each `#name` or `Public`. One scope for all of them |
| Scope | none | The region scope replies and adverts carry |
| Prefix | `!` | One character |
| Broker commands without the prefix | off | On: a command a program on the broker has announced is answered without the prefix |
| Answers to (per command) | `!name` | For each built-in and custom command: with the prefix, without it, or both |
| Text rules | none | Up to 8. See [Text rules](#text-rules) |
| Cooldown | 10 s | 0 to 3600. Minimum time between replies to one sender |
| Max per minute | 6 | 1 to 30. Replies to everyone, on all channels together |
| Ignore | `bot` | Sender names containing any of these are ignored, as is the bot's own name |
| Latitude, longitude | 0, 0 | The node's advertised position, and the place `sun`, `wx`, `alerts` and `dist` use |
| Time zone | `America/Toronto` | An IANA zone name |
| Help link | none | Appended to the `help` reply |
| Zero-hop advert | 240 min | 0 turns it off; at most 10080 |
| Flood advert | 1440 min | 0 turns it off; at most 10080 |
| Watched nodes | none | Up to 4: a label plus an analyzer observer id or a node public key |
| Watched online within | 720 min | 5 to 10080. A plain node counts as online if seen within this |
| MQTT | off | See the next section |

Think before adding `Public` to the channels: a dedicated bot channel keeps replies out of
general chat. Raising the per-minute cap makes the bot more responsive and the channel busier.

## MQTT: commands answered by your own programs

With MQTT switched on, a command that no built-in or custom command handles is published to a
broker. Any program subscribed there can answer it, and the bot sends the answer on the mesh. The
bot firmware does not need to change to add a command.

### Connection

- Plain TCP (`mqtt://`) with a username and password; default port 1883. **The connection is not
  encrypted**, so use a broker on a network you trust, and give the bot its own login restricted
  to its topic prefix.
- Settings: host, port, user, password, topic prefix (default `meshcore/<node name>`), and how
  long to wait for an answer (default 5 s, 1 to 15).
- The password is never sent back to the browser. If you change the host or port without entering
  a password, the stored password is cleared, so it is never sent to a different broker.

### Topics

| Topic | Direction | Content |
|---|---|---|
| `<prefix>/status` | bot to broker, retained, with a last will | `{"state":"online","version":"...","bot":"...","uptime_s":86}` or `{"state":"offline"}` |
| `<prefix>/request` | bot to broker | One request (below) |
| `<prefix>/reply/<id>` | program to bot | `{"text":"..."}` |
| `<prefix>/commands/<name>` | program to bot, retained | `{"desc":"one line"}`; an empty payload removes it |
| `<prefix>/commandlist` | bot to broker, retained | Every enabled command with its description and source (`builtin`, `custom`, `mqtt`) |

### A request

```json
{"id":"9a30ad61","reply_to":"meshcore/MeshBot/reply/9a30ad61","cmd":"beer","args":"two pints",
 "sender":"Ann","sender_key":"","sender_lat":0.00000,"sender_lon":0.00000,"dm":false,
 "channel":"#bot","hops":2,"path":"a53e96,bcadb8","path_names":"","snr":7.5,"rssi":-98,
 "time":"04:22","date":"2026-01-01","tz":"America/Toronto","unix":1767241320,"bot":"MeshBot",
 "scope":"","uptime":"2m","bot_lat":0.00000,"bot_lon":0.00000,"max_len":105}
```

| Field | Meaning |
|---|---|
| `id`, `reply_to` | Eight hex digits, and the full topic to answer on |
| `cmd`, `args` | The command word (lower case, no prefix) and the rest of the line |
| `sender` | The sender's name as shown on the mesh |
| `sender_key` | The start of the sender's key, in direct messages only. A channel message does not identify its sender |
| `sender_lat`, `sender_lon` | The sender's advertised position when known |
| `dm`, `channel` | Whether it was a direct message; otherwise the channel name |
| `hops`, `path`, `path_names` | Hop count, the repeater hashes, and their names where known |
| `snr`, `rssi` | Signal of the received message |
| `time`, `date`, `tz`, `unix` | Local time and date, the zone, and unix time (empty or 0 until the clock is set) |
| `bot`, `scope`, `uptime` | The bot's name, scope and running time |
| `bot_lat`, `bot_lon` | The bot's configured position |
| `max_len` | The most bytes your reply text may be on the air |

A request is at most 1,000 bytes.

### A reply

Publish on the `reply_to` topic:

```json
{"text":"Cheers {sender}, one {args} coming up"}
```

- The text may use the same placeholders as custom commands (except `{fetch}`); the bot fills them
  in from the original message.
- The text must be 1 to 200 bytes; a longer one is ignored, not cut. Keep it within `max_len` or
  it is cut on the air. The whole payload must be within 512 bytes.
- The bot always puts the asker's mention in front, even when your text begins with `@[...]`.
- A reply that arrives after the wait time, for an unknown id, or a second time is ignored.

**Several messages.** A reply may carry up to three more:

```json
{"text":"first message","more":["second","third"]}
```

Each is sent as its own mesh message about four seconds apart, each with the mention and each
counted against the per-minute cap. Every string follows the rule for `text`. If `more` is not an
array of one to three such strings, the whole reply is ignored.

### Announcing commands

Publish a retained `{"desc":"..."}` on `<prefix>/commands/<name>` and the bot shows the command
in `help <name>` and in its published command list while it is connected. The name is 1 to 16
letters or digits; the description is cut to 48 bytes. Publish an empty retained payload to remove
it. Up to sixteen are kept. A command switched off on the bot is never offered to the broker.

### Limits

- Four requests may be waiting for an answer at once; a fifth is not sent.
- At most 30 requests a minute.
- The cooldown applies when the command is received; the per-minute cap is counted when the
  answer is sent, so commands nobody answers do not use it up.

### Treat requests as hostile

`cmd`, `args` and `sender` are text typed by anyone within radio range. Never run them as a
command, put them in a file name, a web address or a database query, or trust them in any way. If
your reply copies them back, turn `{` and `}` into something else first, or a stranger can make
the bot expand its own placeholders.

## Example programs

Both are Node.js (version 18 or later) and live in `tools/`.

**`tools/mqtt-responder`** is a small command-line responder and a simulator.

```sh
cd tools/mqtt-responder && npm install
node responder.js --host broker.example.org --user bot --pass-file ./mqtt.txt \
  --prefix meshcore/MeshBot --cmd "beer=Cheers {sender}, one %args% coming up" \
  --desc "beer=order a drink" --announce
```

It can also pretend to be the bot (`--simulate "beer ipa"`), so you can test a responder with no
radio at all. See its [README](tools/mqtt-responder/README.md).

**`tools/mqtt-extras`** answers two commands from one program:

- `dadjoke`: a random joke from icanhazdadjoke.com that fits a mesh message.
- `zork`: plays Zork I, one saved game per sender, using the multi-message reply. It runs the
  original game file, which Microsoft published under the MIT licence
  ([historicalsource/zork1](https://github.com/historicalsource/zork1)), in a small public-domain
  interpreter. The game file is downloaded by `npm run fetch-story`, not stored here.

It includes a systemd unit and an install script to run it on boot. See its
[README](tools/mqtt-extras/README.md).

## Serial commands

Over USB serial at 115200 baud, alongside the companion firmware's own commands:

| Command | Effect |
|---|---|
| `set bot.webpwd <password>` | Sets or resets the web page password |
| `get bot.status` | One line: look-up state, memory, stack, MQTT state, scope, address, clock, channels |
| `set wifi.ssid <name>`, `set wifi.pwd <password>` | WiFi, as in the stock firmware |
| `get wifi.status`, `get wifi.ip` | WiFi state and address |

## Security and privacy

- **The web page is for your local network.** It is plain HTTP, so anyone on that network can see
  the traffic. Do not forward port 80 to the internet.
- The web password is stored as a salted PBKDF2-HMAC-SHA256 hash. Repeated wrong attempts from one
  address are locked out for a while. Sessions expire.
- The page answers only requests addressed to the node's own address or its `.local` name, and
  refuses oversized or unauthenticated requests before reading their body.
- The page never shows the node's private key, the WiFi password or the broker password. Buffers
  that held a password are wiped before they are freed.
- **The broker connection is not encrypted** and every request published there includes the bot's
  configured position and, when known, the sender's advertised position. Anyone who can read the
  topic can see them.
- The location you enter is also the node's advertised position on the mesh. Enter one only as
  precise as you are happy to broadcast.
- Text from the mesh and from the broker is treated as untrusted everywhere: it is length-limited,
  stripped of control characters, parsed with a bounded amount of memory, and inserted into the
  web page as text only.

## How the code is organised

| Where | What |
|---|---|
| `src/helpers/bot/` | The bot's logic as header-only, hardware-free C++: parsing, settings, limits, reply formatting, templates, the MQTT message formats and state machine. Everything here is unit-tested on a PC |
| `examples/companion_radio/bot/` | The device side: the bot task, internet look-ups, the MQTT client, the web server and its page, storage |
| `examples/companion_radio/MyMesh.cpp` and other stock files | Small additions, each inside `#ifdef WITH_BOT` |
| `variants/xiao_s3_wio/platformio.ini` | The `Xiao_S3_WIO_companion_radio_wifi_bot` build environment |
| `test/test_bot_*` | Host tests for the bot |
| `tools/mqtt-responder`, `tools/mqtt-extras` | The example programs |

Design points worth knowing before changing things:

- The Arduino loop owns all bot state. The MQTT client (`esp-mqtt`) runs on its own tasks and
  talks to the loop only through message buffers and atomics, so the radio is never blocked by the
  network.
- Web request handlers never touch the mesh, the file system or the bot directly; they hand work
  to the loop.
- Every reply leaves through one function, which adds the mention, applies the cap and queues the
  message.

## Tests

```sh
pio test -e native
```

About 1,250 host tests (GoogleTest), covering the bot's logic and the fork's existing suites. They
need no hardware. See [test/README.md](test/README.md).

```sh
cd tools/mqtt-responder && npm test
cd tools/mqtt-extras && npm test
```

## Limits and known gaps

- One board, one build environment.
- No TLS to the MQTT broker, and no HTTPS on the web page.
- `help` lists only as many commands as fit one message; the rest, including custom and announced
  MQTT commands, are on `help 2`.
- The reply queue holds four messages. While a four-message MQTT reply is going out, another
  reply queued in that time is dropped.
- A channel message does not identify its sender, so anything keyed on a channel sender (such as
  a Zork saved game) goes by name, and names can be shared or copied.
- Not built yet: a `multitest` command (every route a message took) and unprompted messages from
  the broker (`say`).
- Tested on a small mesh by one person. Expect rough edges.

## Licence

MIT, as for MeshCore; see [license.txt](license.txt). The example Zork program downloads a game
file that is separately MIT-licensed by Microsoft, and includes JSZM, a public-domain Z-machine
interpreter.
