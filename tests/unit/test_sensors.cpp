// VNV-001, SENS-001..SENS-020 - sensor models and the measurement bus.
#include <gtest/gtest.h>

#include <cmath>
#include <map>
#include <vector>

#include "aerolab/sensors/sensor_suite.hpp"
#include "aerolab/truth/ground_truth.hpp"

using namespace aerolab;

namespace {

struct Harness {
  GroundTruthSimulator truth;
  SensorSuite sensors;
  RunwayScene scene;
  std::vector<Measurement> all;

  explicit Harness(std::uint64_t seed = 4242, const SensorSuiteConfig& config = {}) {
    TrajectoryProfile profile;
    parseTrajectoryProfileName("approach_ils_like", profile);
    scene.heading_rad = 140.0 * kDegToRad;
    truth.reset(profile, scene);
    sensors.reset(config, seed, scene);
  }

  void run(double duration_s, double dt = 0.01) {
    std::vector<Measurement> staging;
    const int ticks = static_cast<int>(duration_s / dt);
    for (int i = 0; i <= ticks; ++i) {
      const double t = i * dt;
      staging.clear();
      sensors.sample(truth.sampleAt(t), t, dt, staging);
      for (const Measurement& m : staging) all.push_back(m);
    }
  }

  std::vector<Measurement> ofType(MeasurementType type) const {
    std::vector<Measurement> out;
    for (const Measurement& m : all) {
      if (m.type == type) out.push_back(m);
    }
    return out;
  }
};

}  // namespace

// SENS-001: identity, sequence and both timestamps on every record.
TEST(Sensors, EveryMeasurementCarriesItsHeader) {
  Harness h;
  h.run(10.0);
  ASSERT_FALSE(h.all.empty());
  std::map<int, std::uint64_t> last_sequence;
  for (const Measurement& m : h.all) {
    EXPECT_GE(m.header.sample_time_s, 0.0);
    EXPECT_GE(m.header.delivery_time_s, m.header.sample_time_s);
    EXPECT_GT(m.dim, 0);
    if (m.type == MeasurementType::kImuSample) {
      const int key = static_cast<int>(m.type);
      if (last_sequence.count(key)) {
        EXPECT_EQ(m.header.sequence, last_sequence[key] + 1);
      }
      last_sequence[key] = m.header.sequence;
    }
  }
}

TEST(Sensors, RatesMatchTheConfiguration) {
  SensorSuiteConfig config;
  config.gnss.rate_hz = 5.0;
  config.imu.rate_hz = 100.0;
  config.baro.rate_hz = 20.0;
  Harness h(1, config);
  h.run(10.0);
  // 10 s inclusive of t=0 gives rate*10 + 1 samples.
  EXPECT_EQ(h.ofType(MeasurementType::kGnssPosition).size(), 51u);
  EXPECT_EQ(h.ofType(MeasurementType::kImuSample).size(), 1001u);
  EXPECT_EQ(h.ofType(MeasurementType::kBaroAltitude).size(), 201u);
}

TEST(Sensors, GnssNoiseMatchesTheDeclaredSigma) {
  SensorSuiteConfig config;
  config.gnss.rate_hz = 50.0;
  config.gnss.sigma_position_horizontal_m = 2.0;
  config.vision.enabled = false;
  Harness h(777, config);
  h.run(120.0);
  const std::vector<Measurement> fixes = h.ofType(MeasurementType::kGnssPosition);
  ASSERT_GT(fixes.size(), 5000u);

  double sum = 0.0;
  double sum_sq = 0.0;
  for (const Measurement& m : fixes) {
    const TruthState truth = h.truth.sampleAt(m.header.sample_time_s);
    const double e = m.v[0] - truth.position_ned_m.x;
    sum += e;
    sum_sq += e * e;
  }
  const double n = static_cast<double>(fixes.size());
  EXPECT_NEAR(sum / n, 0.0, 0.15);
  EXPECT_NEAR(std::sqrt(sum_sq / n), 2.0, 0.1);
}

