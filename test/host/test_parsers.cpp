// Host tests for the JSON helpers (json_util.h) and the config-poll parsers (recipes.h).
// Plain C++ against the stubs in test/host/stubs; no board. Run with test/host/run.sh.

#define LOG(...) ((void)0)
#define LOGS(s) ((void)0)
#define LOGLN(s) ((void)0)

#include "json_util.h"
#include "recipes.h"

#include <algorithm>
#include <string>
#include <vector>

static int gChecks = 0;
static int gFailures = 0;

#define CHECK(cond)                                                    \
  do {                                                                 \
    gChecks++;                                                         \
    if (!(cond)) {                                                     \
      gFailures++;                                                     \
      fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
    }                                                                  \
  } while (0)

#define CHECK_STR(actual, expected)                                                        \
  do {                                                                                     \
    gChecks++;                                                                             \
    const std::string a_(actual);                                                          \
    const std::string e_(expected);                                                        \
    if (a_ != e_) {                                                                        \
      gFailures++;                                                                         \
      fprintf(stderr, "%s:%d: %s is \"%s\", expected \"%s\"\n", __FILE__, __LINE__, #actual, \
              a_.c_str(), e_.c_str());                                                     \
    }                                                                                      \
  } while (0)

// ---------------------------------------------------------------------------------------------
// json_util.h: field helpers

static void testGetString() {
  char out[32];
  CHECK(jsonGetString("{\"a\":\"x\",\"b\":\"yz\"}", "b", out, sizeof(out)));
  CHECK_STR(out, "yz");
  CHECK(jsonGetString("{\"a\": \n \"spaced\"}", "a", out, sizeof(out)));
  CHECK_STR(out, "spaced");
  CHECK(jsonGetString("{\"a\":\"q\\\"t\\\\b\\nc\"}", "a", out, sizeof(out)));
  CHECK_STR(out, "q\"t\\b\nc");
  CHECK(jsonGetString("{\"a\":\"\"}", "a", out, sizeof(out)));
  CHECK_STR(out, "");
  // Truncated to the buffer, still terminated.
  char small[4];
  CHECK(jsonGetString("{\"a\":\"abcdef\"}", "a", small, sizeof(small)));
  CHECK_STR(small, "abc");
  CHECK(!jsonGetString("{\"a\":\"x\"}", "missing", out, sizeof(out)));
  CHECK(!jsonGetString("{\"a\":12}", "a", out, sizeof(out)));
  CHECK(!jsonGetString("{\"a\":null}", "a", out, sizeof(out)));
  CHECK(!jsonGetString(nullptr, "a", out, sizeof(out)));
  CHECK(!jsonGetString("{\"a\":\"x\"}", "a", out, 1));
}

static void testGetInt() {
  CHECK(jsonGetInt("{\"rev\":42}", "rev", -1) == 42);
  CHECK(jsonGetInt("{\"rev\": 7,\"x\":1}", "rev", -1) == 7);
  CHECK(jsonGetInt("{\"rev\":-3}", "rev", 0) == -3);
  CHECK(jsonGetInt("{\"rev\":\"5\"}", "rev", -1) == -1);
  CHECK(jsonGetInt("{\"rev\":null}", "rev", -1) == -1);
  CHECK(jsonGetInt("{}", "rev", 9) == 9);
  CHECK(jsonGetInt(nullptr, "rev", 9) == 9);
}

static void testObjectStringAndHasKey() {
  char out[40];
  const char *j = "{\"id\":\"s1\",\"metadata\":{\"name\":\"Relax\"},\"group\": {\"rtype\":\"room\",\"rid\":\"r1\"}}";
  CHECK(jsonGetObjectString(j, "metadata", "name", out, sizeof(out)));
  CHECK_STR(out, "Relax");
  CHECK(jsonGetObjectString(j, "group", "rid", out, sizeof(out)));
  CHECK_STR(out, "r1");
  CHECK(!jsonGetObjectString(j, "id", "name", out, sizeof(out)));  // not an object
  CHECK(!jsonGetObjectString(j, "owner", "rid", out, sizeof(out)));
  CHECK(jsonHasKey(j, "metadata"));
  CHECK(!jsonHasKey(j, "meta"));
  CHECK(!jsonHasKey(nullptr, "id"));
}

