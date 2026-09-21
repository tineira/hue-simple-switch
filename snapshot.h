#pragma once

#include "channels.h"
#include "hue.h"
#include "json_util.h"
#include "recipes.h"

// Snapshot compacto para POST /api/device/register (no el JSON crudo de Clip v2).

static const uint8_t kMaxOwners = 64;

struct LightOwner {
  char lightId[40];
  char ownerRid[40];
};

inline LightOwner gOwners[kMaxOwners];
inline uint8_t gOwnerCount = 0;

struct SnapBuild {
  String *out;
  const char *groupRtype;
  int count;
};

inline void ownerReset() { gOwnerCount = 0; }

inline void ownerAdd(const char *lightId, const char *ownerRid) {
  if (gOwnerCount >= kMaxOwners || !lightId || !lightId[0]) {
    return;
  }
  recipeCopyField(gOwners[gOwnerCount].lightId, sizeof(gOwners[0].lightId), lightId);
  recipeCopyField(gOwners[gOwnerCount].ownerRid, sizeof(gOwners[0].ownerRid), ownerRid);
  gOwnerCount++;
}

struct LightIdsCtx {
  String *ids;
  bool first;
};

inline void snapshotOnChild(const char *obj, void *ctx) {
  LightIdsCtx *c = static_cast<LightIdsCtx *>(ctx);
  char rid[40];
  char rtype[24];
  if (!jsonGetString(obj, "rid", rid, sizeof(rid)) || !rid[0]) {
    return;
  }
  if (!jsonGetString(obj, "rtype", rtype, sizeof(rtype))) {
    return;
  }
  if (strcmp(rtype, "light") == 0) {
    if (!c->first) {
      *c->ids += ',';
    }
    c->first = false;
    jsonAppendEscaped(*c->ids, rid);
    return;
  }
  if (strcmp(rtype, "device") != 0) {
    return;
  }
  for (uint8_t i = 0; i < gOwnerCount; i++) {
    if (strcmp(gOwners[i].ownerRid, rid) == 0) {
      if (!c->first) {
        *c->ids += ',';
      }
      c->first = false;
      jsonAppendEscaped(*c->ids, gOwners[i].lightId);
    }
  }
}

inline void snapshotOnLight(const char *obj, void *ctx) {
  SnapBuild *s = static_cast<SnapBuild *>(ctx);
  char id[40];
  char name[96];
  char owner[40];
  if (!jsonGetString(obj, "id", id, sizeof(id)) || !id[0]) {
    return;
  }
  if (!jsonGetObjectString(obj, "metadata", "name", name, sizeof(name)) || !name[0]) {
    recipeCopyField(name, sizeof(name), id);
  }
  owner[0] = 0;
  jsonGetObjectString(obj, "owner", "rid", owner, sizeof(owner));
  if (owner[0]) {
    ownerAdd(id, owner);
  }
  bool on = false;
  jsonHueOn(obj, &on);

  if (s->out->length() > 1) {
    *s->out += ',';
  }
  *s->out += "{\"id\":";
  jsonAppendEscaped(*s->out, id);
  *s->out += ",\"name\":";
  jsonAppendEscaped(*s->out, name);
  *s->out += ",\"on\":";
  *s->out += on ? "true" : "false";
  *s->out += ",\"caps\":[";
  bool cap = false;
  if (jsonHasKey(obj, "dimming")) {
    *s->out += "\"dim\"";
    cap = true;
  }
  if (jsonHasKey(obj, "color")) {
    if (cap) {
      *s->out += ',';
    }
    *s->out += "\"color\"";
  } else if (jsonHasKey(obj, "color_temperature")) {
    if (cap) {
      *s->out += ',';
    }
    *s->out += "\"ct\"";
  }
  *s->out += "]}";
  s->count++;
}

