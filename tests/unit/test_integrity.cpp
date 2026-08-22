// VNV-013, VNV-014, INT-001..INT-022 - integrity state machine and monitors.
#include <gtest/gtest.h>

#include <cmath>

#include "aerolab/integrity/integrity_manager.hpp"
#include "aerolab/integrity/raim.hpp"

using namespace aerolab;

namespace {

Measurement gnss(double t, double sequence = 0.0) {
  Measurement m;
  m.type = MeasurementType::kGnssPosition;
  m.dim = 3;
  m.header.sensor = SensorId::kGnss;
  m.header.sample_time_s = t;
  m.header.delivery_time_s = t;
  m.header.sequence = static_cast<std::uint64_t>(sequence);
  return m;
}

InnovationInfo innovation(double nis, int dim = 3) {
  InnovationInfo info;
  info.valid = true;
  info.dim = dim;
  info.nis = nis;
  info.y = Mat(dim, 1);
  info.S = Mat::Identity(dim);
  info.H = Mat(dim, 15);
  info.R = Mat::Identity(dim);
  return info;
}

IntegrityManager makeManager(IntegrityConfig config = {}) {
  IntegrityManager m;
  RunwayScene scene;
  scene.heading_rad = 140.0 * kDegToRad;
  m.configure(config, scene);
  return m;
}

}  // namespace

TEST(Integrity, ThresholdsComeFromTheStatedFalseAlertProbability) {
  IntegrityConfig c;
  c.gate_probability_false_alert = 0.001;
  IntegrityManager m = makeManager(c);
  EXPECT_NEAR(m.gateThresholdFor(3), 16.2662, 1e-3);
  EXPECT_NEAR(m.gateThresholdFor(1), 10.8276, 1e-3);
}

TEST(Integrity, ExplicitThresholdOverridesTheProbability) {
  IntegrityConfig c;
  c.explicit_gate_threshold = 42.0;
  IntegrityManager m = makeManager(c);
  EXPECT_DOUBLE_EQ(m.gateThresholdFor(3), 42.0);
  EXPECT_DOUBLE_EQ(m.gateThresholdFor(1), 42.0);
}

// INT-005: one outlier must not isolate a source.
TEST(Integrity, SingleOutlierDoesNotChangeState) {
  IntegrityManager m = makeManager();
  m.beginTick(0.0);
  const IntegrityDecision d = m.evaluate(gnss(0.0), innovation(1000.0), 0.0);
  EXPECT_TRUE(d.accept);
  EXPECT_EQ(m.stateOf(SensorId::kGnss), SensorState::kActive);
  EXPECT_EQ(d.reason, IntegrityReason::kCrossCheckInertial);
}

// VNV-013: the full ACTIVE -> SUSPECT -> ISOLATED -> ACTIVE cycle.
TEST(Integrity, FullTransitionCycle) {
  IntegrityConfig c;
  c.suspect_persistence_updates = 3;
  c.isolate_persistence_s = 1.0;
  c.recovery_window_s = 4.0;
  IntegrityManager m = makeManager(c);

  double t = 0.0;
  for (int i = 0; i < 3; ++i) {
    t += 0.2;
    m.beginTick(t);
    m.evaluate(gnss(t, i), innovation(500.0), t);
  }
  EXPECT_EQ(m.stateOf(SensorId::kGnss), SensorState::kSuspect);

  // Still SUSPECT before the persistence window elapses.
  t += 0.2;
  m.beginTick(t);
  m.evaluate(gnss(t, 10), innovation(500.0), t);
  EXPECT_EQ(m.stateOf(SensorId::kGnss), SensorState::kSuspect);

  // Past the window: ISOLATED, and the measurement is refused.
  t += 1.2;
  m.beginTick(t);
  const IntegrityDecision d = m.evaluate(gnss(t, 20), innovation(500.0), t);
  EXPECT_EQ(m.stateOf(SensorId::kGnss), SensorState::kIsolated);
  EXPECT_FALSE(d.accept);

  // INT-007: normal residuals alone do not reinstate it before the recovery
  // window has elapsed.
  for (int i = 0; i < 5; ++i) {
    t += 0.2;
    m.beginTick(t);
    m.evaluate(gnss(t, 30 + i), innovation(0.5), t);
    EXPECT_EQ(m.stateOf(SensorId::kGnss), SensorState::kIsolated);
  }
  t += 4.0;
  m.beginTick(t);
  m.evaluate(gnss(t, 100), innovation(0.5), t);
  EXPECT_EQ(m.stateOf(SensorId::kGnss), SensorState::kActive);

  // INT-008 / INT-012: every transition carries a reason, a statistic and a
  // threshold.
  ASSERT_GE(m.events().size(), 3u);
  for (const IntegrityEvent& e : m.events()) {
    EXPECT_NE(e.reason, IntegrityReason::kNone);
    EXPECT_NE(e.from, e.to);
    EXPECT_GE(e.t_s, 0.0);
  }
}

