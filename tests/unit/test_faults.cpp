// VNV-002, FI-001..FI-020, AT-002 - fault injection.
//
// VNV-002 requires, for EVERY fault, a test showing that it changes the
// measurement and never the truth. The truth half of that requirement is
// enforced structurally: FaultInjectionEngine::apply() has no ground truth
// parameter, so there is no handle to modify. TruthIsNeverModified below adds
// the empirical check on top of the structural one.
#include <gtest/gtest.h>

#include <cmath>
#include <string>
#include <vector>

#include "aerolab/faults/fault_engine.hpp"
#include "aerolab/sensors/sensor_suite.hpp"
#include "aerolab/truth/ground_truth.hpp"

using namespace aerolab;

namespace {

Measurement makeGnssPosition(double t, double n = 100.0, double e = 200.0, double d = -300.0) {
  Measurement m;
  m.type = MeasurementType::kGnssPosition;
  m.dim = 3;
  m.header.sensor = SensorId::kGnss;
  m.header.sample_time_s = t;
  m.header.delivery_time_s = t;
  m.header.sequence = static_cast<std::uint64_t>(t * 5.0);
  m.v[0] = n;
  m.v[1] = e;
  m.v[2] = d;
  return m;
}

Measurement makeImu(double t) {
  Measurement m;
  m.type = MeasurementType::kImuSample;
  m.dim = 6;
  m.header.sensor = SensorId::kImu;
  m.header.sample_time_s = t;
  m.header.delivery_time_s = t;
  m.v[0] = 0.1;
  m.v[1] = 0.2;
  m.v[2] = -9.8;
  m.v[3] = 0.01;
  m.v[4] = 0.02;
  m.v[5] = 0.03;
  return m;
}

FaultSpec spec(FaultType type, SensorId target, double start = 10.0, double duration = -1.0) {
  FaultSpec s;
  s.id = std::string("F-") + toString(type);
  s.type = type;
  s.target = target;
  s.start_s = start;
  s.duration_s = duration;
  return s;
}

struct Engine {
  FaultInjectionEngine engine;
  std::vector<FaultEvent> events;
  std::string error;

  bool configure(const std::vector<FaultSpec>& specs) {
    const bool ok = engine.configure(specs, error);
    if (ok) engine.reset(1234);
    return ok;
  }

  std::vector<Measurement> apply(std::vector<Measurement> ms, double t,
                                 MissionPhase phase = MissionPhase::kFinalApproach) {
    engine.apply(ms, t, phase, events);
    return ms;
  }
};

}  // namespace

// AT-002: the strongest test in the suite. A 100 m spoof runs through the whole
// pipeline and the ground truth must be bit-for-bit unchanged.
TEST(FaultEngine, TruthIsNeverModified) {
  TrajectoryProfile profile;
  parseTrajectoryProfileName("approach_ils_like", profile);
  RunwayScene scene;
  scene.heading_rad = 140.0 * kDegToRad;

  GroundTruthSimulator truth;
  truth.reset(profile, scene);
  SensorSuite sensors;
  sensors.reset(SensorSuiteConfig{}, 8080, scene);

  FaultSpec s = spec(FaultType::kGnssPositionStep, SensorId::kGnss, 5.0);
  s.amplitude = Vec3(0.0, 100.0, 0.0);
  Engine engine;
  ASSERT_TRUE(engine.configure({s})) << engine.error;

  std::vector<double> truth_north;
  std::vector<Measurement> staging;
  bool saw_shift = false;
  for (int i = 0; i <= 2000; ++i) {
    const double t = i * 0.01;
    const TruthState state = truth.sampleAt(t);
    truth_north.push_back(state.position_ned_m.x);
    staging.clear();
    sensors.sample(state, t, 0.01, staging);
    engine.engine.apply(staging, t, state.phase, engine.events);
    for (const Measurement& m : staging) {
      if (m.type == MeasurementType::kGnssPosition && t > 6.0) {
        if (std::fabs(m.v[1] - state.position_ned_m.y) > 50.0) saw_shift = true;
      }
    }
  }
  EXPECT_TRUE(saw_shift) << "the spoof never reached the measurements";

  // Re-sample the truth with no fault engine at all and compare bit for bit.
  GroundTruthSimulator clean;
  clean.reset(profile, scene);
  for (std::size_t i = 0; i < truth_north.size(); ++i) {
    const double t = static_cast<double>(i) * 0.01;
    ASSERT_EQ(truth_north[i], clean.sampleAt(t).position_ned_m.x) << "truth changed at t=" << t;
  }
}

