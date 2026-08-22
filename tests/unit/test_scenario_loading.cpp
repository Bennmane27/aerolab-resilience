// AEROLAB RESILIENCE - the scenario loader, and what it refuses.
//
// A scenario file is the input that decides what a published number means. A
// loader that quietly accepts a misspelled fault target, or silently ignores an
// acceptance criterion it does not understand, produces a run that looks valid
// and measures something else. SYS-010 requires a diagnostic; DEV-007 makes the
// acceptance block machine readable precisely so that the verdict is computed
// rather than asserted.
//
// So the property under test is REFUSAL. Every field that names something -- a
// fault type, a sensor, a mission phase, a measurement type, a navigation mode
// -- must reject a name it does not know, and say which name it was.
//
// The existing suite ran the fourteen shipped scenarios, all of which are
// correct by construction. None of these paths had ever been executed.
#include <gtest/gtest.h>

#include <string>

#include "aerolab/io/scenario.hpp"

using namespace aerolab;

namespace {

/// A minimal but complete scenario, in the YAML subset, with `extra` spliced in.
std::string yamlScenario(const std::string& extra) {
  return
      "schema_version: 1\n"
      "id: SCN-TEST\n"
      "name: loader test\n"
      "seed: 7\n"
      "duration_s: 5.0\n"
      "dt_s: 0.01\n" +
      extra;
}

Scenario loadOrFail(const std::string& text) {
  Scenario s;
  std::string error;
  EXPECT_TRUE(loadScenarioFromText(text, "test.yaml", s, error)) << error;
  return s;
}

/// Asserts the load is refused AND that the message names the offending value.
void expectRefused(const std::string& text, const std::string& mustMention) {
  Scenario s;
  std::string error;
  EXPECT_FALSE(loadScenarioFromText(text, "test.yaml", s, error))
      << "accepted a scenario it should have refused";
  EXPECT_NE(error.find(mustMention), std::string::npos)
      << "the diagnostic does not mention '" << mustMention << "': " << error;
  EXPECT_NE(error.find("test.yaml"), std::string::npos)
      << "the diagnostic does not say which document failed: " << error;
}

}  // namespace

TEST(ScenarioLoading, ReadsTheSameContentAsYamlOrAsJson) {
  // A document starting with '{' is JSON, anything else is the YAML subset.
  // Both must produce the identical scenario, because the campaign and the Web
  // Lab do not read the same form of the same file.
  const Scenario fromYaml = loadOrFail(yamlScenario("description: identical\n"));
  Scenario fromJson;
  std::string error;
  ASSERT_TRUE(loadScenarioFromText(
      R"({"schema_version":1,"id":"SCN-TEST","name":"loader test","seed":7,
          "duration_s":5.0,"dt_s":0.01,"description":"identical"})",
      "test.json", fromJson, error))
      << error;

  EXPECT_EQ(fromYaml.id, fromJson.id);
  EXPECT_EQ(fromYaml.name, fromJson.name);
  EXPECT_EQ(fromYaml.description, fromJson.description);
  EXPECT_EQ(fromYaml.seed, fromJson.seed);
  EXPECT_DOUBLE_EQ(fromYaml.duration_s, fromJson.duration_s);

  // The content hash follows the text, not the parsed value: two spellings of
  // the same scenario are two different provenances.
  EXPECT_FALSE(fromYaml.scenario_hash.empty());
  EXPECT_NE(fromYaml.scenario_hash, fromJson.scenario_hash);
  EXPECT_EQ(fromYaml.source_path, "test.yaml");
}

TEST(ScenarioLoading, RefusesAFaultItCannotName) {
  expectRefused(yamlScenario("faults:\n  - target: gnss\n    start_s: 1.0\n"), "type");
  expectRefused(yamlScenario("faults:\n  - type: gnss_teleport\n"), "gnss_teleport");
  expectRefused(yamlScenario("faults:\n  - type: freeze\n    target: magnetometer\n"),
                "magnetometer");
}