static void testHueOnAndBrightness() {
  bool on = false;
  CHECK(jsonHueOn("{\"id\":\"l1\",\"on\":{\"on\":true},\"dimming\":{\"brightness\":50.5}}", &on));
  CHECK(on);
  CHECK(jsonHueOn("{\"on\" : { \"on\" : false }}", &on));
  CHECK(!on);
  CHECK(!jsonHueOn("{\"id\":\"l1\"}", &on));

  float bri = -1;
  CHECK(jsonHueBrightness("{\"dimming\":{\"brightness\":42.3,\"min_dim_level\":0.2}}", &bri));
  CHECK(bri > 42.2f && bri < 42.4f);
  // Only the first dimming object counts; powerup.dimming further down is not read.
  CHECK(!jsonHueBrightness("{\"dimming\":{\"min_dim_level\":1},\"powerup\":{\"dimming\":{\"brightness\":9}}}",
                           &bri));
  CHECK(!jsonHueBrightness("{\"on\":{\"on\":true}}", &bri));
}

static void testFindRidByRtype() {
  char out[40];
  const char *room =
      "{\"id\":\"room1\",\"services\":[{\"rid\":\"gl-1\",\"rtype\":\"grouped_light\"}],"
      "\"children\":[{\"rid\":\"dev-1\",\"rtype\":\"device\"}]}";
  CHECK(jsonFindRidByRtype(room, "grouped_light", out, sizeof(out)));
  CHECK_STR(out, "gl-1");
  CHECK(jsonFindRidByRtype(room, "device", out, sizeof(out)));
  CHECK_STR(out, "dev-1");
  CHECK(jsonFindRidByRtype("{\"services\":[{\"rid\":\"x\", \"rtype\": \"light\"}]}", "light", out,
                           sizeof(out)));
  CHECK_STR(out, "x");
  CHECK(!jsonFindRidByRtype(room, "light", out, sizeof(out)));
}

static void collectObjects(const char *obj, void *ctx) {
  static_cast<std::vector<std::string> *>(ctx)->push_back(obj);
}

static void testEachArrayObject() {
  std::vector<std::string> got;
  jsonEachArrayObject(
      "{\"items\":[{\"a\":{\"b\":1}}, {\"s\":\"has } and { and \\\" inside\"},{}],\"after\":[{\"no\":1}]}",
      "items", collectObjects, &got);
  CHECK(got.size() == 3);
  if (got.size() == 3) {
    CHECK_STR(got[0], "{\"a\":{\"b\":1}}");
    CHECK_STR(got[1], "{\"s\":\"has } and { and \\\" inside\"}");
    CHECK_STR(got[2], "{}");
  }
  got.clear();
  jsonEachArrayObject("{\"items\":[]}", "items", collectObjects, &got);
  CHECK(got.empty());
  jsonEachArrayObject("{\"other\":[{}]}", "items", collectObjects, &got);
  CHECK(got.empty());
}

static void testAppendEscapedRoundTrip() {
  const char *raw = "Living \"room\" \\ 1\n\ttab";
  String s = "{\"name\":";
  jsonAppendEscaped(s, raw);
  s += '}';
  char out[64];
  CHECK(jsonGetString(s.c_str(), "name", out, sizeof(out)));
  CHECK_STR(out, raw);
  // Other control characters are dropped.
  String c;
  jsonAppendEscaped(c, "a\x01" "b");
  CHECK_STR(c.c_str(), "\"ab\"");
  String n;
  jsonAppendEscaped(n, nullptr);
  CHECK_STR(n.c_str(), "\"\"");
}

// ---------------------------------------------------------------------------------------------
// json_util.h: JsonDataSink (streamed Clip v2 bodies)