TEST(FaultEngine, SourceUnavailable) {
  Engine e;
  ASSERT_TRUE(e.configure({spec(FaultType::kSourceUnavailable, SensorId::kGnss)}));
  EXPECT_EQ(e.apply({makeGnssPosition(5.0)}, 5.0)[0].header.validity, Validity::kValid);
  const Measurement after = e.apply({makeGnssPosition(11.0)}, 11.0)[0];
  EXPECT_EQ(after.header.validity, Validity::kUnavailable);
  EXPECT_FALSE(after.usable());
}

TEST(FaultEngine, BiasStepShiftsEveryComponent) {
  FaultSpec s = spec(FaultType::kBiasStep, SensorId::kGnss);
  s.amplitude = Vec3(1.0, -2.0, 3.0);
  Engine e;
  ASSERT_TRUE(e.configure({s}));
  const Measurement m = e.apply({makeGnssPosition(11.0)}, 11.0)[0];
  EXPECT_DOUBLE_EQ(m.v[0], 101.0);
  EXPECT_DOUBLE_EQ(m.v[1], 198.0);
  EXPECT_DOUBLE_EQ(m.v[2], -297.0);
}

TEST(FaultEngine, RampGrowsLinearlyAndSaturates) {
  FaultSpec s = spec(FaultType::kGnssPositionRamp, SensorId::kGnss, 10.0, 20.0);
  s.amplitude = Vec3(0.0, 100.0, 0.0);
  Engine e;
  ASSERT_TRUE(e.configure({s}));
  e.apply({makeGnssPosition(10.0)}, 10.0);  // arm at t=10
  EXPECT_NEAR(e.apply({makeGnssPosition(15.0)}, 15.0)[0].v[1], 200.0 + 25.0, 1e-9);
  EXPECT_NEAR(e.apply({makeGnssPosition(20.0)}, 20.0)[0].v[1], 200.0 + 50.0, 1e-9);
  EXPECT_NEAR(e.apply({makeGnssPosition(30.0)}, 30.0)[0].v[1], 200.0 + 100.0, 1e-9);
  // Past the window the fault deactivates and the measurement is clean again.
  EXPECT_NEAR(e.apply({makeGnssPosition(31.0)}, 31.0)[0].v[1], 200.0, 1e-9);
}

TEST(FaultEngine, FreezeRepeatsValueAndTimestamp) {
  FaultSpec s = spec(FaultType::kFreeze, SensorId::kGnss);
  s.scalar = 1.0;  // freeze the timestamp too
  Engine e;
  ASSERT_TRUE(e.configure({s}));
  const Measurement first = e.apply({makeGnssPosition(11.0, 10.0, 20.0, -30.0)}, 11.0)[0];
  const Measurement later = e.apply({makeGnssPosition(15.0, 55.0, 66.0, -77.0)}, 15.0)[0];
  EXPECT_DOUBLE_EQ(later.v[0], first.v[0]);
  EXPECT_DOUBLE_EQ(later.v[1], first.v[1]);
  EXPECT_DOUBLE_EQ(later.header.sample_time_s, 11.0);
  // Delivery keeps moving: only the content is stale. That asymmetry is what
  // makes age based detection possible (INT-018).
  EXPECT_DOUBLE_EQ(later.header.delivery_time_s, 15.0);
}