TEST(ScenarioLoading, ReadsTheSingleAxisConvenienceFormAndRefusesAnUnknownAxis) {
  for (const auto& [axis, n, e, d] : {
           std::tuple<const char*, double, double, double>{"north", 100.0, 0.0, 0.0},
           {"east", 0.0, 100.0, 0.0},
           {"down", 0.0, 0.0, 100.0},
       }) {
    const Scenario s = loadOrFail(yamlScenario(
        std::string("faults:\n  - type: gnss_position_step\n    target: gnss\n    axis: ") + axis +
        "\n    final_offset_m: 100.0\n"));
    ASSERT_EQ(s.faults.size(), 1u) << axis;
    EXPECT_DOUBLE_EQ(s.faults[0].amplitude.x, n) << axis;
    EXPECT_DOUBLE_EQ(s.faults[0].amplitude.y, e) << axis;
    EXPECT_DOUBLE_EQ(s.faults[0].amplitude.z, d) << axis;
  }

  // offset_m is accepted as a synonym of final_offset_m.
  const Scenario s = loadOrFail(yamlScenario(
      "faults:\n  - type: gnss_position_step\n    target: gnss\n    axis: east\n"
      "    offset_m: -42.0\n"));
  EXPECT_DOUBLE_EQ(s.faults[0].amplitude.y, -42.0);

  expectRefused(yamlScenario("faults:\n  - type: gnss_position_step\n    target: gnss\n"
                             "    axis: sideways\n    final_offset_m: 1.0\n"),
                "sideways");
}

TEST(ScenarioLoading, ReadsTheTriggerPhaseAndRefusesAnUnknownOne) {
  const Scenario s = loadOrFail(yamlScenario(
      "faults:\n  - type: freeze\n    target: gnss\n    trigger_phase: FINAL_APPROACH\n"));
  ASSERT_EQ(s.faults.size(), 1u);
  EXPECT_TRUE(s.faults[0].use_phase_trigger);
  EXPECT_EQ(s.faults[0].trigger_phase, MissionPhase::kFinalApproach);

  expectRefused(
      yamlScenario("faults:\n  - type: freeze\n    target: gnss\n    trigger_phase: GO_AROUND\n"),
      "GO_AROUND");
}

TEST(ScenarioLoading, ReadsTheMeasurementTypeFilterAndRefusesAnUnknownOne) {
  const std::pair<const char*, MeasurementType> cases[] = {
      {"gnss_position", MeasurementType::kGnssPosition},
      {"gnss_velocity", MeasurementType::kGnssVelocity},
      {"baro_altitude", MeasurementType::kBaroAltitude},
      {"vision_relative", MeasurementType::kVisionRelative},
      {"imu_sample", MeasurementType::kImuSample},
  };
  for (const auto& [name, expected] : cases) {
    const Scenario s = loadOrFail(yamlScenario(
        std::string("faults:\n  - type: noise_burst\n    target: gnss\n    measurement_type: ") +
        name + "\n"));
    ASSERT_EQ(s.faults.size(), 1u) << name;
    EXPECT_TRUE(s.faults[0].use_type_filter) << name;
    EXPECT_EQ(s.faults[0].type_filter, expected) << name;
  }

  expectRefused(yamlScenario("faults:\n  - type: noise_burst\n    target: gnss\n"
                             "    measurement_type: barometric\n"),
                "barometric");
}