// INT-010: monitor-only computes everything and refuses nothing.
TEST(Integrity, MonitorOnlyNeverRefuses) {
  IntegrityConfig c;
  c.monitor_only = true;
  c.suspect_persistence_updates = 1;
  IntegrityManager m = makeManager(c);
  double t = 0.0;
  for (int i = 0; i < 20; ++i) {
    t += 0.2;
    m.beginTick(t);
    const IntegrityDecision d = m.evaluate(gnss(t, i), innovation(5000.0), t);
    EXPECT_TRUE(d.accept) << "monitor-only must never refuse";
  }
  EXPECT_NE(m.stateOf(SensorId::kGnss), SensorState::kActive) << "it must still track state";
}

TEST(Integrity, DisabledPolicyIsCompletelyInert) {
  IntegrityConfig c;
  c.enabled = false;
  IntegrityManager m = makeManager(c);
  m.beginTick(1.0);
  const IntegrityDecision d = m.evaluate(gnss(1.0), innovation(1e6), 1.0);
  EXPECT_TRUE(d.accept);
  EXPECT_EQ(d.reason, IntegrityReason::kNone);
  EXPECT_TRUE(m.events().empty());
}

// VNV-014 / INT-018: staleness detected by age alone, on a perfectly plausible
// value.
TEST(Integrity, StaleMeasurementIsCaughtByAge) {
  IntegrityConfig c;
  c.stale_timeout_s = 1.0;
  IntegrityManager m = makeManager(c);
  m.beginTick(0.0);
  m.evaluate(gnss(0.0), innovation(0.5), 0.0);
  EXPECT_EQ(m.stateOf(SensorId::kGnss), SensorState::kActive);

  // A frozen sample: the value is fine, the timestamp is 2 s old.
  m.beginTick(2.0);
  const IntegrityDecision d = m.evaluate(gnss(0.0), innovation(0.5), 2.0);
  EXPECT_FALSE(d.accept);
  EXPECT_EQ(d.reason, IntegrityReason::kMeasurementStale);
  EXPECT_EQ(m.stateOf(SensorId::kGnss), SensorState::kSuspect);
}

// INT-009: an unavailable source is UNAVAILABLE, not ISOLATED.
TEST(Integrity, UnavailableIsDistinctFromIsolated) {
  IntegrityManager m = makeManager();
  m.beginTick(0.0);
  m.evaluate(gnss(0.0), innovation(0.5), 0.0);

  Measurement gone = gnss(1.0);
  gone.header.validity = Validity::kUnavailable;
  m.beginTick(1.0);
  const IntegrityDecision d = m.evaluate(gone, innovation(0.5), 1.0);
  EXPECT_FALSE(d.accept);
  EXPECT_EQ(d.reason, IntegrityReason::kSourceUnavailable);
  EXPECT_EQ(m.stateOf(SensorId::kGnss), SensorState::kUnavailable);
  ASSERT_FALSE(m.events().empty());
  EXPECT_EQ(m.events().back().to, SensorState::kUnavailable);
}