TEST(FaultEngine, LatencyShiftsDeliveryButNotSampleTime) {
  FaultSpec s = spec(FaultType::kLatency, SensorId::kGnss);
  s.scalar = 1.5;
  Engine e;
  ASSERT_TRUE(e.configure({s}));
  const Measurement m = e.apply({makeGnssPosition(12.0)}, 12.0)[0];
  EXPECT_DOUBLE_EQ(m.header.sample_time_s, 12.0);
  EXPECT_DOUBLE_EQ(m.header.delivery_time_s, 13.5);
}

TEST(FaultEngine, NoiseBurstIncreasesScatterWithoutBias) {
  FaultSpec s = spec(FaultType::kNoiseBurst, SensorId::kGnss);
  s.scalar = 10.0;
  Engine e;
  ASSERT_TRUE(e.configure({s}));
  double sum = 0.0;
  double sum_sq = 0.0;
  const int n = 4000;
  for (int i = 0; i < n; ++i) {
    const double t = 11.0 + i * 0.2;
    const double v = e.apply({makeGnssPosition(t)}, t)[0].v[0] - 100.0;
    sum += v;
    sum_sq += v * v;
  }
  EXPECT_NEAR(sum / n, 0.0, 1.0);
  EXPECT_NEAR(std::sqrt(sum_sq / n), 10.0, 0.7);
}

TEST(FaultEngine, DropProbabilisticRampsTowardsTheFinalRate) {
  FaultSpec s = spec(FaultType::kDropProbabilistic, SensorId::kGnss, 10.0, 20.0);
  s.scalar = 0.0;
  s.scalar_final = 1.0;
  Engine e;
  ASSERT_TRUE(e.configure({s}));
  e.apply({makeGnssPosition(10.0)}, 10.0);
  int early_drops = 0;
  int late_drops = 0;
  for (int i = 0; i < 200; ++i) {
    if (e.apply({makeGnssPosition(11.0)}, 11.0)[0].header.validity == Validity::kDropped) {
      ++early_drops;
    }
  }
  for (int i = 0; i < 200; ++i) {
    if (e.apply({makeGnssPosition(29.0)}, 29.0)[0].header.validity == Validity::kDropped) {
      ++late_drops;
    }
  }
  EXPECT_LT(early_drops, 30);
  EXPECT_GT(late_drops, 170);
}

TEST(FaultEngine, ImuAccelAndGyroBiasesHitTheRightTriad) {
  FaultSpec accel = spec(FaultType::kImuAccelBias, SensorId::kImu);
  accel.amplitude = Vec3(0.5, 0.0, 0.0);
  FaultSpec gyro = spec(FaultType::kImuGyroBias, SensorId::kImu);
  gyro.amplitude = Vec3(0.0, 0.0, 0.02);
  Engine e;
  ASSERT_TRUE(e.configure({accel, gyro}));
  const Measurement m = e.apply({makeImu(11.0)}, 11.0)[0];
  EXPECT_DOUBLE_EQ(m.v[0], 0.6);
  EXPECT_DOUBLE_EQ(m.v[1], 0.2);   // untouched
  EXPECT_DOUBLE_EQ(m.v[5], 0.05);  // gyro z
  EXPECT_DOUBLE_EQ(m.v[3], 0.01);  // untouched
}

TEST(FaultEngine, VelocityInconsistencyOnlyTouchesTheVelocityChannel) {
  FaultSpec s = spec(FaultType::kGnssVelocityInconsistency, SensorId::kGnss);
  s.amplitude = Vec3(5.0, -5.0, 0.0);
  Engine e;
  ASSERT_TRUE(e.configure({s}));
  Measurement vel;
  vel.type = MeasurementType::kGnssVelocity;
  vel.dim = 3;
  vel.header.sensor = SensorId::kGnss;
  vel.header.sample_time_s = 11.0;
  vel.v[0] = 60.0;
  vel.v[1] = 10.0;
  vel.v[2] = -3.0;

  const std::vector<Measurement> out = e.apply({makeGnssPosition(11.0), vel}, 11.0);
  EXPECT_DOUBLE_EQ(out[0].v[0], 100.0);  // position untouched: that is the point
  EXPECT_DOUBLE_EQ(out[1].v[0], 65.0);
  EXPECT_DOUBLE_EQ(out[1].v[1], 5.0);
}

