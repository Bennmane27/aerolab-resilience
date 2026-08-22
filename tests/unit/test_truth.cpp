// VNV-001, SIM-001..SIM-014 - ground truth kinematic consistency.
//
// SIM-013 asks for conservation / consistency tests on simple profiles. The
// strongest such test here is that the analytic velocity really is the
// derivative of the analytic position, and that the reported specific force
// really is the body-frame version of (acceleration - gravity). Those two
// checks catch almost every sign or frame error the trajectory generator could
// contain, which is exactly the class of bug that would silently corrupt every
// downstream metric.
#include <gtest/gtest.h>

#include <cmath>

#include "aerolab/truth/ground_truth.hpp"
#include "aerolab/truth/path.hpp"

using namespace aerolab;

namespace {

GroundTruthSimulator makeSimulator(const char* profile_name) {
  TrajectoryProfile profile;
  EXPECT_TRUE(parseTrajectoryProfileName(profile_name, profile));
  RunwayScene scene;
  scene.heading_rad = 140.0 * kDegToRad;
  GroundTruthSimulator sim;
  sim.reset(profile, scene);
  return sim;
}

}  // namespace

TEST(HorizontalPath, StraightSegmentIsExact) {
  HorizontalPath p;
  p.setStart(0.0, 0.0, 0.0);  // heading north
  p.addStraight(1000.0);
  const PathPoint at500 = p.evaluate(500.0);
  EXPECT_NEAR(at500.north_m, 500.0, 1e-9);
  EXPECT_NEAR(at500.east_m, 0.0, 1e-9);
  EXPECT_NEAR(at500.heading_rad, 0.0, 1e-12);
}

TEST(HorizontalPath, ArcTurnsThroughTheRequestedSweep) {
  HorizontalPath p;
  p.setStart(0.0, 0.0, 0.0);
  p.addArc(1000.0, kPi * 0.5);  // right turn, quarter circle
  const double length = 1000.0 * kPi * 0.5;
  const PathPoint end = p.evaluate(length);
  EXPECT_NEAR(wrapPi(end.heading_rad - kPi * 0.5), 0.0, 1e-9);
  // A quarter circle of radius R starting north and turning right ends at
  // (R, R) relative to the start.
  EXPECT_NEAR(end.north_m, 1000.0, 1e-6);
  EXPECT_NEAR(end.east_m, 1000.0, 1e-6);
  EXPECT_NEAR(p.totalLength_m(), length, 1e-9);
}

TEST(HorizontalPath, TranslationPlacesTheThresholdExactly) {
  HorizontalPath p;
  p.setStart(0.0, 0.0, 1.0);
  p.addStraight(5000.0);
  p.translateSoThat(4000.0, 123.0, -456.0);
  const PathPoint at = p.evaluate(4000.0);
  EXPECT_NEAR(at.north_m, 123.0, 1e-9);
  EXPECT_NEAR(at.east_m, -456.0, 1e-9);
}

// SIM-001 / SIM-002: velocity is the derivative of position, everywhere.
TEST(GroundTruth, VelocityIsTheDerivativeOfPosition) {
  GroundTruthSimulator sim = makeSimulator("approach_ils_like");
  const double h = 1e-4;
  for (double t = 1.0; t < 80.0; t += 3.7) {
    const TruthState before = sim.sampleAt(t - h);
    const TruthState after = sim.sampleAt(t + h);
    const TruthState now = sim.sampleAt(t);
    const Vec3 numeric = (after.position_ned_m - before.position_ned_m) / (2.0 * h);
    EXPECT_NEAR(numeric.x, now.velocity_ned_mps.x, 1e-4) << "at t=" << t;
    EXPECT_NEAR(numeric.y, now.velocity_ned_mps.y, 1e-4) << "at t=" << t;
    EXPECT_NEAR(numeric.z, now.velocity_ned_mps.z, 1e-4) << "at t=" << t;
  }
}

