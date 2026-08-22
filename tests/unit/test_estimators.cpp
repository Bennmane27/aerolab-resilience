// VNV-011, VNV-012, NAV-001..NAV-016 - estimator behaviour on analytic cases.
#include <gtest/gtest.h>

#include <cmath>

#include "aerolab/navigation/baseline_estimators.hpp"
#include "aerolab/navigation/error_state_ekf.hpp"
#include "aerolab/navigation/solution_separation.hpp"

using namespace aerolab;

namespace {

NavSolution seedAt(const Vec3& p, const Vec3& v) {
  NavSolution s;
  s.t_s = 0.0;
  s.position_ned_m = p;
  s.velocity_ned_mps = v;
  s.attitude_body_to_ned = Quat::Identity();
  s.valid = true;
  return s;
}

Measurement imuLevel(double t) {
  // Straight and level: the accelerometer reads -g on the body z axis.
  Measurement m;
  m.type = MeasurementType::kImuSample;
  m.dim = 6;
  m.header.sensor = SensorId::kImu;
  m.header.sample_time_s = t;
  m.header.delivery_time_s = t;
  m.v[0] = 0.0;
  m.v[1] = 0.0;
  m.v[2] = -kGravityMps2;
  m.v[3] = 0.0;
  m.v[4] = 0.0;
  m.v[5] = 0.0;
  return m;
}

Measurement gnssAt(double t, const Vec3& p) {
  Measurement m;
  m.type = MeasurementType::kGnssPosition;
  m.dim = 3;
  m.header.sensor = SensorId::kGnss;
  m.header.sample_time_s = t;
  m.header.delivery_time_s = t;
  m.v[0] = p.x;
  m.v[1] = p.y;
  m.v[2] = p.z;
  return m;
}

EstimatorConfig tightConfig() {
  EstimatorConfig c;
  c.init_sigma_position_m = 5.0;
  c.init_sigma_velocity_mps = 0.5;
  c.gnss_sigma_horizontal_m = 2.0;
  c.gnss_sigma_vertical_m = 3.0;
  return c;
}

}  // namespace

// VNV-011: with a level, bias free IMU the strapdown integration must hold the
// state exactly. Any sign error in the gravity term shows up immediately.
TEST(Ekf, LevelFlightPropagationIsExact) {
  ErrorStateEkf ekf;
  ekf.initialize(tightConfig(), seedAt(Vec3(0, 0, -100), Vec3(50, 0, 0)));
  for (int i = 1; i <= 1000; ++i) ekf.consumeImu(imuLevel(i * 0.01));
  const NavSolution s = ekf.solution();
  EXPECT_NEAR(s.t_s, 10.0, 1e-9);
  EXPECT_NEAR(s.position_ned_m.x, 500.0, 1e-6);
  EXPECT_NEAR(s.position_ned_m.y, 0.0, 1e-9);
  EXPECT_NEAR(s.position_ned_m.z, -100.0, 1e-6);
  EXPECT_NEAR(s.velocity_ned_mps.x, 50.0, 1e-9);
  EXPECT_TRUE(ekf.healthy());
}

TEST(Ekf, CovarianceGrowsUnderPropagationAndStaysSymmetric) {
  ErrorStateEkf ekf;
  ekf.initialize(tightConfig(), seedAt(Vec3::Zero(), Vec3::Zero()));
  const double p0 = ekf.covariance()(0, 0);
  for (int i = 1; i <= 500; ++i) ekf.consumeImu(imuLevel(i * 0.01));
  const Mat P = ekf.covariance();
  EXPECT_GT(P(0, 0), p0);
  EXPECT_TRUE(P.isFinite());
  EXPECT_TRUE(P.hasPositiveDiagonal());
  for (int i = 0; i < P.rows(); ++i) {
    for (int j = 0; j < P.cols(); ++j) EXPECT_NEAR(P(i, j), P(j, i), 1e-15);
  }
}

// A measurement update must reduce the covariance of the updated states.
TEST(Ekf, GnssUpdateShrinksPositionCovarianceAndPullsTheState) {
  ErrorStateEkf ekf;
  ekf.initialize(tightConfig(), seedAt(Vec3(0, 0, -100), Vec3::Zero()));
  const double before = ekf.covariance()(0, 0);
  InnovationInfo info;
  ASSERT_TRUE(ekf.prepareUpdate(gnssAt(0.0, Vec3(10, 0, -100)), info));
  ASSERT_TRUE(info.valid);
  EXPECT_NEAR(info.y(0, 0), 10.0, 1e-12);
  ekf.applyUpdate(gnssAt(0.0, Vec3(10, 0, -100)), info);
  EXPECT_LT(ekf.covariance()(0, 0), before);
  EXPECT_GT(ekf.solution().position_ned_m.x, 0.0);
  EXPECT_LT(ekf.solution().position_ned_m.x, 10.0);
}

