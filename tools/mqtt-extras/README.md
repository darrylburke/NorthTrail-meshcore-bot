# Extra bot commands over MQTT: Zork and dad jokes

One program that answers two commands for the MeshCore bot. The bot sends a command it does not
know to the MQTT broker; this program replies.

| Command | What it does |
|---|---|
| `zork` | Plays Zork I. Each sender has their own game, saved after every move. |
| `dadjoke` | A random joke from <https://icanhazdadjoke.com/>. |

## Set up

```
npm install
npm run fetch-story
```

`fetch-story` downloads the game file `zork1.z3` and its licence from
<https://github.com/historicalsource/zork1> into `story/`. Zork I is published there under the MIT
licence. The game file is not kept in this repository.

## Run

```
node extras.js --host broker.example.org --user bot --pass-file ./mqtt.txt --prefix meshcore/MeshBot
```

`--prefix` must match the topic prefix on the bot's page. `--commands dadjoke` (or `zork`) runs just
one of them. `node extras.js --help` lists every option. Stop it with Ctrl-C; that also removes the
commands from the bot's help.

## Run it on boot (Linux with systemd)

Copy this folder to the server and run, from inside it:

```
sudo sh deploy/install.sh
```

The first run writes `/etc/meshcore-bot-extras/mqtt.env` (readable by root only). Fill in the broker
user, password and topic prefix there, then run the script again: it installs the program in
`/opt/meshcore-bot-extras`, and starts the `meshcore-bot-extras` service, which also starts on every
boot. Saved games are kept in `/var/lib/meshcore-bot-extras/saves`. Run the script again after an
update. See what it is doing with `journalctl -u meshcore-bot-extras -f`.

Run only one copy against a broker: two copies both answer, and each keeps its own saved games.

## Use

Send these to the bot, with its command prefix:

| Message | What happens |
|---|---|
| `!dadjoke` | A joke |
| `!zork` | Starts your game, or shows the short help if you have one |
| `!zork look`, `!zork open mailbox`, `!zork n` | Any game command |
| `!zork more` | The rest of a very long reply that ended with `[more]` |
| `!zork restart` | Throws your game away and starts a new one |

A mesh message is short, so a Zork reply is sent as up to four messages, a few seconds apart. Only
a description too long for four ends with `[more]`; a new command drops what was left. This needs
bot firmware that sends multi-message replies (a reply document with a `more` array); with older
firmware run with `--parts 1`, which sends one message per reply and pages the rest with
`!zork more`. Every message counts against the bot's replies-per-minute cap.

When a joke is too long the program asks for another, up to three times, and cuts the shortest one
if none fits; if the site does not answer in time the bot says nothing. The bot's own limits
(cooldown per sender, replies per minute) apply to both commands.

## How Zork players are told apart

In a direct message from a contact the bot knows, by the contact's key. On a channel, by the
sender's name, so two people using the same name share a game. Saved games are in `saves/`, one
file per player, named by a hash of that identity.

## Files

| File | Purpose |
|---|---|
| `extras.js` | Broker connection, command-line options, routing |
| `game.js` | Runs Zork, pages the text, saves and reloads |
| `joke.js` | Fetches a joke that fits |
| `vendor/jszm.js` | JSZM, a public-domain Z-machine interpreter |
| `extras.test.js` | Tests (`npm test`; no network, and the Zork tests are skipped without the game file) |