TEST(ScenarioLoading, ReadsEveryAcceptanceCriterionField) {
  // DEV-007: the verdict in the manifest is the conjunction of these. A field
  // that is silently dropped turns a criterion into a comment.
  const Scenario s = loadOrFail(yamlScenario(
      "acceptance:\n"
      "  - id: AC-9\n"
      "    description: everything at once\n"
      "    estimator: integrity_ekf\n"
      "    max_position_rmse_m: 12.5\n"
      "    max_error_m: 40.0\n"
      "    max_horizontal_p95_m: 25.0\n"
      "    max_time_to_detect_s: 3.5\n"
      "    max_time_to_isolate_s: 6.0\n"
      "    max_false_isolations: 2\n"
      "    min_availability: 0.97\n"
      "    require_detection: true\n"
      "    require_isolation: true\n"
      "    forbid_end_mode: UNSAFE\n"
      "    require_end_mode: DEGRADED\n"));

  ASSERT_EQ(s.acceptance.size(), 1u);
  const AcceptanceCriterion& a = s.acceptance[0];
  EXPECT_EQ(a.id, "AC-9");
  EXPECT_EQ(a.description, "everything at once");
  EXPECT_EQ(a.estimator, "integrity_ekf");

  EXPECT_TRUE(a.has_max_position_rmse);
  EXPECT_DOUBLE_EQ(a.max_position_rmse_m, 12.5);
  EXPECT_TRUE(a.has_max_error);
  EXPECT_DOUBLE_EQ(a.max_error_m, 40.0);
  EXPECT_TRUE(a.has_max_horizontal_p95);
  EXPECT_DOUBLE_EQ(a.max_horizontal_p95_m, 25.0);
  EXPECT_TRUE(a.has_max_time_to_detect);
  EXPECT_DOUBLE_EQ(a.max_time_to_detect_s, 3.5);
  EXPECT_TRUE(a.has_max_time_to_isolate);
  EXPECT_DOUBLE_EQ(a.max_time_to_isolate_s, 6.0);
  EXPECT_TRUE(a.has_max_false_isolations);
  EXPECT_EQ(a.max_false_isolations, 2);
  EXPECT_TRUE(a.has_min_availability);
  EXPECT_DOUBLE_EQ(a.min_availability, 0.97);
  EXPECT_TRUE(a.require_detection);
  EXPECT_TRUE(a.require_isolation);
  EXPECT_TRUE(a.has_forbidden_end_mode);
  EXPECT_EQ(a.forbidden_end_mode, NavMode::kUnsafe);
  EXPECT_TRUE(a.has_required_end_mode);
  EXPECT_EQ(a.required_end_mode, NavMode::kDegraded);
}

TEST(ScenarioLoading, AnAbsentCriterionFieldStaysAbsentRatherThanDefaultingToZero) {
  // A criterion with no threshold must not read as "the threshold is zero",
  // which would fail every run for a reason nobody wrote down.
  const Scenario s = loadOrFail(yamlScenario("acceptance:\n  - id: AC-1\n"));
  ASSERT_EQ(s.acceptance.size(), 1u);
  const AcceptanceCriterion& a = s.acceptance[0];
  EXPECT_FALSE(a.has_max_position_rmse);
  EXPECT_FALSE(a.has_max_error);
  EXPECT_FALSE(a.has_max_horizontal_p95);
  EXPECT_FALSE(a.has_max_time_to_detect);
  EXPECT_FALSE(a.has_max_time_to_isolate);
  EXPECT_FALSE(a.has_max_false_isolations);
  EXPECT_FALSE(a.has_min_availability);
  EXPECT_FALSE(a.has_forbidden_end_mode);
  EXPECT_FALSE(a.has_required_end_mode);
  EXPECT_FALSE(a.require_detection);
  EXPECT_FALSE(a.require_isolation);
  EXPECT_EQ(a.estimator, "any") << "an unbound criterion applies to every channel";
}

TEST(ScenarioLoading, EveryNavigationModeCanBeNamedInAnAcceptanceBlock) {
  const std::pair<const char*, NavMode> modes[] = {
      {"INITIALIZING", NavMode::kInitializing}, {"NORMAL", NavMode::kNormal},
      {"DEGRADED", NavMode::kDegraded},         {"DEAD_RECKONING", NavMode::kDeadReckoning},
      {"LOW_CONFIDENCE", NavMode::kLowConfidence}, {"UNSAFE", NavMode::kUnsafe},
  };
  for (const auto& [name, expected] : modes) {
    const Scenario s = loadOrFail(
        yamlScenario(std::string("acceptance:\n  - id: AC-1\n    forbid_end_mode: ") + name + "\n"));
    ASSERT_EQ(s.acceptance.size(), 1u) << name;
    EXPECT_EQ(s.acceptance[0].forbidden_end_mode, expected) << name;
  }

  expectRefused(yamlScenario("acceptance:\n  - id: AC-1\n    forbid_end_mode: BROKEN\n"), "BROKEN");
  expectRefused(yamlScenario("acceptance:\n  - id: AC-1\n    require_end_mode: PERFECT\n"),
                "PERFECT");
}