struct SinkObj {
  std::string id;
  std::string name;
  std::string raw;
};

static void sinkCollect(const char *obj, void *ctx) {
  SinkObj o;
  char buf[96];
  if (jsonGetString(obj, "id", buf, sizeof(buf))) {
    o.id = buf;
  }
  if (jsonGetObjectString(obj, "metadata", "name", buf, sizeof(buf))) {
    o.name = buf;
  }
  o.raw = obj;
  static_cast<std::vector<SinkObj> *>(ctx)->push_back(o);
}

// Feeds body in pieces of `step` bytes (0 = one write, 1 = byte by byte through write(uint8_t)).
static std::vector<SinkObj> sinkRun(const std::string &body, size_t step, JsonDataSink *keep = nullptr) {
  std::vector<SinkObj> got;
  JsonDataSink local;
  JsonDataSink &sink = keep ? *keep : local;
  sink.onObject = sinkCollect;
  sink.ctx = &got;
  const uint8_t *p = reinterpret_cast<const uint8_t *>(body.data());
  if (step == 0) {
    sink.write(p, body.size());
  } else if (step == 1) {
    for (size_t i = 0; i < body.size(); i++) {
      sink.write(p[i]);
    }
  } else {
    for (size_t i = 0; i < body.size(); i += step) {
      sink.write(p + i, std::min(step, body.size() - i));
    }
  }
  return got;
}

// A Clip v2 scene action entry, ~200 bytes.
static std::string sceneAction(int i) {
  char buf[256];
  snprintf(buf, sizeof(buf),
           "{\"target\":{\"rid\":\"%08d-0000-4000-8000-000000000000\",\"rtype\":\"light\"},"
           "\"action\":{\"on\":{\"on\":true},\"dimming\":{\"brightness\":%d.0},"
           "\"color\":{\"xy\":{\"x\":0.4573,\"y\":0.41}},\"note\":\"} ] { \\\" \"}}",
           i, i % 100);
  return buf;
}

static void testSinkBasic() {
  const std::string body =
      "{\"errors\":[],\"data\":[{\"id\":\"a\",\"metadata\":{\"name\":\"One\"}},"
      "{\"id\":\"b\",\"metadata\":{\"name\":\"Two, {braces}\"}}]}";
  for (size_t step : {0, 1, 3, 7}) {
    JsonDataSink sink;
    const std::vector<SinkObj> got = sinkRun(body, step, &sink);
    CHECK(got.size() == 2);
    CHECK(sink.objects == 2);
    CHECK(!sink.overflow);
    if (got.size() == 2) {
      CHECK_STR(got[0].id, "a");
      CHECK_STR(got[0].name, "One");
      CHECK_STR(got[1].id, "b");
      CHECK_STR(got[1].name, "Two, {braces}");
    }
  }
  // Pretty-printed, with whitespace around the colon and between objects.
  const std::string pretty = "{\n  \"errors\": [],\n  \"data\" : [\n    { \"id\": \"p\" } ,\n    { \"id\": \"q\" }\n  ]\n}";
  const std::vector<SinkObj> got = sinkRun(pretty, 0);
  CHECK(got.size() == 2);
  if (got.size() == 2) {
    CHECK_STR(got[0].id, "p");
    CHECK_STR(got[1].id, "q");
  }
  // No data array, or an empty one: nothing.
  CHECK(sinkRun("{\"errors\":[{\"description\":\"unauthorized user\"}]}", 0).empty());
  CHECK(sinkRun("{\"errors\":[],\"data\":[]}", 0).empty());
}

