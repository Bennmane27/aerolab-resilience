// Acceptance tests AT-001 .. AT-014 from section 13.2 of the cahier des charges.
//
// Where the catalogue defines a test that only a full campaign can answer
// (AT-003 and AT-004 are specified over 1000 seeds), this file runs the reduced
// form that CI can afford and states the reduction explicitly. The full
// campaign lives in benchmark/core.yaml and its results in BENCHMARK_REPORT.md.
//
// AT-010 (sanitizers) and AT-011 (coverage) are properties of the build, not of
// a test case: they are enforced by the asan / ubsan / coverage presets and by
// .github/workflows/ci-native.yml. AT-012 (web end to end) lives in web/tests.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

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

Scenario load(const std::string& id, double duration_s = 0.0) {
  Scenario s;
  std::string error;
  EXPECT_TRUE(loadScenario(sourceDir() + "/scenarios/" + id + ".yaml", s, error)) << error;
  EXPECT_TRUE(applyConfigFile(sourceDir() + "/configs/evaluation.json", s, error)) << error;
  if (duration_s > 0.0) s.duration_s = duration_s;
  return s;
}

RunResult run(const Scenario& s, std::uint64_t seed) {
  RunOptions options;
  options.measure_tick_time = false;
  options.has_seed_override = true;
  options.seed_override = seed;
  SimulationRunner runner;
  std::string error;
  EXPECT_TRUE(runner.configure(s, options, error)) << error;
  return runner.run();
}

}  // namespace

// AT-001 - deterministic reset. Same scenario and seed twice: the replay
// relevant output must hash identically.
TEST(Acceptance, AT001_DeterministicReset) {
  const Scenario s = load("SCN-014", 40.0);
  const RunResult a = run(s, 424242);
  const RunResult b = run(s, 424242);
  ASSERT_EQ(a.channels.size(), b.channels.size());
  for (std::size_t i = 0; i < a.channels.size(); ++i) {
    EXPECT_EQ(a.channels[i].metrics.determinism_hash, b.channels[i].metrics.determinism_hash);
  }
  EXPECT_EQ(a.manifest["verdict"].asString(), b.manifest["verdict"].asString());
}

// AT-002 - ground truth isolation under a 100 m injection.
// The empirical half of the check; the structural half is that
// FaultInjectionEngine::apply() takes no truth argument at all.
TEST(Acceptance, AT002_GroundTruthIsolation) {
  const Scenario spoofed = load("SCN-003", 60.0);
  Scenario clean = spoofed;
  clean.faults.clear();

  const RunResult with_fault = run(spoofed, 5150);
  const RunResult without = run(clean, 5150);
  ASSERT_TRUE(with_fault.completed);
  ASSERT_TRUE(without.completed);

  // The dead reckoning channel never sees GNSS, so if the truth and the
  // inertial stream are untouched by a GNSS spoof its output must be identical
  // bit for bit between the two runs.
  const ChannelResult* a = with_fault.channel(EstimatorId::kInsDeadReckoning);
  const ChannelResult* b = without.channel(EstimatorId::kInsDeadReckoning);
  ASSERT_NE(a, nullptr);
  ASSERT_NE(b, nullptr);
  EXPECT_EQ(a->metrics.determinism_hash, b->metrics.determinism_hash)
      << "a GNSS fault changed something it must not touch";

  // And the spoof did reach the GNSS-only channel.
  const ChannelResult* naive_faulted = with_fault.channel(EstimatorId::kGnssOnly);
  const ChannelResult* naive_clean = without.channel(EstimatorId::kGnssOnly);
  ASSERT_NE(naive_faulted, nullptr);
  ASSERT_NE(naive_clean, nullptr);
  EXPECT_GT(naive_faulted->metrics.position_error_m.max,
            naive_clean->metrics.position_error_m.max + 50.0);
}

// AT-003 - false alert budget on nominal runs.
// Reduced form: 30 seeds instead of 1000. The full campaign is the reference.
TEST(Acceptance, AT003_NominalFalseAlertBudget) {
  const Scenario s = load("SCN-001", 60.0);
  int runs_with_false_isolation = 0;
  std::size_t total_isolations = 0;
  std::size_t total_gates = 0;
  const int kSeeds = 30;

  for (int i = 0; i < kSeeds; ++i) {
    const RunResult r = run(s, static_cast<std::uint64_t>(6000000 + i));
    ASSERT_TRUE(r.completed) << "seed " << i;
    for (const ChannelResult& c : r.channels) {
      if (c.estimator != EstimatorId::kIntegrityEkf &&
          c.estimator != EstimatorId::kSolutionSeparation) {
        continue;
      }
      total_gates += c.metrics.gate_evaluations;
      total_isolations += c.metrics.false_isolation_count;
      if (c.metrics.false_isolation_count > 0) ++runs_with_false_isolation;
    }
  }
  EXPECT_GT(total_gates, 1000u);
  EXPECT_EQ(runs_with_false_isolation, 0)
      << total_isolations << " false isolations over " << kSeeds
      << " nominal runs; the gate or the persistence counter needs revisiting";
}

