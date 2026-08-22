#include "aerolab/core/types.hpp"

#include <cstring>

namespace aerolab {

bool parseSensorId(const char* name, SensorId& out) {
  if (name == nullptr) return false;
  if (std::strcmp(name, "gnss") == 0) {
    out = SensorId::kGnss;
    return true;
  }
  if (std::strcmp(name, "imu") == 0) {
    out = SensorId::kImu;
    return true;
  }
  if (std::strcmp(name, "baro") == 0) {
    out = SensorId::kBaro;
    return true;
  }
  if (std::strcmp(name, "vision") == 0) {
    out = SensorId::kVision;
    return true;
  }
  if (std::strcmp(name, "gnss_pseudorange") == 0) {
    out = SensorId::kGnssPseudorange;
    return true;
  }
  return false;
}

bool parseMissionPhase(const char* name, MissionPhase& out) {
  if (name == nullptr) return false;
  if (std::strcmp(name, "CRUISE") == 0) {
    out = MissionPhase::kCruise;
    return true;
  }
  if (std::strcmp(name, "TURN") == 0) {
    out = MissionPhase::kTurn;
    return true;
  }
  if (std::strcmp(name, "DESCENT") == 0) {
    out = MissionPhase::kDescent;
    return true;
  }
  if (std::strcmp(name, "FINAL_APPROACH") == 0) {
    out = MissionPhase::kFinalApproach;
    return true;
  }
  if (std::strcmp(name, "FLARE") == 0) {
    out = MissionPhase::kFlare;
    return true;
  }
  if (std::strcmp(name, "ROLLOUT") == 0) {
    out = MissionPhase::kRollout;
    return true;
  }
  if (std::strcmp(name, "TAXI") == 0) {
    out = MissionPhase::kTaxi;
    return true;
  }
  return false;
}

bool parseEstimatorId(const char* name, EstimatorId& out) {
  if (name == nullptr) return false;
  if (std::strcmp(name, "gnss_only") == 0) {
    out = EstimatorId::kGnssOnly;
    return true;
  }
  if (std::strcmp(name, "ins_dr") == 0) {
    out = EstimatorId::kInsDeadReckoning;
    return true;
  }
  if (std::strcmp(name, "ekf") == 0) {
    out = EstimatorId::kEkf;
    return true;
  }
  if (std::strcmp(name, "integrity_ekf") == 0) {
    out = EstimatorId::kIntegrityEkf;
    return true;
  }
  if (std::strcmp(name, "solsep_ekf") == 0) {
    out = EstimatorId::kSolutionSeparation;
    return true;
  }
  if (std::strcmp(name, "robust_student_t") == 0) {
    out = EstimatorId::kRobustStudentT;
    return true;
  }
  return false;
}

}  // namespace aerolab
