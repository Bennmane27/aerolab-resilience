// VNV-001, M-01..M-15 - metric definitions.
#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "aerolab/metrics/metrics.hpp"

using namespace aerolab;

namespace {

TruthState truthAt(double t, double n, double e, double d) {
  TruthState s;
  s.t_s = t;
  s.position_ned_m = Vec3(n, e, d);
  return s;
}

NavSolution solutionAt(double t, double n, double e, double d, NavMode mode = NavMode::kNormal) {
  NavSolution s;
  s.t_s = t;
  s.position_ned_m = Vec3(n, e, d);
  s.mode = mode;
  s.valid = mode != NavMode::kUnsafe;
  return s;
}

IntegrityEvent transition(double t, SensorState from, SensorState to) {
  IntegrityEvent e;
  e.t_s = t;
  e.sensor = SensorId::kGnss;
  e.from = from;
  e.to = to;
  e.reason = IntegrityReason::kNisPersistent;
  return e;
}

}  // namespace

TEST(Distribution, NearestRankPercentiles) {
  const Distribution d = Distribution::from({1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0});
  EXPECT_EQ(d.count, 10u);
  EXPECT_DOUBLE_EQ(d.mean, 5.5);
  EXPECT_DOUBLE_EQ(d.median, 5.5);
  EXPECT_DOUBLE_EQ(d.min, 1.0);
  EXPECT_DOUBLE_EQ(d.max, 10.0);
  // Nearest rank: ceil(0.95 * 10) = 10 -> the tenth value.
  EXPECT_DOUBLE_EQ(d.p95, 10.0);
}

TEST(Distribution, HandlesOddCountsAndEmptyInput) {
  const Distribution odd = Distribution::from({3.0, 1.0, 2.0});
  EXPECT_DOUBLE_EQ(odd.median, 2.0);
  const Distribution empty = Distribution::from({});
  EXPECT_EQ(empty.count, 0u);
  EXPECT_DOUBLE_EQ(empty.mean, 0.0);
  EXPECT_DOUBLE_EQ(empty.p95, 0.0);
}

TEST(Metrics, RmseAndErrorDistribution) {
  MetricsAccumulator m;
  m.reset(-1.0, -1.0);
  for (int i = 0; i < 100; ++i) {
    const double t = i * 0.1;
    m.sample(t, truthAt(t, 0, 0, 0), solutionAt(t, 3.0, 4.0, 0.0));  // 5 m error
  }
  const MetricsSummary s = m.finalize(10.0);
  EXPECT_NEAR(s.position_rmse_m, 5.0, 1e-9);
  EXPECT_NEAR(s.horizontal_rmse_m, 5.0, 1e-9);
  EXPECT_NEAR(s.vertical_rmse_m, 0.0, 1e-12);
  EXPECT_NEAR(s.position_error_m.max, 5.0, 1e-9);
  EXPECT_NEAR(s.position_error_m.p95, 5.0, 1e-9);
  EXPECT_EQ(s.position_error_m.count, 100u);
}

// M-05 / M-06 measured from the fault start instant.
TEST(Metrics, TimeToDetectAndIsolate) {
  MetricsAccumulator m;
  m.reset(10.0, 40.0, {SensorId::kGnss});
  m.noteIntegrityEvent(transition(12.5, SensorState::kActive, SensorState::kSuspect));
  m.noteIntegrityEvent(transition(14.0, SensorState::kSuspect, SensorState::kIsolated));
  const MetricsSummary s = m.finalize(60.0);
  EXPECT_NEAR(s.time_to_detect_s, 2.5, 1e-12);
  EXPECT_NEAR(s.time_to_isolate_s, 4.0, 1e-12);
  EXPECT_TRUE(s.fault_detected);
  EXPECT_TRUE(s.fault_isolated);
  EXPECT_EQ(s.false_alert_count, 0u);
}

