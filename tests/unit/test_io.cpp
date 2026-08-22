// VNV-016, VNV-017, DATA-001..DATA-009, SYS-007, SYS-010 - configuration IO.
//
// VNV-016 asks for the configuration parsers to be fuzzed or property tested on
// malformed input. MalformedInputNeverCrashes below throws several thousand
// mutations of a valid document at both parsers and only requires that they
// return an error rather than crash or hang.
#include <gtest/gtest.h>

#include <cmath>
#include <string>
#include <vector>

#include "aerolab/core/rng.hpp"
#include "aerolab/io/json.hpp"
#include "aerolab/io/scenario.hpp"
#include "aerolab/io/yaml.hpp"

using namespace aerolab;

namespace {

std::string sourceDir() {
#ifdef AEROLAB_SOURCE_DIR
  return std::string(AEROLAB_SOURCE_DIR);
#else
  return std::string(".");
#endif
}

}  // namespace

TEST(Json, RoundTripsEveryScalarType) {
  Json root = Json::object();
  root["flag"] = Json(true);
  root["count"] = Json(42);
  root["ratio"] = Json(0.1 + 0.2);  // deliberately not representable
  root["text"] = Json(std::string("hello \"world\"\n"));
  root["nothing"] = Json();
  Json arr = Json::array();
  arr.push(Json(1.0));
  arr.push(Json("two"));
  root["list"] = arr;

  Json parsed;
  std::string error;
  ASSERT_TRUE(Json::parse(root.dump(), parsed, error)) << error;
  EXPECT_TRUE(parsed["flag"].asBool());
  EXPECT_DOUBLE_EQ(parsed["count"].asDouble(), 42.0);
  // DATA-009: %.17g must reproduce the double exactly.
  EXPECT_DOUBLE_EQ(parsed["ratio"].asDouble(), 0.1 + 0.2);
  EXPECT_EQ(parsed["text"].asString(), "hello \"world\"\n");
  EXPECT_TRUE(parsed["nothing"].isNull());
  ASSERT_EQ(parsed["list"].size(), 2u);
  EXPECT_EQ(parsed["list"].at(1).asString(), "two");
}

TEST(Json, OutputIsStableAcrossInsertionOrder) {
  Json a = Json::object();
  a["z"] = Json(1);
  a["a"] = Json(2);
  a["m"] = Json(3);
  Json b = Json::object();
  b["a"] = Json(2);
  b["m"] = Json(3);
  b["z"] = Json(1);
  // DATA-005 depends on this: the config hash must not change because a field
  // was written in a different order.
  EXPECT_EQ(a.dump(), b.dump());
}

TEST(Json, NonFiniteBecomesNull) {
  EXPECT_EQ(jsonNumber(std::nan("")), "null");
  EXPECT_EQ(jsonNumber(1.0 / 0.0), "null");
  Json j = Json::object();
  j["x"] = Json(std::nan(""));
  Json parsed;
  std::string error;
  EXPECT_TRUE(Json::parse(j.dump(), parsed, error)) << error;
  EXPECT_TRUE(parsed["x"].isNull());
}

TEST(Json, MissingKeysReturnFalseInsteadOfThrowing) {
  Json j = Json::object();
  j["present"] = Json(1.0);
  double d = -1.0;
  int i = -1;
  bool b = true;
  std::string s = "untouched";
  EXPECT_FALSE(j.getDouble("absent", d));
  EXPECT_FALSE(j.getInt("absent", i));
  EXPECT_FALSE(j.getBool("absent", b));
  EXPECT_FALSE(j.getString("absent", s));
  EXPECT_DOUBLE_EQ(d, -1.0);
  EXPECT_EQ(s, "untouched");
  EXPECT_TRUE(j["absent"].isNull());
  EXPECT_TRUE(j["absent"]["deeper"].isNull());  // chaining must stay safe
}