static void testSinkSceneWithHugeActions() {
  // A scene whose actions alone are larger than the object buffer.
  std::string actions = "[";
  int n = 0;
  while (actions.size() < JsonDataSink::kMaxObj + 4096) {
    if (n) {
      actions += ',';
    }
    actions += sceneAction(n++);
  }
  actions += ']';
  CHECK(actions.size() > JsonDataSink::kMaxObj);

  const std::string body = "{\"errors\":[],\"data\":[{\"id\":\"scene-big\",\"type\":\"scene\",\"actions\":" + actions +
                           ",\"metadata\":{\"name\":\"Big scene\"},\"group\":{\"rid\":\"room-1\",\"rtype\":\"room\"}},"
                           "{\"id\":\"scene-small\",\"actions\":[" + sceneAction(1) +
                           "],\"metadata\":{\"name\":\"Small\"}}]}";
  for (size_t step : {0, 1, 512}) {
    JsonDataSink sink;
    const std::vector<SinkObj> got = sinkRun(body, step, &sink);
    CHECK(got.size() == 2);
    CHECK(sink.objects == 2);
    CHECK(!sink.overflow);
    if (got.size() == 2) {
      CHECK_STR(got[0].id, "scene-big");
      CHECK_STR(got[0].name, "Big scene");
      // actions is replaced by an empty array; the fields after it are kept.
      CHECK(got[0].raw.find("\"actions\":[]") != std::string::npos);
      CHECK(got[0].raw.size() < 200);
      char rid[40];
      CHECK(jsonGetObjectString(got[0].raw.c_str(), "group", "rid", rid, sizeof(rid)));
      CHECK_STR(rid, "room-1");
      CHECK_STR(got[1].id, "scene-small");
      CHECK_STR(got[1].name, "Small");
    }
  }
}

static void testSinkActionsPrimitive() {
  // actions as a scalar, at the end of the object and before another key.
  const std::string body =
      "{\"data\":[{\"id\":\"n\",\"actions\":null},{\"id\":\"s\",\"actions\": \"x,}\\\"]\",\"metadata\":{\"name\":\"S\"}},"
      "{\"id\":\"k\",\"actions\":7}]}";
  for (size_t step : {0, 1}) {
    const std::vector<SinkObj> got = sinkRun(body, step);
    CHECK(got.size() == 3);
    if (got.size() == 3) {
      CHECK_STR(got[0].id, "n");
      CHECK_STR(got[1].id, "s");
      CHECK_STR(got[1].name, "S");
      CHECK_STR(got[2].id, "k");
    }
  }
}

static void testSinkOversizedObjectDropped() {
  // An object over kMaxObj with no actions to skip is dropped; the next one still arrives.
  const std::string filler(JsonDataSink::kMaxObj + 100, 'x');
  const std::string body = "{\"data\":[{\"id\":\"huge\",\"blob\":\"" + filler + "\"},{\"id\":\"after\"}]}";
  JsonDataSink sink;
  const std::vector<SinkObj> got = sinkRun(body, 0, &sink);
  CHECK(got.size() == 1);
  CHECK(sink.objects == 1);
  if (got.size() == 1) {
    CHECK_STR(got[0].id, "after");
  }
  // The flag outlives the dropped object, so the caller can log it after the stream.
  CHECK(sink.overflow);
  CHECK(sink.dropped == 1);
}

static void testSinkOverflowCountsEachDrop() {
  const std::string filler(JsonDataSink::kMaxObj + 1, 'x');
  const std::string huge = "{\"id\":\"huge\",\"blob\":\"" + filler + "\"}";
  const std::string body = "{\"data\":[" + huge + ",{\"id\":\"a\"}," + huge + "," + huge + ",{\"id\":\"b\"}]}";
  for (size_t step : {size_t(1), size_t(4096)}) {
    JsonDataSink sink;
    const std::vector<SinkObj> got = sinkRun(body, step, &sink);
    CHECK(got.size() == 2);
    CHECK(sink.objects == 2);
    CHECK(sink.overflow);
    CHECK(sink.dropped == 3);
  }
  // Nothing dropped: no flag.
  JsonDataSink sink;
  sinkRun("{\"data\":[{\"id\":\"a\"},{\"id\":\"b\"}]}", 0, &sink);
  CHECK(sink.objects == 2);
  CHECK(!sink.overflow);
  CHECK(sink.dropped == 0);
}