TEST(Integrity, TimeoutMarksASilentSourceUnavailable) {
  IntegrityConfig c;
  c.unavailable_timeout_s = 2.0;
  IntegrityManager m = makeManager(c);
  m.beginTick(0.0);
  m.evaluate(gnss(0.0), innovation(0.5), 0.0);
  m.beginTick(1.0);
  EXPECT_EQ(m.stateOf(SensorId::kGnss), SensorState::kActive);
  m.beginTick(5.0);
  EXPECT_EQ(m.stateOf(SensorId::kGnss), SensorState::kUnavailable);
}

// INT-016 / SCN-005: the velocity channel gets its own reason code.
TEST(Integrity, VelocityInconsistencyHasItsOwnReason) {
  IntegrityConfig c;
  c.suspect_persistence_updates = 2;
  IntegrityManager m = makeManager(c);
  Measurement vel = gnss(0.0);
  vel.type = MeasurementType::kGnssVelocity;
  double t = 0.0;
  IntegrityDecision d;
  for (int i = 0; i < 3; ++i) {
    t += 0.2;
    vel.header.sample_time_s = t;
    m.beginTick(t);
    d = m.evaluate(vel, innovation(900.0), t);
  }
  EXPECT_EQ(d.reason, IntegrityReason::kVelocityInconsistent);
  EXPECT_EQ(m.stateOf(SensorId::kGnss), SensorState::kSuspect);
}

TEST(Integrity, VisionQualityFloorIsEnforced) {
  IntegrityConfig c;
  c.vision_min_quality = 0.3;
  IntegrityManager m = makeManager(c);
  Measurement v;
  v.type = MeasurementType::kVisionRelative;
  v.dim = 3;
  v.header.sensor = SensorId::kVision;
  v.header.sample_time_s = 1.0;
  v.quality = 0.1;
  m.beginTick(1.0);
  const IntegrityDecision d = m.evaluate(v, innovation(0.4), 1.0);
  EXPECT_FALSE(d.accept);
  EXPECT_EQ(d.reason, IntegrityReason::kQualityBelowThreshold);
}

TEST(Integrity, InvalidInnovationIsRefused) {
  IntegrityManager m = makeManager();
  InnovationInfo bad;
  bad.valid = false;
  bad.rejection_hint = IntegrityReason::kInnovationCovarianceInvalid;
  m.beginTick(1.0);
  const IntegrityDecision d = m.evaluate(gnss(1.0), bad, 1.0);
  EXPECT_FALSE(d.accept);
  EXPECT_EQ(d.reason, IntegrityReason::kInnovationCovarianceInvalid);
}

// DEV-005: the separation test drives the same state machine.
TEST(Integrity, SolutionSeparationIsolatesWhenPersistent) {
  IntegrityConfig c;
  c.enable_solution_separation = true;
  c.isolate_persistence_s = 1.0;
  IntegrityManager m = makeManager(c);
  m.beginTick(0.0);
  m.evaluate(gnss(0.0), innovation(0.5), 0.0);

  const double above = m.separationThreshold() * 10.0;
  m.submitSolutionSeparation(above, 80.0, 2, 1.0);
  EXPECT_EQ(m.stateOf(SensorId::kGnss), SensorState::kSuspect);
  m.submitSolutionSeparation(above, 90.0, 2, 2.5);
  EXPECT_EQ(m.stateOf(SensorId::kGnss), SensorState::kIsolated);
  EXPECT_EQ(m.events().back().reason, IntegrityReason::kSolutionSeparation);
  EXPECT_NEAR(m.lastSeparation_m(), 90.0, 1e-12);
}

TEST(Integrity, SolutionSeparationIsInertWhenDisabled) {
  IntegrityManager m = makeManager();  // disabled by default
  m.beginTick(0.0);
  m.evaluate(gnss(0.0), innovation(0.5), 0.0);
  m.submitSolutionSeparation(1e6, 500.0, 2, 1.0);
  EXPECT_EQ(m.stateOf(SensorId::kGnss), SensorState::kActive);
}