// VNV-012: the NIS on a hand-built case must match the closed form.
TEST(Ekf, NisMatchesTheClosedForm) {
  EstimatorConfig c = tightConfig();
  c.init_sigma_position_m = 3.0;    // P_pos = 9
  c.gnss_sigma_horizontal_m = 4.0;  // R_h  = 16
  c.gnss_sigma_vertical_m = 4.0;
  ErrorStateEkf ekf;
  ekf.initialize(c, seedAt(Vec3::Zero(), Vec3::Zero()));

  InnovationInfo info;
  const Measurement m = gnssAt(0.0, Vec3(5.0, 0.0, 0.0));
  ASSERT_TRUE(ekf.prepareUpdate(m, info));
  ASSERT_TRUE(info.valid);
  // S = P + R = 9 + 16 = 25 on each axis; NIS = 25 / 25 = 1.
  EXPECT_NEAR(info.S(0, 0), 25.0, 1e-12);
  EXPECT_NEAR(info.nis, 1.0, 1e-12);
  EXPECT_EQ(info.dim, 3);
}

// DEV-004: prepareUpdate must not change any state. This is the whole reason
// the interface was split, so it gets an explicit test.
TEST(Ekf, PrepareUpdateHasNoSideEffect) {
  ErrorStateEkf ekf;
  ekf.initialize(tightConfig(), seedAt(Vec3(1, 2, 3), Vec3(4, 5, 6)));
  const NavSolution before = ekf.solution();
  const Mat P_before = ekf.covariance();
  InnovationInfo info;
  for (int i = 0; i < 10; ++i) {
    ekf.prepareUpdate(gnssAt(0.0, Vec3(100, 200, 300)), info);
  }
  const NavSolution after = ekf.solution();
  EXPECT_DOUBLE_EQ(before.position_ned_m.x, after.position_ned_m.x);
  EXPECT_DOUBLE_EQ(before.position_ned_m.y, after.position_ned_m.y);
  EXPECT_DOUBLE_EQ(P_before(0, 0), ekf.covariance()(0, 0));
}

TEST(Ekf, BaroUpdatesOnlyTheVerticalChannel) {
  ErrorStateEkf ekf;
  ekf.initialize(tightConfig(), seedAt(Vec3(0, 0, -100), Vec3::Zero()));
  Measurement baro;
  baro.type = MeasurementType::kBaroAltitude;
  baro.dim = 1;
  baro.header.sensor = SensorId::kBaro;
  baro.v[0] = 120.0;  // altitude, i.e. pD = -120
  InnovationInfo info;
  ASSERT_TRUE(ekf.prepareUpdate(baro, info));
  ASSERT_TRUE(info.valid);
  EXPECT_EQ(info.dim, 1);
  EXPECT_NEAR(info.y(0, 0), 20.0, 1e-12);
  ekf.applyUpdate(baro, info);
  EXPECT_LT(ekf.solution().position_ned_m.z, -100.0);  // pushed down (up in altitude)
  EXPECT_NEAR(ekf.solution().position_ned_m.x, 0.0, 1e-9);
}

TEST(Ekf, RejectsMeasurementsOlderThanTheBuffer) {
  EstimatorConfig c = tightConfig();
  c.max_measurement_age_s = 0.5;
  c.rollback_window_s = 0.5;
  ErrorStateEkf ekf;
  ekf.initialize(c, seedAt(Vec3::Zero(), Vec3::Zero()));
  for (int i = 1; i <= 500; ++i) ekf.consumeImu(imuLevel(i * 0.01));  // t = 5 s
  InnovationInfo info;
  ASSERT_TRUE(ekf.prepareUpdate(gnssAt(1.0, Vec3(0, 0, 0)), info));  // 4 s old
  EXPECT_FALSE(info.valid);
  EXPECT_EQ(info.rejection_hint, IntegrityReason::kMeasurementStale);
}

