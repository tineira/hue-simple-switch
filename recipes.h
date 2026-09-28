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

// NVS keeps the wire shape, without scene names, one blob per channel (key c_<id>): the
// channel's setting (console config only) and its recipes. "n" is the recipe count, so a
// truncated or mixed-up blob is caught on load. Caller holds the lock.
inline String recipesChannelJson(const char *id) {
  uint8_t n = 0;
  for (uint8_t i = 0; i < gRecipeCount; i++) {
    if (strcmp(gRecipes[i].channelId, id) == 0) {
      n++;
    }
  }
  String s = "{\"n\":";
  s += String(n);
  if (gChannelsFromConsole) {
    for (uint8_t i = 0; i < gChannelSettingCount; i++) {
      if (strcmp(gChannelSettings[i].id, id) != 0) {
        continue;
      }
      s += ",\"channels\":[{\"id\":";
      jsonAppendEscaped(s, gChannelSettings[i].id);
      s += ",\"kind\":";
      jsonAppendEscaped(s, gChannelSettings[i].kind == CHK_MOMENTARY ? "momentary" : "maintained");
      s += "}]";
      break;
    }
  }
  s += ",\"recipes\":[";
  bool first = true;
  for (uint8_t i = 0; i < gRecipeCount; i++) {
    const HueRecipe &r = gRecipes[i];
    if (strcmp(r.channelId, id) != 0) {
      continue;
    }
    if (!first) {
      s += ',';
    }
    first = false;
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

// Caller holds the lock.
inline void recipesApply(const RecipeConfig &cfg) {
  gRecipeCount = cfg.recipeCount;
  memcpy(gRecipes, cfg.recipes, sizeof(gRecipes));
  gChannelSettingCount = cfg.channelCount;
  memcpy(gChannelSettings, cfg.channels, sizeof(gChannelSettings));
  gChannelsFromConsole = cfg.fromConsole;
  gRecipesGen++;
}

// NVS layout, namespace "recipes" (since 0.5.0):
//   c_<id>    blob per channel that has a setting or a recipe (recipesChannelJson)
//   channels  string "<mode>:<id>,<id>,...": mode "console" (the config had channels[]) or
//             "defaults" (old payload); the ids are the c_<id> blobs that make up the config
//   rev       u32, written last. Absent: a save did not finish, and the console resends.
//   bid       the Bridge the recipes belong to
//   ls_<id>   scene cursor per channel (hue_worker.h)
// Firmware < 0.5.0 kept the whole config in one blob, jsonb (< 0.3.0: the string json). A
// rewrite writes the new copy before it erases the old one: with one blob per channel a save
// needs one channel's size free, not the whole config's twice (the worst case did not fit).

static const uint8_t kMaxStoredChannels = kMaxChannelSettings + kMaxRecipes;
// Save scratch, under gRecipesMux (off the task stacks).
inline char gStoreIds[kMaxStoredChannels][12];
inline uint8_t gStoreIdCount = 0;
inline char gStaleKeys[kMaxStoredChannels][NVS_KEY_NAME_MAX_SIZE];

// Debug builds: NVS entry use around a config save or the migration. Worst case (7 channels
// x 3 recipes, an 8-scene list per channel): check on a board that it saves twice in a row.
#if SERIAL_DEBUG
inline void recipesLogNvsStats(const char *when) {
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
  LOG("NVS %s: used %u, available %u, free %u, total %u, namespaces %u; recipes ns %u\n", when,
      static_cast<unsigned>(st.used_entries), static_cast<unsigned>(st.available_entries),
      static_cast<unsigned>(st.free_entries), static_cast<unsigned>(st.total_entries),
      static_cast<unsigned>(st.namespace_count), static_cast<unsigned>(ns));
}
#else
inline void recipesLogNvsStats(const char *) {}
#endif

inline void recipesBlobKey(const char *id, char *key, size_t n) { snprintf(key, n, "c_%s", id); }

inline bool recipesStoreHasId(const char *id) {
  for (uint8_t i = 0; i < gStoreIdCount; i++) {
    if (strcmp(gStoreIds[i], id) == 0) {
      return true;
    }
  }
  return false;
}

inline void recipesStoreIdAdd(const char *id) {
  if (!id || !id[0] || recipesStoreHasId(id) || gStoreIdCount >= kMaxStoredChannels) {
    return;
  }
  recipeCopyField(gStoreIds[gStoreIdCount], sizeof(gStoreIds[0]), id);
  gStoreIdCount++;
}

// Channels that get a blob: each console setting, then each recipe's channel. Caller holds the lock.
inline void recipesCollectIds() {
  gStoreIdCount = 0;
  if (gChannelsFromConsole) {
    for (uint8_t i = 0; i < gChannelSettingCount; i++) {
      recipesStoreIdAdd(gChannelSettings[i].id);
    }
  }
  for (uint8_t i = 0; i < gRecipeCount; i++) {
    recipesStoreIdAdd(gRecipes[i].channelId);
  }
}

inline String recipesChannelsValue() {
  String s = gChannelsFromConsole ? "console:" : "defaults:";
  for (uint8_t i = 0; i < gStoreIdCount; i++) {
    if (i) {
      s += ',';
    }
    s += gStoreIds[i];
  }
  return s;
}

// true when the stored blob already holds exactly v.
inline bool recipesBlobSame(Preferences &prefs, const char *key, const String &v) {
  if (!prefs.isKey(key)) {
    return false;
  }
  const size_t len = prefs.getBytesLength(key);
  if (len != v.length() || !len) {
    return false;
  }
  char *buf = static_cast<char *>(malloc(len));
  if (!buf) {
    return false;
  }
  const bool same = prefs.getBytes(key, buf, len) == len && memcmp(buf, v.c_str(), len) == 0;
  free(buf);
  return same;
}

// Before the first change: from here until rev is written again, a reset leaves no rev.
inline bool recipesDropRev(Preferences &prefs, bool *dropped) {
  if (*dropped) {
    return true;
  }
  *dropped = true;
  return !prefs.isKey("rev") || prefs.remove("rev");
}

// Erases the c_<id> blobs of channels not in gStoreIds (including leftovers of an interrupted
// save). Keys are collected first: NVS entries are not erased while iterating.
inline bool recipesRemoveStale(Preferences &prefs, bool *revDropped) {
  uint8_t n = 0;
  nvs_iterator_t it = nullptr;
  esp_err_t err = nvs_entry_find(NVS_DEFAULT_PART_NAME, "recipes", NVS_TYPE_BLOB, &it);
  while (err == ESP_OK) {
    nvs_entry_info_t info;
    nvs_entry_info(it, &info);
    if (strncmp(info.key, "c_", 2) == 0 && !recipesStoreHasId(info.key + 2) && n < kMaxStoredChannels) {
      recipeCopyField(gStaleKeys[n], sizeof(gStaleKeys[0]), info.key);
      n++;
    }
    err = nvs_entry_next(&it);
  }
  nvs_release_iterator(it);
  for (uint8_t i = 0; i < n; i++) {
    if (!recipesDropRev(prefs, revDropped) || !prefs.remove(gStaleKeys[i])) {
      return false;
    }
  }
  return true;
}

// false if NVS did not take the config. Caller holds the lock. Order: drop rev before the first
// change; write each channel blob whose content changed; erase the blobs of channels that are
// gone; write channels; write rev last. A failed write or a reset before the end leaves no rev
// in NVS, and a failed write also sets the RAM rev to 0, so the next poll gets the config again.
inline bool recipesSave() {
  recipesCollectIds();
  recipesLogNvsStats("before save");
  Preferences prefs;
  if (!prefs.begin("recipes", false)) {
    gRecipeRev = 0;
    return false;
  }
  bool revDropped = false;
  bool ok = true;
  char key[NVS_KEY_NAME_MAX_SIZE];
  for (uint8_t i = 0; ok && i < gStoreIdCount; i++) {
    const String blob = recipesChannelJson(gStoreIds[i]);
    recipesBlobKey(gStoreIds[i], key, sizeof(key));
    if (recipesBlobSame(prefs, key, blob)) {
      continue;
    }
    ok = recipesDropRev(prefs, &revDropped) &&
         prefs.putBytes(key, blob.c_str(), blob.length()) == blob.length();
  }
  ok = ok && recipesRemoveStale(prefs, &revDropped);
  if (ok) {
    const String list = recipesChannelsValue();
    if (!prefs.isKey("channels") || prefs.getString("channels", "") != list) {
      ok = recipesDropRev(prefs, &revDropped) && prefs.putString("channels", list) == list.length();
    }
  }
  if (ok && (!prefs.isKey("bid") || prefs.getString("bid", "") != gRecipeBridgeId)) {
    ok = prefs.putString("bid", gRecipeBridgeId) == gRecipeBridgeId.length();
  }
  if (ok && (revDropped || !prefs.isKey("rev") || prefs.getUInt("rev", 0) != gRecipeRev)) {
    ok = prefs.putUInt("rev", gRecipeRev) > 0;
  }
  prefs.end();
  if (!ok) {
    gRecipeRev = 0;
  }
  recipesLogNvsStats(ok ? "after save" : "after FAILED save");
  return ok;
}

// Appends one channel blob to cfg. false: missing, unreadable, or not the recipes it claims.
inline bool recipesLoadChannel(Preferences &prefs, const char *id, RecipeConfig *cfg) {
  char key[NVS_KEY_NAME_MAX_SIZE];
  recipesBlobKey(id, key, sizeof(key));
  if (!prefs.isKey(key)) {
    return false;
  }
  const size_t len = prefs.getBytesLength(key);
  if (!len) {
    return false;
  }
  char *buf = static_cast<char *>(malloc(len + 1));
  if (!buf) {
    return false;
  }
  const bool read = prefs.getBytes(key, buf, len) == len;
  buf[len] = 0;
  const int n = (read && buf[0] == '{' && buf[len - 1] == '}') ? jsonGetInt(buf, "n", -1) : -1;
  const uint8_t r0 = cfg->recipeCount;
  const uint8_t c0 = cfg->channelCount;
  if (n >= 0) {
    if (cfg->fromConsole) {
      jsonEachArrayObject(buf, "channels", channelSettingParseOne, cfg);
    }
    jsonEachArrayObject(buf, "recipes", recipesParseOne, cfg);
  }
  free(buf);
  bool ok = n >= 0 && cfg->recipeCount - r0 == n;
  for (uint8_t k = r0; ok && k < cfg->recipeCount; k++) {
    ok = strcmp(cfg->recipes[k].channelId, id) == 0;
  }
  for (uint8_t k = c0; ok && k < cfg->channelCount; k++) {
    ok = strcmp(cfg->channels[k].id, id) == 0;
  }
  return ok;
}

// Fills cfg from channels + c_<id>. false: a listed blob is missing or corrupt (cfg is then empty).
inline bool recipesLoadChannels(Preferences &prefs, RecipeConfig *cfg) {
  memset(cfg, 0, sizeof(*cfg));
  const String list = prefs.getString("channels", "");
  const int colon = list.indexOf(':');
  if (colon < 0) {
    return false;
  }
  cfg->fromConsole = list.substring(0, colon) == "console";
  int start = colon + 1;
  while (start < static_cast<int>(list.length())) {
    int end = list.indexOf(',', start);
    if (end < 0) {
      end = list.length();
    }
    const String id = list.substring(start, end);
    if (!id.length() || !recipesLoadChannel(prefs, id.c_str(), cfg)) {
      memset(cfg, 0, sizeof(*cfg));
      return false;
    }
    start = end + 1;
  }
  return true;
}

// Firmware < 0.5.0: the whole config in jsonb (or json). There is no room for it and the channel
// blobs at once, so: parse it into RAM, erase jsonb and rev, write the channel blobs, channels,
// then rev. A reset midway leaves no rev and the console resends; this boot runs from RAM.
inline void recipesMigrate(Preferences &prefs) {
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
  const uint32_t rev = prefs.getUInt("rev", 0);
  recipesParseInto(json.c_str(), &gRecipeStage);
  recipesLogNvsStats("before migration");
  bool ok = (!prefs.isKey("jsonb") || prefs.remove("jsonb")) && (!prefs.isKey("json") || prefs.remove("json")) &&
            (!prefs.isKey("rev") || prefs.remove("rev"));
  prefs.end();
  recipesLock();
  recipesApply(gRecipeStage);
  gRecipeRev = ok ? rev : 0;
  ok = ok && recipesSave();
  if (!ok) {
    gRecipeRev = 0;
  }
  recipesUnlock();
  LOG("NVS migration to one blob per channel %s (%u recipes)\n", ok ? "done" : "FAILED, the console resends",
      static_cast<unsigned>(gRecipeCount));
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
  // Read-write: the first boot after an update from < 0.5.0 migrates the old blob.
  if (!prefs.begin("recipes", false)) {
    LOGLN("NVS recipes: open failed");
    return;
  }
  gRecipeBridgeId = prefs.getString("bid", "");
  if (prefs.isKey("jsonb") || prefs.isKey("json")) {
    recipesMigrate(prefs);  // ends prefs
  } else {
    // A listed blob missing or corrupt: nothing loads and there is no rev. No rev alone (a save
    // did not finish): each blob is one whole channel, so they load, and the console resends.
    const bool hasList = prefs.isKey("channels");
    const bool loaded = hasList && recipesLoadChannels(prefs, &gRecipeStage);
    if (!hasList) {
      memset(&gRecipeStage, 0, sizeof(gRecipeStage));
    } else if (!loaded) {
      LOGLN("NVS recipes: a channel blob is missing or corrupt, waiting for the console");
    }
    const uint32_t rev = (loaded && prefs.isKey("rev")) ? prefs.getUInt("rev", 0) : 0;
    prefs.end();
    recipesLock();
    recipesApply(gRecipeStage);
    gRecipeRev = rev;
    recipesUnlock();
  }
  LOG("NVS recipes rev=%u count=%u channels=%s\n", static_cast<unsigned>(gRecipeRev),
      static_cast<unsigned>(gRecipeCount), gChannelsFromConsole ? "console" : "defaults");
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