inline void snapshotOnGroup(const char *obj, void *ctx) {
  SnapBuild *s = static_cast<SnapBuild *>(ctx);
  char id[40];
  char name[96];
  char gl[40];
  if (!jsonGetString(obj, "id", id, sizeof(id)) || !id[0]) {
    return;
  }
  if (!jsonGetObjectString(obj, "metadata", "name", name, sizeof(name)) || !name[0]) {
    recipeCopyField(name, sizeof(name), id);
  }
  gl[0] = 0;
  jsonFindRidByRtype(obj, "grouped_light", gl, sizeof(gl));

  String lightIds = "[";
  LightIdsCtx ids{&lightIds, true};
  jsonEachArrayObject(obj, "children", snapshotOnChild, &ids);
  lightIds += ']';

  if (s->out->length() > 1) {
    *s->out += ',';
  }
  *s->out += "{\"id\":";
  jsonAppendEscaped(*s->out, id);
  *s->out += ",\"name\":";
  jsonAppendEscaped(*s->out, name);
  *s->out += ",\"grouped_light_id\":";
  if (gl[0]) {
    jsonAppendEscaped(*s->out, gl);
  } else {
    *s->out += "null";
  }
  *s->out += ",\"light_ids\":";
  *s->out += lightIds;
  *s->out += ",\"rtype\":";
  jsonAppendEscaped(*s->out, s->groupRtype ? s->groupRtype : "room");
  *s->out += '}';
  s->count++;
}

inline void snapshotOnScene(const char *obj, void *ctx) {
  SnapBuild *s = static_cast<SnapBuild *>(ctx);
  char id[40];
  char name[96];
  char groupRtype[24];
  char groupRid[40];
  if (!jsonGetString(obj, "id", id, sizeof(id)) || !id[0]) {
    return;
  }
  if (!jsonGetObjectString(obj, "metadata", "name", name, sizeof(name)) || !name[0]) {
    recipeCopyField(name, sizeof(name), id);
  }
  groupRtype[0] = 0;
  groupRid[0] = 0;
  jsonGetObjectString(obj, "group", "rtype", groupRtype, sizeof(groupRtype));
  jsonGetObjectString(obj, "group", "rid", groupRid, sizeof(groupRid));

  if (s->out->length() > 1) {
    *s->out += ',';
  }
  *s->out += "{\"id\":";
  jsonAppendEscaped(*s->out, id);
  *s->out += ",\"name\":";
  jsonAppendEscaped(*s->out, name);
  *s->out += ",\"group_rtype\":";
  jsonAppendEscaped(*s->out, groupRtype);
  *s->out += ",\"group_rid\":";
  jsonAppendEscaped(*s->out, groupRid);
  *s->out += '}';
  s->count++;
}

inline bool hueStreamResource(const char *resource, JsonObjFn fn, void *ctx, int *countOut) {
  JsonDataSink sink;
  sink.onObject = fn;
  sink.ctx = ctx;
  const int code = hueClipStream(resource, sink);
  if (countOut) {
    *countOut = sink.objects;
  }
  if (code != HTTP_CODE_OK) {
    LOG("Hue stream %s %d\n", resource, code);
    return false;
  }
  if (sink.overflow) {
    LOG("Hue stream %s: object overflow\n", resource);
  }
  LOG("Hue stream %s ok objects=%d\n", resource, sink.objects);
  return true;
}

inline bool hueBuildSnapshot(String *lights, String *rooms, String *scenes) {
  if (!lights || !rooms || !scenes) {
    return false;
  }
  ownerReset();
  *lights = "[";
  *rooms = "[";
  *scenes = "[";

  SnapBuild lightCtx{lights, nullptr, 0};
  if (!hueStreamResource("light", snapshotOnLight, &lightCtx, nullptr)) {
    LOGLN("snapshot aborted: light stream failed");
    return false;
  }

  SnapBuild roomCtx{rooms, "room", 0};
  if (!hueStreamResource("room", snapshotOnGroup, &roomCtx, nullptr)) {
    LOGLN("snapshot aborted: room stream failed");
    return false;
  }
  SnapBuild zoneCtx{rooms, "zone", 0};
  if (!hueStreamResource("zone", snapshotOnGroup, &zoneCtx, nullptr)) {
    LOGLN("snapshot aborted: zone stream failed");
    return false;
  }

  SnapBuild sceneCtx{scenes, nullptr, 0};
  if (!hueStreamResource("scene", snapshotOnScene, &sceneCtx, nullptr)) {
    LOGLN("snapshot aborted: scene stream failed");
    return false;
  }

  *lights += ']';
  *rooms += ']';
  *scenes += ']';
  LOG("Snapshot lights=%d rooms+zones=%d scenes=%d\n", lightCtx.count, roomCtx.count + zoneCtx.count,
                sceneCtx.count);
  return true;
}
