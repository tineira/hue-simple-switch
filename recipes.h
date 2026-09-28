#pragma once

#include <Preferences.h>
#include <nvs.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "json_util.h"

// Recipes and channel settings in NVS: the GPIO only reads this, never Vercel.

// 7 channels (BOOT, D0-D5) x at most 3 recipes each: the console never builds more.
static const uint8_t kMaxRecipes = 21;
static const uint8_t kMaxSceneTargets = 8;
// At least the channel count (7).
static const uint8_t kMaxChannelSettings = 8;

struct HueRecipe {
  char channelId[12];
  char event[16];
  char action[16];
  char rtype[16];
  char rid[40];
  // recall_scene: the scene list (targets[], or the old single scene target). rtype/rid unused.
  uint8_t sceneCount;
  char scenes[kMaxSceneTargets][40];
};

enum ChannelSettingKind : uint8_t { CHK_NONE = 0, CHK_MAINTAINED = 1, CHK_MOMENTARY = 2 };

struct ChannelSetting {
  char id[12];
  uint8_t kind;
};

// One config: recipes plus channels[]. fromConsole false = the old payload (no channels[]):
// compiled defaults for every pin.
struct RecipeConfig {
  HueRecipe recipes[kMaxRecipes];
  uint8_t recipeCount;
  ChannelSetting channels[kMaxChannelSettings];
  uint8_t channelCount;
  bool fromConsole;
};

inline HueRecipe gRecipes[kMaxRecipes];
inline uint8_t gRecipeCount = 0;
inline ChannelSetting gChannelSettings[kMaxChannelSettings];
inline uint8_t gChannelSettingCount = 0;
inline bool gChannelsFromConsole = false;
// Bumped on every change of recipes or channels; the GPIO loop re-reads its channel modes.
inline volatile uint32_t gRecipesGen = 0;
inline uint32_t gRecipeRev = 0;
inline String gRecipeBridgeId;
inline volatile bool gNeedConsoleSync = false;
inline volatile uint32_t gNvsEpoch = 0;
inline SemaphoreHandle_t gRecipesMux = nullptr;
// Parse buffer (console task and boot only): too big for the task stack.
inline RecipeConfig gRecipeStage;

inline void recipesMuxEnsure() {
  if (!gRecipesMux) {
    gRecipesMux = xSemaphoreCreateMutex();
  }
}

inline void recipesLock() {
  recipesMuxEnsure();
  if (gRecipesMux) {
    xSemaphoreTake(gRecipesMux, portMAX_DELAY);
  }
}

inline void recipesUnlock() {
  if (gRecipesMux) {
    xSemaphoreGive(gRecipesMux);
  }
}

inline bool recipeEventOk(const char *e) {
  return e && (strcmp(e, "on") == 0 || strcmp(e, "off") == 0 || strcmp(e, "double_click") == 0 ||
               strcmp(e, "short") == 0 || strcmp(e, "hold") == 0);
}

// dim needs a release to stop the ramp: hold only.
inline bool recipeActionOk(const char *a, const char *event) {
  if (a && strcmp(a, "dim") == 0) {
    return event && strcmp(event, "hold") == 0;
  }
  return a && (strcmp(a, "on") == 0 || strcmp(a, "off") == 0 || strcmp(a, "recall_scene") == 0 ||
               strcmp(a, "toggle") == 0);
}

inline bool recipeRtypeOk(const char *r) {
  return r && (strcmp(r, "light") == 0 || strcmp(r, "grouped_light") == 0);
}

inline void recipeCopyField(char *dst, size_t n, const char *src) {
  if (!dst || n == 0) {
    return;
  }
  if (!src) {
    dst[0] = 0;
    return;
  }
  strncpy(dst, src, n - 1);
  dst[n - 1] = 0;
}