// A run where nothing was detected must report "never", not zero. Averaging a
// missed detection as a zero second response is the single most misleading
// thing this file could do.
//
// Detection is also only credited on a source the scenario actually faulted:
// see KF-007 for the artefact that convention prevents.
TEST(Metrics, MissedDetectionIsNegativeNotZero) {
  MetricsAccumulator m;
  m.reset(10.0, 40.0, {SensorId::kGnss});
  const MetricsSummary s = m.finalize(60.0);
  EXPECT_LT(s.time_to_detect_s, 0.0);
  EXPECT_FALSE(s.fault_detected);
  EXPECT_TRUE(s.fault_present);
}

// M-07: an alert outside the fault window is a false alert.
// A source the scenario never faulted cannot produce a detection, however it
// behaves. This is what stops the vision sensor losing sight of the runway at
// touchdown from being scored as detecting a GNSS drift (KF-007).
TEST(Metrics, DetectionIsCreditedOnlyOnTheFaultedSource) {
  MetricsAccumulator m;
  m.reset(10.0, 40.0, {SensorId::kGnss});
  IntegrityEvent vision_dropout = transition(12.0, SensorState::kActive, SensorState::kUnavailable);
  vision_dropout.sensor = SensorId::kVision;
  m.noteIntegrityEvent(vision_dropout);
  const MetricsSummary s = m.finalize(60.0);
  EXPECT_FALSE(s.fault_detected) << "a dropout on an unfaulted source is not a detection";
  EXPECT_LT(s.time_to_detect_s, 0.0);
}

TEST(Metrics, FalseAlertsAreCountedOutsideTheFaultWindow) {
  MetricsAccumulator m;
  m.reset(-1.0, -1.0, {SensorId::kGnss});  // nominal run
  m.noteIntegrityEvent(transition(5.0, SensorState::kActive, SensorState::kSuspect));
  m.noteIntegrityEvent(transition(6.0, SensorState::kSuspect, SensorState::kIsolated));
  for (int i = 0; i < 500; ++i) m.noteGateEvaluation();
  const MetricsSummary s = m.finalize(60.0);
  EXPECT_EQ(s.false_alert_count, 1u);
  EXPECT_EQ(s.false_isolation_count, 1u);
  EXPECT_NEAR(s.false_alert_rate, 1.0 / 500.0, 1e-12);
}

// M-11.
TEST(Metrics, RecoveryTimeIsMeasuredFromTheFaultEnd) {
  MetricsAccumulator m;
  m.reset(10.0, 30.0, {SensorId::kGnss});
  m.noteIntegrityEvent(transition(12.0, SensorState::kActive, SensorState::kSuspect));
  m.noteIntegrityEvent(transition(13.0, SensorState::kSuspect, SensorState::kIsolated));
  m.noteIntegrityEvent(transition(36.5, SensorState::kIsolated, SensorState::kActive));
  const MetricsSummary s = m.finalize(60.0);
  EXPECT_NEAR(s.recovery_time_s, 6.5, 1e-12);
}

// M-09 / M-10.
TEST(Metrics, AvailabilityAndContinuity) {
  MetricsAccumulator m;
  m.reset(-1.0, -1.0);
  for (int i = 0; i <= 100; ++i) {
    const double t = i * 0.1;  // 0 .. 10 s
    const NavMode mode = (t >= 4.0 && t < 6.0) ? NavMode::kUnsafe : NavMode::kNormal;
    m.sample(t, truthAt(t, 0, 0, 0), solutionAt(t, 0, 0, 0, mode));
  }
  const MetricsSummary s = m.finalize(10.0);
  EXPECT_NEAR(s.availability, 0.8, 0.02);
  EXPECT_DOUBLE_EQ(s.continuity, 0.0) << "one interruption means the run was not continuous";
  EXPECT_EQ(s.interruption_count, 1u);
}

TEST(Metrics, ContinuityIsOneWhenNothingWasInterrupted) {
  MetricsAccumulator m;
  m.reset(-1.0, -1.0);
  for (int i = 0; i <= 100; ++i) {
    const double t = i * 0.1;
    m.sample(t, truthAt(t, 0, 0, 0), solutionAt(t, 0, 0, 0));
  }
  const MetricsSummary s = m.finalize(10.0);
  EXPECT_DOUBLE_EQ(s.continuity, 1.0);
  EXPECT_NEAR(s.availability, 1.0, 1e-9);
}

