#ifdef WITH_BOT

// The "mqtt" member of /api/state: built on the loop task (from buildState(), which holds _mux)
// from what BotTask reports about the broker connection. See the thread model in BotWeb.h: no
// handler calls anything here. The writer and its size bound are pure (BotWebMqtt.h, host-tested).

#include "BotWeb.h"
#include "BotTask.h"
#include <helpers/bot/BotWebMqtt.h>

// Loop task only. Never the password, the user name or the host: the state's words come from
// botMqttShow, which names none of them.
size_t BotWeb::writeMqtt(char* dst, size_t cap) {
  BotMqttStatus ms;
  _bot->mqttStatus(ms);
  const BotConfig& c = _bot->config();
  const char* node = _mesh->getNodePrefs()->node_name;
  char prefix[BOT_MQTT_PREFIX_MAX + 1], def[BOT_MQTT_PREFIX_MAX + 1];
  botMqttEffectivePrefix(prefix, sizeof(prefix), c.mqtt_prefix, node);
  botMqttDefaultPrefix(def, sizeof(def), node);
  BotWebMqttView v;
  memset(&v, 0, sizeof(v));
  v.state = ms.state;
  v.detail = ms.detail;
  v.prefix = prefix;
  v.default_prefix = def;
  v.connects = ms.connects;
  v.published = ms.published;
  v.received = ms.received;
  v.dropped = ms.dropped;
  v.answered = ms.answered;
  v.timed_out = ms.timed_out;
  v.capped = ms.capped;
  const BotMqttCommands& a = _bot->mqttCommands();
  for (int i = 0; i < a.count() && v.count < BOT_MQTT_CMD_SLOTS; i++) {
    v.names[v.count] = a.nameAt(i);
    v.descs[v.count] = a.descAt(i);
    v.hidden[v.count] = botAskHiddenBy(c, a.nameAt(i));
    v.count++;
  }
  return botWebMqttJson(dst, cap, v);
}

#endif  // WITH_BOT