// NAV-009 / SCN-009: a delayed measurement inside the rollback window must be
// reprocessed at its own sample time, not at the current filter time.
TEST(Ekf, DelayedMeasurementIsReprocessedByRollback) {
  EstimatorConfig c = tightConfig();
  c.enable_rollback = true;
  c.rollback_window_s = 2.0;
  c.max_measurement_age_s = 2.0;
  c.gnss_sigma_horizontal_m = 0.01;  // trust the fix almost completely

  ErrorStateEkf ekf;
  ekf.initialize(c, seedAt(Vec3::Zero(), Vec3(50.0, 0.0, 0.0)));
  for (int i = 1; i <= 200; ++i) ekf.consumeImu(imuLevel(i * 0.01));  // t = 2 s, x = 100 m
  EXPECT_NEAR(ekf.solution().position_ned_m.x, 100.0, 1e-3);

  // A fix sampled at t = 1.0 s saying "you were at 60 m" arrives now. At 50 m/s
  // the aircraft was at 50 m then, so the fix implies +10 m. Applied naively at
  // t = 2 s it would drag the state back towards 60 m; reprocessed correctly it
  // shifts the trajectory by about +10 m, leaving the current estimate near
  // 110 m.
  const Measurement delayed = gnssAt(1.0, Vec3(60.0, 0.0, 0.0));
  InnovationInfo info;
  ASSERT_TRUE(ekf.prepareUpdate(delayed, info));
  ASSERT_TRUE(info.valid);
  EXPECT_NEAR(info.y(0, 0), 10.0, 0.5) << "innovation must be built at the sample instant";
  ekf.applyUpdate(delayed, info);

  EXPECT_NEAR(ekf.time_s(), 2.0, 1e-9) << "the filter must end up back at the current time";
  EXPECT_GT(ekf.solution().position_ned_m.x, 100.0);
  EXPECT_NEAR(ekf.solution().position_ned_m.x, 110.0, 3.0);
}

// NAV-015: reset restores exactly the post-initialise state.
TEST(Ekf, ResetRestoresTheInitialState) {
  ErrorStateEkf ekf;
  const NavSolution seed = seedAt(Vec3(7, 8, 9), Vec3(1, 2, 3));
  ekf.initialize(tightConfig(), seed);
  const Mat P0 = ekf.covariance();
  for (int i = 1; i <= 300; ++i) ekf.consumeImu(imuLevel(i * 0.01));
  InnovationInfo info;
  if (ekf.prepareUpdate(gnssAt(3.0, Vec3(500, 500, 500)), info) && info.valid) {
    ekf.applyUpdate(gnssAt(3.0, Vec3(500, 500, 500)), info);
  }
  ekf.reset();
  EXPECT_DOUBLE_EQ(ekf.solution().position_ned_m.x, 7.0);
  EXPECT_DOUBLE_EQ(ekf.solution().velocity_ned_mps.z, 3.0);
  EXPECT_DOUBLE_EQ(ekf.time_s(), 0.0);
  for (int i = 0; i < 15; ++i) EXPECT_DOUBLE_EQ(ekf.covariance()(i, i), P0(i, i));
}

TEST(Ekf, GnssFreeSubFilterIgnoresGnss) {
  ErrorStateEkf sub;
  sub.setGnssEnabled(false);
  sub.initialize(tightConfig(), seedAt(Vec3::Zero(), Vec3::Zero()));
  InnovationInfo info;
  EXPECT_FALSE(sub.prepareUpdate(gnssAt(0.0, Vec3(100, 0, 0)), info));
}

// NAV-A: the baseline must refuse to publish once its only source is stale.
TEST(GnssOnly, GoesUnsafeWhenTheFixExpires) {
  EstimatorConfig c = tightConfig();
  c.gnss_only_timeout_s = 2.0;
  GnssOnlyEstimator est;
  est.initialize(c, seedAt(Vec3::Zero(), Vec3::Zero()));
  EXPECT_EQ(est.solution().mode, NavMode::kInitializing);

  InnovationInfo info;
  const Measurement fix = gnssAt(0.0, Vec3(10, 20, -30));
  ASSERT_TRUE(est.prepareUpdate(fix, info));
  est.applyUpdate(fix, info);
  EXPECT_EQ(est.solution().mode, NavMode::kNormal);
  EXPECT_DOUBLE_EQ(est.solution().position_ned_m.x, 10.0);

  est.predict(3.0);
  EXPECT_EQ(est.solution().mode, NavMode::kUnsafe);
  EXPECT_FALSE(est.solution().valid);
}

TEST(GnssOnly, IgnoresInertialData) {
  GnssOnlyEstimator est;
  est.initialize(tightConfig(), seedAt(Vec3::Zero(), Vec3(50, 0, 0)));
  for (int i = 1; i <= 100; ++i) est.consumeImu(imuLevel(i * 0.01));
  EXPECT_DOUBLE_EQ(est.solution().position_ned_m.x, 0.0);
}