TEST(FaultEngine, VisionDegradeLowersQualityWithoutRemovingTheSource) {
  FaultSpec s = spec(FaultType::kVisionDegrade, SensorId::kVision, 10.0, 20.0);
  s.scalar = 0.8;
  s.scalar_final = 0.1;
  s.target = SensorId::kVision;
  Engine e;
  ASSERT_TRUE(e.configure({s}));
  Measurement v;
  v.type = MeasurementType::kVisionRelative;
  v.dim = 3;
  v.header.sensor = SensorId::kVision;
  v.header.sample_time_s = 10.0;
  v.quality = 1.0;
  e.apply({v}, 10.0);

  v.header.sample_time_s = 29.0;
  const Measurement late = e.apply({v}, 29.0)[0];
  EXPECT_LT(late.quality, 0.3);
  EXPECT_GT(late.quality, 0.0);
  EXPECT_EQ(late.header.validity, Validity::kValid) << "degraded is not the same as unavailable";
}

TEST(FaultEngine, PseudorangeOutlierTargetsOneSatellite) {
  FaultSpec s = spec(FaultType::kPseudorangeOutlier, SensorId::kGnssPseudorange);
  s.satellite_index = 3;
  s.scalar = 60.0;
  Engine e;
  ASSERT_TRUE(e.configure({s}));
  std::vector<Measurement> epoch;
  for (int i = 0; i < 8; ++i) {
    Measurement m;
    m.type = MeasurementType::kPseudorange;
    m.dim = 1;
    m.header.sensor = SensorId::kGnssPseudorange;
    m.header.sample_time_s = 11.0;
    m.header.sequence = 100u + static_cast<std::uint64_t>(i);
    m.v[0] = 1000.0;
    epoch.push_back(m);
  }
  const std::vector<Measurement> out = e.apply(epoch, 11.0);
  for (int i = 0; i < 8; ++i) {
    EXPECT_DOUBLE_EQ(out[static_cast<std::size_t>(i)].v[0], i == 3 ? 1060.0 : 1000.0);
  }
}

TEST(FaultEngine, MultipleFaultsCombine) {
  FaultSpec step = spec(FaultType::kGnssPositionStep, SensorId::kGnss);
  step.amplitude = Vec3(10.0, 0.0, 0.0);
  FaultSpec bias = spec(FaultType::kBiasStep, SensorId::kGnss);
  bias.amplitude = Vec3(5.0, 0.0, 0.0);
  Engine e;
  ASSERT_TRUE(e.configure({step, bias}));
  EXPECT_DOUBLE_EQ(e.apply({makeGnssPosition(11.0)}, 11.0)[0].v[0], 115.0);
}

// FI-015: start and end events must be emitted, because M-05 and M-06 measure
// from them.
TEST(FaultEngine, EmitsStartAndEndEvents) {
  Engine e;
  ASSERT_TRUE(e.configure({spec(FaultType::kSourceUnavailable, SensorId::kGnss, 10.0, 5.0)}));
  e.apply({makeGnssPosition(5.0)}, 5.0);
  EXPECT_TRUE(e.events.empty());
  e.apply({makeGnssPosition(10.0)}, 10.0);
  ASSERT_EQ(e.events.size(), 1u);
  EXPECT_TRUE(e.events[0].activated);
  EXPECT_DOUBLE_EQ(e.events[0].t_s, 10.0);
  e.apply({makeGnssPosition(16.0)}, 16.0);
  ASSERT_EQ(e.events.size(), 2u);
  EXPECT_FALSE(e.events[1].activated);
}

// FI-020: phase triggers.
TEST(FaultEngine, PhaseTriggerArmsOnTheMissionPhase) {
  FaultSpec s = spec(FaultType::kSourceUnavailable, SensorId::kGnss, 0.0);
  s.use_phase_trigger = true;
  s.trigger_phase = MissionPhase::kFlare;
  Engine e;
  ASSERT_TRUE(e.configure({s}));
  EXPECT_EQ(e.apply({makeGnssPosition(5.0)}, 5.0, MissionPhase::kFinalApproach)[0].header.validity,
            Validity::kValid);
  EXPECT_EQ(e.apply({makeGnssPosition(60.0)}, 60.0, MissionPhase::kFlare)[0].header.validity,
            Validity::kUnavailable);
}