// ---------------------------------------------------------------------------------------------
// recipes.h: GET /api/device/config body

static const char *kConfigBody =
    "{\"rev\":12,\"ota\":null,"
    "\"channels\":[{\"id\":\"boot\",\"kind\":\"momentary\"},{\"id\":\"d0\",\"kind\":\"maintained\"},"
    "{\"id\":\"d1\",\"kind\":\"sideways\"},{\"kind\":\"momentary\"}],"
    "\"recipes\":["
    // 0: light on
    "{\"channelId\":\"d0\",\"event\":\"on\",\"action\":\"on\",\"target\":{\"rtype\":\"light\",\"rid\":\"L1\"}},"
    // 1: grouped_light off, with a name the firmware ignores
    "{\"channelId\":\"d0\",\"event\":\"off\",\"action\":\"off\",\"target\":{\"rtype\":\"grouped_light\",\"rid\":\"G1\",\"name\":\"Kitchen\"}},"
    // 2: scene list, one non-scene target skipped
    "{\"channelId\":\"boot\",\"event\":\"short\",\"action\":\"recall_scene\",\"targets\":["
    "{\"rtype\":\"scene\",\"rid\":\"S1\",\"name\":\"Read\"},{\"rtype\":\"light\",\"rid\":\"L9\"},{\"rtype\":\"scene\",\"rid\":\"S2\"}]},"
    // 3: the old single scene target
    "{\"channelId\":\"d1\",\"event\":\"double_click\",\"action\":\"recall_scene\",\"target\":{\"rtype\":\"scene\",\"rid\":\"S3\"}},"
    // 4: dim on hold
    "{\"channelId\":\"boot\",\"event\":\"hold\",\"action\":\"dim\",\"target\":{\"rtype\":\"grouped_light\",\"rid\":\"G2\"}},"
    // rejected: dim on short, unknown event, unknown action, scene rtype for on, no target, no channel
    "{\"channelId\":\"d2\",\"event\":\"short\",\"action\":\"dim\",\"target\":{\"rtype\":\"light\",\"rid\":\"L2\"}},"
    "{\"channelId\":\"d2\",\"event\":\"triple\",\"action\":\"on\",\"target\":{\"rtype\":\"light\",\"rid\":\"L2\"}},"
    "{\"channelId\":\"d2\",\"event\":\"on\",\"action\":\"blink\",\"target\":{\"rtype\":\"light\",\"rid\":\"L2\"}},"
    "{\"channelId\":\"d2\",\"event\":\"on\",\"action\":\"on\",\"target\":{\"rtype\":\"scene\",\"rid\":\"S4\"}},"
    "{\"channelId\":\"d2\",\"event\":\"on\",\"action\":\"toggle\"},"
    "{\"event\":\"on\",\"action\":\"on\",\"target\":{\"rtype\":\"light\",\"rid\":\"L2\"}},"
    // 5: toggle, spaced out
    "{ \"channelId\": \"d2\", \"event\": \"on\", \"action\": \"toggle\", \"target\": { \"rtype\": \"light\", \"rid\": \"L3\" } }"
    "]}";