TEST(Metrics, LowConfidenceCountsAsUnavailable) {
  MetricsAccumulator m;
  m.reset(-1.0, -1.0);
  for (int i = 0; i <= 100; ++i) {
    const double t = i * 0.1;
    m.sample(t, truthAt(t, 0, 0, 0), solutionAt(t, 0, 0, 0, NavMode::kLowConfidence));
  }
  const MetricsSummary s = m.finalize(10.0);
  EXPECT_NEAR(s.availability, 0.0, 1e-9);
}

// M-12.
TEST(Metrics, NisConsistencyNormalisesByDegreesOfFreedom) {
  MetricsAccumulator m;
  m.reset(-1.0, -1.0);
  for (int i = 0; i < 100; ++i) m.noteNis(3.0, 3, 16.27);  // exactly consistent
  for (int i = 0; i < 10; ++i) m.noteNis(30.0, 3, 16.27);  // above the gate
  const MetricsSummary s = m.finalize(10.0);
  EXPECT_EQ(s.nis_samples, 110u);
  EXPECT_NEAR(s.nis_mean_normalised, (100 * 1.0 + 10 * 10.0) / 110.0, 1e-12);
  EXPECT_NEAR(s.nis_fraction_within_gate, 100.0 / 110.0, 1e-12);
  m.noteNis(std::nan(""), 3, 16.27);  // must be ignored, not poison the mean
}

// M-03 during the fault window specifically.
TEST(Metrics, MaximumErrorDuringTheFaultIsTracked) {
  MetricsAccumulator m;
  m.reset(5.0, 8.0);
  m.sample(1.0, truthAt(1, 0, 0, 0), solutionAt(1, 100, 0, 0));  // before: ignored
  m.sample(6.0, truthAt(6, 0, 0, 0), solutionAt(6, 30, 0, 0));
  m.sample(7.0, truthAt(7, 0, 0, 0), solutionAt(7, 50, 0, 0));
  m.sample(9.0, truthAt(9, 0, 0, 0), solutionAt(9, 200, 0, 0));  // after: ignored
  const MetricsSummary s = m.finalize(10.0);
  EXPECT_NEAR(s.max_error_during_fault_m, 50.0, 1e-9);
  EXPECT_NEAR(s.error_at_fault_end_m, 50.0, 1e-9);
}

// M-15.
TEST(Metrics, DeterminismHashDependsOnTheOutput) {
  MetricsAccumulator a;
  MetricsAccumulator b;
  MetricsAccumulator c;
  a.reset(-1, -1);
  b.reset(-1, -1);
  c.reset(-1, -1);
  for (int i = 0; i < 50; ++i) {
    const double t = i * 0.1;
    a.sample(t, truthAt(t, t, 0, 0), solutionAt(t, t + 0.5, 0, 0));
    b.sample(t, truthAt(t, t, 0, 0), solutionAt(t, t + 0.5, 0, 0));
    c.sample(t, truthAt(t, t, 0, 0), solutionAt(t, t + 0.5000001, 0, 0));
  }
  const std::string ha = a.finalize(5.0).determinism_hash;
  const std::string hb = b.finalize(5.0).determinism_hash;
  const std::string hc = c.finalize(5.0).determinism_hash;
  EXPECT_EQ(ha, hb);
  EXPECT_NE(ha, hc);
  EXPECT_EQ(ha.size(), 64u);
}

TEST(Metrics, TickTimesAreSummarised) {
  MetricsAccumulator m;
  m.reset(-1, -1);
  for (int i = 1; i <= 100; ++i) m.noteTickTime(i * 0.01);
  const MetricsSummary s = m.finalize(1.0);
  EXPECT_EQ(s.tick_time_ms.count, 100u);
  EXPECT_NEAR(s.tick_time_ms.max, 1.0, 1e-12);
  EXPECT_NEAR(s.tick_time_ms.p95, 0.95, 1e-12);
}

TEST(Metrics, PeakMemoryIsReportedOrExplicitlyZero) {
  const double mb = peakResidentMemoryMb();
  EXPECT_GE(mb, 0.0);
  EXPECT_LT(mb, 100000.0);
}