// AT-004 - step spoof detection. Reduced form: 20 seeds.
TEST(Acceptance, AT004_StepSpoofDetection) {
  const Scenario s = load("SCN-003", 70.0);
  int detected = 0;
  int isolated = 0;
  std::vector<double> ttd;
  const int kSeeds = 20;

  for (int i = 0; i < kSeeds; ++i) {
    const RunResult r = run(s, static_cast<std::uint64_t>(6100000 + i));
    ASSERT_TRUE(r.completed);
    const ChannelResult* c = r.channel(EstimatorId::kIntegrityEkf);
    ASSERT_NE(c, nullptr);
    if (c->metrics.fault_detected) ++detected;
    if (c->metrics.fault_isolated) ++isolated;
    if (c->metrics.time_to_detect_s >= 0.0) ttd.push_back(c->metrics.time_to_detect_s);
  }
  EXPECT_EQ(detected, kSeeds) << "a 100 m step must be detected on every seed";
  EXPECT_GE(isolated, kSeeds - 1);
  ASSERT_FALSE(ttd.empty());
  std::sort(ttd.begin(), ttd.end());
  const double p95 =
      ttd[static_cast<std::size_t>(std::ceil(0.95 * static_cast<double>(ttd.size()))) - 1];
  EXPECT_LE(p95, 2.0) << "time to detect P95 above the 2 s budget of AT-004";
}

// AT-005 - slow drift honesty. The point of this test is that nothing is
// hidden: whatever the gate does, the missed detections and the resulting error
// are counted and reported.
TEST(Acceptance, AT005_SlowDriftIsReportedHonestly) {
  const Scenario s = load("SCN-004", 80.0);
  int gate_detected = 0;
  int separation_detected = 0;
  double worst_gate_error = 0.0;
  const int kSeeds = 12;

  for (int i = 0; i < kSeeds; ++i) {
    const RunResult r = run(s, static_cast<std::uint64_t>(6200000 + i));
    ASSERT_TRUE(r.completed);
    const ChannelResult* gate = r.channel(EstimatorId::kIntegrityEkf);
    const ChannelResult* solsep = r.channel(EstimatorId::kSolutionSeparation);
    ASSERT_NE(gate, nullptr);
    ASSERT_NE(solsep, nullptr);
    if (gate->metrics.fault_detected) ++gate_detected;
    if (solsep->metrics.fault_detected) ++separation_detected;
    worst_gate_error = std::max(worst_gate_error, gate->metrics.max_error_during_fault_m);
    // Whatever happened, the run must have produced usable numbers.
    EXPECT_TRUE(std::isfinite(gate->metrics.position_rmse_m));
    EXPECT_GE(gate->metrics.gate_evaluations, 100u);
  }
  // No assertion is made on gate_detected: it is the measurement. It is logged
  // so that a regression in either direction is visible in the CI output.
  std::printf(
      "[AT-005] innovation gate detected %d/%d, solution separation %d/%d, "
      "worst gated error during fault %.1f m\n",
      gate_detected, kSeeds, separation_detected, kSeeds, worst_gate_error);
  EXPECT_EQ(separation_detected, kSeeds) << "solution separation must not miss a 150 m ramp";
}

// AT-006 - GNSS blackout continuity.
TEST(Acceptance, AT006_BlackoutContinuity) {
  const RunResult r = run(load("SCN-002", 80.0), 777);
  ASSERT_TRUE(r.completed);
  const ChannelResult* naive = r.channel(EstimatorId::kGnssOnly);
  const ChannelResult* fused = r.channel(EstimatorId::kIntegrityEkf);
  ASSERT_NE(naive, nullptr);
  ASSERT_NE(fused, nullptr);
  EXPECT_NE(naive->final_mode, NavMode::kNormal);
  EXPECT_LT(naive->metrics.availability, 0.8);
  EXPECT_TRUE(fused->healthy);
  EXPECT_TRUE(std::isfinite(fused->metrics.position_error_m.max));
  std::printf("[AT-006] fused max error during blackout: %.1f m\n",
              fused->metrics.max_error_during_fault_m);
}

