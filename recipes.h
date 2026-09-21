#pragma once

#include <Preferences.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "json_util.h"

// Recetas en NVS: el GPIO solo mira esto, nunca Vercel.

static const uint8_t kMaxRecipes = 16;

struct HueRecipe {
  char channelId[12];
  char event[16];
  char action[16];
  char rtype[16];
  char rid[40];
};

inline HueRecipe gRecipes[kMaxRecipes];
inline uint8_t gRecipeCount = 0;
inline uint32_t gRecipeRev = 0;
inline String gRecipeBridgeId;
inline volatile bool gNeedConsoleSync = false;
inline SemaphoreHandle_t gRecipesMux = nullptr;

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
               strcmp(e, "short") == 0);
}

inline bool recipeActionOk(const char *a) {
  return a && (strcmp(a, "on") == 0 || strcmp(a, "off") == 0 || strcmp(a, "recall_scene") == 0 ||
               strcmp(a, "toggle") == 0);
}

inline bool recipeRtypeOk(const char *r) {
  return r && (strcmp(r, "light") == 0 || strcmp(r, "grouped_light") == 0 || strcmp(r, "scene") == 0);
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

inline String recipesToJson() {
  String s = "[";
  for (uint8_t i = 0; i < gRecipeCount; i++) {
    if (i) {
      s += ',';
    }
    s += "{\"channelId\":";
    jsonAppendEscaped(s, gRecipes[i].channelId);
    s += ",\"event\":";
    jsonAppendEscaped(s, gRecipes[i].event);
    s += ",\"action\":";
    jsonAppendEscaped(s, gRecipes[i].action);
    s += ",\"target\":{\"rtype\":";
    jsonAppendEscaped(s, gRecipes[i].rtype);
    s += ",\"rid\":";
    jsonAppendEscaped(s, gRecipes[i].rid);
    s += "}}";
  }
  s += "]";
  return s;
}

inline bool recipeFromObject(const char *obj, HueRecipe *out) {
  if (!obj || !out) {
    return false;
  }
  char channelId[12];
  char event[16];
  char action[16];
  char rtype[16];
  char rid[40];
  if (!jsonGetString(obj, "channelId", channelId, sizeof(channelId)) || !channelId[0]) {
    return false;
  }
  if (!jsonGetString(obj, "event", event, sizeof(event)) || !recipeEventOk(event)) {
    return false;
  }
  if (!jsonGetString(obj, "action", action, sizeof(action)) || !recipeActionOk(action)) {
    return false;
  }
  if (!jsonGetObjectString(obj, "target", "rtype", rtype, sizeof(rtype)) || !recipeRtypeOk(rtype)) {
    return false;
  }
  if (!jsonGetObjectString(obj, "target", "rid", rid, sizeof(rid)) || !rid[0]) {
    return false;
  }
  memset(out, 0, sizeof(*out));
  recipeCopyField(out->channelId, sizeof(out->channelId), channelId);
  recipeCopyField(out->event, sizeof(out->event), event);
  recipeCopyField(out->action, sizeof(out->action), action);
  recipeCopyField(out->rtype, sizeof(out->rtype), rtype);
  recipeCopyField(out->rid, sizeof(out->rid), rid);
  return true;
}

inline void recipesParseOne(const char *obj, void *ctx) {
  uint8_t *n = static_cast<uint8_t *>(ctx);
  if (!n || *n >= kMaxRecipes) {
    return;
  }
  HueRecipe rec;
  if (!recipeFromObject(obj, &rec)) {
    return;
  }
  gRecipes[*n] = rec;
  (*n)++;
}

inline bool recipesParseArray(const char *json, uint8_t *countOut) {
  uint8_t n = 0;
  if (!json) {
    if (countOut) {
      *countOut = 0;
    }
    return false;
  }
  if (!strchr(json, '[')) {
    return false;
  }
  jsonEachArrayObject(json, "recipes", recipesParseOne, &n);
  // NVS guarda el array suelto, sin clave "recipes".
  if (n == 0 && json[0] == '[') {
    const char *p = json;
    const char *start = nullptr;
    int depth = 0;
    bool inString = false;
    bool escape = false;
    for (; *p; p++) {
      const char c = *p;
      if (depth == 0 && !inString) {
        if (c == ']') {
          break;
        }
        if (c == '{') {
          depth = 1;
          start = p;
          inString = false;
          escape = false;
        }
        continue;
      }
      if (escape) {
        escape = false;
        continue;
      }
      if (inString) {
        if (c == '\\') {
          escape = true;
        } else if (c == '"') {
          inString = false;
        }
        continue;
      }
      if (c == '"') {
        inString = true;
        continue;
      }
      if (c == '{') {
        depth++;
      } else if (c == '}') {
        depth--;
        if (depth == 0 && start) {
          const size_t len = static_cast<size_t>(p - start + 1);
          char *tmp = static_cast<char *>(malloc(len + 1));
          if (tmp) {
            memcpy(tmp, start, len);
            tmp[len] = 0;
            recipesParseOne(tmp, &n);
            free(tmp);
          }
          start = nullptr;
        }
      }
    }
  }
  if (countOut) {
    *countOut = n;
  }
  return true;
}

inline bool recipesParseConfig(const char *body, uint32_t *revOut) {
  if (!body || !revOut) {
    return false;
  }
  if (!jsonHasKey(body, "rev") || !strstr(body, "\"recipes\"")) {
    return false;
  }
  const int rev = jsonGetInt(body, "rev", -1);
  if (rev < 0) {
    return false;
  }
  gRecipeCount = 0;
  uint8_t n = 0;
  jsonEachArrayObject(body, "recipes", recipesParseOne, &n);
  gRecipeCount = n;
  *revOut = static_cast<uint32_t>(rev);
  return true;
}

inline void recipesSave() {
  Preferences prefs;
  prefs.begin("recipes", false);
  prefs.putUInt("rev", gRecipeRev);
  prefs.putString("bid", gRecipeBridgeId);
  prefs.putString("json", recipesToJson());
  prefs.end();
}

inline void recipesClear() {
  gRecipeCount = 0;
  gRecipeRev = 0;
  memset(gRecipes, 0, sizeof(gRecipes));
  recipesSave();
}

inline void recipesLoad() {
  recipesMuxEnsure();
  Preferences prefs;
  prefs.begin("recipes", true);
  gRecipeRev = prefs.getUInt("rev", 0);
  gRecipeBridgeId = prefs.getString("bid", "");
  const String json = prefs.getString("json", "[]");
  prefs.end();
  gRecipeCount = 0;
  uint8_t n = 0;
  recipesParseArray(json.c_str(), &n);
  gRecipeCount = n;
  Serial.printf("NVS recipes rev=%u count=%u\n", gRecipeRev, gRecipeCount);
}

// true si cambió el bridgeid y se tiraron recetas/rev (llamar ANTES del poll).
inline bool recipesBindBridge(const String &bid) {
  if (!bid.length()) {
    return false;
  }
  recipesLock();
  bool dropped = false;
  if (gRecipeBridgeId.length() && !gRecipeBridgeId.equalsIgnoreCase(bid)) {
    Serial.println("Bridge id changed — dropping recipes");
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

inline void recipesReplace(uint32_t rev, const HueRecipe *list, uint8_t n) {
  gRecipeRev = rev;
  gRecipeCount = n > kMaxRecipes ? kMaxRecipes : n;
  if (list && gRecipeCount) {
    memcpy(gRecipes, list, sizeof(HueRecipe) * gRecipeCount);
  }
  recipesSave();
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