// INT-013 / INT-014.
TEST(Integrity, NavigationModeDegradesHonestly) {
  IntegrityConfig c;
  c.low_confidence_fix_age_s = 6.0;
  c.unsafe_fix_age_s = 20.0;
  IntegrityManager m = makeManager(c);
  m.beginTick(0.0);
  m.evaluate(gnss(0.0), innovation(0.5), 0.0);
  EXPECT_EQ(m.navigationMode(1.0, 0.5), NavMode::kNormal);

  Measurement gone = gnss(1.0);
  gone.header.validity = Validity::kUnavailable;
  m.beginTick(1.0);
  m.evaluate(gone, innovation(0.5), 1.0);
  EXPECT_EQ(m.navigationMode(2.0, 1.0), NavMode::kDeadReckoning);
  EXPECT_EQ(m.navigationMode(10.0, 9.0), NavMode::kLowConfidence);
  EXPECT_EQ(m.navigationMode(30.0, 25.0), NavMode::kUnsafe);
}

// INT-015 / section 6.7: the trust score is presentation only and must never
// be the thing that decides.
TEST(Integrity, TrustScoreIsBoundedAndFollowsState) {
  IntegrityManager m = makeManager();
  EXPECT_DOUBLE_EQ(m.trustScore(SensorId::kGnss, 0.0), 0.0) << "unseen source has no trust";
  m.beginTick(0.0);
  m.evaluate(gnss(0.0), innovation(0.2), 0.0);
  const double healthy = m.trustScore(SensorId::kGnss, 0.0);
  EXPECT_GT(healthy, 0.5);
  EXPECT_LE(healthy, 1.0);

  Measurement gone = gnss(1.0);
  gone.header.validity = Validity::kUnavailable;
  m.beginTick(1.0);
  m.evaluate(gone, innovation(0.2), 1.0);
  EXPECT_DOUBLE_EQ(m.trustScore(SensorId::kGnss, 1.0), 0.0);
}

TEST(Integrity, ResetClearsEverything) {
  IntegrityManager m = makeManager();
  for (int i = 0; i < 10; ++i) {
    m.beginTick(i * 0.2);
    m.evaluate(gnss(i * 0.2, i), innovation(9000.0), i * 0.2);
  }
  EXPECT_NE(m.stateOf(SensorId::kGnss), SensorState::kActive);
  m.reset();
  EXPECT_EQ(m.stateOf(SensorId::kGnss), SensorState::kActive);
  EXPECT_TRUE(m.events().empty());
}

// INT-021 / SCN-013.
TEST(Raim, DetectsAndExcludesASingleOutlier) {
  std::vector<SyntheticSatellite> satellites;
  for (int i = 0; i < 8; ++i) {
    SyntheticSatellite s;
    s.azimuth_rad = 2.0 * kPi * i / 8.0;
    s.elevation_rad = (20.0 + 50.0 * std::fabs(std::sin(2.1 * i))) * kDegToRad;
    satellites.push_back(s);
  }
  RaimMonitor raim;
  raim.configure(satellites, 3.0, 1e-5);
  ASSERT_TRUE(raim.ready());

  // Consistent epoch: user at the origin with a 100 m clock offset.
  std::vector<double> ranges;
  for (const SyntheticSatellite& s : satellites) {
    (void)s;
    ranges.push_back(100.0);
  }
  RaimResult clean = raim.evaluate(ranges);
  ASSERT_TRUE(clean.computed);
  EXPECT_FALSE(clean.detected);
  EXPECT_EQ(clean.degrees_of_freedom, 4);
  EXPECT_GT(clean.horizontal_protection_proxy_m, 0.0);

  ranges[3] += 60.0;
  const RaimResult faulted = raim.evaluate(ranges);
  ASSERT_TRUE(faulted.computed);
  EXPECT_TRUE(faulted.detected);
  EXPECT_TRUE(faulted.excluded);
  EXPECT_EQ(faulted.excluded_satellite, 3);
}

TEST(Raim, SaysNothingWithoutRedundancy) {
  std::vector<SyntheticSatellite> satellites(4);
  RaimMonitor raim;
  raim.configure(satellites, 3.0, 1e-5);
  const std::vector<double> ranges(4, 100.0);
  EXPECT_FALSE(raim.evaluate(ranges).computed);
}