// The accelerometer model is the single easiest place to get a sign wrong.
// f_body must equal R^T (dv/dt - g_ned) with g_ned = (0, 0, +9.80665).
TEST(GroundTruth, SpecificForceMatchesAccelerationMinusGravity) {
  GroundTruthSimulator sim = makeSimulator("approach_ils_like");
  const double h = 1e-3;
  for (double t = 2.0; t < 80.0; t += 5.3) {
    const TruthState before = sim.sampleAt(t - h);
    const TruthState after = sim.sampleAt(t + h);
    const TruthState now = sim.sampleAt(t);
    const Vec3 accel = (after.velocity_ned_mps - before.velocity_ned_mps) / (2.0 * h);
    const Vec3 expected_ned = accel - Vec3(0.0, 0.0, kGravityMps2);
    const Vec3 expected_body = now.attitude_body_to_ned.rotateInverse(expected_ned);
    EXPECT_NEAR(now.specific_force_body_mps2.x, expected_body.x, 2e-3) << "at t=" << t;
    EXPECT_NEAR(now.specific_force_body_mps2.y, expected_body.y, 2e-3) << "at t=" << t;
    EXPECT_NEAR(now.specific_force_body_mps2.z, expected_body.z, 2e-3) << "at t=" << t;
  }
}

TEST(GroundTruth, LevelFlightAccelerometerReadsOneG) {
  GroundTruthSimulator sim = makeSimulator("taxi");
  const TruthState s = sim.sampleAt(10.0);
  EXPECT_NEAR(s.specific_force_body_mps2.x, 0.0, 1e-6);
  EXPECT_NEAR(s.specific_force_body_mps2.y, 0.0, 1e-6);
  EXPECT_NEAR(s.specific_force_body_mps2.z, -kGravityMps2, 1e-6);
  EXPECT_EQ(s.phase, MissionPhase::kTaxi);
  EXPECT_NEAR(s.altitude_m(), 0.0, 1e-12);
}

// SIM-008: speed and acceleration must stay inside the declared envelope.
TEST(GroundTruth, RespectsSpeedAndAccelerationBounds) {
  GroundTruthSimulator sim = makeSimulator("approach_ils_like");
  const double h = 1e-3;
  double max_speed = 0.0;
  double max_accel = 0.0;
  for (double t = 0.0; t < 120.0; t += 0.05) {
    const TruthState s = sim.sampleAt(t);
    max_speed = std::max(max_speed, s.velocity_ned_mps.norm());
    const Vec3 a =
        (sim.sampleAt(t + h).velocity_ned_mps - sim.sampleAt(t - h).velocity_ned_mps) / (2.0 * h);
    max_accel = std::max(max_accel, a.norm());
  }
  EXPECT_LT(max_speed, sim.profile().max_speed_mps);
  EXPECT_LT(max_accel, sim.profile().max_acceleration_mps2);
}

// The flare polynomial is built so that its derivative has a single root at
// touchdown. Height must therefore decrease monotonically all the way down.
TEST(GroundTruth, FlareIsMonotoneAndTouchdownIsSmooth) {
  GroundTruthSimulator sim = makeSimulator("approach_ils_like");
  const double start = sim.flareStartTime_s();
  const double end = sim.touchdownTime_s();
  ASSERT_GT(end, start);
  double previous = sim.sampleAt(start).altitude_m();
  for (double t = start; t <= end; t += 0.02) {
    const double h = sim.sampleAt(t).altitude_m();
    EXPECT_LE(h, previous + 1e-9) << "height increased during the flare at t=" << t;
    previous = h;
  }
  EXPECT_NEAR(sim.sampleAt(end).altitude_m(), 0.0, 1e-6);
  // Vertical speed must be continuous through touchdown: no infinite jerk.
  const double before = sim.sampleAt(end - 1e-3).velocity_ned_mps.z;
  const double after = sim.sampleAt(end + 1e-3).velocity_ned_mps.z;
  EXPECT_NEAR(before, 0.0, 1e-2);
  EXPECT_NEAR(after, 0.0, 1e-9);
}

