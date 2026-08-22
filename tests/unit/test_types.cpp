// AEROLAB RESILIENCE - the identifier vocabulary.
//
// These strings are not labels. They are the tokens the telemetry writes, the
// manifests record, the acceptance blocks match against and the Web Lab is
// forbidden from translating, precisely so that a reader comparing the screen
// against a JSON file sees the same word. Renaming one is a schema change, and
// nothing else in the suite would notice.
//
// Two properties are pinned here.
//
//  1. EXHAUSTIVENESS. Every enumerator maps to a distinct string that is not the
//     fallback. Adding a member to an enum without extending its switch is the
//     one mistake these conversions invite, and it fails silently: the new value
//     serialises as "unknown" and every downstream consumer sees a source, a
//     state or a reason it cannot name.
//
//  2. THE EXACT SPELLING, for the enums that cross the boundary. A round trip
//     alone would not catch renaming both sides at once.
#include <gtest/gtest.h>

#include <map>
#include <set>
#include <string>

#include "aerolab/core/types.hpp"

using namespace aerolab;

namespace {

/// Every value maps to something, nothing maps to the fallback, no duplicates.
template <typename Enum>
void expectTotalAndDistinct(const std::set<Enum>& values, const char* fallback) {
  std::set<std::string> seen;
  for (Enum v : values) {
    const std::string s = toString(v);
    EXPECT_NE(s, fallback) << "enumerator " << static_cast<int>(v)
                           << " has no case in its switch, so it serialises as the fallback";
    EXPECT_TRUE(seen.insert(s).second) << "two enumerators both serialise as '" << s << "'";
  }
  EXPECT_EQ(seen.size(), values.size());
}

}  // namespace

TEST(Types, EverySensorIdSerialisesToItsDocumentedToken) {
  const std::map<SensorId, std::string> expected{
      {SensorId::kGnss, "gnss"},
      {SensorId::kImu, "imu"},
      {SensorId::kBaro, "baro"},
      {SensorId::kVision, "vision"},
      {SensorId::kGnssPseudorange, "gnss_pseudorange"},
  };
  for (const auto& [id, token] : expected) EXPECT_EQ(toString(id), token);

  // kCount is a sentinel, not a source: it must not acquire a name.
  EXPECT_STREQ(toString(SensorId::kCount), "unknown");
}

TEST(Types, SensorIdRoundTripsAndRejectsAnythingElse) {
  for (SensorId id : {SensorId::kGnss, SensorId::kImu, SensorId::kBaro, SensorId::kVision,
                      SensorId::kGnssPseudorange}) {
    SensorId back{};
    ASSERT_TRUE(parseSensorId(toString(id), back)) << toString(id);
    EXPECT_EQ(back, id);
  }

  SensorId out = SensorId::kImu;
  EXPECT_FALSE(parseSensorId("GNSS", out));   // the tokens are case sensitive
  EXPECT_FALSE(parseSensorId("gnss ", out));  // and not trimmed
  EXPECT_FALSE(parseSensorId("", out));
  EXPECT_FALSE(parseSensorId("magnetometer", out));
  EXPECT_FALSE(parseSensorId(nullptr, out));
  EXPECT_EQ(out, SensorId::kImu) << "a rejected parse must leave the output alone";
}

TEST(Types, EveryMeasurementTypeSerialisesToItsDocumentedToken) {
  const std::map<MeasurementType, std::string> expected{
      {MeasurementType::kGnssPosition, "gnss_position"},
      {MeasurementType::kGnssVelocity, "gnss_velocity"},
      {MeasurementType::kImuSample, "imu_sample"},
      {MeasurementType::kBaroAltitude, "baro_altitude"},
      {MeasurementType::kVisionRelative, "vision_relative"},
      {MeasurementType::kPseudorange, "pseudorange"},
  };
  for (const auto& [t, token] : expected) EXPECT_EQ(toString(t), token);
  expectTotalAndDistinct<MeasurementType>(
      {MeasurementType::kGnssPosition, MeasurementType::kGnssVelocity, MeasurementType::kImuSample,
       MeasurementType::kBaroAltitude, MeasurementType::kVisionRelative,
       MeasurementType::kPseudorange},
      "unknown");
}