// NVS keeps the wire shape, without scene names.
inline String recipesToJson() {
  String s = "{";
  if (gChannelsFromConsole) {
    s += "\"channels\":[";
    for (uint8_t i = 0; i < gChannelSettingCount; i++) {
      if (i) {
        s += ',';
      }
      s += "{\"id\":";
      jsonAppendEscaped(s, gChannelSettings[i].id);
      s += ",\"kind\":";
      jsonAppendEscaped(s, gChannelSettings[i].kind == CHK_MOMENTARY ? "momentary" : "maintained");
      s += '}';
    }
    s += "],";
  }
  s += "\"recipes\":[";
  for (uint8_t i = 0; i < gRecipeCount; i++) {
    const HueRecipe &r = gRecipes[i];
    if (i) {
      s += ',';
    }
    s += "{\"channelId\":";
    jsonAppendEscaped(s, r.channelId);
    s += ",\"event\":";
    jsonAppendEscaped(s, r.event);
    s += ",\"action\":";
    jsonAppendEscaped(s, r.action);
    if (r.sceneCount) {
      s += ",\"targets\":[";
      for (uint8_t k = 0; k < r.sceneCount; k++) {
        if (k) {
          s += ',';
        }
        s += "{\"rtype\":\"scene\",\"rid\":";
        jsonAppendEscaped(s, r.scenes[k]);
        s += '}';
      }
      s += "]}";
    } else {
      s += ",\"target\":{\"rtype\":";
      jsonAppendEscaped(s, r.rtype);
      s += ",\"rid\":";
      jsonAppendEscaped(s, r.rid);
      s += "}}";
    }
  }
  s += "]}";
  return s;
}

inline void recipeSceneTargetOne(const char *obj, void *ctx) {
  HueRecipe *r = static_cast<HueRecipe *>(ctx);
  if (!r || r->sceneCount >= kMaxSceneTargets) {
    return;
  }
  char rtype[16];
  char rid[40];
  if (!jsonGetString(obj, "rtype", rtype, sizeof(rtype)) || strcmp(rtype, "scene") != 0) {
    return;
  }
  if (!jsonGetString(obj, "rid", rid, sizeof(rid)) || !rid[0]) {
    return;
  }
  recipeCopyField(r->scenes[r->sceneCount], sizeof(r->scenes[0]), rid);
  r->sceneCount++;
}

// recall_scene: targets[] (1–8 scenes), or the old single target with rtype scene.
// on / off / toggle / dim: target with rtype light or grouped_light.
inline bool recipeFromObject(const char *obj, HueRecipe *out) {
  if (!obj || !out) {
    return false;
  }
  memset(out, 0, sizeof(*out));
  if (!jsonGetString(obj, "channelId", out->channelId, sizeof(out->channelId)) || !out->channelId[0]) {
    return false;
  }
  if (!jsonGetString(obj, "event", out->event, sizeof(out->event)) || !recipeEventOk(out->event)) {
    return false;
  }
  if (!jsonGetString(obj, "action", out->action, sizeof(out->action)) ||
      !recipeActionOk(out->action, out->event)) {
    return false;
  }
  char rtype[16] = "";
  char rid[40] = "";
  const bool hasTarget = jsonGetObjectString(obj, "target", "rtype", rtype, sizeof(rtype)) &&
                         jsonGetObjectString(obj, "target", "rid", rid, sizeof(rid)) && rid[0];
  if (strcmp(out->action, "recall_scene") == 0) {
    jsonEachArrayObject(obj, "targets", recipeSceneTargetOne, out);
    if (!out->sceneCount && hasTarget && strcmp(rtype, "scene") == 0) {
      recipeCopyField(out->scenes[0], sizeof(out->scenes[0]), rid);
      out->sceneCount = 1;
    }
    return out->sceneCount > 0;
  }
  if (!hasTarget || !recipeRtypeOk(rtype)) {
    return false;
  }
  recipeCopyField(out->rtype, sizeof(out->rtype), rtype);
  recipeCopyField(out->rid, sizeof(out->rid), rid);
  return true;
}

inline void recipesParseOne(const char *obj, void *ctx) {
  RecipeConfig *cfg = static_cast<RecipeConfig *>(ctx);
  if (!cfg || cfg->recipeCount >= kMaxRecipes) {
    return;
  }
  if (recipeFromObject(obj, &cfg->recipes[cfg->recipeCount])) {
    cfg->recipeCount++;
  }
}