TEST(GroundTruth, GlideslopeGeometryIsCorrect) {
  GroundTruthSimulator sim = makeSimulator("approach_ils_like");
  // On the glideslope, height / (distance + aim point) must equal tan(gamma).
  for (double t = 5.0; t < sim.flareStartTime_s() - 5.0; t += 7.0) {
    const TruthState s = sim.sampleAt(t);
    const double d = sim.distanceToThresholdAt_m(t);
    const double expected =
        (d + sim.profile().aim_point_beyond_threshold_m) * std::tan(sim.profile().glideslope_rad);
    EXPECT_NEAR(s.altitude_m(), expected, 1e-6) << "at t=" << t;
  }
}

TEST(GroundTruth, TurnProfileBanksIntoTheTurn) {
  GroundTruthSimulator sim = makeSimulator("approach_turn");
  bool saw_turn = false;
  double max_bank = 0.0;
  for (double t = 0.0; t < 100.0; t += 0.25) {
    const TruthState s = sim.sampleAt(t);
    if (s.phase == MissionPhase::kTurn) {
      saw_turn = true;
      max_bank = std::max(max_bank, std::fabs(s.attitude_body_to_ned.roll()));
      // A coordinated turn also produces a non-zero yaw rate.
      EXPECT_GT(std::fabs(s.angular_rate_body_radps.z), 1e-4);
    }
  }
  EXPECT_TRUE(saw_turn);
  EXPECT_GT(max_bank, 5.0 * kDegToRad);
  EXPECT_LT(max_bank, 35.0 * kDegToRad);
}

TEST(GroundTruth, YawFollowsTheGroundTrack) {
  GroundTruthSimulator sim = makeSimulator("approach_ils_like");
  for (double t = 1.0; t < 50.0; t += 6.0) {
    const TruthState s = sim.sampleAt(t);
    const double track = std::atan2(s.velocity_ned_mps.y, s.velocity_ned_mps.x);
    EXPECT_NEAR(wrapPi(s.attitude_body_to_ned.yaw() - track), 0.0, 1e-9);
  }
}

// SYS-003 / NAV-015: a reset must return the object to its initial state.
TEST(GroundTruth, ResetRestoresTheInitialState) {
  GroundTruthSimulator sim = makeSimulator("approach_ils_like");
  const TruthState initial = sim.state();
  for (int i = 0; i < 500; ++i) sim.step(0.01);
  EXPECT_GT(sim.time_s(), 4.0);
  TrajectoryProfile profile;
  parseTrajectoryProfileName("approach_ils_like", profile);
  RunwayScene scene;
  scene.heading_rad = 140.0 * kDegToRad;
  sim.reset(profile, scene);
  EXPECT_DOUBLE_EQ(sim.time_s(), 0.0);
  EXPECT_DOUBLE_EQ(sim.state().position_ned_m.x, initial.position_ned_m.x);
  EXPECT_DOUBLE_EQ(sim.state().position_ned_m.y, initial.position_ned_m.y);
}

TEST(GroundTruth, SteppingMatchesAnalyticSampling) {
  GroundTruthSimulator sim = makeSimulator("approach_ils_like");
  for (int i = 0; i < 1000; ++i) sim.step(0.01);
  const TruthState stepped = sim.state();
  const TruthState sampled = sim.sampleAt(sim.time_s());
  EXPECT_NEAR(stepped.position_ned_m.x, sampled.position_ned_m.x, 1e-9);
  EXPECT_NEAR(stepped.position_ned_m.y, sampled.position_ned_m.y, 1e-9);
  EXPECT_NEAR(stepped.position_ned_m.z, sampled.position_ned_m.z, 1e-9);
}

// SIM-006: a run of at least 10 simulated minutes must stay well defined.
TEST(GroundTruth, TenMinuteRunStaysFinite) {
  GroundTruthSimulator sim = makeSimulator("approach_ils_like");
  for (double t = 0.0; t <= 600.0; t += 1.0) {
    const TruthState s = sim.sampleAt(t);
    ASSERT_TRUE(s.isFinite()) << "at t=" << t;
  }
}

