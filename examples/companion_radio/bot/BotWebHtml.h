#pragma once

// The bot's web page: one self-contained document (no external resources), kept in flash.
// Everything that comes from the device or from other mesh users is inserted with
// textContent / .value, never as HTML.

#include <pgmspace.h>

static const char BOT_WEB_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Mesh bot</title>
<style>
:root{--bg:#f6f6f4;--fg:#1d1d1b;--mut:#6b6b66;--card:#fff;--line:#d9d9d4;--acc:#1f6f5c;--bad:#a8321e;--ok:#1f6f2c}
@media (prefers-color-scheme:dark){:root{--bg:#161715;--fg:#e9e9e4;--mut:#9a9a92;--card:#20211f;--line:#383935;--acc:#5fc2a6;--bad:#ff8a73;--ok:#7fd58c}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--fg);font:16px/1.45 system-ui,sans-serif}
main{max-width:760px;margin:0 auto;padding:12px 16px 48px}
h1{font-size:1.3em;margin:8px 0}
h2{font-size:1.05em;margin:0 0 10px}
h3{font-size:1em;margin:14px 0 6px}
section{background:var(--card);border:1px solid var(--line);border-radius:8px;padding:14px;margin:12px 0}
label{display:block;margin:8px 0 2px;font-size:.9em;color:var(--mut)}
input,select,textarea{width:100%;font:inherit;padding:7px;border:1px solid var(--line);border-radius:6px;background:var(--bg);color:var(--fg)}
input[type=checkbox]{width:auto}
textarea{min-height:3em}
button{font:inherit;padding:7px 14px;margin:10px 6px 0 0;border:1px solid var(--acc);border-radius:6px;background:var(--acc);color:var(--bg);cursor:pointer}
button.sec{background:transparent;color:var(--acc)}
button:disabled{opacity:.6;cursor:default}
.row{display:grid;grid-template-columns:1fr 1fr;gap:0 12px}
.w3{display:grid;grid-template-columns:2fr 1fr 3fr;gap:0 8px}
.inl{display:flex;gap:8px;align-items:center}
.inl input{width:7em}
.chrow{display:flex;gap:8px;align-items:center;margin:4px 0}
.chrow input{flex:1;min-width:0}
.chrow button{margin:0;white-space:nowrap}
.chrow .c_chmiss{white-space:nowrap}
.tryrow{display:flex;gap:8px;align-items:flex-end}
.tryrow button{margin-top:0;white-space:nowrap}
.note{font-size:.85em;color:var(--mut)}
.msg{min-height:1.2em;font-size:.9em;margin-top:6px}
.bad{color:var(--bad)}.good{color:var(--ok)}
.item{border-top:1px solid var(--line);padding:8px 0}
.reply{white-space:pre-wrap;overflow-wrap:anywhere;font-size:1em}
table{width:100%;border-collapse:collapse;font-size:.9em}
td,th{text-align:left;vertical-align:top;padding:4px;border-top:1px solid var(--line);overflow-wrap:anywhere}
code{overflow-wrap:anywhere}
.hide{display:none}
.topics{margin:4px 0 0;padding-left:1.2em;font-size:.85em}
.chk{display:flex;gap:8px;align-items:center;color:var(--fg);font-size:1em}
@media (max-width:520px){.w3{grid-template-columns:1fr}}
</style></head><body><main>
<h1>Mesh bot</h1>
<p id="status" class="note">Loading…</p>

<section id="setup" class="hide"><h2>First-time setup</h2>
<p class="note">Connect this node to your home WiFi and choose a password for this page. The node restarts when you save.</p>
<label>Home WiFi name</label><input id="s_ssid" maxlength="32" autocomplete="off">
<label id="s_wpwlabel">Home WiFi password (empty for an open network)</label><input id="s_wpw" type="password" maxlength="63">
<p class="note" id="s_wifihelp">If the node does not appear on your network after it restarts, the WiFi name or password was wrong: correct them over USB with <code>set wifi.ssid &lt;name&gt;</code> and <code>set wifi.pwd &lt;password&gt;</code>, then <code>reboot</code>.</p>
<div id="s_curbox" class="hide"><label>Current password for this page</label><input id="s_cur" type="password" maxlength="63"></div>
<label id="s_pwlabel">Password for this page (8 to 63 characters)</label><input id="s_pw" type="password" maxlength="63">
<label>Node name</label><input id="s_name" maxlength="31">
<div class="row"><div><label>Latitude</label><input id="s_lat" inputmode="decimal" placeholder="0"></div>
<div><label>Longitude</label><input id="s_lon" inputmode="decimal" placeholder="0"></div></div>
<p class="note">Weather and alerts look-ups send this location to Open-Meteo and Environment Canada.</p>
<label>Region scope (lower-case letters, digits, hyphens; may be empty)</label><input id="s_scope" maxlength="30">
<button id="s_save">Save and restart</button><div id="s_msg" class="msg"></div>
</section>

<section id="needpw" class="hide"><h2>Set the page password first</h2>
<p>This page has no password yet, and it cannot be set from the network.</p>
<p>Connect the node to a computer over USB, open a serial terminal, and type:</p>
<p><code>set bot.webpwd &lt;password&gt;</code></p>
<p class="note">The password must be 8 to 63 characters. Then reload this page.</p>
</section>

<section id="login" class="hide"><h2>Sign in</h2>
<p id="l_note" class="note"></p>
<label>Password</label><input id="l_pw" type="password" autocomplete="current-password">
<button id="l_go">Sign in</button><div id="l_msg" class="msg"></div>
</section>

<div id="dash" class="hide">
<section><h2>Try a command</h2>
<p class="note">Runs a command on the node as if you had sent it in a direct message, and shows the reply here. Nothing is transmitted.</p>
<div class="tryrow"><div style="flex:1"><label for="t_text">Command</label><input id="t_text" maxlength="120" placeholder="wx" autocomplete="off"></div>
<button id="t_go">Try</button></div>
<div id="t_out" class="msg"></div>
<p class="note">Text rules are tried too: type the message a rule should catch.</p>
<p id="t_mqnote" class="note hide">To try a broker command, type it with the prefix (or without, when commands without the prefix are allowed).</p>
</section>

<section><h2>Settings</h2>
<label>Channels the bot answers on (#name or Public, 1 to 4)</label>
<div id="c_chlist"></div>
<button class="sec" id="c_chadd">Add a channel</button>
<p class="note">The bot answers commands on every channel listed, always with its one scope. Think twice before adding Public: a bot channel keeps replies out of general chat.</p>
<div id="c_chwarn" class="note bad"></div>
<label>Region scope</label><input id="c_scope" maxlength="30">
<div class="row"><div><label>Command prefix</label><input id="c_prefix" maxlength="1"></div>
<div><label>Cooldown per sender (seconds)</label><input id="c_cooldown_s" type="number" min="0" max="3600"></div></div>
<label class="chk"><input type="checkbox" id="c_bare"> Also answer commands written without the prefix</label>
<p class="note">With this on, <code>ping</code> works like <code>!ping</code> in channels (direct messages never need the prefix for the bot's own commands), and a command from the broker also works without it, in channels and direct messages. Only a message that starts with the name of a command is answered, so a chat line that happens to start with one (“test again later”) is answered too.</p>
<label>Replies per minute, at most</label><input id="c_max_per_min" type="number" min="1" max="30">
<label>Ignore senders whose name contains (comma separated)</label><input id="c_ignore" maxlength="95">
<div class="row"><div><label>Latitude</label><input id="c_lat" inputmode="decimal"></div>
<div><label>Longitude</label><input id="c_lon" inputmode="decimal"></div></div>
<p class="note">Weather and alerts look-ups send this location to Open-Meteo and Environment Canada.</p>
<label>Time zone</label><select id="c_tz"></select>
<label>Help link</label><input id="c_help_link" maxlength="63">
<div class="row"><div><label>Local advert every (minutes, 0 = off)</label><input id="c_advert_zero_hop_min" type="number" min="0" max="10080"></div>
<div><label>Wide advert every (minutes, 0 = off)</label><input id="c_advert_flood_min" type="number" min="0" max="10080"></div></div>
<label for="c_watch_online_min">Count a node as online if heard within</label>
<div class="inl"><input id="c_watch_online_min" type="number" min="5" max="10080"><span>minutes</span></div>
<label>Node name</label><input id="c_node_name" maxlength="31">
<button id="c_save">Save settings</button>
<button class="sec" id="adv_local">Advertise now (local)</button><button class="sec" id="adv_wide">Advertise now (wide)</button>
<div id="c_msg" class="msg"></div>
</section>

<section><h2>Built-in commands</h2>
<p class="note">wx &lt;place&gt; sends the typed place name to Open-Meteo.</p>
<div id="b_list"></div>
<button id="b_save">Save built-in commands</button><div id="b_msg" class="msg"></div></section>

<section><h2>Custom commands</h2>
<p class="note">Replies can use {sender} {hops} {snr} {rssi} {path} {args} {time} {date} {channel} {uptime}, and {fetch} for a value looked up from the web.</p>
<div id="u_list"></div><p id="u_none" class="note hide">No custom commands yet.</p>
<button class="sec" id="u_add">Add command</button><button id="u_save">Save custom commands</button>
<div id="u_msg" class="msg"></div></section>

<section><h2>Text rules</h2>
<p class="note">Answer a message that is not written as a command. Each rule names the text to look for and the command to run when it is found, exactly as if that command had been sent: a built-in, a custom command or one from the broker. Rules are tried from the top, before the message is read as a command; the first that fits wins. Replies from bots (starting @[) are never matched.</p>
<p class="note"><b>Exact text</b>: the whole message, capitals and outer spaces ignored (it may include the prefix, such as <code>!gm</code>). <b>Pattern</b>: a regular expression found anywhere in the message; use ^ and $ for the start and end. In the command, {0} is the text found and {1} to {9} the pattern's groups: <code>^weather (?:in )?(.+)$</code> with <code>wx {1}</code>. Supported: <code>. ^ $ \b \d \w \s [a-z] [^,] ( ) (?: ) | * + ?</code>; capitals are ignored. A pattern that needs too much work on a message is given up as no match: a repeated group such as <code>(\w+ )+</code> stops matching after about a dozen rounds, so prefer <code>.*</code> or a set such as <code>[a-z ]*</code>.</p>
<div id="x_list"></div><p id="x_none" class="note hide">No text rules yet.</p>
<button class="sec" id="x_add">Add a rule</button><button id="x_save">Save text rules</button>
<div id="x_msg" class="msg"></div></section>

<section><h2>Watched nodes</h2>
<p class="note">The nodes command says which of these were heard recently. An observer's id is the UUID in its analyzer address; a node's id is its 64-character public key.</p>
<p class="note">Checking these sends their ids to live.meshcore.ca.</p>
<div id="w_list"></div><p id="w_none" class="note hide">No watched nodes yet.</p><p id="w_fit" class="note bad"></p>
<button class="sec" id="w_add">Add a watched node</button><button id="w_save">Save watched nodes</button>
<div id="w_msg" class="msg"></div></section>

<section id="mqtt" class="hide"><h2>Broker (MQTT)</h2>
<p class="note bad">The connection to the broker is not encrypted. Use a broker on your own network, with a user name and password made only for this bot.</p>
<p class="note">Anything typed after an unknown command is sent to the broker, and whatever answers there is sent on the mesh under this bot's name.</p>
<p class="note">A command the bot does not know is sent to the broker with the sender's name, key, position if known, the message's route and signal figures. A command written without the prefix is sent only when commands without the prefix are allowed and a program on the broker has announced that name; a text rule's command is sent like one written with the prefix. Commands you have switched off are never sent.</p>
<p id="m_state"></p><p id="m_counts" class="note"></p>
<label class="chk"><input type="checkbox" id="m_enabled"> Answer unknown commands through the broker</label>
<label for="m_host">Broker host</label><input id="m_host" maxlength="64" placeholder="broker.example.org" autocomplete="off" spellcheck="false">
<div class="row"><div><label for="m_port">Port</label><input id="m_port" type="number" min="1" max="65535"></div>
<div><label for="m_wait">Wait (seconds)</label><input id="m_wait" type="number" min="1" max="15"></div></div>
<label for="m_user">User name</label><input id="m_user" maxlength="32" autocomplete="off" spellcheck="false">
<label for="m_pass">Password: <span id="m_passmark"></span></label><input id="m_pass" type="password" maxlength="64" autocomplete="new-password">
<p id="m_passnote" class="note"></p><p id="m_movenote" class="note bad"></p>
<label for="m_prefix">Topic prefix (empty: the default shown)</label><input id="m_prefix" maxlength="64" autocomplete="off" spellcheck="false">
<p class="note">Topics in use, under <code id="m_pfx"></code>:</p><ul id="m_topics" class="topics"></ul>
<button id="m_save">Save broker settings</button><button class="sec" id="m_clear">Remove password</button>
<div id="m_msg" class="msg"></div>
<h3>Commands from the broker</h3>
<p class="note">Announced by programs on the broker. A name that a built-in or custom command already uses is answered by that command (or, when it is switched off, not at all), never by the broker.</p>
<table><thead><tr><th>Command</th><th>Description</th><th>Note</th></tr></thead><tbody id="m_cmds"></tbody></table>
</section>

<section><h2>Node</h2>
<label>Name</label><div id="n_name"></div>
<label>Address</label><div id="n_addr"></div>
<label>Public key</label><code id="n_key"></code>
<label>Default scope</label><div id="n_scope"></div>
<label>Channels on the node</label><div id="n_ch"></div></section>

<section><h2>Recent</h2><table><thead><tr><th>Time</th><th>From</th><th>Request</th><th>Reply</th></tr></thead>
<tbody id="r_body"></tbody></table></section>

<section><h2>Change password</h2>
<label>Current password</label><input id="p_cur" type="password" autocomplete="current-password">
<label>New password (8 to 63 characters)</label><input id="p_new" type="password" autocomplete="new-password">
<button id="p_save">Change password</button><div id="p_msg" class="msg"></div></section>

<button class="sec" id="logout">Log out</button>
</div>
</main>
<script>
"use strict";
// ---- helpers ----
const $=id=>document.getElementById(id);
const ZONES=["America/St_Johns","America/Halifax","America/Toronto","America/Winnipeg","America/Regina","America/Edmonton","America/Vancouver","UTC"];
// What each built-in does, for the labels only. The list of built-ins itself comes from the
// node (the keys of config.builtin, in its order), so a new one appears without a page change.
const BDESC={ping:"hops, signal and route",test:"signal report",path:"the route a message took",help:"lists the commands",heard:"nodes heard lately",
  mesh:"traffic on the mesh",dist:"distance to the sender",sun:"sunrise and sunset",roll:"dice",
  "8ball":"magic 8-ball",wx:"weather",aurora:"aurora (Kp) outlook",alerts:"weather alerts",nodes:"watched nodes online"};
let S=null,C=null,wasAuthed=false;
function expired(){wasAuthed=false;S={authed:false};C=null;show("login");$("l_note").textContent="You have been signed out. Sign in again.";$("status").textContent="Signed out"}
function el(tag,props,kids){const e=document.createElement(tag);if(props)for(const k in props)e[k]=props[k];(kids||[]).forEach(c=>e.appendChild(typeof c==="string"?document.createTextNode(c):c));return e}
// ok: true (done), false (failed), "warn" (done, but the owner must act: the page's warning style).
function say(id,text,ok){const m=$(id);m.textContent=text||"";m.className="msg "+(ok===true?"good":ok===false?"bad":ok==="warn"?"note bad":"")}
function show(which){["setup","needpw","login","dash"].forEach(s=>$(s).classList.toggle("hide",s!==which))}
async function api(path,body){
  const o=body===undefined?{}:{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify(body)};
  o.cache="no-store";const r=await fetch(path,o);let j={};try{j=await r.json()}catch(e){}
  return{code:r.status,j:j};
}
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
// A save normally answers within a second or two (30 s at most here). While the bot is running a
// look-up the node holds a settings save back ("waiting") and applies it once the look-up is over,
// or after 60 s at the latest; the page then keeps polling for up to WAIT_MS from the save.
const RESULT_MS=30000,WAIT_MS=65000,WAITING="Waiting for a look-up to finish…";
async function waitResult(id,setup,msgId){
  const t0=Date.now();let waiting=false;
  while(Date.now()-t0<(waiting?WAIT_MS:RESULT_MS)){await sleep(500);
    let r;try{r=await api(setup?"/api/state":"/api/result")}catch(e){continue}
    const res=setup?r.j.result:r.j;
    if(r.code===401){expired();return null}
    if(!res||res.id!==id||res.status==="pending")continue;
    if(res.status==="waiting"){if(!waiting&&msgId)say(msgId,WAITING);waiting=true;continue}
    return res}
  return{status:"rejected",message:"No answer from the node"};
}
async function submit(path,body,msgId,after){
  say(msgId,"Saving…");
  let r;try{r=await api(path,body)}catch(e){say(msgId,"Could not reach the node",false);return}
  if(r.code===401){expired();return}
  if(r.code!==202&&r.code!==200){say(msgId,r.j.error||("Error "+r.code),false);return}
  if(r.code===200){say(msgId,"Done",true);return}
  const res=await waitResult(r.j.id,path==="/api/setup",msgId);if(!res)return;
  // Saved, but the broker's password was cleared: a warning, not a success or a failure.
  const warn=res.status==="ok"&&/password cleared/.test(res.message||"");
  say(msgId,res.message||res.status,warn?"warn":res.status==="ok");
  if(after)after(res);
}
function num(v){v=String(v).trim();return v===""?0:Number(v)}
function r5(x){return Math.round(Number(x)*1e5)/1e5}
function fmtUp(s){const d=Math.floor(s/86400),h=Math.floor(s%86400/3600),m=Math.floor(s%3600/60);return(d?d+"d ":"")+h+"h "+m+"m"}
function fmtT(t){return t?new Date(t*1000).toLocaleString():"-"}
const kb=b=>Math.round(b/1024)+" KB";
const oneLine=t=>t.replace(/[\r\n\t]+/g," ");
const CTRL=/[\x00-\x1f\x7f]/;

// ---- validators: ports of the node's checks (botUrlCheckTemplate in BotUrl.h, botJsonPathCheck
// in BotJsonPath.h) so a mistake is shown before saving. The node is the authority: it checks
// every saved value again and refuses what these might miss. ----
const utf8len=t=>new TextEncoder().encode(t).length;
const BU={OK:0,SCHEME:1,HOST:2,CREDENTIALS:3,ARGS_PLACE:4,CHARS:5,LENGTH:6};
const BU_TEXT=["","the web address must start with http:// or https://","the web address has a bad host or port (a port is 1 to 65535)",
  "the web address must not contain a user name or password","{args} is only allowed after the ? in the web address",
  "the web address may hold only visible ASCII characters and {args}","the web address must be 1 to 160 bytes"];
function urlCheck(t){
  if(typeof t!=="string")t="";
  const len=utf8len(t);if(len===0||len>160)return BU.LENGTH;
  let p;if(t.startsWith("https://"))p=8;else if(t.startsWith("http://"))p=7;else return BU.SCHEME;
  for(let i=0;i<t.length;i++){const c=t.charCodeAt(i);
    if(c<=0x20||c>0x7e)return BU.CHARS;
    if(c===0x7b){if(t.substr(i,6)!=="{args}")return BU.CHARS;i+=5}else if(c===0x7d)return BU.CHARS}
  for(let i=p;i<t.length&&t[i]!=="/";i++)if(t[i]==="@")return BU.CREDENTIALS;
  let e=p;while(e<t.length&&t[e]!=="/"&&t[e]!=="?")e++;
  let argsInHost=false;for(let i=p;i<e;i++)if(t[i]==="{")argsInHost=true;
  if(!argsInHost){let h=p;while(h<e&&t[h]!==":")h++;
    const hl=h-p;if(hl<1||hl>100)return BU.HOST;
    for(let i=p;i<h;i++)if(!/[A-Za-z0-9.-]/.test(t[i]))return BU.HOST;
    if(h<e){const d=e-h-1;if(d<1||d>5)return BU.HOST;for(let i=h+1;i<e;i++)if(t[i]<"0"||t[i]>"9")return BU.HOST;
      const port=Number(t.substring(h+1,e));if(port<1||port>65535)return BU.HOST}}
  let q=t.indexOf("?");if(q<0)q=t.length;
  for(let i=0;i<q;i++)if(t[i]==="{")return BU.ARGS_PLACE;
  return BU.OK}
function jsonPathCheck(s){
  if(typeof s!=="string")return false;
  const len=utf8len(s);if(len===0||len>48)return false;
  const n=s.length,key=/[A-Za-z0-9_-]/;let i=0,steps=0;
  while(i<n){
    if(++steps>10)return false;   // BOT_JSON_PATH_STEPS: keys and indices in all
    if(s[i]==="["){let d=0;for(i++;i<n&&s[i]>="0"&&s[i]<="9";i++)d++;
      if(d<1||d>3||i>=n||s[i]!=="]")return false;i++}
    else{const st=i;while(i<n&&key.test(s[i]))i++;const kl=i-st;if(kl<1||kl>32)return false}
    if(i>=n)break;
    if(s[i]==="[")continue;
    if(s[i]!==".")return false;
    i++;if(i>=n||s[i]==="[")return false}
  return true}
const UUID=/^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$/,KEY64=/^[0-9a-fA-F]{64}$/;
// ---- end validators ----

// ---- loading and the read-only parts ----
let retry=null;
function busy(full){$("status").textContent="Node busy, retrying…";retry=setTimeout(()=>load(full),2000)}
async function load(full){
  clearTimeout(retry);
  let r;try{r=await api("/api/state")}catch(e){r=null}
  if(!r||r.code!==200){busy(full);return}
  const st=r.j;
  if(st.setup){S=st;show("setup");$("status").textContent="Setup mode (WiFi "+(S.ap||"")+")";
    $("s_curbox").classList.toggle("hide",!S.has_password);
    $("s_pwlabel").textContent=S.has_password?"New password for this page (empty keeps the current one)":"Password for this page (8 to 63 characters)";
    if(S.ssid)$("s_wpwlabel").textContent="Home WiFi password (leave empty to keep the saved password for "+S.ssid+"; a different network with no password is treated as open)";
    if(full){if(!$("s_name").value)$("s_name").value=S.name||"";if(!$("s_ssid").value)$("s_ssid").value=S.ssid||""}return}
  if(st.needs_password){S=st;show("needpw");$("status").textContent="Page password not set";return}
  if(!st.authed){S=st;if(wasAuthed){expired();return}show("login");$("status").textContent="Signed out";return}
  // The settings are their own document: fetched on a full load (sign-in, after a save) only.
  if(full||!C){let c;try{c=await api("/api/config")}catch(e){c=null}
    if(c&&c.code===401){expired();return}
    if(!c||c.code!==200||!c.j||!c.j.builtin){busy(full);return}
    C=c.j}
  S=st;wasAuthed=true;$("l_note").textContent="";
  show("dash");status();node();recent();mqttStatus();if(full){settings();builtins();customs();matches();watches();mqttForm()}
}
function status(){const s=S.status;
  let net="look-ups "+(s.net_busy?"running":"idle");
  if(s.net_parked)net+=", "+s.net_parked+" waiting";
  // net_ms is the whole request; DNS and connect (TCP plus the TLS handshake) are parts of it.
  net+=s.net_last&&s.net_last!=="none"?", last "+s.net_last+" in "+s.net_ms+" ms (DNS "+(s.net_dns_ms||0)+" ms, connect "+(s.net_conn_ms||0)+" ms)":", none yet";
  $("status").textContent="Up "+fmtUp(s.uptime_s)+" · WiFi "+(s.wifi_connected?s.wifi_ssid+" "+s.ip+" ("+s.rssi+" dBm)":"disconnected")+
  " · clock "+(s.clock_valid?"set":"not set")+" · heard "+s.heard_24h+" in 24 h · "+s.packets_hour+" packets in the last hour"+
  " · memory "+kb(s.free_heap)+" free"+(s.min_free_heap?" (lowest "+kb(s.min_free_heap)+")":"")+" · "+net}
function node(){const n=S.node;
  $("n_name").textContent=n.name;$("n_addr").textContent="http://"+n.mdns+"/"+(S.status.ip?"  or  http://"+S.status.ip+"/":"");
  $("n_key").textContent=n.public_key;$("n_scope").textContent=n.default_scope||"(none)";
  $("n_ch").textContent=n.channels.length?n.channels.join(", "):"(none)";
  chFlags()}
function recent(){const b=$("r_body");b.textContent="";
  S.recent.forEach(e=>b.appendChild(el("tr",null,[el("td",{textContent:fmtT(e.t)}),el("td",{textContent:e.from}),el("td",{textContent:e.req}),el("td",{textContent:e.reply})])));
  if(!S.recent.length)b.appendChild(el("tr",null,[el("td",{colSpan:4,textContent:"Nothing yet"})]))}

// ---- try a command ----
// Above the node's own limits: a parked command is answered within 45 s and a try given up
// after 50 s (BOT_PARK_CEILING_MS, BOT_TRY_TIMEOUT_MS in BotNetLogic.h).
const TRY_MS=52000;
let tryRun=0;
function tryShow(kids,ok){const o=$("t_out");o.textContent="";o.className="msg "+(ok===false?"bad":"");kids.forEach(k=>o.appendChild(k))}
function tryDone(){const b=$("t_go");b.disabled=false;b.textContent="Try"}
const TRY_BUSY="The bot is busy with another request, try again.";
async function tryCmd(){
  const text=$("t_text").value;
  if(!text.trim()){tryShow([el("span",{textContent:"Type a command first."})],false);return}
  if(CTRL.test(text)||utf8len(text)>120){tryShow([el("span",{textContent:"The command must be 1 to 120 characters, on one line."})],false);return}
  const run=++tryRun,b=$("t_go");b.disabled=true;b.textContent="Waiting…";
  tryShow([el("span",{textContent:"Waiting for the bot…"})]);
  const t0=Date.now();
  let r;try{r=await api("/api/try",{text:text})}catch(e){tryDone();tryShow([el("span",{textContent:"Could not reach the node."})],false);return}
  if(r.code===401){tryDone();expired();return}
  if(r.code===503){tryDone();tryShow([el("span",{textContent:TRY_BUSY})],false);return}
  if(r.code!==202){tryDone();tryShow([el("span",{textContent:r.j.error||("Error "+r.code)})],false);return}
  const id=r.j.id;
  while(Date.now()-t0<TRY_MS){
    await sleep(500);if(run!==tryRun)return;
    let g;try{g=await api("/api/try")}catch(e){continue}
    if(g.code===401){tryDone();expired();return}
    if(g.code!==200||!g.j)continue;
    if(g.j.id!==id){tryDone();tryShow([el("span",{textContent:"Another try (from another page) replaced this one."})],false);return}
    if(!g.j.done)continue;
    tryDone();
    if(g.j.busy)tryShow([el("span",{textContent:TRY_BUSY})],false);
    else if(!g.j.reply)tryShow([el("span",{textContent:"The bot would stay silent."})]);
    else tryShow([el("div",{className:"reply",textContent:g.j.reply}),el("span",{className:"note",textContent:g.j.broker?"(answered by the broker, not transmitted)":"(not transmitted)"})]);
    return}
  tryDone();tryShow([el("span",{textContent:"No answer from the bot (timed out)."})],false)}

// ---- channels the bot answers on ----
// The node's list, or (from a firmware older than the list) its one channel. Such a node holds one.
const cfgChannels=c=>Array.isArray(c.channels)?c.channels:(typeof c.channel==="string"?[c.channel]:[]);
const chMax=()=>C&&!Array.isArray(C.channels)?1:4;
const chInputs=()=>Array.from(document.querySelectorAll("#c_chlist .c_ch"));
function chRow(name){const d=el("div",{className:"chrow"});
  const i=el("input",{className:"c_ch",value:name||"",maxLength:31,placeholder:"#bot",autocomplete:"off",spellcheck:false,oninput:chFlags});
  const miss=el("span",{className:"note bad c_chmiss"});
  const del=el("button",{className:"sec c_chdel",textContent:"Remove"});del.onclick=()=>{if(chInputs().length>1){d.remove();chRows()}};
  [i,miss,del].forEach(k=>d.appendChild(k));return d}
// Remove is disabled on the last row, Add once the list is full.
function chRows(){const n=chInputs().length;document.querySelectorAll("#c_chlist .c_chdel").forEach(b=>b.disabled=n<=1);$("c_chadd").disabled=n>=chMax();chFlags()}
// A row is flagged when the node has no channel of exactly that name (the bot never hears it there).
function chFlags(){const have=S&&S.node&&S.node.channels?S.node.channels:[];let missing=false;
  document.querySelectorAll("#c_chlist .chrow").forEach(d=>{const v=d.querySelector(".c_ch").value.trim(),m=v!==""&&have.indexOf(v)<0;
    d.querySelector(".c_chmiss").textContent=m?"not on this node":"";if(m)missing=true});
  $("c_chwarn").textContent=missing?"Add the missing channels in the MeshCore app, or the bot cannot answer there.":""}
function channels(){const l=$("c_chlist");l.textContent="";let list=cfgChannels(C);if(!list.length)list=[""];
  list.slice(0,chMax()).forEach(n=>l.appendChild(chRow(n)));chRows()}
// The node's rules (botChannelListCheck in BotConfig.h): 1 to 4 names, each "#" and at least one
// more character or exactly "Public", at most 31 bytes, no control characters, no two the same
// ignoring the case of ASCII letters.
const foldAscii=t=>t.replace(/[A-Z]/g,c=>c.toLowerCase());
function checkChannels(list,max){
  if(!list.length)return"List at least one channel";
  if(list.length>max)return"At most "+max+" channel"+(max>1?"s":"");
  for(let i=0;i<list.length;i++){const c=list[i],row="Channel "+(i+1)+(c?" ("+c+")":"")+": ";
    if(!((c.length>=2&&c[0]==="#")||c==="Public"))return row+"a channel name is # followed by its name, or Public";
    if(CTRL.test(c))return row+"remove the control characters";
    const n=utf8len(c);if(n>31)return row+"the name is "+n+" bytes, at most 31";
    for(let j=0;j<i;j++)if(foldAscii(list[j])===foldAscii(c))return row+"the same channel as channel "+(j+1)}
  return null}

// ---- settings ----
const FIELDS=["scope","prefix","cooldown_s","max_per_min","ignore","help_link","advert_zero_hop_min","advert_flood_min","watch_online_min"];
const NUMS=["cooldown_s","max_per_min","advert_zero_hop_min","advert_flood_min","watch_online_min"];
function settings(){const c=C;
  FIELDS.forEach(f=>$("c_"+f).value=c[f]===undefined?"":c[f]);$("c_lat").value=r5(c.lat);$("c_lon").value=r5(c.lon);$("c_node_name").value=S.node.name;
  const z=$("c_tz");z.textContent="";const list=ZONES.indexOf(c.tz)<0&&c.tz?ZONES.concat([c.tz]):ZONES;
  list.forEach(t=>z.appendChild(el("option",{value:t,textContent:t})));z.value=c.tz||"UTC";$("c_bare").checked=c.bare===true;channels()}

// ---- built-in commands (the list is the node's) ----
const bids=()=>Object.keys(C.builtin);
function builtins(){const l=$("b_list");l.textContent="";
  bids().forEach((id,i)=>{const b=C.builtin[id];
    l.appendChild(el("div",{className:"item"},[el("label",null,[el("input",{type:"checkbox",checked:b.enabled,id:"be_"+i}),
      " Built-in “"+id+"”"+(BDESC[id]?" ("+BDESC[id]+")":"")+", answers to:"]),el("input",{id:"bn_"+i,value:b.name,maxLength:16})]))})}

// ---- custom commands ----
function customRow(c){const d=el("div",{className:"item"});
  d.appendChild(el("label",null,["Trigger word"]));d.appendChild(el("input",{className:"u_t",value:c.trigger||"",maxLength:16}));
  d.appendChild(el("label",null,["Reply"]));d.appendChild(el("textarea",{className:"u_r",value:c.reply||"",maxLength:160}));
  d.appendChild(el("label",null,["Description (shown by help)"]));d.appendChild(el("input",{className:"u_d",value:c.desc||"",maxLength:48}));
  d.appendChild(el("label",null,[el("input",{type:"checkbox",className:"u_e",checked:c.enabled!==false})," Enabled"]));
  const has=!!(c.url||c.path);
  const tog=el("input",{type:"checkbox",className:"u_l",checked:has});
  const box=el("div",{className:"u_lk"+(has?"":" hide")},[
    el("p",{className:"note",textContent:"The web address may contain {args} after the ? (what follows the command); the reply may use {fetch} for the value read."}),
    el("p",{className:"note",textContent:"The address is requested from this device, so that server sees your home network's address and whatever is typed after the command. Anyone on the mesh can run it: never use an address that changes something."}),
    el("label",null,["Web address"]),el("input",{className:"u_url",value:c.url||"",maxLength:160,placeholder:"https://example.org/api?q={args}"}),
    el("div",{className:"row"},[
      el("div",null,[el("label",null,["Value to read (JSON path)"]),el("input",{className:"u_path",value:c.path||"",maxLength:48,placeholder:"current.temperature"})]),
      el("div",null,[el("label",null,["Reuse for (seconds)"]),el("input",{className:"u_cache",type:"number",min:0,max:3600,value:c.cache_s||0})])])]);
  tog.onchange=()=>box.classList.toggle("hide",!tog.checked);
  d.appendChild(el("label",null,[tog," Look up a value from the web"]));d.appendChild(box);
  const del=el("button",{className:"sec",textContent:"Delete"});del.onclick=()=>{d.remove();customsEmpty()};d.appendChild(del);return d}
function customsEmpty(){$("u_none").classList.toggle("hide",document.querySelectorAll("#u_list .item").length>0)}
function customs(){const l=$("u_list");l.textContent="";C.custom.forEach(c=>l.appendChild(customRow(c)));customsEmpty()}
// Checks the custom list as the node will; returns null or the first problem, naming the row.
function checkCustoms(list){
  const taken=bids().map(id=>(C.builtin[id].name||id).toLowerCase()),seen={};
  if(list.length>16)return"At most 16 custom commands ("+list.length+" now)";
  for(let i=0;i<list.length;i++){const c=list[i],row="Command "+(i+1)+(c.trigger?" ("+c.trigger+")":"")+": ";
    if(!/^[a-z0-9]{1,16}$/.test(c.trigger))return row+"the trigger must be 1 to 16 letters or digits, nothing else";
    const b=taken.indexOf(c.trigger);if(b>=0)return row+"the trigger is the name of a built-in command ("+taken[b]+")";
    if(seen[c.trigger])return row+"the same trigger as command "+seen[c.trigger];seen[c.trigger]=i+1;
    if(!c.reply.trim())return row+"the reply is empty";
    if(CTRL.test(c.reply)||CTRL.test(c.desc))return row+"remove the control characters";
    const rl=utf8len(c.reply);if(rl>160)return row+"the reply is "+rl+" bytes, at most 160 (emoji and accents take 2 to 4 each)";
    const dl=utf8len(c.desc);if(dl>48)return row+"the description is "+dl+" bytes, at most 48 (emoji and accents take 2 to 4 each)";
    if(c.url||c.path){
      if(!c.url)return row+"enter the web address to look up, or turn the look-up off";
      const u=urlCheck(c.url);if(u!==BU.OK)return row+BU_TEXT[u];
      if(!c.path)return row+"enter the value to read (a JSON path such as current.temperature)";
      if(!jsonPathCheck(c.path))return row+"the value to read is not a valid JSON path (keys of letters, digits, _ or -, joined by dots, each optionally followed by [n]; at most 10 keys and indices)";
      if(!Number.isInteger(c.cache_s)||c.cache_s<0||c.cache_s>3600)return row+"reuse for must be a whole number of seconds from 0 to 3600"}
    if(c.reply.indexOf("{fetch}")>=0&&!c.url)return row+"the reply uses {fetch}, which needs a web address (turn on the look-up)"}
  return null}

// ---- text rules ----
function matchRow(r){const d=el("div",{className:"item"});
  const kind=el("select",{className:"x_k"},[el("option",{value:"exact",textContent:"Exact text"}),el("option",{value:"regex",textContent:"Pattern (regular expression)"})]);
  kind.value=r.kind==="regex"?"regex":"exact";
  d.appendChild(el("div",{className:"row"},[
    el("div",null,[el("label",null,["When a message"]),kind]),
    el("div",null,[el("label",null,["Text or pattern"]),el("input",{className:"x_t",value:r.text||"",maxLength:48,autocomplete:"off",spellcheck:false})])]));
  d.appendChild(el("label",null,["Run this command (no prefix)"]));d.appendChild(el("input",{className:"x_r",value:r.run||"",maxLength:48,placeholder:"wx {1}",autocomplete:"off",spellcheck:false}));
  d.appendChild(el("label",{className:"chk"},[el("input",{type:"checkbox",className:"x_e",checked:r.enabled!==false})," Enabled"]));
  const del=el("button",{className:"sec",textContent:"Delete"});del.onclick=()=>{d.remove();matchesEmpty()};d.appendChild(del);return d}
function matchesEmpty(){const n=document.querySelectorAll("#x_list .item").length;$("x_none").classList.toggle("hide",n>0);$("x_add").disabled=n>=8}
function matches(){const l=$("x_list");l.textContent="";(C.match||[]).forEach(r=>l.appendChild(matchRow(r)));matchesEmpty()}
// A first check of the rule list; the node checks again and its word on a pattern is final.
function checkMatches(list){
  if(list.length>8)return"At most 8 text rules ("+list.length+" now)";
  for(let i=0;i<list.length;i++){const r=list[i],row="Rule "+(i+1)+": ";
    if(!r.text.trim())return row+"the text is empty";
    if(CTRL.test(r.text)||CTRL.test(r.run))return row+"remove the control characters";
    if(utf8len(r.text)>48)return row+"the text is "+utf8len(r.text)+" bytes, at most 48";
    if(utf8len(r.run)>48)return row+"the command is "+utf8len(r.run)+" bytes, at most 48";
    if(r.kind==="exact"&&/^\s*@\[/.test(r.text))return row+"the text must not start with @[ (a bot's reply)";
    if(r.kind==="regex"){try{new RegExp(r.text)}catch(e){return row+"the pattern is not valid ("+e.message+")"}
      if(/(^|[^\\])(\\\\)*[{}]/.test(r.text))return row+"{ } repeats are not supported in a pattern (write \\{ for a brace)"}
    if(!/^[A-Za-z0-9]{1,16}( |$)/.test(r.run))return row+"the command must start with a command name of 1 to 16 letters or digits, without the prefix"}
  return null}

// ---- watched nodes ----
function watchRow(w){const d=el("div",{className:"item"});
  const kind=el("select",{className:"w_k"},[el("option",{value:"observer",textContent:"Observer"}),el("option",{value:"node",textContent:"Node"})]);
  kind.value=w.kind==="node"?"node":"observer";
  d.appendChild(el("div",{className:"w3"},[
    el("div",null,[el("label",null,["Label"]),el("input",{className:"w_l",value:w.label||"",maxLength:24,oninput:watchFit})]),
    el("div",null,[el("label",null,["Kind"]),kind]),
    el("div",null,[el("label",null,["Id"]),el("input",{className:"w_i",value:w.id||"",maxLength:64,autocomplete:"off",spellcheck:false})])]));
  const del=el("button",{className:"sec",textContent:"Remove"});del.onclick=()=>{d.remove();watchesEmpty()};d.appendChild(del);return d}
function watchesEmpty(){const n=document.querySelectorAll("#w_list .item").length;$("w_none").classList.toggle("hide",n>0);$("w_add").disabled=n>=4;watchFit()}
// An estimate of whether every watched node fits in one nodes reply (the node drops the entries
// that do not fit, without saying so). The room is the node's: BOT_MAX_MSG (127) bytes on air,
// less "<bot name>: " and the "@[sender] " mention (a sender name of WATCH_SENDER bytes is
// assumed); each entry is "<label>: <status>" with the status at its longest usual form
// ("unavailable", as long as "offline" with a two-digit age), joined by " | ". Real replies can
// be shorter.
const BOT_MAX_MSG=127,WATCH_SENDER=12,WATCH_STATUS=utf8len("unavailable");
function watchFitText(labels,botName){
  const room=BOT_MAX_MSG-utf8len((botName||"")+": ")-utf8len("@[] ")-WATCH_SENDER;
  let need=0;labels.forEach((l,i)=>{need+=(i?utf8len(" | "):0)+utf8len(l+": ")+WATCH_STATUS});
  return labels.length>1&&need>room?"With labels this long the nodes reply may not have room for every watched node (about "+need+" bytes needed, about "+room+" available): the last ones would be left out. Shorter labels fit more.":""}
function watchFit(){const labels=[];document.querySelectorAll("#w_list .w_l").forEach(i=>labels.push(i.value.trim()));
  $("w_fit").textContent=watchFitText(labels,S&&S.node?S.node.name:"")}
function watches(){const l=$("w_list");l.textContent="";(C.watch||[]).forEach(w=>l.appendChild(watchRow(w)));watchesEmpty()}
function checkWatches(list){
  if(list.length>4)return"At most 4 watched nodes";
  for(let i=0;i<list.length;i++){const w=list[i],row="Watched "+(i+1)+(w.label?" ("+w.label+")":"")+": ";
    const ll=utf8len(w.label);if(ll<1||ll>24)return row+"the label must be 1 to 24 bytes";
    if(CTRL.test(w.label))return row+"remove the control characters from the label";
    if(w.kind==="observer"&&!UUID.test(w.id))return row+"an observer's id is a UUID like 0a1b2c3d-4e5f-6a7b-8c9d-0e1f2a3b4c5d";
    if(w.kind==="node"&&!KEY64.test(w.id))return row+"a node's id is its public key, 64 hex characters"}
  return null}

// ---- the broker (MQTT) ----
// Ports of the node's rules (botCfgMqttHostOk, botCfgMqttTokenOk, botCfgGetInt ranges in
// BotConfig.h, botMqttPrefixValid in BotMqttTopics.h). The node checks every value again.
const MQ_HOST=/^[A-Za-z0-9.-]{0,64}$/,MQ_TOKEN=/^[\x21-\x7e]*$/;
function mqttPrefixValid(p){
  if(typeof p!=="string"||p.length<1||p.length>64)return false;
  for(let i=0;i<p.length;i++){const c=p[i];if(!/[A-Za-z0-9_\/-]/.test(c))return false;if(c==="/"&&(i===0||p[i-1]==="/"))return false}
  return p[p.length-1]!=="/"}
const intIn=(v,lo,hi)=>{const t=String(v).trim(),n=Number(t);return t!==""&&Number.isInteger(n)&&n>=lo&&n<=hi};
// Returns null or the first problem.
function checkMqtt(m,pass){
  if(!MQ_HOST.test(m.mqtt_host))return"Broker host: letters, digits, dots and hyphens only, at most 64";
  if(!intIn(m.mqtt_port,1,65535))return"Port: a whole number from 1 to 65535";
  if(!intIn(m.mqtt_wait_s,1,15))return"Wait: a whole number of seconds from 1 to 15";
  if(m.mqtt_user.length>32||!MQ_TOKEN.test(m.mqtt_user))return"User name: at most 32 visible ASCII characters, no spaces";
  if(pass.length>64||!MQ_TOKEN.test(pass))return"Password: at most 64 visible ASCII characters, no spaces";
  if(m.mqtt_prefix!==""&&!mqttPrefixValid(m.mqtt_prefix))return"Topic prefix: 1 to 64 letters, digits, _ - and /, not starting or ending with /, no empty level";
  return null}
const MQ_TOPICS=[["/request","requests, bot to broker"],["/reply/<id>","answers, broker to bot"],["/commands/<name>","announced commands, broker to bot (retained)"],
  ["/commandlist","the bot's command list, bot to broker (retained)"],["/status","online or offline, bot to broker (retained)"]];
function mqttStatus(){const m=S.mqtt;$("t_mqnote").classList.toggle("hide",!(m&&C&&C.mqtt_enabled===true));if(!m)return;
  // The state in words; an error is told by its detail, other states add theirs when it says more.
  const word=m.state==="error"?m.detail||"error":m.state+(m.detail&&m.detail!==m.state?" ("+m.detail+")":"");
  $("m_state").textContent="Connection: "+word;$("m_state").className=m.state==="connected"?"good":m.state==="error"||m.state==="login refused"?"bad":"";
  $("m_counts").textContent="Requests sent "+m.published+" · answered "+m.answered+" · timed out "+m.timed_out+" · messages dropped "+m.dropped+
    " · answers held back by the per-minute limit "+m.capped+" · messages received "+m.received+" · connections "+m.connects;
  $("m_pfx").textContent=m.prefix;const t=$("m_topics");t.textContent="";
  MQ_TOPICS.forEach(x=>t.appendChild(el("li",null,[el("code",{textContent:m.prefix+x[0]})," "+x[1]])));
  $("m_prefix").placeholder=m.default_prefix||"";
  const b=$("m_cmds");b.textContent="";
  (m.commands||[]).forEach(c=>b.appendChild(el("tr",null,[el("td",{textContent:c.name}),el("td",{textContent:c.desc}),
    el("td",{className:c.hidden?"bad":"",textContent:c.hidden==="builtin"?"hidden by the built-in command of this name":c.hidden==="custom"?"hidden by the custom command of this name":""})])));
  if(!(m.commands||[]).length)b.appendChild(el("tr",null,[el("td",{colSpan:3,textContent:m.state==="connected"?"None announced":"None (not connected)"})]))}
function passMark(){const set=!!C.mqtt_pass_set;$("m_passmark").textContent=set?"set":"not set";$("m_passmark").className=set?"good":"";
  $("m_passnote").textContent=set?"A password is set. Leave empty to keep it.":"";$("m_clear").disabled=!set;mqttMove()}
// A save that changes the broker without a new password clears the stored one (the node does it).
function mqttMove(){const moved=$("m_host").value.trim()!==C.mqtt_host||String(num($("m_port").value))!==String(C.mqtt_port);
  $("m_movenote").textContent=C.mqtt_pass_set&&moved&&!$("m_pass").value?"Saving a different broker clears the stored password: enter it again.":""}
function mqttForm(){const has=C.mqtt_enabled!==undefined;$("mqtt").classList.toggle("hide",!has);if(!has)return;
  $("m_enabled").checked=!!C.mqtt_enabled;$("m_host").value=C.mqtt_host||"";$("m_port").value=C.mqtt_port;$("m_wait").value=C.mqtt_wait_s;
  $("m_user").value=C.mqtt_user||"";$("m_prefix").value=C.mqtt_prefix||"";$("m_pass").value="";passMark()}

// ---- actions ----
$("s_save").onclick=()=>submit("/api/setup",{current:$("s_cur").value,ssid:$("s_ssid").value,wifi_password:$("s_wpw").value,web_password:$("s_pw").value,
  name:$("s_name").value.trim(),lat:num($("s_lat").value),lon:num($("s_lon").value),scope:$("s_scope").value.trim()},"s_msg",
  res=>{if(res.status==="ok")say("s_msg",res.message+". Reconnect this phone to your home WiFi, then open http://"+S.mdns+"/ (or the node's address from your router, or 'get wifi.ip' on USB). "+$("s_wifihelp").textContent,true)});
$("l_go").onclick=async()=>{say("l_msg","Checking…");let r;try{r=await api("/api/login",{password:$("l_pw").value})}catch(e){say("l_msg","Could not reach the node",false);return}
  if(r.code===200){$("l_pw").value="";say("l_msg","");load(true)}else say("l_msg",r.j.error||("Error "+r.code),false)};
$("l_pw").onkeydown=e=>{if(e.key==="Enter")$("l_go").click()};
$("t_go").onclick=tryCmd;
$("t_text").onkeydown=e=>{if(e.key==="Enter"&&!$("t_go").disabled)tryCmd()};
$("c_chadd").onclick=()=>{if(chInputs().length>=chMax())return;const r=chRow("");$("c_chlist").appendChild(r);chRows();r.querySelector(".c_ch").focus()};
$("c_save").onclick=()=>{const b={};FIELDS.forEach(f=>b[f]=$("c_"+f).value);
  NUMS.forEach(f=>b[f]=num(b[f]));
  b.channels=chInputs().map(i=>i.value.trim());b.bare=$("c_bare").checked;
  const bad=checkChannels(b.channels,chMax());if(bad){say("c_msg",bad+". Nothing was saved.",false);return}
  if(!Array.isArray(C.channels))b.channel=b.channels[0];   // a firmware older than the list knows only this key
  b.lat=num($("c_lat").value);b.lon=num($("c_lon").value);b.tz=$("c_tz").value;b.node_name=$("c_node_name").value.trim();
  submit("/api/config",b,"c_msg",()=>load(true))};
$("adv_local").onclick=()=>submit("/api/advert",{flood:false},"c_msg");
$("adv_wide").onclick=()=>submit("/api/advert",{flood:true},"c_msg");
$("b_save").onclick=()=>{const b={},seen={};let bad=null;bids().forEach((id,i)=>{const n=$("bn_"+i).value.trim().toLowerCase();b[id]={name:n,enabled:$("be_"+i).checked};
    if(bad)return;if(!/^[a-z0-9]{1,16}$/.test(n))bad="Built-in "+id+": the name must be 1 to 16 letters or digits";
    else if(seen[n])bad="Built-in "+id+": the same name as built-in "+seen[n];else seen[n]=id});
  if(bad){say("b_msg",bad+". Nothing was saved.",false);return}
  submit("/api/config",{builtin:b},"b_msg",()=>load(true))};
$("u_add").onclick=()=>{$("u_list").appendChild(customRow({}));customsEmpty()};
$("u_save").onclick=()=>{const list=[];document.querySelectorAll("#u_list .item").forEach(d=>{const look=d.querySelector(".u_l").checked;
  list.push({trigger:d.querySelector(".u_t").value.trim().toLowerCase(),reply:oneLine(d.querySelector(".u_r").value),desc:oneLine(d.querySelector(".u_d").value),
    enabled:d.querySelector(".u_e").checked,url:look?d.querySelector(".u_url").value.trim():"",path:look?d.querySelector(".u_path").value.trim():"",
    cache_s:look?num(d.querySelector(".u_cache").value):0})});
  const bad=checkCustoms(list);if(bad){say("u_msg",bad+". Nothing was saved.",false);return}
  submit("/api/config",{custom:list},"u_msg",()=>load(true))};
$("x_add").onclick=()=>{if(document.querySelectorAll("#x_list .item").length>=8)return;$("x_list").appendChild(matchRow({}));matchesEmpty()};
$("x_save").onclick=()=>{const list=[];document.querySelectorAll("#x_list .item").forEach(d=>{const k=d.querySelector(".x_k").value,t=d.querySelector(".x_t").value;
  list.push({kind:k,text:k==="exact"?t.trim():t,run:d.querySelector(".x_r").value.trim(),enabled:d.querySelector(".x_e").checked})});
  const bad=checkMatches(list);if(bad){say("x_msg",bad+". Nothing was saved.",false);return}
  submit("/api/config",{match:list},"x_msg",()=>load(true))};
$("w_add").onclick=()=>{if(document.querySelectorAll("#w_list .item").length>=4)return;$("w_list").appendChild(watchRow({}));watchesEmpty()};
$("w_save").onclick=()=>{const list=[];document.querySelectorAll("#w_list .item").forEach(d=>list.push({label:d.querySelector(".w_l").value.trim(),
  kind:d.querySelector(".w_k").value,id:d.querySelector(".w_i").value.trim()}));
  const bad=checkWatches(list);if(bad){say("w_msg",bad+". Nothing was saved.",false);return}
  submit("/api/config",{watch:list},"w_msg",()=>load(true))};
["m_host","m_port","m_pass"].forEach(id=>$(id).oninput=mqttMove);
$("m_save").onclick=()=>{const m={mqtt_enabled:$("m_enabled").checked,mqtt_host:$("m_host").value.trim(),mqtt_port:$("m_port").value,
    mqtt_user:$("m_user").value,mqtt_prefix:$("m_prefix").value.trim(),mqtt_wait_s:$("m_wait").value},pass=$("m_pass").value;
  const bad=checkMqtt(m,pass);if(bad){say("m_msg",bad+". Nothing was saved.",false);return}
  m.mqtt_port=num(m.mqtt_port);m.mqtt_wait_s=num(m.mqtt_wait_s);
  if(pass)m.mqtt_pass=pass;   // empty: the stored password is kept (absent from the request)
  $("m_pass").value="";
  submit("/api/config",m,"m_msg",()=>load(true))};
$("m_clear").onclick=()=>submit("/api/config",{mqtt_pass:""},"m_msg",()=>load(true));
$("p_save").onclick=()=>submit("/api/password",{current:$("p_cur").value,next:$("p_new").value},"p_msg",res=>{if(res.status!=="rejected"){$("p_cur").value="";$("p_new").value=""}});
$("logout").onclick=async()=>{try{await api("/api/logout",{})}catch(e){}location.reload()};
load(true);
setInterval(()=>{if(S&&S.authed&&!document.hidden)load(false)},15000);
// Setup: a light request every minute while the page is open and visible, so the access
// point's 15-minute idle timeout does not close it under someone filling in the form.
setInterval(()=>{if(S&&S.setup&&!document.hidden)api("/api/state").catch(()=>{})},60000);
</script></body></html>
)HTML";