TEST(Json, RejectsMalformedDocuments) {
  const char* bad[] = {
      "{", "}",         "[1,", "{\"a\" 1}", "{\"a\":}", "tru", "\"unterminated", "{\"a\":1}extra",
      "",  "{\"a\":1,}"};
  Json out;
  std::string error;
  for (const char* text : bad) {
    EXPECT_FALSE(Json::parse(text, out, error)) << "accepted: " << text;
    EXPECT_FALSE(error.empty());
  }
}

TEST(Yaml, ParsesTheScenarioSubset) {
  const std::string text =
      "# a comment\n"
      "id: SCN-001\n"
      "seed: 424242\n"
      "enabled: true\n"
      "ratio: 0.25\n"
      "nested:\n"
      "  inner: hello\n"
      "  deeper:\n"
      "    value: -3.5\n"
      "flow_list: [1, 2, 3]\n"
      "flow_map: {a: 1, b: two}\n"
      "faults:\n"
      "  - type: gnss_position_step\n"
      "    start_s: 30.0\n"
      "    amplitude_ned_m: [0.0, 100.0, 0.0]\n"
      "  - type: latency\n"
      "    latency_s: 1.5\n"
      "quoted: \"a: colon inside\"\n";
  Json root;
  std::string error;
  ASSERT_TRUE(parseYaml(text, root, error)) << error;
  EXPECT_EQ(root["id"].asString(), "SCN-001");
  EXPECT_DOUBLE_EQ(root["seed"].asDouble(), 424242.0);
  EXPECT_TRUE(root["enabled"].asBool());
  EXPECT_DOUBLE_EQ(root["ratio"].asDouble(), 0.25);
  EXPECT_EQ(root["nested"]["inner"].asString(), "hello");
  EXPECT_DOUBLE_EQ(root["nested"]["deeper"]["value"].asDouble(), -3.5);
  ASSERT_EQ(root["flow_list"].size(), 3u);
  EXPECT_DOUBLE_EQ(root["flow_list"].at(2).asDouble(), 3.0);
  EXPECT_EQ(root["flow_map"]["b"].asString(), "two");
  ASSERT_EQ(root["faults"].size(), 2u);
  EXPECT_EQ(root["faults"].at(0)["type"].asString(), "gnss_position_step");
  EXPECT_DOUBLE_EQ(root["faults"].at(0)["amplitude_ned_m"].at(1).asDouble(), 100.0);
  EXPECT_DOUBLE_EQ(root["faults"].at(1)["latency_s"].asDouble(), 1.5);
  EXPECT_EQ(root["quoted"].asString(), "a: colon inside");
}

// Block scalars are supported because the acceptance blocks of the scenarios
// carry several sentences of reasoning each; folding them onto one line would
// make the auditable record of what a scenario asserts unreadable.
TEST(Yaml, SupportsBlockScalars) {
  const std::string text =
      "literal: |\n"
      "  multi\n"
      "  line\n"
      "folded: >\n"
      "  one\n"
      "  two\n"
      "after: 1\n";
  Json out;
  std::string error;
  ASSERT_TRUE(parseYaml(text, out, error)) << error;
  EXPECT_EQ(out["literal"].asString(), "multi\nline");
  EXPECT_EQ(out["folded"].asString(), "one two");
  EXPECT_DOUBLE_EQ(out["after"].asDouble(), 1.0)
      << "structure after a block scalar must still parse";
}

TEST(Yaml, RejectsUnsupportedConstructsExplicitly) {
  Json out;
  std::string error;
  EXPECT_FALSE(parseYaml("key: &anchor value\nother: *anchor\n", out, error));
  EXPECT_FALSE(error.empty());
  EXPECT_FALSE(parseYaml("tagged: !!custom x\n", out, error));

  error.clear();
  EXPECT_FALSE(parseYaml("bad\n", out, error));
  EXPECT_NE(error.find("line"), std::string::npos) << "errors must carry a line number";
}