static void testParseConfig() {
  uint32_t rev = 0;
  CHECK(recipesParseConfig(kConfigBody, &rev));
  CHECK(rev == 12);
  const RecipeConfig &c = gRecipeStage;
  CHECK(c.fromConsole);
  CHECK(c.channelCount == 2);
  if (c.channelCount == 2) {
    CHECK_STR(c.channels[0].id, "boot");
    CHECK(c.channels[0].kind == CHK_MOMENTARY);
    CHECK_STR(c.channels[1].id, "d0");
    CHECK(c.channels[1].kind == CHK_MAINTAINED);
  }
  CHECK(c.recipeCount == 6);
  if (c.recipeCount != 6) {
    return;
  }
  CHECK_STR(c.recipes[0].channelId, "d0");
  CHECK_STR(c.recipes[0].event, "on");
  CHECK_STR(c.recipes[0].action, "on");
  CHECK_STR(c.recipes[0].rtype, "light");
  CHECK_STR(c.recipes[0].rid, "L1");
  CHECK(c.recipes[0].sceneCount == 0);

  CHECK_STR(c.recipes[1].rtype, "grouped_light");
  CHECK_STR(c.recipes[1].rid, "G1");

  CHECK_STR(c.recipes[2].action, "recall_scene");
  CHECK(c.recipes[2].sceneCount == 2);
  CHECK_STR(c.recipes[2].scenes[0], "S1");
  CHECK_STR(c.recipes[2].scenes[1], "S2");

  CHECK_STR(c.recipes[3].event, "double_click");
  CHECK(c.recipes[3].sceneCount == 1);
  CHECK_STR(c.recipes[3].scenes[0], "S3");

  CHECK_STR(c.recipes[4].event, "hold");
  CHECK_STR(c.recipes[4].action, "dim");
  CHECK_STR(c.recipes[4].rid, "G2");

  CHECK_STR(c.recipes[5].channelId, "d2");
  CHECK_STR(c.recipes[5].action, "toggle");
  CHECK_STR(c.recipes[5].rid, "L3");
}

static void testParseConfigRejects() {
  uint32_t rev = 99;
  CHECK(!recipesParseConfig("{\"recipes\":[]}", &rev));
  CHECK(!recipesParseConfig("{\"rev\":3}", &rev));
  CHECK(!recipesParseConfig("{\"rev\":-1,\"recipes\":[]}", &rev));
  CHECK(!recipesParseConfig("{\"rev\":\"3\",\"recipes\":[]}", &rev));
  CHECK(!recipesParseConfig(nullptr, &rev));
  CHECK(rev == 99);
  // An empty config is valid: it clears the recipes.
  CHECK(recipesParseConfig("{\"rev\":0,\"recipes\":[],\"channels\":[]}", &rev));
  CHECK(rev == 0);
  CHECK(gRecipeStage.recipeCount == 0);
  CHECK(gRecipeStage.fromConsole);
}

static void testParseOldPayload() {
  // No channels[]: the old payload, compiled defaults for every pin.
  uint32_t rev = 0;
  CHECK(recipesParseConfig(
      "{\"rev\":4,\"recipes\":[{\"channelId\":\"d0\",\"event\":\"on\",\"action\":\"on\","
      "\"target\":{\"rtype\":\"light\",\"rid\":\"L1\"}}]}",
      &rev));
  CHECK(rev == 4);
  CHECK(!gRecipeStage.fromConsole);
  CHECK(gRecipeStage.channelCount == 0);
  CHECK(gRecipeStage.recipeCount == 1);
}

static void testParseLimits() {
  // More than kMaxSceneTargets scenes: the first 8 are kept.
  std::string body = "{\"rev\":1,\"recipes\":[{\"channelId\":\"d0\",\"event\":\"on\",\"action\":\"recall_scene\",\"targets\":[";
  for (int i = 0; i < 11; i++) {
    body += (i ? "," : "");
    body += "{\"rtype\":\"scene\",\"rid\":\"S" + std::to_string(i) + "\"}";
  }
  body += "]}";
  // Then more recipes than kMaxRecipes: the rest are dropped.
  for (int i = 0; i < kMaxRecipes + 5; i++) {
    body += ",{\"channelId\":\"d1\",\"event\":\"on\",\"action\":\"on\",\"target\":{\"rtype\":\"light\",\"rid\":\"R" +
            std::to_string(i) + "\"}}";
  }
  body += "]}";
  uint32_t rev = 0;
  CHECK(recipesParseConfig(body.c_str(), &rev));
  CHECK(gRecipeStage.recipeCount == kMaxRecipes);
  CHECK(gRecipeStage.recipes[0].sceneCount == kMaxSceneTargets);
  CHECK_STR(gRecipeStage.recipes[0].scenes[7], "S7");
  CHECK_STR(gRecipeStage.recipes[kMaxRecipes - 1].rid, "R19");

  // Channel settings are capped at kMaxChannelSettings.
  std::string ch = "{\"channels\":[";
  for (int i = 0; i < kMaxChannelSettings + 3; i++) {
    ch += (i ? "," : "");
    ch += "{\"id\":\"c" + std::to_string(i) + "\",\"kind\":\"momentary\"}";
  }
  ch += "],\"recipes\":[]}";
  RecipeConfig cfg;
  recipesParseInto(ch.c_str(), &cfg);
  CHECK(cfg.channelCount == kMaxChannelSettings);

  // A rid longer than the field is cut, not overrun.
  const std::string longRid(80, 'z');
  recipesParseInto(("{\"recipes\":[{\"channelId\":\"d0\",\"event\":\"on\",\"action\":\"on\",\"target\":{\"rtype\":\"light\",\"rid\":\"" +
                    longRid + "\"}}]}")
                       .c_str(),
                   &cfg);
  CHECK(cfg.recipeCount == 1);
  CHECK(strlen(cfg.recipes[0].rid) == sizeof(cfg.recipes[0].rid) - 1);
}

