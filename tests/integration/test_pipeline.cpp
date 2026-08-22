// VNV-003 - integration: truth -> sensors -> faults -> estimator -> integrity
// -> metrics, driven through the real runner.
#include <gtest/gtest.h>

#include <cmath>
#include <string>

#include "aerolab/io/runner.hpp"
#include "aerolab/io/scenario.hpp"

using namespace aerolab;

namespace {

std::string sourceDir() {
#ifdef AEROLAB_SOURCE_DIR
  return std::string(AEROLAB_SOURCE_DIR);
#else
  return std::string(".");
#endif
}

Scenario load(const std::string& id, double duration_s = 40.0) {
  Scenario s;
  std::string error;
  EXPECT_TRUE(loadScenario(sourceDir() + "/scenarios/" + id + ".yaml", s, error)) << error;
  EXPECT_TRUE(applyConfigFile(sourceDir() + "/configs/evaluation.json", s, error)) << error;
  if (duration_s > 0.0) s.duration_s = duration_s;
  return s;
}

RunResult runScenario(const Scenario& s, std::uint64_t seed = 0) {
  RunOptions options;
  options.measure_tick_time = false;
  if (seed != 0) {
    options.has_seed_override = true;
    options.seed_override = seed;
  }
  SimulationRunner runner;
  std::string error;
  EXPECT_TRUE(runner.configure(s, options, error)) << error;
  return runner.run();
}

const ChannelResult& channel(const RunResult& r, EstimatorId id) {
  const ChannelResult* c = r.channel(id);
  EXPECT_NE(c, nullptr) << "missing channel " << toString(id);
  return *c;
}

}  // namespace

TEST(Pipeline, NominalRunConvergesAndStaysConsistent) {
  const RunResult r = runScenario(load("SCN-001", 60.0));
  ASSERT_TRUE(r.completed) << (r.verdict_failures.empty() ? "" : r.verdict_failures[0]);
  ASSERT_EQ(r.channels.size(), 5u);

  const ChannelResult& ekf = channel(r, EstimatorId::kIntegrityEkf);
  EXPECT_TRUE(ekf.healthy);
  // Fusing a 2 m GNSS with inertial and vision must beat the raw fix.
  EXPECT_LT(ekf.metrics.position_rmse_m, 6.0);
  EXPECT_GT(ekf.metrics.availability, 0.95);

  // M-12: on a nominal run the normalised NIS should sit around 1. A value far
  // above means the filter is overconfident, far below means Q or R is inflated.
  EXPECT_GT(ekf.metrics.nis_mean_normalised, 0.2);
  EXPECT_LT(ekf.metrics.nis_mean_normalised, 6.0);

  // The naive baseline must be measurably worse than the fused solution.
  const ChannelResult& naive = channel(r, EstimatorId::kGnssOnly);
  EXPECT_GT(naive.metrics.position_rmse_m, ekf.metrics.position_rmse_m);
}

TEST(Pipeline, EveryChannelSeesTheSameFaultWindow) {
  const Scenario s = load("SCN-003", 60.0);
  const RunResult r = runScenario(s);
  ASSERT_TRUE(r.completed);
  EXPECT_NEAR(r.fault_start_s, 30.0, 1e-9);
  for (const ChannelResult& c : r.channels) {
    EXPECT_TRUE(c.metrics.fault_present) << toString(c.estimator);
  }
  ASSERT_FALSE(r.fault_events.empty());
  EXPECT_TRUE(r.fault_events[0].activated);
  EXPECT_NEAR(r.fault_events[0].t_s, 30.0, 0.02);
}

// The central comparison of the project: same filter, different policy.
TEST(Pipeline, GatingChangesTheOutcomeOfAStepSpoof) {
  const RunResult r = runScenario(load("SCN-003", 70.0));
  ASSERT_TRUE(r.completed);

  const ChannelResult& plain = channel(r, EstimatorId::kEkf);           // no integrity layer
  const ChannelResult& gated = channel(r, EstimatorId::kIntegrityEkf);  // gating

  EXPECT_FALSE(plain.metrics.fault_detected) << "the control case has no integrity layer";
  EXPECT_FALSE(plain.metrics.fault_isolated);
  EXPECT_EQ(plain.final_mode, NavMode::kNormal) << "with no policy it keeps claiming NORMAL";
  EXPECT_TRUE(gated.metrics.fault_detected);
  EXPECT_TRUE(gated.metrics.fault_isolated);
  EXPECT_GE(gated.metrics.time_to_detect_s, 0.0);
  EXPECT_LT(gated.metrics.time_to_detect_s, 3.0);
  // Rejecting the spoof must actually pay off in accuracy.
  EXPECT_LT(gated.metrics.max_error_during_fault_m, plain.metrics.max_error_during_fault_m);
}