inline void channelSettingParseOne(const char *obj, void *ctx) {
  RecipeConfig *cfg = static_cast<RecipeConfig *>(ctx);
  if (!cfg || cfg->channelCount >= kMaxChannelSettings) {
    return;
  }
  ChannelSetting &c = cfg->channels[cfg->channelCount];
  char kind[16];
  if (!jsonGetString(obj, "id", c.id, sizeof(c.id)) || !c.id[0]) {
    return;
  }
  if (!jsonGetString(obj, "kind", kind, sizeof(kind))) {
    return;
  }
  if (strcmp(kind, "maintained") == 0) {
    c.kind = CHK_MAINTAINED;
  } else if (strcmp(kind, "momentary") == 0) {
    c.kind = CHK_MOMENTARY;
  } else {
    return;
  }
  cfg->channelCount++;
}

// Fills cfg from a config body or the NVS copy. channels[] present = the console picked the
// channels (absent pins are ignored); missing = old payload, compiled defaults.
inline void recipesParseInto(const char *json, RecipeConfig *cfg) {
  memset(cfg, 0, sizeof(*cfg));
  if (!json) {
    return;
  }
  cfg->fromConsole = jsonHasKey(json, "channels");
  if (cfg->fromConsole) {
    jsonEachArrayObject(json, "channels", channelSettingParseOne, cfg);
  }
  jsonEachArrayObject(json, "recipes", recipesParseOne, cfg);
}

inline bool recipesParseConfig(const char *body, uint32_t *revOut) {
  if (!body || !revOut) {
    return false;
  }
  if (!jsonHasKey(body, "rev") || !jsonHasKey(body, "recipes")) {
    return false;
  }
  const int rev = jsonGetInt(body, "rev", -1);
  if (rev < 0) {
    return false;
  }
  recipesParseInto(body, &gRecipeStage);
  *revOut = static_cast<uint32_t>(rev);
  return true;
}

// Debug builds: NVS entry use around a config save. The worst case (7 channels x 3 recipes,
// one 8-scene list per channel) is a ~6.1 KB blob, ~192 entries, and a rewrite holds the old
// and new copies at once. Check on a board that such a config saves twice in a row.
#if SERIAL_DEBUG
inline void recipesLogNvsStats(const char *when, size_t blobLen) {
  nvs_stats_t st;
  if (nvs_get_stats(nullptr, &st) != ESP_OK) {
    LOG("NVS %s: stats failed\n", when);
    return;
  }
  size_t ns = 0;
  nvs_handle_t h;
  if (nvs_open("recipes", NVS_READONLY, &h) == ESP_OK) {
    nvs_get_used_entry_count(h, &ns);
    nvs_close(h);
  }
  LOG("NVS %s save (blob %u bytes): used %u, available %u, free %u, total %u, namespaces %u; recipes ns %u\n",
      when, static_cast<unsigned>(blobLen), static_cast<unsigned>(st.used_entries),
      static_cast<unsigned>(st.available_entries), static_cast<unsigned>(st.free_entries),
      static_cast<unsigned>(st.total_entries), static_cast<unsigned>(st.namespace_count),
      static_cast<unsigned>(ns));
}
#else
inline void recipesLogNvsStats(const char *, size_t) {}
#endif

// false if NVS did not take the recipes or the rev. The rev goes last and only after the
// recipes saved: a failed write or a reset in between leaves the old rev, so the console
// sends this config again instead of answering "up to date" to recipes we never stored.
inline bool recipesSave() {
  const String json = recipesToJson();
  recipesLogNvsStats("before", json.length());
  Preferences prefs;
  if (!prefs.begin("recipes", false)) {
    return false;
  }
  // A blob: scene lists can pass the 4000-byte limit of an NVS string.
  bool ok = prefs.putBytes("jsonb", json.c_str(), json.length()) == json.length();
  ok = ok && prefs.putString("bid", gRecipeBridgeId) == gRecipeBridgeId.length();
  ok = ok && prefs.putUInt("rev", gRecipeRev) > 0;
  if (ok && prefs.isKey("json")) {
    prefs.remove("json");
  }
  prefs.end();
  recipesLogNvsStats(ok ? "after" : "after FAILED", json.length());
  return ok;
}