TEST(Types, ValidityDistinguishesAbsentFromNumericallyBroken) {
  // SENS-014 and FI-001: "the source said nothing", "the source produced a NaN"
  // and "the sample was dropped" are three different events, and the telemetry
  // has to be able to tell them apart after the fact.
  EXPECT_STREQ(toString(Validity::kValid), "valid");
  EXPECT_STREQ(toString(Validity::kUnavailable), "unavailable");
  EXPECT_STREQ(toString(Validity::kInvalidNumeric), "invalid_numeric");
  EXPECT_STREQ(toString(Validity::kDropped), "dropped");
  expectTotalAndDistinct<Validity>(
      {Validity::kValid, Validity::kUnavailable, Validity::kInvalidNumeric, Validity::kDropped},
      "unknown");
}

TEST(Types, SensorStateKeepsIsolatedApartFromUnavailable) {
  // INT-009. Conflating a source the policy EXCLUDED with a source that STOPPED
  // is the mistake this whole project exists to measure, so the two words are
  // pinned rather than merely required to differ.
  EXPECT_STREQ(toString(SensorState::kActive), "ACTIVE");
  EXPECT_STREQ(toString(SensorState::kSuspect), "SUSPECT");
  EXPECT_STREQ(toString(SensorState::kIsolated), "ISOLATED");
  EXPECT_STREQ(toString(SensorState::kUnavailable), "UNAVAILABLE");
  expectTotalAndDistinct<SensorState>({SensorState::kActive, SensorState::kSuspect,
                                       SensorState::kIsolated, SensorState::kUnavailable},
                                      "UNKNOWN");
}

TEST(Types, EveryIntegrityReasonHasAStableCode) {
  // INT-008: a decision has to be auditable months later from the telemetry
  // alone, which is only true while these codes are total and stable.
  const std::map<IntegrityReason, std::string> expected{
      {IntegrityReason::kNone, "NONE"},
      {IntegrityReason::kNisAboveThreshold, "NIS_ABOVE_THRESHOLD"},
      {IntegrityReason::kNisPersistent, "NIS_PERSISTENT"},
      {IntegrityReason::kNisNormalCleared, "NIS_NORMAL_CLEARED"},
      {IntegrityReason::kRecoveryWindowElapsed, "RECOVERY_WINDOW_ELAPSED"},
      {IntegrityReason::kMeasurementStale, "MEASUREMENT_STALE"},
      {IntegrityReason::kSequenceRepeated, "SEQUENCE_REPEATED"},
      {IntegrityReason::kSourceUnavailable, "SOURCE_UNAVAILABLE"},
      {IntegrityReason::kSourceReturned, "SOURCE_RETURNED"},
      {IntegrityReason::kCrossCheckInertial, "CROSS_CHECK_INERTIAL"},
      {IntegrityReason::kCrossCheckVision, "CROSS_CHECK_VISION"},
      {IntegrityReason::kSolutionSeparation, "SOLUTION_SEPARATION"},
      {IntegrityReason::kInnovationCovarianceInvalid, "INNOVATION_COVARIANCE_INVALID"},
      {IntegrityReason::kVelocityInconsistent, "VELOCITY_INCONSISTENT"},
      {IntegrityReason::kQualityBelowThreshold, "QUALITY_BELOW_THRESHOLD"},
      {IntegrityReason::kRedundancyInsufficient, "REDUNDANCY_INSUFFICIENT"},
      {IntegrityReason::kManualIsolation, "MANUAL_ISOLATION"},
  };
  // 0 .. 16 inclusive, with no gap: a hole would mean an enumerator was dropped
  // and the codes after it silently renumbered.
  ASSERT_EQ(expected.size(), 17u);
  for (int i = 0; i <= 16; ++i) {
    const auto r = static_cast<IntegrityReason>(i);
    ASSERT_EQ(expected.count(r), 1u) << "reason code " << i << " is not accounted for";
    EXPECT_EQ(toString(r), expected.at(r));
  }

  std::set<IntegrityReason> all;
  for (int i = 0; i <= 16; ++i) all.insert(static_cast<IntegrityReason>(i));
  expectTotalAndDistinct<IntegrityReason>(all, "UNKNOWN");
}