TEST(Pipeline, GnssBlackoutLeavesTheFusedSolutionAlive) {
  const RunResult r = runScenario(load("SCN-002", 70.0));
  ASSERT_TRUE(r.completed);

  const ChannelResult& naive = channel(r, EstimatorId::kGnssOnly);
  EXPECT_NE(naive.final_mode, NavMode::kNormal) << "GNSS-only must not claim NORMAL with no GNSS";

  const ChannelResult& gated = channel(r, EstimatorId::kIntegrityEkf);
  EXPECT_TRUE(gated.healthy);
  EXPECT_EQ(gated.final_gnss_state, SensorState::kUnavailable);
  EXPECT_TRUE(gated.metrics.fault_detected);
}

TEST(Pipeline, FrozenSourceIsCaughtByAge) {
  const RunResult r = runScenario(load("SCN-008", 60.0));
  ASSERT_TRUE(r.completed);
  const ChannelResult& gated = channel(r, EstimatorId::kIntegrityEkf);
  EXPECT_TRUE(gated.metrics.fault_detected);
  ASSERT_FALSE(gated.events.empty());
  bool saw_stale_reason = false;
  for (const IntegrityEvent& e : gated.events) {
    if (e.reason == IntegrityReason::kMeasurementStale ||
        e.reason == IntegrityReason::kSequenceRepeated) {
      saw_stale_reason = true;
    }
  }
  EXPECT_TRUE(saw_stale_reason) << "a freeze must be reported as stale, not as a generic outlier";
}

// AT-008 / INT-013: with two simultaneous faults the architecture must degrade
// its own claim rather than force a solution.
TEST(Pipeline, DualFaultDoesNotStillClaimNormal) {
  const RunResult r = runScenario(load("SCN-012", 70.0));
  ASSERT_TRUE(r.completed);
  const ChannelResult& gated = channel(r, EstimatorId::kIntegrityEkf);
  EXPECT_NE(gated.final_mode, NavMode::kNormal);
}

// DEV-005, the headline comparison. On a slow ramp the innovation gate is
// structurally weak; solution separation is not. This test asserts the
// mechanism, not a particular margin.
TEST(Pipeline, SolutionSeparationSeesTheSlowDrift) {
  const RunResult r = runScenario(load("SCN-004", 80.0));
  ASSERT_TRUE(r.completed);
  const ChannelResult& solsep = channel(r, EstimatorId::kSolutionSeparation);
  EXPECT_TRUE(solsep.metrics.fault_detected)
      << "solution separation failed to see a 150 m ramp; the project claim would be wrong";
  // The comparative statement is the meaningful one: the filter with no
  // integrity layer absorbs more of the ramp than the one that isolates it.
  // An absolute threshold here would just be a number tuned to today s result.
  const ChannelResult& plain = channel(r, EstimatorId::kEkf);
  const ChannelResult& gated = channel(r, EstimatorId::kIntegrityEkf);
  EXPECT_GT(plain.metrics.max_error_during_fault_m, gated.metrics.max_error_during_fault_m)
      << "isolating the drifting source did not reduce the error";
  EXPECT_GT(plain.metrics.position_rmse_m, solsep.metrics.position_rmse_m);
}

TEST(Pipeline, LatencyBurstIsSurvivable) {
  const RunResult r = runScenario(load("SCN-009", 70.0));
  ASSERT_TRUE(r.completed);
  const ChannelResult& gated = channel(r, EstimatorId::kIntegrityEkf);
  EXPECT_TRUE(gated.healthy);
  EXPECT_LT(gated.metrics.position_rmse_m, 60.0);
}

TEST(Pipeline, RecoveryReinstatesGnss) {
  const RunResult r = runScenario(load("SCN-011", 90.0));
  ASSERT_TRUE(r.completed);
  const ChannelResult& gated = channel(r, EstimatorId::kIntegrityEkf);
  bool went_unavailable = false;
  bool came_back = false;
  for (const IntegrityEvent& e : gated.events) {
    if (e.to == SensorState::kUnavailable) went_unavailable = true;
    if (went_unavailable && e.to == SensorState::kActive) came_back = true;
  }
  EXPECT_TRUE(went_unavailable);
  EXPECT_TRUE(came_back) << "GNSS never returned to ACTIVE after the outage ended";
}