// VNV-016: mutation fuzzing of both parsers. The requirement is not that every
// mutation is rejected, only that none crashes, hangs or reads out of bounds.
TEST(Parsers, MalformedInputNeverCrashes) {
  const std::string valid_yaml =
      "id: SCN-001\nseed: 42\nnested:\n  a: 1\n  b: [1, 2]\nlist:\n  - x: 1\n    y: 2\n";
  const std::string valid_json = "{\"id\":\"SCN-001\",\"seed\":42,\"n\":{\"a\":1,\"b\":[1,2]}}";
  Pcg32 rng(20260821, 5);
  const char alphabet[] = "{}[]:,\"'#- \n\t0123456789abcXYZ&*|\\";
  const std::size_t alphabet_size = sizeof(alphabet) - 1;

  for (int iteration = 0; iteration < 3000; ++iteration) {
    const bool use_yaml = (iteration % 2) == 0;
    std::string text = use_yaml ? valid_yaml : valid_json;
    const int mutations = 1 + static_cast<int>(rng.nextUniform01() * 6.0);
    for (int k = 0; k < mutations && !text.empty(); ++k) {
      const std::size_t pos =
          static_cast<std::size_t>(rng.nextUniform01() * static_cast<double>(text.size()));
      const double action = rng.nextUniform01();
      const char c = alphabet[static_cast<std::size_t>(rng.nextUniform01() *
                                                       static_cast<double>(alphabet_size))];
      if (action < 0.4) {
        text[std::min(pos, text.size() - 1)] = c;
      } else if (action < 0.7) {
        text.insert(std::min(pos, text.size()), 1, c);
      } else {
        text.erase(std::min(pos, text.size() - 1), 1);
      }
    }
    Json out;
    std::string error;
    const bool ok = use_yaml ? parseYaml(text, out, error) : Json::parse(text, out, error);
    if (!ok) {
      EXPECT_FALSE(error.empty());
    }
    // Whatever came back must be safe to walk.
    (void)out["anything"].asDouble();
    (void)out.dump(0).size();
  }
}

// DATA-002 / DATA-008 / SYS-007.
TEST(Scenario, RejectsAnUnknownSchemaVersion) {
  Json root;
  std::string error;
  ASSERT_TRUE(parseYaml("schema_version: 99\nid: SCN-X\n", root, error));
  Scenario s;
  EXPECT_FALSE(loadScenarioFromJson(root, s, error));
  EXPECT_NE(error.find("schema version"), std::string::npos);
}

TEST(Scenario, RejectsMissingOrInvalidFields) {
  Json root;
  Scenario s;
  std::string error;

  ASSERT_TRUE(parseYaml("id: SCN-X\n", root, error));
  EXPECT_FALSE(loadScenarioFromJson(root, s, error));  // no schema_version

  ASSERT_TRUE(parseYaml("schema_version: 1\n", root, error));
  EXPECT_FALSE(loadScenarioFromJson(root, s, error));  // no id

  ASSERT_TRUE(parseYaml("schema_version: 1\nid: X\nduration_s: -5\n", root, error));
  EXPECT_FALSE(loadScenarioFromJson(root, s, error));

  ASSERT_TRUE(parseYaml("schema_version: 1\nid: X\ndt_s: 0.5\n", root, error));
  EXPECT_FALSE(loadScenarioFromJson(root, s, error));

  ASSERT_TRUE(parseYaml("schema_version: 1\nid: X\ntruth_profile: nope\n", root, error));
  EXPECT_FALSE(loadScenarioFromJson(root, s, error));

  ASSERT_TRUE(parseYaml("schema_version: 1\nid: X\nestimators: [not_an_estimator]\n", root, error));
  EXPECT_FALSE(loadScenarioFromJson(root, s, error));

  ASSERT_TRUE(parseYaml("schema_version: 1\nid: X\nfaults:\n  - type: nope\n", root, error));
  EXPECT_FALSE(loadScenarioFromJson(root, s, error));
}