// Caller holds the lock.
inline void recipesApply(const RecipeConfig &cfg) {
  gRecipeCount = cfg.recipeCount;
  memcpy(gRecipes, cfg.recipes, sizeof(gRecipes));
  gChannelSettingCount = cfg.channelCount;
  memcpy(gChannelSettings, cfg.channels, sizeof(gChannelSettings));
  gChannelsFromConsole = cfg.fromConsole;
  gRecipesGen++;
}

// Caller holds the lock.
inline void recipesClear() {
  gRecipeCount = 0;
  gChannelSettingCount = 0;
  gChannelsFromConsole = false;
  gRecipesGen++;
  gRecipeRev = 0;
  recipesSave();
}

// HUECLR: erases the recipes namespace, not the whole flash.
inline void recipesWipe() {
  recipesLock();
  gNvsEpoch++;
  gRecipeCount = 0;
  gChannelSettingCount = 0;
  gChannelsFromConsole = false;
  gRecipesGen++;
  gRecipeRev = 0;
  gRecipeBridgeId = "";
  Preferences prefs;
  if (prefs.begin("recipes", false)) {
    prefs.clear();
    prefs.end();
  }
  recipesUnlock();
}

inline void recipesLoad() {
  recipesMuxEnsure();
  Preferences prefs;
  prefs.begin("recipes", true);
  gRecipeRev = prefs.getUInt("rev", 0);
  gRecipeBridgeId = prefs.getString("bid", "");
  String json;
  const size_t len = prefs.isKey("jsonb") ? prefs.getBytesLength("jsonb") : 0;
  if (len) {
    char *buf = static_cast<char *>(malloc(len + 1));
    if (buf) {
      prefs.getBytes("jsonb", buf, len);
      buf[len] = 0;
      json = buf;
      free(buf);
    }
  } else if (prefs.isKey("json")) {
    // Firmware < 0.3.0 stored the bare recipes array as a string.
    json = "{\"recipes\":" + prefs.getString("json", "[]") + "}";
  }
  prefs.end();
  recipesParseInto(json.c_str(), &gRecipeStage);
  recipesLock();
  recipesApply(gRecipeStage);
  recipesUnlock();
  LOG("NVS recipes rev=%u count=%u channels=%s\n", gRecipeRev, gRecipeCount,
      gChannelsFromConsole ? "console" : "defaults");
}

// true if the bridgeid changed and recipes/rev were dropped (call BEFORE the poll).
inline bool recipesBindBridge(const String &bid) {
  if (!bid.length()) {
    return false;
  }
  recipesLock();
  bool dropped = false;
  if (gRecipeBridgeId.length() && !gRecipeBridgeId.equalsIgnoreCase(bid)) {
    LOGLN("Bridge id changed — dropping recipes");
    gRecipeBridgeId = bid;
    recipesClear();
    dropped = true;
  } else if (gRecipeBridgeId != bid) {
    gRecipeBridgeId = bid;
    recipesSave();
  }
  recipesUnlock();
  return dropped;
}

inline const HueRecipe *recipesFind(const char *channelId, const char *event) {
  if (!channelId || !event) {
    return nullptr;
  }
  for (uint8_t i = 0; i < gRecipeCount; i++) {
    if (strcmp(gRecipes[i].channelId, channelId) == 0 && strcmp(gRecipes[i].event, event) == 0) {
      return &gRecipes[i];
    }
  }
  return nullptr;
}

// Channel kind from the console's channels[]. CHK_NONE: not configured (pin ignored).
// Only meaningful when gChannelsFromConsole. Caller holds the lock.
inline uint8_t recipesChannelKind(const char *channelId) {
  for (uint8_t i = 0; i < gChannelSettingCount; i++) {
    if (strcmp(gChannelSettings[i].id, channelId) == 0) {
      return gChannelSettings[i].kind;
    }
  }
  return CHK_NONE;
}