TEST(ScenarioLoading, ReadsTheTrajectoryProfile) {
  const Scenario s = loadOrFail(yamlScenario(
      "trajectory:\n"
      "  approach_speed_mps: 72.0\n"
      "  taxi_speed_mps: 9.0\n"
      "  initial_distance_to_threshold_m: 5200.0\n"
      "  flare_height_m: 11.0\n"
      "  rollout_decel_time_s: 21.0\n"
      "  turn_radius_m: 1800.0\n"
      "  base_leg_length_m: 2600.0\n"
      "  roll_in_time_s: 4.0\n"));
  EXPECT_DOUBLE_EQ(s.profile.approach_speed_mps, 72.0);
  EXPECT_DOUBLE_EQ(s.profile.taxi_speed_mps, 9.0);
  EXPECT_DOUBLE_EQ(s.profile.initial_distance_to_threshold_m, 5200.0);
  EXPECT_DOUBLE_EQ(s.profile.flare_height_m, 11.0);
  EXPECT_DOUBLE_EQ(s.profile.rollout_decel_time_s, 21.0);
  EXPECT_DOUBLE_EQ(s.profile.turn_radius_m, 1800.0);
  EXPECT_DOUBLE_EQ(s.profile.base_leg_length_m, 2600.0);
  EXPECT_DOUBLE_EQ(s.profile.roll_in_time_s, 4.0);
}

TEST(ScenarioLoading, RefusesADocumentThatIsNotAScenarioAtAll) {
  Scenario s;
  std::string error;
  EXPECT_FALSE(loadScenarioFromText("{ this is not json", "broken.json", s, error));
  EXPECT_NE(error.find("broken.json"), std::string::npos) << error;

  error.clear();
  EXPECT_FALSE(loadScenario("/definitely/not/a/path/SCN-999.yaml", s, error));
  EXPECT_FALSE(error.empty());
}

TEST(ScenarioLoading, AConfigurationOverlayCanBeAppliedFromEitherForm) {
  Scenario s = loadOrFail(yamlScenario(""));
  const std::string before = s.config_hash;

  std::string error;
  ASSERT_TRUE(applyConfigText("{\"seeds_per_scenario\": 1000}", "config.json", s, error)) << error;
  EXPECT_NE(s.config_hash, before) << "the configuration hash must follow its text (DATA-005)";

  const std::string afterJson = s.config_hash;
  ASSERT_TRUE(applyConfigText("seeds_per_scenario: 1000\n", "config.yaml", s, error)) << error;
  EXPECT_NE(s.config_hash, afterJson) << "same value, different text, different provenance";

  EXPECT_FALSE(applyConfigText("{ broken", "config.json", s, error));
  EXPECT_NE(error.find("config.json"), std::string::npos) << error;
  EXPECT_FALSE(applyConfigFile("/definitely/not/a/path/config.json", s, error));
}

TEST(ScenarioLoading, TheDefaultChannelSetCoversTheFiveComparedArchitectures) {
  const std::vector<NavigationChannelSpec> channels = defaultChannels();
  ASSERT_EQ(channels.size(), 5u);

  // NAV-C carries no integrity architecture AT ALL, which is stronger than
  // monitor-only and is the point: comparing it against NAV-D has to measure
  // the difference between having an integrity layer and not having one, not
  // the difference between acting and not acting on a detection. NAV-A and
  // NAV-B have none either, for the same reason.
  for (const NavigationChannelSpec& c : channels) {
    if (c.estimator != EstimatorId::kEkf && c.estimator != EstimatorId::kGnssOnly &&
        c.estimator != EstimatorId::kInsDeadReckoning) {
      continue;
    }
    EXPECT_FALSE(c.integrity.enabled)
        << "channel " << toString(c.estimator) << " is a control case and must carry no policy";
  }

  bool hasSolutionSeparation = false;
  for (const NavigationChannelSpec& c : channels) {
    if (c.estimator == EstimatorId::kSolutionSeparation) {
      hasSolutionSeparation = true;
      EXPECT_TRUE(c.integrity.enable_solution_separation);
      EXPECT_FALSE(c.integrity.monitor_only);
    }
  }
  EXPECT_TRUE(hasSolutionSeparation);
}