// FI-016: identical seed and configuration give identical output.
TEST(FaultEngine, IsDeterministic) {
  FaultSpec s = spec(FaultType::kNoiseBurst, SensorId::kGnss);
  s.scalar = 3.0;
  std::vector<double> a, b;
  for (int pass = 0; pass < 2; ++pass) {
    FaultInjectionEngine engine;
    std::string error;
    ASSERT_TRUE(engine.configure({s}, error));
    engine.reset(9090);
    std::vector<FaultEvent> events;
    for (int i = 0; i < 100; ++i) {
      std::vector<Measurement> ms = {makeGnssPosition(11.0 + i * 0.2)};
      engine.apply(ms, 11.0 + i * 0.2, MissionPhase::kFinalApproach, events);
      (pass == 0 ? a : b).push_back(ms[0].v[0]);
    }
  }
  EXPECT_EQ(a, b);
}

// FI-018: incoherent specifications are refused before the run, with a message.
TEST(FaultEngine, RejectsIncoherentSpecifications) {
  FaultInjectionEngine engine;
  std::string error;

  FaultSpec bad_probability = spec(FaultType::kDropProbabilistic, SensorId::kGnss);
  bad_probability.scalar = 1.7;
  EXPECT_FALSE(engine.configure({bad_probability}, error));
  EXPECT_NE(error.find("[0, 1]"), std::string::npos);

  FaultSpec bad_latency = spec(FaultType::kLatency, SensorId::kGnss);
  bad_latency.scalar = -1.0;
  EXPECT_FALSE(engine.configure({bad_latency}, error));

  FaultSpec huge_latency = spec(FaultType::kLatency, SensorId::kGnss);
  huge_latency.scalar = 120.0;
  EXPECT_FALSE(engine.configure({huge_latency}, error));

  FaultSpec ramp_without_duration = spec(FaultType::kGnssPositionRamp, SensorId::kGnss, 5.0, -1.0);
  ramp_without_duration.amplitude = Vec3(0.0, 10.0, 0.0);
  EXPECT_FALSE(engine.configure({ramp_without_duration}, error));

  FaultSpec zero_ramp = spec(FaultType::kGnssPositionRamp, SensorId::kGnss, 5.0, 10.0);
  EXPECT_FALSE(engine.configure({zero_ramp}, error));

  FaultSpec wrong_target = spec(FaultType::kImuAccelBias, SensorId::kGnss);
  EXPECT_FALSE(engine.configure({wrong_target}, error));

  FaultSpec ok = spec(FaultType::kGnssPositionStep, SensorId::kGnss);
  ok.amplitude = Vec3(0.0, 50.0, 0.0);
  EXPECT_TRUE(engine.configure({ok}, error)) << error;
}

TEST(FaultEngine, TypeNamesRoundTrip) {
  const FaultType types[] = {FaultType::kSourceUnavailable, FaultType::kFreeze,
                             FaultType::kBiasStep,          FaultType::kBiasRamp,
                             FaultType::kNoiseBurst,        FaultType::kLatency,
                             FaultType::kDropProbabilistic, FaultType::kGnssPositionStep,
                             FaultType::kGnssPositionRamp,  FaultType::kGnssVelocityInconsistency,
                             FaultType::kImuAccelBias,      FaultType::kImuGyroBias,
                             FaultType::kVisionDegrade,     FaultType::kPseudorangeOutlier};
  for (FaultType t : types) {
    FaultType parsed;
    ASSERT_TRUE(parseFaultType(toString(t), parsed)) << toString(t);
    EXPECT_EQ(parsed, t);
  }
  FaultType unused;
  EXPECT_FALSE(parseFaultType("not_a_fault", unused));
}