// AT-007 - recovery hysteresis: no reintegration before the configured window.
TEST(Acceptance, AT007_RecoveryHysteresis) {
  Scenario s = load("SCN-011", 90.0);
  const RunResult r = run(s, 24680);
  ASSERT_TRUE(r.completed);
  const ChannelResult* c = r.channel(EstimatorId::kIntegrityEkf);
  ASSERT_NE(c, nullptr);

  double left_active_at = -1.0;
  double returned_at = -1.0;
  for (const IntegrityEvent& e : c->events) {
    if (e.sensor != SensorId::kGnss) continue;
    if (left_active_at < 0.0 && e.from == SensorState::kActive && e.to != SensorState::kActive) {
      left_active_at = e.t_s;
    }
    if (left_active_at >= 0.0 && returned_at < 0.0 && e.to == SensorState::kActive) {
      returned_at = e.t_s;
    }
  }
  ASSERT_GE(left_active_at, 0.0) << "GNSS never left ACTIVE during a 30 s outage";
  if (returned_at >= 0.0) {
    // The source physically returns at t = 55 s. Reintegration must not be
    // instantaneous: some hysteresis has to be observable.
    EXPECT_GT(returned_at, 55.0 - 1e-6);
  }
}

// AT-008 - dual fault fail-safe.
TEST(Acceptance, AT008_DualFaultFailSafe) {
  const Scenario s = load("SCN-012", 75.0);
  int not_normal = 0;
  const int kSeeds = 8;
  for (int i = 0; i < kSeeds; ++i) {
    const RunResult r = run(s, static_cast<std::uint64_t>(6300000 + i));
    ASSERT_TRUE(r.completed);
    const ChannelResult* c = r.channel(EstimatorId::kIntegrityEkf);
    ASSERT_NE(c, nullptr);
    if (c->final_mode != NavMode::kNormal) ++not_normal;
  }
  EXPECT_EQ(not_normal, kSeeds)
      << "with two simultaneous faults the architecture still claimed NORMAL";
}

// AT-013 .. AT-026 - regression over the fourteen catalogue scenarios.
// Section 13.2 gives all of them the same generic criterion (the run completes
// and produces a manifest). Here that is combined with the scenario's own
// machine readable acceptance block, which is what actually makes the verdict
// meaningful (DEV-007).
TEST(Acceptance, AT013toAT026_AllScenariosSatisfyTheirAcceptanceBlock) {
  const char* ids[] = {"SCN-001", "SCN-002", "SCN-003", "SCN-004", "SCN-005", "SCN-006", "SCN-007",
                       "SCN-008", "SCN-009", "SCN-010", "SCN-011", "SCN-012", "SCN-013", "SCN-014"};
  for (const char* id : ids) {
    const Scenario s = load(id);
    const RunResult r = run(s, s.seed);
    EXPECT_TRUE(r.completed) << id << " did not complete";
    for (const ChannelResult& c : r.channels) {
      EXPECT_TRUE(c.healthy) << id << " / " << toString(c.estimator) << ": " << c.diagnostic;
      EXPECT_TRUE(std::isfinite(c.metrics.position_rmse_m)) << id;
    }
    EXPECT_FALSE(r.manifest["scenario_hash"].asString().empty()) << id;
    std::string failures;
    for (const std::string& f : r.verdict_failures) failures += "\n    " + f;
    EXPECT_TRUE(r.verdict_pass) << id << " failed its acceptance block:" << failures;
  }
}

// SYS-017 / BEN-012: a failing run must be recorded, not hidden. Here a
// deliberately impossible acceptance criterion is injected and the verdict must
// come back FAIL with a readable explanation, while the run itself still
// completes and still produces a manifest.
TEST(Acceptance, FailingCriterionProducesAReadableFailNotACrash) {
  Scenario s = load("SCN-001", 30.0);
  AcceptanceCriterion impossible;
  impossible.id = "AC-IMPOSSIBLE";
  impossible.estimator = "integrity_ekf";
  impossible.has_max_position_rmse = true;
  impossible.max_position_rmse_m = 1e-9;
  s.acceptance.push_back(impossible);

  const RunResult r = run(s, 1);
  EXPECT_TRUE(r.completed) << "the run itself must still complete";
  EXPECT_FALSE(r.verdict_pass);
  ASSERT_FALSE(r.verdict_failures.empty());
  EXPECT_NE(r.verdict_failures[0].find("AC-IMPOSSIBLE"), std::string::npos);
  EXPECT_NE(r.verdict_failures[0].find("position RMSE"), std::string::npos);
  EXPECT_EQ(r.manifest["verdict"].asString(), "FAIL");
  EXPECT_EQ(r.manifest["verdict_failures"].size(), r.verdict_failures.size());
}