// NAV-B: pure inertial integration, with an uncertainty that grows.
TEST(InsDeadReckoning, IntegratesAndReportsGrowingUncertainty) {
  InsDeadReckoningEstimator est;
  est.initialize(tightConfig(), seedAt(Vec3::Zero(), Vec3(60, 0, 0)));
  const double sigma0 = std::sqrt(est.solution().position_covariance_m2(0, 0));
  for (int i = 1; i <= 1000; ++i) est.consumeImu(imuLevel(i * 0.01));
  const NavSolution s = est.solution();
  EXPECT_NEAR(s.position_ned_m.x, 600.0, 1e-5);
  EXPECT_EQ(s.mode, NavMode::kDeadReckoning);
  EXPECT_GT(std::sqrt(s.position_covariance_m2(0, 0)), sigma0 * 1.5);
  InnovationInfo info;
  EXPECT_FALSE(est.prepareUpdate(gnssAt(1.0, Vec3(0, 0, 0)), info));
}

TEST(InsDeadReckoning, AccelerometerBiasProducesQuadraticDrift) {
  InsDeadReckoningEstimator est;
  est.initialize(tightConfig(), seedAt(Vec3::Zero(), Vec3::Zero()));
  for (int i = 1; i <= 1000; ++i) {
    Measurement m = imuLevel(i * 0.01);
    m.v[0] = 0.2;  // 0.2 m/s^2 of unmodelled bias
    est.consumeImu(m);
  }
  // 0.5 * a * t^2 = 0.5 * 0.2 * 100 = 10 m after 10 s.
  EXPECT_NEAR(est.solution().position_ned_m.x, 10.0, 0.05);
}

// DEV-005: the separation statistic must stay small with a clean GNSS and grow
// once the GNSS drags the main filter away from the sub-filter.
TEST(SolutionSeparation, StatisticGrowsWhenGnssDisagrees) {
  SolutionSeparationEstimator est;
  EstimatorConfig c = tightConfig();
  c.gnss_sigma_horizontal_m = 1.0;
  est.initialize(c, seedAt(Vec3::Zero(), Vec3(50.0, 0.0, 0.0)));

  double statistic = 0.0;
  double separation = 0.0;
  int dof = 0;

  for (int i = 1; i <= 300; ++i) {
    const double t = i * 0.01;
    est.consumeImu(imuLevel(t));
    if (i % 20 == 0) {  // 5 Hz honest fixes
      const Measurement m = gnssAt(t, Vec3(50.0 * t, 0.0, 0.0));
      InnovationInfo info;
      if (est.prepareUpdate(m, info) && info.valid) est.applyUpdate(m, info);
    }
  }
  ASSERT_TRUE(est.horizontalSeparation(statistic, separation, dof));
  EXPECT_EQ(dof, 2);
  const double clean_separation = separation;

  for (int i = 301; i <= 900; ++i) {
    const double t = i * 0.01;
    est.consumeImu(imuLevel(t));
    if (i % 20 == 0) {
      const double drift = 40.0 * (t - 3.0);  // 40 m/s of injected drift
      const Measurement m = gnssAt(t, Vec3(50.0 * t, drift, 0.0));
      InnovationInfo info;
      if (est.prepareUpdate(m, info) && info.valid) est.applyUpdate(m, info);
    }
  }
  double drifted_statistic = 0.0;
  ASSERT_TRUE(est.horizontalSeparation(drifted_statistic, separation, dof));
  EXPECT_GT(separation, clean_separation + 10.0);
  EXPECT_GT(drifted_statistic, statistic);

  NavSolution sub;
  ASSERT_TRUE(est.subSolution(sub));
  EXPECT_LT(std::fabs(sub.position_ned_m.y), std::fabs(est.solution().position_ned_m.y));
}

TEST(SolutionSeparation, SubFilterStillConsumesNonGnssUpdates) {
  SolutionSeparationEstimator est;
  est.initialize(tightConfig(), seedAt(Vec3(0, 0, -100), Vec3::Zero()));
  Measurement baro;
  baro.type = MeasurementType::kBaroAltitude;
  baro.dim = 1;
  baro.header.sensor = SensorId::kBaro;
  baro.v[0] = 130.0;
  InnovationInfo info;
  ASSERT_TRUE(est.prepareUpdate(baro, info));
  est.applyUpdate(baro, info);
  NavSolution sub;
  ASSERT_TRUE(est.subSolution(sub));
  EXPECT_LT(sub.position_ned_m.z, -100.0);
}