// The NVS blob per channel (recipesChannelJson) parses back to the same recipes and settings.
static void testChannelBlobRoundTrip() {
  uint32_t rev = 0;
  CHECK(recipesParseConfig(kConfigBody, &rev));
  const RecipeConfig original = gRecipeStage;
  recipesApply(original);

  RecipeConfig back;
  memset(&back, 0, sizeof(back));
  back.fromConsole = true;
  const char *ids[] = {"boot", "d0", "d1", "d2"};
  for (const char *id : ids) {
    const String blob = recipesChannelJson(id);
    uint8_t before = back.recipeCount;
    jsonEachArrayObject(blob.c_str(), "channels", channelSettingParseOne, &back);
    jsonEachArrayObject(blob.c_str(), "recipes", recipesParseOne, &back);
    int n = jsonGetInt(blob.c_str(), "n", -1);
    CHECK(n == back.recipeCount - before);
  }
  CHECK(back.recipeCount == original.recipeCount);
  CHECK(back.channelCount == original.channelCount);
  for (uint8_t i = 0; i < back.recipeCount; i++) {
    // Order is per channel in the blobs; find each original recipe.
    const HueRecipe &o = original.recipes[i];
    bool found = false;
    for (uint8_t k = 0; k < back.recipeCount && !found; k++) {
      const HueRecipe &b = back.recipes[k];
      found = strcmp(o.channelId, b.channelId) == 0 && strcmp(o.event, b.event) == 0 &&
              strcmp(o.action, b.action) == 0 && strcmp(o.rtype, b.rtype) == 0 && strcmp(o.rid, b.rid) == 0 &&
              o.sceneCount == b.sceneCount && memcmp(o.scenes, b.scenes, sizeof(o.scenes)) == 0;
    }
    CHECK(found);
  }
  const HueRecipe *r = recipesFind("boot", "short");
  CHECK(r && r->sceneCount == 2);
  CHECK(recipesFind("boot", "on") == nullptr);
  CHECK(recipesChannelKind("d0") == CHK_MAINTAINED);
  CHECK(recipesChannelKind("d5") == CHK_NONE);
}

int main() {
  testGetString();
  testGetInt();
  testObjectStringAndHasKey();
  testHueOnAndBrightness();
  testFindRidByRtype();
  testEachArrayObject();
  testAppendEscapedRoundTrip();
  testSinkBasic();
  testSinkSceneWithHugeActions();
  testSinkActionsPrimitive();
  testSinkOversizedObjectDropped();
  testSinkOverflowCountsEachDrop();
  testParseConfig();
  testParseConfigRejects();
  testParseOldPayload();
  testParseLimits();
  testChannelBlobRoundTrip();
  if (gFailures) {
    fprintf(stderr, "%d of %d checks failed\n", gFailures, gChecks);
    return 1;
  }
  printf("host tests: %d checks passed\n", gChecks);
  return 0;
}