TEST(Sensors, ImuCarriesTheConfiguredBias) {
  SensorSuiteConfig config;
  config.imu.accel_bias_initial_mps2 = Vec3(0.10, -0.20, 0.30);
  config.imu.accel_bias_walk_mps2_sqrts = 0.0;  // isolate the constant term
  config.imu.gyro_bias_walk_radps_sqrts = 0.0;
  Harness h(31337, config);
  h.run(20.0);
  const std::vector<Measurement> imu = h.ofType(MeasurementType::kImuSample);
  ASSERT_GT(imu.size(), 1000u);
  double sum_x = 0.0;
  for (const Measurement& m : imu) {
    const TruthState truth = h.truth.sampleAt(m.header.sample_time_s);
    sum_x += m.v[0] - truth.specific_force_body_mps2.x;
  }
  EXPECT_NEAR(sum_x / static_cast<double>(imu.size()), 0.10, 0.005);
}

TEST(Sensors, ImuBiasRandomWalkAccumulates) {
  SensorSuiteConfig config;
  config.imu.accel_bias_initial_mps2 = Vec3::Zero();
  config.imu.accel_bias_walk_mps2_sqrts = 0.01;  // deliberately large
  Harness h(5150, config);
  h.run(60.0);
  // After 60 s the random walk standard deviation is 0.01 * sqrt(60) = 0.077.
  EXPECT_GT(h.sensors.trueAccelBias_mps2().norm(), 1e-4);
  EXPECT_LT(h.sensors.trueAccelBias_mps2().norm(), 1.0);
}

TEST(Sensors, BaroIsIndependentOfTheGnssVerticalChannel) {
  SensorSuiteConfig config;
  config.baro.bias_m = 5.0;
  config.baro.bias_drift_m_per_s = 0.0;
  config.baro.sigma_m = 0.01;
  Harness h(11, config);
  h.run(20.0);
  const std::vector<Measurement> baro = h.ofType(MeasurementType::kBaroAltitude);
  ASSERT_FALSE(baro.empty());
  for (const Measurement& m : baro) {
    const TruthState truth = h.truth.sampleAt(m.header.sample_time_s);
    EXPECT_NEAR(m.v[0], truth.altitude_m() + 5.0, 0.1);
  }
}

// SENS-010 / SENS-018: vision reports a quality separate from its value, and
// becomes unavailable outside its geometric envelope rather than lying.
TEST(Sensors, VisionAvailabilityFollowsGeometry) {
  Harness h(99);
  h.run(90.0);
  const std::vector<Measurement> vision = h.ofType(MeasurementType::kVisionRelative);
  ASSERT_FALSE(vision.empty());
  bool saw_unavailable = false;
  bool saw_valid = false;
  bool saw_partial_quality = false;
  for (const Measurement& m : vision) {
    if (m.header.validity == Validity::kUnavailable) {
      saw_unavailable = true;
      EXPECT_DOUBLE_EQ(m.quality, 0.0);
    } else {
      saw_valid = true;
      EXPECT_GT(m.quality, 0.0);
      EXPECT_LE(m.quality, 1.0);
      if (m.quality < 0.99) saw_partial_quality = true;
    }
  }
  EXPECT_TRUE(saw_unavailable) << "vision should be out of range early in the approach";
  EXPECT_TRUE(saw_valid);
  EXPECT_TRUE(saw_partial_quality) << "quality should degrade with range before it is lost";
}

TEST(Sensors, VisionLateralOffsetIsNearZeroOnTheCentreline) {
  Harness h(2718);
  h.run(90.0);
  int checked = 0;
  for (const Measurement& m : h.ofType(MeasurementType::kVisionRelative)) {
    if (m.header.validity != Validity::kValid) continue;
    // Noise is inflated by 1/quality, so only the samples the estimator would
    // actually use carry a meaningful bound. At quality >= 0.5 the lateral
    // sigma is at most 1.6 m, so 8 m is a 5 sigma envelope.
    if (m.quality < 0.5) continue;
    EXPECT_NEAR(m.v[0], 0.0, 8.0);
    ++checked;
  }
  EXPECT_GT(checked, 50) << "no usable vision samples were produced at all";
}

TEST(Sensors, LatencySeparatesSampleFromDelivery) {
  SensorSuiteConfig config;
  config.gnss.latency_s = 0.25;
  Harness h(3, config);
  h.run(5.0);
  for (const Measurement& m : h.ofType(MeasurementType::kGnssPosition)) {
    EXPECT_NEAR(m.header.delivery_time_s - m.header.sample_time_s, 0.25, 1e-12);
  }
}