// Every shipped scenario must load, and its acceptance block must be readable.
TEST(Scenario, EveryShippedScenarioLoads) {
  const char* ids[] = {"SCN-001", "SCN-002", "SCN-003", "SCN-004", "SCN-005", "SCN-006", "SCN-007",
                       "SCN-008", "SCN-009", "SCN-010", "SCN-011", "SCN-012", "SCN-013", "SCN-014"};
  for (const char* id : ids) {
    Scenario s;
    std::string error;
    const std::string path = sourceDir() + "/scenarios/" + id + ".yaml";
    ASSERT_TRUE(loadScenario(path, s, error)) << path << ": " << error;
    EXPECT_EQ(s.id, id);
    EXPECT_GT(s.duration_s, 0.0);
    EXPECT_FALSE(s.channels.empty());
    EXPECT_FALSE(s.acceptance.empty()) << id << " has no machine readable acceptance block";
    EXPECT_EQ(s.scenario_hash.size(), 64u);
    EXPECT_FALSE(s.name.empty());
    EXPECT_FALSE(s.description.empty());
  }
}

TEST(Scenario, ConfigOverlayIsApplied) {
  Scenario s;
  std::string error;
  ASSERT_TRUE(loadScenario(sourceDir() + "/scenarios/SCN-003.yaml", s, error)) << error;
  const double before = s.estimator_config.baro_bias_uncertainty_m;
  ASSERT_TRUE(applyConfigFile(sourceDir() + "/configs/evaluation.json", s, error)) << error;
  EXPECT_EQ(s.config_hash.size(), 64u);
  EXPECT_GT(s.estimator_config.rollback_window_s, 0.0);
  EXPECT_GT(before, 0.0);
}

TEST(Scenario, FaultWindowIsDerivedFromTheFaultList) {
  Scenario s;
  std::string error;
  ASSERT_TRUE(loadScenario(sourceDir() + "/scenarios/SCN-004.yaml", s, error)) << error;
  EXPECT_NEAR(s.faultWindowStart_s(), 25.0, 1e-9);
  EXPECT_NEAR(s.faultWindowEnd_s(), 70.0, 1e-9);

  Scenario nominal;
  ASSERT_TRUE(loadScenario(sourceDir() + "/scenarios/SCN-001.yaml", nominal, error)) << error;
  EXPECT_LT(nominal.faultWindowStart_s(), 0.0) << "a nominal run has no fault window";
}

TEST(Scenario, DefaultChannelsCoverTheComparisonMatrix) {
  const std::vector<NavigationChannelSpec> channels = defaultChannels();
  ASSERT_EQ(channels.size(), 5u);
  EXPECT_FALSE(channels[0].integrity.enabled);       // gnss_only
  EXPECT_FALSE(channels[1].integrity.enabled);       // ins_dr
  EXPECT_FALSE(channels[2].integrity.enabled);       // ekf: fusion only, the control case
  EXPECT_FALSE(channels[3].integrity.monitor_only);  // integrity_ekf: gating
  EXPECT_FALSE(channels[3].integrity.enable_solution_separation);
  EXPECT_TRUE(channels[4].integrity.enable_solution_separation);  // solsep_ekf
}

TEST(Scenario, SerialisesBackToJson) {
  Scenario s;
  std::string error;
  ASSERT_TRUE(loadScenario(sourceDir() + "/scenarios/SCN-012.yaml", s, error)) << error;
  const Json j = scenarioToJson(s);
  EXPECT_EQ(j["id"].asString(), "SCN-012");
  EXPECT_EQ(j["faults"].size(), 2u);
  EXPECT_EQ(j["estimators"].size(), 5u);
  Json reparsed;
  ASSERT_TRUE(Json::parse(j.dump(), reparsed, error)) << error;
  EXPECT_EQ(reparsed["scenario_hash"].asString(), s.scenario_hash);
}
