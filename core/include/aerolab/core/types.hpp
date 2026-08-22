// AEROLAB RESILIENCE - shared enumerations and stable string mappings.
//
// Every identifier in this file is part of the telemetry contract (DATA-002,
// API-004). Enumerator values are stable: append only, never renumber, because
// recorded runs and golden files depend on the string forms below.
#pragma once

#include <cstdint>

namespace aerolab {

// --- Sensors -----------------------------------------------------------------

enum class SensorId : int {
  kGnss = 0,
  kImu = 1,
  kBaro = 2,
  kVision = 3,
  kGnssPseudorange = 4,
  kCount = 5,
};

inline const char* toString(SensorId id) {
  switch (id) {
    case SensorId::kGnss: return "gnss";
    case SensorId::kImu: return "imu";
    case SensorId::kBaro: return "baro";
    case SensorId::kVision: return "vision";
    case SensorId::kGnssPseudorange: return "gnss_pseudorange";
    case SensorId::kCount: break;
  }
  return "unknown";
}

bool parseSensorId(const char* name, SensorId& out);

// --- Measurements ------------------------------------------------------------

enum class MeasurementType : int {
  kGnssPosition = 0,    // NED position, m,      dim 3
  kGnssVelocity = 1,    // NED velocity, m/s,    dim 3
  kImuSample = 2,       // specific force m/s^2 + angular rate rad/s, dim 6
  kBaroAltitude = 3,    // altitude above field, m, dim 1
  kVisionRelative = 4,  // runway-frame lateral m, longitudinal m, heading rad, dim 3
  kPseudorange = 5,     // range m, dim 1 (extension)
};

inline const char* toString(MeasurementType t) {
  switch (t) {
    case MeasurementType::kGnssPosition: return "gnss_position";
    case MeasurementType::kGnssVelocity: return "gnss_velocity";
    case MeasurementType::kImuSample: return "imu_sample";
    case MeasurementType::kBaroAltitude: return "baro_altitude";
    case MeasurementType::kVisionRelative: return "vision_relative";
    case MeasurementType::kPseudorange: return "pseudorange";
  }
  return "unknown";
}

// SENS-014: an accidentally produced NaN/Inf must be detected and marked
// invalid BEFORE it reaches an estimator. SENS-013 / DATA-004: an invalid
// measurement stays in the log, it is never silently dropped.
enum class Validity : int {
  kValid = 0,
  kUnavailable = 1,     // source declared itself absent (FI-001)
  kInvalidNumeric = 2,  // NaN or Inf detected at the sensor boundary
  kDropped = 3,         // probabilistic drop (FI-007)
};

inline const char* toString(Validity v) {
  switch (v) {
    case Validity::kValid: return "valid";
    case Validity::kUnavailable: return "unavailable";
    case Validity::kInvalidNumeric: return "invalid_numeric";
    case Validity::kDropped: return "dropped";
  }
  return "unknown";
}

// --- Integrity ---------------------------------------------------------------

// INT-009: ISOLATED (the integrity policy excluded a source that is still
// transmitting) is a different thing from UNAVAILABLE (the source stopped
// producing). Conflating them is the classic mistake this project is about.
enum class SensorState : int {
  kActive = 0,
  kSuspect = 1,
  kIsolated = 2,
  kUnavailable = 3,
};

inline const char* toString(SensorState s) {
  switch (s) {
    case SensorState::kActive: return "ACTIVE";
    case SensorState::kSuspect: return "SUSPECT";
    case SensorState::kIsolated: return "ISOLATED";
    case SensorState::kUnavailable: return "UNAVAILABLE";
  }
  return "UNKNOWN";
}

// INT-008: every state transition carries a stable reason code so a decision
// can be audited months later from the telemetry alone.
enum class IntegrityReason : int {
  kNone = 0,
  kNisAboveThreshold = 1,
  kNisPersistent = 2,
  kNisNormalCleared = 3,
  kRecoveryWindowElapsed = 4,
  kMeasurementStale = 5,
  kSequenceRepeated = 6,
  kSourceUnavailable = 7,
  kSourceReturned = 8,
  kCrossCheckInertial = 9,
  kCrossCheckVision = 10,
  kSolutionSeparation = 11,
  kInnovationCovarianceInvalid = 12,
  kVelocityInconsistent = 13,
  kQualityBelowThreshold = 14,
  kRedundancyInsufficient = 15,
  kManualIsolation = 16,
};

inline const char* toString(IntegrityReason r) {
  switch (r) {
    case IntegrityReason::kNone: return "NONE";
    case IntegrityReason::kNisAboveThreshold: return "NIS_ABOVE_THRESHOLD";
    case IntegrityReason::kNisPersistent: return "NIS_PERSISTENT";
    case IntegrityReason::kNisNormalCleared: return "NIS_NORMAL_CLEARED";
    case IntegrityReason::kRecoveryWindowElapsed: return "RECOVERY_WINDOW_ELAPSED";
    case IntegrityReason::kMeasurementStale: return "MEASUREMENT_STALE";
    case IntegrityReason::kSequenceRepeated: return "SEQUENCE_REPEATED";
    case IntegrityReason::kSourceUnavailable: return "SOURCE_UNAVAILABLE";
    case IntegrityReason::kSourceReturned: return "SOURCE_RETURNED";
    case IntegrityReason::kCrossCheckInertial: return "CROSS_CHECK_INERTIAL";
    case IntegrityReason::kCrossCheckVision: return "CROSS_CHECK_VISION";
    case IntegrityReason::kSolutionSeparation: return "SOLUTION_SEPARATION";
    case IntegrityReason::kInnovationCovarianceInvalid: return "INNOVATION_COVARIANCE_INVALID";
    case IntegrityReason::kVelocityInconsistent: return "VELOCITY_INCONSISTENT";
    case IntegrityReason::kQualityBelowThreshold: return "QUALITY_BELOW_THRESHOLD";
    case IntegrityReason::kRedundancyInsufficient: return "REDUNDANCY_INSUFFICIENT";
    case IntegrityReason::kManualIsolation: return "MANUAL_ISOLATION";
  }
  return "UNKNOWN";
}

// INT-013 / INT-014: when the policy can no longer support its own claim, the
// navigation mode degrades. It never fabricates a "valid" position.
enum class NavMode : int {
  kInitializing = 0,
  kNormal = 1,         // full sensor set, all consistency checks satisfied
  kDegraded = 2,       // at least one source isolated or unavailable
  kDeadReckoning = 3,  // no absolute position source in use
  kLowConfidence = 4,  // redundancy insufficient to support integrity claim
  kUnsafe = 5,         // policy criteria violated; solution must not be trusted
};

inline const char* toString(NavMode m) {
  switch (m) {
    case NavMode::kInitializing: return "INITIALIZING";
    case NavMode::kNormal: return "NORMAL";
    case NavMode::kDegraded: return "DEGRADED";
    case NavMode::kDeadReckoning: return "DEAD_RECKONING";
    case NavMode::kLowConfidence: return "LOW_CONFIDENCE";
    case NavMode::kUnsafe: return "UNSAFE";
  }
  return "UNKNOWN";
}

// --- Mission -----------------------------------------------------------------

enum class MissionPhase : int {
  kCruise = 0,
  kTurn = 1,
  kDescent = 2,
  kFinalApproach = 3,
  kFlare = 4,
  kRollout = 5,
  kTaxi = 6,
};

inline const char* toString(MissionPhase p) {
  switch (p) {
    case MissionPhase::kCruise: return "CRUISE";
    case MissionPhase::kTurn: return "TURN";
    case MissionPhase::kDescent: return "DESCENT";
    case MissionPhase::kFinalApproach: return "FINAL_APPROACH";
    case MissionPhase::kFlare: return "FLARE";
    case MissionPhase::kRollout: return "ROLLOUT";
    case MissionPhase::kTaxi: return "TAXI";
  }
  return "UNKNOWN";
}

bool parseMissionPhase(const char* name, MissionPhase& out);

// --- Estimators --------------------------------------------------------------

enum class EstimatorId : int {
  kGnssOnly = 0,            // NAV-A
  kInsDeadReckoning = 1,    // NAV-B
  kEkf = 2,                 // NAV-C
  kIntegrityEkf = 3,        // NAV-D
  kSolutionSeparation = 4,  // NAV-F (promoted from INT-022, see docs/deviations.md)
  kRobustStudentT = 5,      // NAV-E
};

inline const char* toString(EstimatorId e) {
  switch (e) {
    case EstimatorId::kGnssOnly: return "gnss_only";
    case EstimatorId::kInsDeadReckoning: return "ins_dr";
    case EstimatorId::kEkf: return "ekf";
    case EstimatorId::kIntegrityEkf: return "integrity_ekf";
    case EstimatorId::kSolutionSeparation: return "solsep_ekf";
    case EstimatorId::kRobustStudentT: return "robust_student_t";
  }
  return "unknown";
}

bool parseEstimatorId(const char* name, EstimatorId& out);

// Physical constants -----------------------------------------------------------
inline constexpr double kGravityMps2 = 9.80665;  // WGS-84 normal gravity, Down positive in NED

}  // namespace aerolab