TEST(Sensors, DisabledSensorsProduceNothing) {
  SensorSuiteConfig config;
  config.gnss.enabled = false;
  config.vision.enabled = false;
  Harness h(4, config);
  h.run(10.0);
  EXPECT_TRUE(h.ofType(MeasurementType::kGnssPosition).empty());
  EXPECT_TRUE(h.ofType(MeasurementType::kVisionRelative).empty());
  EXPECT_FALSE(h.ofType(MeasurementType::kImuSample).empty());
}

TEST(Sensors, PseudorangesCoverTheWholeConstellation) {
  SensorSuiteConfig config;
  config.pseudorange.enabled = true;
  config.pseudorange.satellite_count = 8;
  config.pseudorange.rate_hz = 1.0;
  Harness h(6, config);
  h.run(5.0);
  const std::vector<Measurement> pr = h.ofType(MeasurementType::kPseudorange);
  EXPECT_EQ(pr.size(), 8u * 6u);
  std::vector<bool> seen(8, false);
  for (const Measurement& m : pr) seen[m.header.sequence % 100u] = true;
  for (bool s : seen) EXPECT_TRUE(s);
}

TEST(Sensors, SameSeedProducesIdenticalMeasurements) {
  Harness a(555);
  Harness b(555);
  a.run(20.0);
  b.run(20.0);
  ASSERT_EQ(a.all.size(), b.all.size());
  for (std::size_t i = 0; i < a.all.size(); ++i) {
    for (int k = 0; k < a.all[i].dim; ++k) {
      ASSERT_DOUBLE_EQ(a.all[i].v[static_cast<std::size_t>(k)],
                       b.all[i].v[static_cast<std::size_t>(k)]);
    }
  }
}

TEST(Measurement, NumericValidationMarksNonFinitePayloads) {
  Measurement m;
  m.type = MeasurementType::kGnssPosition;
  m.dim = 3;
  m.v[0] = std::nan("");
  m.validateNumeric();
  EXPECT_EQ(m.header.validity, Validity::kInvalidNumeric);
  EXPECT_FALSE(m.usable());
}

// DEV-002: the bus is ordered by delivery time, which is what makes a latency
// burst representable at all.
TEST(MeasurementBus, DeliversInDeliveryOrderNotSampleOrder) {
  MeasurementBus bus;
  Measurement early;
  early.type = MeasurementType::kGnssPosition;
  early.dim = 3;
  early.header.sensor = SensorId::kGnss;
  early.header.sample_time_s = 1.0;
  early.header.delivery_time_s = 3.0;  // delayed
  early.header.sequence = 1;

  Measurement late;
  late.type = MeasurementType::kBaroAltitude;
  late.dim = 1;
  late.header.sensor = SensorId::kBaro;
  late.header.sample_time_s = 2.0;
  late.header.delivery_time_s = 2.0;  // on time
  late.header.sequence = 2;

  bus.push(early);
  bus.push(late);

  std::vector<Measurement> due;
  bus.popDue(2.0, due);
  ASSERT_EQ(due.size(), 1u);
  EXPECT_EQ(due[0].type, MeasurementType::kBaroAltitude);
  EXPECT_EQ(bus.pending(), 1u);

  bus.popDue(3.0, due);
  ASSERT_EQ(due.size(), 1u);
  EXPECT_EQ(due[0].type, MeasurementType::kGnssPosition);
  // SENS-013: the delayed record still carries its ORIGINAL sample time.
  EXPECT_DOUBLE_EQ(due[0].header.sample_time_s, 1.0);
  EXPECT_EQ(bus.pending(), 0u);
}

TEST(MeasurementBus, OrderIsTotalAndReproducible) {
  MeasurementBus bus;
  for (int i = 5; i >= 0; --i) {
    Measurement m;
    m.type = MeasurementType::kGnssPosition;
    m.dim = 3;
    m.header.sensor = SensorId::kGnss;
    m.header.sample_time_s = 0.0;
    m.header.delivery_time_s = 1.0;  // all identical: tie breakers decide
    m.header.sequence = static_cast<std::uint64_t>(i);
    bus.push(m);
  }
  std::vector<Measurement> due;
  bus.popDue(1.0, due);
  ASSERT_EQ(due.size(), 6u);
  for (std::size_t i = 0; i < due.size(); ++i) EXPECT_EQ(due[i].header.sequence, i);
}