// SIM-012: every instant carries a mission phase, and the approach profile
// visits the phases in a sensible order.
TEST(GroundTruth, MissionPhasesProgress) {
  GroundTruthSimulator sim = makeSimulator("approach_ils_like");
  bool saw_final = false, saw_flare = false, saw_rollout = false, saw_taxi = false;
  for (double t = 0.0; t < 200.0; t += 0.1) {
    switch (sim.sampleAt(t).phase) {
      case MissionPhase::kFinalApproach: saw_final = true; break;
      case MissionPhase::kFlare:
        saw_flare = true;
        EXPECT_TRUE(saw_final);
        break;
      case MissionPhase::kRollout:
        saw_rollout = true;
        EXPECT_TRUE(saw_flare);
        break;
      case MissionPhase::kTaxi: saw_taxi = true; break;
      default: break;
    }
  }
  EXPECT_TRUE(saw_final);
  EXPECT_TRUE(saw_flare);
  EXPECT_TRUE(saw_rollout);
  EXPECT_TRUE(saw_taxi);
}

TEST(GroundTruth, UnknownProfileIsRejected) {
  TrajectoryProfile p;
  EXPECT_FALSE(parseTrajectoryProfileName("does_not_exist", p));
  EXPECT_TRUE(parseTrajectoryProfileName("taxi", p));
  EXPECT_TRUE(parseTrajectoryProfileName("approach_turn", p));
}

// Regression guard for the defect that broke SCN-014: a curvature step at a
// segment boundary makes the coordinated-turn bank angle jump instantaneously,
// which implies an infinite roll rate. No gyroscope can report that, so any
// estimator fed by such a truth inherits a permanent attitude error at the turn
// entry. The bank is now derived from a curvature blended over the roll-in
// distance; this test bounds the resulting body rates.
TEST(GroundTruth, BodyRatesStayBoundedThroughTheTurn) {
  GroundTruthSimulator sim = makeSimulator("approach_turn");
  double max_rate = 0.0;
  double max_roll_rate = 0.0;
  double previous_roll = sim.sampleAt(0.0).attitude_body_to_ned.roll();
  const double dt = 0.01;
  for (double t = dt; t < 120.0; t += dt) {
    const TruthState s = sim.sampleAt(t);
    max_rate = std::max(max_rate, s.angular_rate_body_radps.norm());
    const double roll = s.attitude_body_to_ned.roll();
    max_roll_rate = std::max(max_roll_rate, std::fabs(roll - previous_roll) / dt);
    previous_roll = roll;
    ASSERT_TRUE(s.isFinite()) << "at t=" << t;
  }
  // A transport aircraft rolls at well under 30 deg/s and pitches slowly.
  EXPECT_LT(max_rate, 30.0 * kDegToRad) << "body rate spike: the attitude is not differentiable";
  EXPECT_LT(max_roll_rate, 20.0 * kDegToRad);
  EXPECT_GT(max_roll_rate, 0.5 * kDegToRad) << "the aircraft never actually rolled";
}

TEST(HorizontalPath, SmoothedCurvatureIsContinuousAcrossBoundaries) {
  HorizontalPath p;
  p.setStart(0.0, 0.0, 0.0);
  p.addStraight(1000.0);
  p.addArc(1400.0, kPi * 0.5);
  p.addStraight(1000.0);

  const double blend = 210.0;  // 3 s at 70 m/s
  double previous = p.curvatureSmoothed(0.0, blend);
  double worst_jump = 0.0;
  for (double s = 0.5; s < p.totalLength_m(); s += 0.5) {
    const double k = p.curvatureSmoothed(s, blend);
    worst_jump = std::max(worst_jump, std::fabs(k - previous));
    previous = k;
  }
  // The raw curvature jumps by 1/1400 at once; smoothed it must arrive
  // gradually, in steps far below that.
  EXPECT_LT(worst_jump, (1.0 / 1400.0) * 0.05);
  // And it must still reach the full arc curvature in the middle of the turn.
  EXPECT_NEAR(p.curvatureSmoothed(1000.0 + 1100.0, blend), 1.0 / 1400.0, 1e-9);
  EXPECT_NEAR(p.curvatureSmoothed(200.0, blend), 0.0, 1e-12);
}
