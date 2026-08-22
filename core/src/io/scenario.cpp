#include "aerolab/io/scenario.hpp"

#include <cmath>

#include "aerolab/core/hash.hpp"
#include "aerolab/io/yaml.hpp"

namespace aerolab {

void applyConfigJson(const Json& root, Scenario& scenario);

namespace {

// A document that starts with '{' is JSON; anything else is treated as the
// YAML subset. Both end up in the same Json tree.
bool documentLooksLikeJson(const std::string& text) {
  const std::size_t first = text.find_first_not_of(" \t\r\n");
  return first != std::string::npos && text[first] == '{';
}

void readDouble(const Json& obj, const char* key, double& target) {
  double v = 0.0;
  if (obj.getDouble(key, v)) target = v;
}

void readBool(const Json& obj, const char* key, bool& target) {
  bool v = false;
  if (obj.getBool(key, v)) target = v;
}

void readInt(const Json& obj, const char* key, int& target) {
  int v = 0;
  if (obj.getInt(key, v)) target = v;
}

bool readVec3(const Json& obj, const char* key, Vec3& target) {
  const Json& a = obj[key];
  if (!a.isArray() || a.size() != 3) return false;
  target = Vec3(a.at(0).asDouble(), a.at(1).asDouble(), a.at(2).asDouble());
  return true;
}

bool parseNavMode(const std::string& s, NavMode& out) {
  if (s == "NORMAL") {
    out = NavMode::kNormal;
    return true;
  }
  if (s == "DEGRADED") {
    out = NavMode::kDegraded;
    return true;
  }
  if (s == "DEAD_RECKONING") {
    out = NavMode::kDeadReckoning;
    return true;
  }
  if (s == "LOW_CONFIDENCE") {
    out = NavMode::kLowConfidence;
    return true;
  }
  if (s == "UNSAFE") {
    out = NavMode::kUnsafe;
    return true;
  }
  if (s == "INITIALIZING") {
    out = NavMode::kInitializing;
    return true;
  }
  return false;
}

void readGnss(const Json& j, GnssConfig& c) {
  readBool(j, "enabled", c.enabled);
  readDouble(j, "rate_hz", c.rate_hz);
  readDouble(j, "sigma_position_horizontal_m", c.sigma_position_horizontal_m);
  readDouble(j, "sigma_position_vertical_m", c.sigma_position_vertical_m);
  readDouble(j, "sigma_velocity_mps", c.sigma_velocity_mps);
  readDouble(j, "latency_s", c.latency_s);
  readBool(j, "publish_velocity", c.publish_velocity);
  readVec3(j, "bias_position_ned_m", c.bias_position_ned_m);
}

void readImu(const Json& j, ImuConfig& c) {
  readBool(j, "enabled", c.enabled);
  readDouble(j, "rate_hz", c.rate_hz);
  readDouble(j, "accel_noise_density_mps2_sqrthz", c.accel_noise_density_mps2_sqrthz);
  readDouble(j, "gyro_noise_density_radps_sqrthz", c.gyro_noise_density_radps_sqrthz);
  readDouble(j, "accel_bias_walk_mps2_sqrts", c.accel_bias_walk_mps2_sqrts);
  readDouble(j, "gyro_bias_walk_radps_sqrts", c.gyro_bias_walk_radps_sqrts);
  readDouble(j, "latency_s", c.latency_s);
  readVec3(j, "accel_bias_initial_mps2", c.accel_bias_initial_mps2);
  readVec3(j, "gyro_bias_initial_radps", c.gyro_bias_initial_radps);
}

void readBaro(const Json& j, BaroConfig& c) {
  readBool(j, "enabled", c.enabled);
  readDouble(j, "rate_hz", c.rate_hz);
  readDouble(j, "sigma_m", c.sigma_m);
  readDouble(j, "bias_m", c.bias_m);
  readDouble(j, "bias_drift_m_per_s", c.bias_drift_m_per_s);
  readDouble(j, "latency_s", c.latency_s);
}

void readVision(const Json& j, VisionConfig& c) {
  readBool(j, "enabled", c.enabled);
  readDouble(j, "rate_hz", c.rate_hz);
  readDouble(j, "sigma_lateral_m", c.sigma_lateral_m);
  readDouble(j, "sigma_longitudinal_m", c.sigma_longitudinal_m);
  readDouble(j, "max_range_m", c.max_range_m);
  readDouble(j, "max_height_m", c.max_height_m);
  readDouble(j, "latency_s", c.latency_s);
  readDouble(j, "quality_full_range_m", c.quality_full_range_m);
  double deg = 0.0;
  if (j.getDouble("sigma_heading_deg", deg)) c.sigma_heading_rad = deg * kDegToRad;
  if (j.getDouble("half_field_of_view_deg", deg)) c.half_field_of_view_rad = deg * kDegToRad;
}

void readPseudorange(const Json& j, PseudorangeConfig& c) {
  readBool(j, "enabled", c.enabled);
  readDouble(j, "rate_hz", c.rate_hz);
  readInt(j, "satellite_count", c.satellite_count);
  readDouble(j, "sigma_m", c.sigma_m);
  readDouble(j, "clock_bias_m", c.clock_bias_m);
  readDouble(j, "clock_drift_m_per_s", c.clock_drift_m_per_s);
}

bool readFault(const Json& j, FaultSpec& f, std::string& error) {
  std::string type_name;
  if (!j.getString("type", type_name)) {
    error = "fault entry without a 'type'";
    return false;
  }
  if (!parseFaultType(type_name, f.type)) {
    error = "unknown fault type '" + type_name + "'";
    return false;
  }
  j.getString("id", f.id);
  std::string target;
  if (j.getString("target", target)) {
    if (!parseSensorId(target.c_str(), f.target)) {
      error = "unknown fault target '" + target + "'";
      return false;
    }
  }
  readDouble(j, "start_s", f.start_s);
  readDouble(j, "duration_s", f.duration_s);
  readDouble(j, "scalar", f.scalar);
  readDouble(j, "scalar_final", f.scalar_final);
  readInt(j, "satellite_index", f.satellite_index);
  // Friendlier aliases so scenario files read like the catalogue in section 7.
  readDouble(j, "latency_s", f.scalar);
  readDouble(j, "sigma", f.scalar);
  readDouble(j, "probability", f.scalar);
  readDouble(j, "probability_final", f.scalar_final);
  readDouble(j, "quality", f.scalar);
  readDouble(j, "quality_final", f.scalar_final);
  readDouble(j, "range_bias_m", f.scalar);
  bool freeze_timestamp = false;
  if (j.getBool("freeze_timestamp", freeze_timestamp)) f.scalar = freeze_timestamp ? 1.0 : 0.0;

  readVec3(j, "amplitude", f.amplitude);
  readVec3(j, "amplitude_ned_m", f.amplitude);
  readVec3(j, "amplitude_ned_mps", f.amplitude);
  readVec3(j, "amplitude_body_mps2", f.amplitude);
  readVec3(j, "amplitude_body_radps", f.amplitude);

  // Single-axis convenience form: axis + final_offset_m.
  std::string axis;
  double offset = 0.0;
  if (j.getString("axis", axis) &&
      (j.getDouble("final_offset_m", offset) || j.getDouble("offset_m", offset))) {
    Vec3 v;
    if (axis == "north")
      v = Vec3(offset, 0.0, 0.0);
    else if (axis == "east")
      v = Vec3(0.0, offset, 0.0);
    else if (axis == "down")
      v = Vec3(0.0, 0.0, offset);
    else {
      error = "unknown axis '" + axis + "' (expected north, east or down)";
      return false;
    }
    f.amplitude = v;
  }

  std::string phase;
  if (j.getString("trigger_phase", phase)) {
    if (!parseMissionPhase(phase.c_str(), f.trigger_phase)) {
      error = "unknown trigger_phase '" + phase + "'";
      return false;
    }
    f.use_phase_trigger = true;
  }
  std::string type_filter;
  if (j.getString("measurement_type", type_filter)) {
    if (type_filter == "gnss_position")
      f.type_filter = MeasurementType::kGnssPosition;
    else if (type_filter == "gnss_velocity")
      f.type_filter = MeasurementType::kGnssVelocity;
    else if (type_filter == "baro_altitude")
      f.type_filter = MeasurementType::kBaroAltitude;
    else if (type_filter == "vision_relative")
      f.type_filter = MeasurementType::kVisionRelative;
    else if (type_filter == "imu_sample")
      f.type_filter = MeasurementType::kImuSample;
    else {
      error = "unknown measurement_type '" + type_filter + "'";
      return false;
    }
    f.use_type_filter = true;
  }
  return true;
}

bool readAcceptance(const Json& j, AcceptanceCriterion& a, std::string& error) {
  j.getString("id", a.id);
  j.getString("description", a.description);
  j.getString("estimator", a.estimator);
  double v = 0.0;
  int i = 0;
  if (j.getDouble("max_position_rmse_m", v)) {
    a.has_max_position_rmse = true;
    a.max_position_rmse_m = v;
  }
  if (j.getDouble("max_error_m", v)) {
    a.has_max_error = true;
    a.max_error_m = v;
  }
  if (j.getDouble("max_horizontal_p95_m", v)) {
    a.has_max_horizontal_p95 = true;
    a.max_horizontal_p95_m = v;
  }
  if (j.getDouble("max_time_to_detect_s", v)) {
    a.has_max_time_to_detect = true;
    a.max_time_to_detect_s = v;
  }
  if (j.getDouble("max_time_to_isolate_s", v)) {
    a.has_max_time_to_isolate = true;
    a.max_time_to_isolate_s = v;
  }
  if (j.getInt("max_false_isolations", i)) {
    a.has_max_false_isolations = true;
    a.max_false_isolations = i;
  }
  if (j.getDouble("min_availability", v)) {
    a.has_min_availability = true;
    a.min_availability = v;
  }
  readBool(j, "require_detection", a.require_detection);
  readBool(j, "require_isolation", a.require_isolation);
  std::string mode;
  if (j.getString("forbid_end_mode", mode)) {
    if (!parseNavMode(mode, a.forbidden_end_mode)) {
      error = "unknown nav mode '" + mode + "'";
      return false;
    }
    a.has_forbidden_end_mode = true;
  }
  if (j.getString("require_end_mode", mode)) {
    if (!parseNavMode(mode, a.required_end_mode)) {
      error = "unknown nav mode '" + mode + "'";
      return false;
    }
    a.has_required_end_mode = true;
  }
  return true;
}

}  // namespace

double Scenario::faultWindowStart_s() const {
  double first = -1.0;
  for (const FaultSpec& f : faults) {
    if (f.use_phase_trigger) continue;  // resolved at run time, reported by the runner
    if (first < 0.0 || f.start_s < first) first = f.start_s;
  }
  return first;
}

double Scenario::faultWindowEnd_s() const {
  double last = -1.0;
  for (const FaultSpec& f : faults) {
    if (f.use_phase_trigger) continue;
    const double e = f.duration_s < 0.0 ? duration_s : f.start_s + f.duration_s;
    if (e > last) last = e;
  }
  return last;
}

std::vector<SensorId> Scenario::faultedSensors() const {
  std::vector<SensorId> out;
  for (const FaultSpec& f : faults) {
    bool already = false;
    for (SensorId seen : out) {
      if (seen == f.target) already = true;
    }
    if (!already) out.push_back(f.target);
  }
  return out;
}

IntegrityConfig defaultIntegrityFor(EstimatorId id) {
  IntegrityConfig c;
  switch (id) {
    case EstimatorId::kGnssOnly:
    case EstimatorId::kInsDeadReckoning:
      c.enabled = false;  // no integrity architecture at all
      break;
    case EstimatorId::kEkf:
      // NAV-C is "fusion standard" in section 6.4: the same EKF as NAV-D with
      // no integrity architecture at all. It is the control case, so it must
      // neither gate nor report - otherwise the comparison against NAV-D would
      // measure the difference between acting and not acting on a detection,
      // instead of the difference between having an integrity layer and not
      // having one, which is the question the benchmark is asking.
      //
      // The NIS is still recorded for NAV-C: the runner computes it from the
      // innovation itself, independently of the policy, so M-12 (filter
      // consistency) is available for every channel including this one.
      //
      // Monitor-only (INT-010) remains a supported mode of the manager and is
      // covered by tests/unit/test_integrity.cpp; it is simply not what NAV-C
      // is.
      c.enabled = false;
      break;
    case EstimatorId::kIntegrityEkf:
      c.enabled = true;
      c.monitor_only = false;
      c.enable_solution_separation = false;
      break;
    case EstimatorId::kSolutionSeparation:
      c.enabled = true;
      c.monitor_only = false;
      c.enable_solution_separation = true;
      break;
    case EstimatorId::kRobustStudentT:
      c.enabled = true;
      c.monitor_only = false;
      break;
  }
  return c;
}

std::vector<NavigationChannelSpec> defaultChannels() {
  const EstimatorId ids[] = {EstimatorId::kGnssOnly, EstimatorId::kInsDeadReckoning,
                             EstimatorId::kEkf, EstimatorId::kIntegrityEkf,
                             EstimatorId::kSolutionSeparation};
  std::vector<NavigationChannelSpec> out;
  for (EstimatorId id : ids) {
    NavigationChannelSpec spec;
    spec.estimator = id;
    spec.integrity = defaultIntegrityFor(id);
    out.push_back(spec);
  }
  return out;
}

bool loadScenarioFromJson(const Json& root, Scenario& out, std::string& error) {
  Scenario s;
  int version = 0;
  if (!root.getInt("schema_version", version)) {
    error = "missing 'schema_version'";
    return false;
  }
  // DATA-008: an unknown schema is refused, never guessed at.
  if (version != kScenarioSchemaVersion) {
    error = "scenario schema version " + std::to_string(version) + " is not supported (expected " +
            std::to_string(kScenarioSchemaVersion) + ")";
    return false;
  }
  s.schema_version = version;
  if (!root.getString("id", s.id)) {
    error = "missing 'id'";
    return false;
  }
  root.getString("name", s.name);
  root.getString("description", s.description);
  root.getString("objective", s.objective);
  unsigned long long seed = 0;
  if (root.getUint64("seed", seed)) s.seed = seed;
  readDouble(root, "duration_s", s.duration_s);
  readDouble(root, "dt_s", s.dt_s);
  if (!(s.duration_s > 0.0) || !(s.dt_s > 0.0)) {
    error = "duration_s and dt_s must both be strictly positive";
    return false;
  }
  if (s.dt_s > 0.1) {
    error = "dt_s above 0.1 s cannot resolve a 100 Hz inertial stream";
    return false;
  }

  std::string profile_name = "approach_ils_like";
  root.getString("truth_profile", profile_name);
  if (!parseTrajectoryProfileName(profile_name, s.profile)) {
    error = "unknown truth_profile '" + profile_name + "'";
    return false;
  }

  const Json& scene = root["scene"];
  if (scene.isObject()) {
    double deg = 0.0;
    if (scene.getDouble("runway_heading_deg", deg)) s.scene.heading_rad = deg * kDegToRad;
    if (scene.getDouble("glideslope_deg", deg)) {
      s.scene.glideslope_rad = deg * kDegToRad;
      s.profile.glideslope_rad = s.scene.glideslope_rad;
    }
    readVec3(scene, "threshold_ned_m", s.scene.threshold_ned_m);
    readDouble(scene, "runway_length_m", s.scene.length_m);
    readDouble(scene, "runway_width_m", s.scene.width_m);
    readDouble(scene, "origin_latitude_deg", s.scene.origin_latitude_deg);
    readDouble(scene, "origin_longitude_deg", s.scene.origin_longitude_deg);
    readDouble(scene, "origin_altitude_m", s.scene.origin_altitude_m);
  }

  const Json& traj = root["trajectory"];
  if (traj.isObject()) {
    readDouble(traj, "approach_speed_mps", s.profile.approach_speed_mps);
    readDouble(traj, "taxi_speed_mps", s.profile.taxi_speed_mps);
    readDouble(traj, "initial_distance_to_threshold_m", s.profile.initial_distance_to_threshold_m);
    readDouble(traj, "flare_height_m", s.profile.flare_height_m);
    readDouble(traj, "rollout_decel_time_s", s.profile.rollout_decel_time_s);
    readDouble(traj, "turn_radius_m", s.profile.turn_radius_m);
    readDouble(traj, "base_leg_length_m", s.profile.base_leg_length_m);
    readDouble(traj, "roll_in_time_s", s.profile.roll_in_time_s);
  }

  const Json& sensors = root["sensors"];
  if (sensors.isObject()) {
    if (sensors["gnss"].isObject()) readGnss(sensors["gnss"], s.sensors.gnss);
    if (sensors["imu"].isObject()) readImu(sensors["imu"], s.sensors.imu);
    if (sensors["baro"].isObject()) readBaro(sensors["baro"], s.sensors.baro);
    if (sensors["vision"].isObject()) readVision(sensors["vision"], s.sensors.vision);
    if (sensors["pseudorange"].isObject()) {
      readPseudorange(sensors["pseudorange"], s.sensors.pseudorange);
    }
  }
  // SENS-016: the R the estimators use tracks the sensor model unless the
  // scenario overrides it, so a scenario cannot accidentally hand the filter a
  // covariance that no longer matches the sensor it was written for.
  s.estimator_config.gnss_sigma_horizontal_m = s.sensors.gnss.sigma_position_horizontal_m;
  s.estimator_config.gnss_sigma_vertical_m = s.sensors.gnss.sigma_position_vertical_m;
  s.estimator_config.gnss_sigma_velocity_mps = s.sensors.gnss.sigma_velocity_mps;
  s.estimator_config.baro_sigma_m = s.sensors.baro.sigma_m;
  s.estimator_config.vision_sigma_lateral_m = s.sensors.vision.sigma_lateral_m;
  s.estimator_config.vision_sigma_longitudinal_m = s.sensors.vision.sigma_longitudinal_m;
  s.estimator_config.vision_sigma_heading_rad = s.sensors.vision.sigma_heading_rad;
  s.estimator_config.accel_noise_density_mps2_sqrthz =
      s.sensors.imu.accel_noise_density_mps2_sqrthz;
  s.estimator_config.gyro_noise_density_radps_sqrthz =
      s.sensors.imu.gyro_noise_density_radps_sqrthz;
  s.estimator_config.accel_bias_walk_mps2_sqrts = s.sensors.imu.accel_bias_walk_mps2_sqrts;
  s.estimator_config.gyro_bias_walk_radps_sqrts = s.sensors.imu.gyro_bias_walk_radps_sqrts;

  const Json& faults = root["faults"];
  if (faults.isArray()) {
    for (std::size_t i = 0; i < faults.size(); ++i) {
      FaultSpec f;
      f.id = "F-" + std::to_string(i + 1);
      if (!readFault(faults.at(i), f, error)) return false;
      s.faults.push_back(f);
    }
  }

  const Json& estimators = root["estimators"];
  if (estimators.isArray() && estimators.size() > 0) {
    for (std::size_t i = 0; i < estimators.size(); ++i) {
      const std::string name = estimators.at(i).asString();
      NavigationChannelSpec spec;
      if (!parseEstimatorId(name.c_str(), spec.estimator)) {
        error = "unknown estimator '" + name + "'";
        return false;
      }
      spec.integrity = defaultIntegrityFor(spec.estimator);
      s.channels.push_back(spec);
    }
  } else {
    s.channels = defaultChannels();
  }

  const Json& acceptance = root["acceptance"];
  if (acceptance.isArray()) {
    for (std::size_t i = 0; i < acceptance.size(); ++i) {
      AcceptanceCriterion a;
      a.id = "AC-" + std::to_string(i + 1);
      if (!readAcceptance(acceptance.at(i), a, error)) return false;
      s.acceptance.push_back(a);
    }
  }

  out = s;
  return true;
}

bool loadScenario(const std::string& path, Scenario& out, std::string& error) {
  Json root;
  if (!loadStructuredFile(path, root, error)) return false;
  if (!loadScenarioFromJson(root, out, error)) {
    error = path + ": " + error;
    return false;
  }
  bool ok = false;
  out.scenario_hash = Sha256::ofFile(path, ok);
  if (!ok) out.scenario_hash = "unavailable";
  out.source_path = path;
  return true;
}

bool loadScenarioFromText(const std::string& text, const std::string& label, Scenario& out,
                          std::string& error) {
  Json root;
  const bool ok =
      documentLooksLikeJson(text) ? Json::parse(text, root, error) : parseYaml(text, root, error);
  if (!ok) {
    error = label + ": " + error;
    return false;
  }
  if (!loadScenarioFromJson(root, out, error)) {
    error = label + ": " + error;
    return false;
  }
  out.scenario_hash = Sha256::ofString(text);
  out.source_path = label;
  return true;
}

bool applyConfigText(const std::string& text, const std::string& label, Scenario& scenario,
                     std::string& error) {
  Json root;
  const bool ok =
      documentLooksLikeJson(text) ? Json::parse(text, root, error) : parseYaml(text, root, error);
  if (!ok) {
    error = label + ": " + error;
    return false;
  }
  applyConfigJson(root, scenario);
  scenario.config_hash = Sha256::ofString(text);
  return true;
}

bool applyConfigFile(const std::string& path, Scenario& scenario, std::string& error) {
  Json root;
  if (!loadStructuredFile(path, root, error)) return false;

  applyConfigJson(root, scenario);
  bool ok = false;
  scenario.config_hash = Sha256::ofFile(path, ok);
  if (!ok) scenario.config_hash = "unavailable";
  return true;
}

namespace {

void applyConfigJsonImpl(const Json& root, Scenario& scenario) {
  const Json& est = root["estimator"];
  EstimatorConfig& e = scenario.estimator_config;
  if (est.isObject()) {
    readDouble(est, "accel_noise_density_mps2_sqrthz", e.accel_noise_density_mps2_sqrthz);
    readDouble(est, "gyro_noise_density_radps_sqrthz", e.gyro_noise_density_radps_sqrthz);
    readDouble(est, "accel_bias_walk_mps2_sqrts", e.accel_bias_walk_mps2_sqrts);
    readDouble(est, "gyro_bias_walk_radps_sqrts", e.gyro_bias_walk_radps_sqrts);
    readDouble(est, "gnss_sigma_horizontal_m", e.gnss_sigma_horizontal_m);
    readDouble(est, "gnss_sigma_vertical_m", e.gnss_sigma_vertical_m);
    readDouble(est, "gnss_sigma_velocity_mps", e.gnss_sigma_velocity_mps);
    readDouble(est, "baro_sigma_m", e.baro_sigma_m);
    readDouble(est, "baro_bias_uncertainty_m", e.baro_bias_uncertainty_m);
    readDouble(est, "vision_sigma_lateral_m", e.vision_sigma_lateral_m);
    readDouble(est, "vision_sigma_longitudinal_m", e.vision_sigma_longitudinal_m);
    readDouble(est, "init_sigma_position_m", e.init_sigma_position_m);
    readDouble(est, "init_sigma_velocity_mps", e.init_sigma_velocity_mps);
    readDouble(est, "init_sigma_accel_bias_mps2", e.init_sigma_accel_bias_mps2);
    readDouble(est, "init_sigma_gyro_bias_radps", e.init_sigma_gyro_bias_radps);
    readDouble(est, "max_measurement_age_s", e.max_measurement_age_s);
    readDouble(est, "rollback_window_s", e.rollback_window_s);
    readBool(est, "enable_rollback", e.enable_rollback);
    readDouble(est, "gnss_only_timeout_s", e.gnss_only_timeout_s);
    double deg = 0.0;
    if (est.getDouble("init_sigma_attitude_deg", deg)) e.init_sigma_attitude_rad = deg * kDegToRad;
  }

  const Json& integ = root["integrity"];
  if (integ.isObject()) {
    for (NavigationChannelSpec& spec : scenario.channels) {
      IntegrityConfig& c = spec.integrity;
      if (!c.enabled) continue;
      readDouble(integ, "gate_probability_false_alert", c.gate_probability_false_alert);
      readDouble(integ, "explicit_gate_threshold", c.explicit_gate_threshold);
      readInt(integ, "suspect_persistence_updates", c.suspect_persistence_updates);
      readDouble(integ, "isolate_persistence_s", c.isolate_persistence_s);
      readDouble(integ, "clear_time_s", c.clear_time_s);
      readDouble(integ, "recovery_window_s", c.recovery_window_s);
      readDouble(integ, "stale_timeout_s", c.stale_timeout_s);
      readDouble(integ, "unavailable_timeout_s", c.unavailable_timeout_s);
      readDouble(integ, "vision_cross_check_threshold_m", c.vision_cross_check_threshold_m);
      readDouble(integ, "vision_min_quality", c.vision_min_quality);
      readDouble(integ, "solution_separation_probability_false_alert",
                 c.solution_separation_probability_false_alert);
      readDouble(integ, "solution_separation_covariance_inflation",
                 c.solution_separation_covariance_inflation);
      readBool(integ, "solution_separation_on_gnss_update_only",
               c.solution_separation_on_gnss_update_only);
      readDouble(integ, "low_confidence_fix_age_s", c.low_confidence_fix_age_s);
      readDouble(integ, "unsafe_fix_age_s", c.unsafe_fix_age_s);
      readBool(integ, "enable_velocity_consistency", c.enable_velocity_consistency);
      readBool(integ, "enable_vision_cross_check", c.enable_vision_cross_check);
    }
  }
}

}  // namespace

void applyConfigJson(const Json& root, Scenario& scenario) {
  applyConfigJsonImpl(root, scenario);
}

Json scenarioToJson(const Scenario& s) {
  Json j = Json::object();
  j["schema_version"] = Json(s.schema_version);
  j["id"] = Json(s.id);
  j["name"] = Json(s.name);
  j["description"] = Json(s.description);
  j["objective"] = Json(s.objective);
  j["seed"] = Json(static_cast<unsigned long long>(s.seed));
  j["duration_s"] = Json(s.duration_s);
  j["dt_s"] = Json(s.dt_s);
  j["truth_profile"] = Json(s.profile.name);
  j["scenario_hash"] = Json(s.scenario_hash);
  j["config_hash"] = Json(s.config_hash);

  Json scene = Json::object();
  scene["runway_heading_deg"] = Json(s.scene.heading_rad * kRadToDeg);
  Json thr = Json::array();
  thr.push(Json(s.scene.threshold_ned_m.x));
  thr.push(Json(s.scene.threshold_ned_m.y));
  thr.push(Json(s.scene.threshold_ned_m.z));
  scene["threshold_ned_m"] = thr;
  scene["runway_length_m"] = Json(s.scene.length_m);
  scene["runway_width_m"] = Json(s.scene.width_m);
  j["scene"] = scene;

  Json faults = Json::array();
  for (const FaultSpec& f : s.faults) {
    Json fj = Json::object();
    fj["id"] = Json(f.id);
    fj["type"] = Json(toString(f.type));
    fj["target"] = Json(toString(f.target));
    fj["start_s"] = Json(f.start_s);
    fj["duration_s"] = Json(f.duration_s);
    fj["scalar"] = Json(f.scalar);
    Json amp = Json::array();
    amp.push(Json(f.amplitude.x));
    amp.push(Json(f.amplitude.y));
    amp.push(Json(f.amplitude.z));
    fj["amplitude"] = amp;
    faults.push(fj);
  }
  j["faults"] = faults;

  Json estimators = Json::array();
  for (const NavigationChannelSpec& c : s.channels) estimators.push(Json(toString(c.estimator)));
  j["estimators"] = estimators;
  return j;
}

}  // namespace aerolab