TEST(Types, EveryNavModeSerialisesToItsDocumentedToken) {
  const std::map<NavMode, std::string> expected{
      {NavMode::kInitializing, "INITIALIZING"},
      {NavMode::kNormal, "NORMAL"},
      {NavMode::kDegraded, "DEGRADED"},
      {NavMode::kDeadReckoning, "DEAD_RECKONING"},
      {NavMode::kLowConfidence, "LOW_CONFIDENCE"},
      {NavMode::kUnsafe, "UNSAFE"},
  };
  for (const auto& [m, token] : expected) EXPECT_EQ(toString(m), token);
  std::set<NavMode> all;
  for (int i = 0; i <= 5; ++i) all.insert(static_cast<NavMode>(i));
  expectTotalAndDistinct<NavMode>(all, "UNKNOWN");
}

TEST(Types, MissionPhaseRoundTripsThroughItsName) {
  const std::map<MissionPhase, std::string> expected{
      {MissionPhase::kCruise, "CRUISE"},   {MissionPhase::kTurn, "TURN"},
      {MissionPhase::kDescent, "DESCENT"}, {MissionPhase::kFinalApproach, "FINAL_APPROACH"},
      {MissionPhase::kFlare, "FLARE"},     {MissionPhase::kRollout, "ROLLOUT"},
      {MissionPhase::kTaxi, "TAXI"},
  };
  for (const auto& [p, token] : expected) {
    EXPECT_EQ(toString(p), token);
    MissionPhase back{};
    ASSERT_TRUE(parseMissionPhase(token.c_str(), back)) << token;
    EXPECT_EQ(back, p);
  }
  std::set<MissionPhase> all;
  for (int i = 0; i <= 6; ++i) all.insert(static_cast<MissionPhase>(i));
  expectTotalAndDistinct<MissionPhase>(all, "UNKNOWN");

  MissionPhase out = MissionPhase::kCruise;
  EXPECT_FALSE(parseMissionPhase("cruise", out));  // upper case, like the telemetry
  EXPECT_FALSE(parseMissionPhase("GO_AROUND", out));
  EXPECT_FALSE(parseMissionPhase("", out));
  EXPECT_FALSE(parseMissionPhase(nullptr, out));
  EXPECT_EQ(out, MissionPhase::kCruise);
}

TEST(Types, EstimatorIdRoundTripsThroughItsName) {
  // These are the identifiers the scenario files name their estimators with and
  // the manifests report results under, so a rename breaks stored results.
  const std::map<EstimatorId, std::string> expected{
      {EstimatorId::kGnssOnly, "gnss_only"},
      {EstimatorId::kInsDeadReckoning, "ins_dr"},
      {EstimatorId::kEkf, "ekf"},
      {EstimatorId::kIntegrityEkf, "integrity_ekf"},
      {EstimatorId::kSolutionSeparation, "solsep_ekf"},
      {EstimatorId::kRobustStudentT, "robust_student_t"},
  };
  for (const auto& [e, token] : expected) {
    EXPECT_EQ(toString(e), token);
    EstimatorId back{};
    ASSERT_TRUE(parseEstimatorId(token.c_str(), back)) << token;
    EXPECT_EQ(back, e);
  }
  std::set<EstimatorId> all;
  for (int i = 0; i <= 5; ++i) all.insert(static_cast<EstimatorId>(i));
  expectTotalAndDistinct<EstimatorId>(all, "unknown");

  EstimatorId out = EstimatorId::kEkf;
  EXPECT_FALSE(parseEstimatorId("EKF", out));
  EXPECT_FALSE(parseEstimatorId("kalman", out));
  EXPECT_FALSE(parseEstimatorId("", out));
  EXPECT_FALSE(parseEstimatorId(nullptr, out));
  EXPECT_EQ(out, EstimatorId::kEkf);
}