TEST(Pipeline, PseudorangeScenarioProducesARaimVerdict) {
  const RunResult r = runScenario(load("SCN-013", 60.0));
  ASSERT_TRUE(r.completed);
  EXPECT_TRUE(r.last_raim.computed);
  EXPECT_EQ(r.last_raim.degrees_of_freedom, 4);
  EXPECT_TRUE(r.last_raim.detected) << "a 60 m range bias should be visible in the residuals";
}

// SYS-004 / AT-001: same scenario, same seed, identical output.
TEST(Pipeline, SameSeedGivesIdenticalHashes) {
  const Scenario s = load("SCN-004", 40.0);
  const RunResult a = runScenario(s, 987654);
  const RunResult b = runScenario(s, 987654);
  ASSERT_EQ(a.channels.size(), b.channels.size());
  for (std::size_t i = 0; i < a.channels.size(); ++i) {
    EXPECT_EQ(a.channels[i].metrics.determinism_hash, b.channels[i].metrics.determinism_hash)
        << toString(a.channels[i].estimator);
    EXPECT_DOUBLE_EQ(a.channels[i].metrics.position_rmse_m, b.channels[i].metrics.position_rmse_m);
  }
}

TEST(Pipeline, DifferentSeedsGiveDifferentNoise) {
  const Scenario s = load("SCN-001", 30.0);
  const RunResult a = runScenario(s, 111);
  const RunResult b = runScenario(s, 222);
  EXPECT_NE(a.channels[0].metrics.determinism_hash, b.channels[0].metrics.determinism_hash);
}

// SYS-016 / DEV-007: the manifest carries the verdict and full provenance.
TEST(Pipeline, ManifestCarriesProvenanceAndVerdict) {
  const Scenario s = load("SCN-001", 30.0);
  const RunResult r = runScenario(s, 4242);
  const Json& m = r.manifest;
  EXPECT_EQ(m["scenario_id"].asString(), "SCN-001");
  EXPECT_EQ(m["scenario_hash"].asString().size(), 64u);
  EXPECT_EQ(m["config_hash"].asString().size(), 64u);
  EXPECT_FALSE(m["commit"].asString().empty());
  EXPECT_FALSE(m["compiler"].asString().empty());
  EXPECT_DOUBLE_EQ(m["seed"].asDouble(), 4242.0);
  EXPECT_EQ(m["channels"].size(), 5u);
  EXPECT_TRUE(m["verdict"].asString() == "PASS" || m["verdict"].asString() == "FAIL");
  const Json& first = m["channels"].at(0);
  EXPECT_FALSE(first["determinism_hash"].asString().empty());
  EXPECT_TRUE(first.has("position_rmse_m"));
  EXPECT_TRUE(first.has("availability"));
  EXPECT_TRUE(first.has("nis_mean_normalised"));
}

// BEN-002: every architecture must be judged on the same measurement sequence.
// The truth is generated once per tick, so this is structural, but the test
// makes the guarantee visible.
TEST(Pipeline, AllChannelsShareTheSameTruth) {
  const RunResult r = runScenario(load("SCN-006", 40.0), 31415);
  ASSERT_TRUE(r.completed);
  // Every channel logged the same number of samples over the same window.
  const std::size_t reference = r.channels[0].metrics.position_error_m.count;
  EXPECT_GT(reference, 100u);
  for (const ChannelResult& c : r.channels) {
    EXPECT_EQ(c.metrics.position_error_m.count, reference) << toString(c.estimator);
  }
}

TEST(Pipeline, InvalidScenarioIsRefusedBeforeRunning) {
  Scenario s = load("SCN-001", 30.0);
  FaultSpec bad;
  bad.id = "F-BAD";
  bad.type = FaultType::kLatency;
  bad.target = SensorId::kGnss;
  bad.scalar = -3.0;  // negative latency
  s.faults.push_back(bad);

  SimulationRunner runner;
  RunOptions options;
  std::string error;
  EXPECT_FALSE(runner.configure(s, options, error));
  EXPECT_FALSE(error.empty());
}
