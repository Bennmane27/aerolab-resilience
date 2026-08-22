#include "aerolab/io/runner.hpp"

#include <chrono>
#include <cmath>
#include <fstream>

#include "aerolab/build_info_shim.hpp"
#include "aerolab/navigation/baseline_estimators.hpp"
#include "aerolab/navigation/error_state_ekf.hpp"
#include "aerolab/navigation/solution_separation.hpp"

namespace aerolab {
namespace {

double wallClockSeconds() {
  using clock = std::chrono::steady_clock;
  return std::chrono::duration<double>(clock::now().time_since_epoch()).count();
}

const char* modeName(NavMode m) {
  return toString(m);
}

}  // namespace

bool SimulationRunner::configure(const Scenario& scenario, const RunOptions& options,
                                 std::string& error) {
  scenario_ = scenario;
  options_ = options;
  seed_ = options.has_seed_override ? options.seed_override : scenario.seed;

  if (!(scenario_.duration_s > 0.0) || !(scenario_.dt_s > 0.0)) {
    error = "invalid duration or time step";
    return false;
  }
  if (scenario_.channels.empty()) {
    error = "scenario declares no estimator channel";
    return false;
  }
  // FI-018 / SYS-010: refuse a bad fault list before anything runs.
  if (!faults_.configure(scenario_.faults, error)) return false;

  truth_.reset(scenario_.profile, scenario_.scene);
  sensors_.reset(scenario_.sensors, seed_, scenario_.scene);
  faults_.reset(seed_);
  bus_.clear();
  if (scenario_.sensors.pseudorange.enabled) {
    raim_.configure(sensors_.satellites(), scenario_.sensors.pseudorange.sigma_m, 1.0e-5);
    pseudorange_epoch_.assign(sensors_.satellites().size(), 0.0);
  }

  buildChannels();

  total_ticks_ = static_cast<std::int64_t>(std::llround(scenario_.duration_s / scenario_.dt_s));
  tick_ = 0;
  t_s_ = 0.0;
  fault_events_.clear();
  pending_integrity_events_.clear();
  pending_fault_events_.clear();
  accumulated_wall_s_ = 0.0;
  error.clear();
  return true;
}

void SimulationRunner::buildChannels() {
  channels_.clear();
  truth_state_ = truth_.sampleAt(0.0);
  const NavSolution seed_solution = buildSeedSolution(truth_state_);

  for (const NavigationChannelSpec& spec : scenario_.channels) {
    Channel c;
    c.spec = spec;
    switch (spec.estimator) {
      case EstimatorId::kGnssOnly: c.estimator.reset(new GnssOnlyEstimator()); break;
      case EstimatorId::kInsDeadReckoning:
        c.estimator.reset(new InsDeadReckoningEstimator());
        break;
      case EstimatorId::kSolutionSeparation: {
        SolutionSeparationEstimator* e = new SolutionSeparationEstimator();
        e->setScene(scenario_.scene);
        e->setCovarianceInflation(spec.integrity.solution_separation_covariance_inflation);
        c.estimator.reset(e);
        break;
      }
      case EstimatorId::kRobustStudentT:
      case EstimatorId::kEkf:
      case EstimatorId::kIntegrityEkf:
      default: {
        ErrorStateEkf* e = new ErrorStateEkf(spec.estimator);
        e->setScene(scenario_.scene);
        c.estimator.reset(e);
        break;
      }
    }
    c.estimator->initialize(scenario_.estimator_config, seed_solution);
    c.integrity.configure(spec.integrity, scenario_.scene);
    c.metrics.reset(scenario_.faultWindowStart_s(), scenario_.faultWindowEnd_s(),
                    scenario_.faultedSensors());
    channels_.push_back(std::move(c));
  }
}

NavSolution SimulationRunner::buildSeedSolution(const TruthState& truth0) {
  // Stands in for an alignment phase. Produced ONCE, before the run, from the
  // truth plus a perturbation drawn with the configured alignment sigmas, and
  // handed identically to every channel. No estimator ever reads the truth
  // during the run (NAV-006); this is the single controlled exception and it is
  // the standard way a navigation benchmark is initialised.
  Pcg32 rng = makeStream(seed_, StreamId::kEstimatorAuxiliary);
  const EstimatorConfig& c = scenario_.estimator_config;

  NavSolution s;
  s.t_s = 0.0;
  s.position_ned_m =
      truth0.position_ned_m +
      Vec3(rng.nextGaussian(), rng.nextGaussian(), rng.nextGaussian()) * c.init_sigma_position_m;
  s.velocity_ned_mps =
      truth0.velocity_ned_mps +
      Vec3(rng.nextGaussian(), rng.nextGaussian(), rng.nextGaussian()) * c.init_sigma_velocity_mps;
  const Vec3 attitude_error(rng.nextGaussian(), rng.nextGaussian(), rng.nextGaussian());
  s.attitude_body_to_ned = (truth0.attitude_body_to_ned *
                            Quat::FromRotationVector(attitude_error * c.init_sigma_attitude_rad))
                               .normalized();
  // The alignment knows nothing about the true IMU bias: it starts at zero with
  // the configured uncertainty. That is why NAV-B drifts.
  s.accel_bias_mps2 = Vec3::Zero();
  s.gyro_bias_radps = Vec3::Zero();
  s.mode = NavMode::kInitializing;
  s.valid = true;
  return s;
}

void SimulationRunner::processTick() {
  t_s_ = static_cast<double>(tick_) * scenario_.dt_s;
  truth_state_ = truth_.sampleAt(t_s_);

  // 2. nominal sensors
  staging_.clear();
  sensors_.sample(truth_state_, t_s_, scenario_.dt_s, staging_);

  // 3. fault injection - no truth handle by construction (FI-017)
  const std::size_t faults_before = fault_events_.size();
  faults_.apply(staging_, t_s_, truth_state_.phase, fault_events_);
  for (std::size_t i = faults_before; i < fault_events_.size(); ++i) {
    pending_fault_events_.push_back(fault_events_[i]);
  }

  // 4. delivery ordered bus
  for (const Measurement& m : staging_) bus_.push(m);
  bus_.popDue(t_s_, due_);

  // Collect a pseudorange epoch for the RAIM monitor.
  if (scenario_.sensors.pseudorange.enabled) {
    for (const Measurement& m : due_) {
      if (m.type != MeasurementType::kPseudorange) continue;
      const std::size_t index = static_cast<std::size_t>(m.header.sequence % 100u);
      if (index < pseudorange_epoch_.size()) {
        pseudorange_epoch_[index] = m.usable() ? m.v[0] : 0.0;
        pseudorange_epoch_time_s_ = m.header.sample_time_s;
      }
    }
    if (pseudorange_epoch_time_s_ >= 0.0) last_raim_ = raim_.evaluate(pseudorange_epoch_);
  }

  for (Channel& c : channels_) {
    const double tick_start = options_.measure_tick_time ? wallClockSeconds() : 0.0;
    c.integrity.beginTick(t_s_);

    bool saw_gnss_position = false;
    for (const Measurement& m : due_) {
      if (m.type == MeasurementType::kImuSample) {
        if (m.usable()) c.estimator->consumeImu(m);
        continue;
      }
      if (m.type == MeasurementType::kPseudorange) continue;  // handled by RAIM only in V1
      if (m.type == MeasurementType::kGnssPosition) saw_gnss_position = true;

      InnovationInfo info;
      if (!c.estimator->prepareUpdate(m, info)) continue;  // not consumed by this architecture

      const IntegrityDecision decision = c.integrity.evaluate(m, info, t_s_);
      c.metrics.noteGateEvaluation();
      if (info.valid) {
        c.metrics.noteNis(info.nis, info.dim, c.integrity.gateThresholdFor(info.dim));
      }
      if (decision.accept && info.valid) c.estimator->applyUpdate(m, info);
    }

    // Solution separation (NAV-F only), evaluated at the GNSS update rate.
    const bool separation_due =
        !c.spec.integrity.solution_separation_on_gnss_update_only || saw_gnss_position;
    NavSolution sub;
    if (separation_due && c.spec.integrity.enable_solution_separation &&
        c.estimator->subSolution(sub)) {
      SolutionSeparationEstimator* ss =
          static_cast<SolutionSeparationEstimator*>(c.estimator.get());
      double statistic = 0.0;
      double separation_m = 0.0;
      int dof = 2;
      if (ss->horizontalSeparation(statistic, separation_m, dof)) {
        c.integrity.submitSolutionSeparation(statistic, separation_m, dof, t_s_);
      }
    }

    // Top up the propagation when no inertial sample advanced the filter.
    const double lag = t_s_ - c.estimator->time_s();
    if (lag > 1.0e-9) c.estimator->predict(lag);

    NavSolution solution = c.estimator->solution();
    if (c.spec.integrity.enabled) {
      const NavMode mode = c.integrity.navigationMode(t_s_, solution.last_absolute_fix_age_s);
      if (mode != NavMode::kInitializing) {
        switch (c.spec.estimator) {
          case EstimatorId::kSolutionSeparation:
            static_cast<SolutionSeparationEstimator*>(c.estimator.get())->setModeOverride(mode);
            break;
          case EstimatorId::kEkf:
          case EstimatorId::kIntegrityEkf:
            static_cast<ErrorStateEkf*>(c.estimator.get())->setModeOverride(mode);
            break;
          default: break;
        }
        solution = c.estimator->solution();
      }
    }
    c.mode = solution.mode;
    const Vec3 err = solution.position_ned_m - truth_state_.position_ned_m;
    c.error_m = err.norm();

    c.metrics.sample(t_s_, truth_state_, solution);
    for (const IntegrityEvent& e : c.integrity.events()) {
      c.metrics.noteIntegrityEvent(e);
      c.events.push_back(e);
      pending_integrity_events_.emplace_back(c.spec.estimator, e);
    }
    c.integrity.clearEvents();

    if (options_.measure_tick_time) {
      c.metrics.noteTickTime((wallClockSeconds() - tick_start) * 1000.0);
    }
  }
}

bool SimulationRunner::beginStreaming(std::string& error) {
  error.clear();
  tick_ = 0;
  wall_start_s_ = wallClockSeconds();
  if (!options_.telemetry_path.empty()) {
    telemetry_path_ = options_.telemetry_path;
    telemetry_stream_.close();
    telemetry_stream_.clear();
    telemetry_stream_.open(telemetry_path_, std::ios::binary | std::ios::trunc);
    if (!telemetry_stream_) {
      error = "cannot write telemetry to " + telemetry_path_;
      return false;
    }
    Json header = Json::object();
    header["record"] = Json("header");
    header["schema"] = Json(kTelemetrySchemaVersion);
    header["scenario"] = scenarioToJson(scenario_);
    header["seed"] = Json(static_cast<unsigned long long>(seed_));
    header["commit"] = Json(buildGitCommit());
    header["dt_s"] = Json(scenario_.dt_s);
    header["decimation"] = Json(options_.telemetry_decimation);
    telemetry_stream_ << header.dump(0) << "\n";
    telemetry_open_ = true;
  }
  return true;
}

bool SimulationRunner::stepOnce() {
  if (tick_ > total_ticks_) return false;
  processTick();
  if (telemetry_open_ && options_.telemetry_decimation > 0 &&
      tick_ % options_.telemetry_decimation == 0) {
    // DATA-004: append only, one JSON object per line, never rewritten.
    // The stream stays open for the whole run. An earlier version reopened the
    // file per frame in append mode, which both cost a syscall per tick and
    // raced with the still-buffered header, corrupting the first frame.
    telemetry_stream_ << currentFrame().dump(0) << "\n";
  }
  ++tick_;
  return true;
}

Json SimulationRunner::currentFrame() {
  Json f = Json::object();
  f["record"] = Json("frame");
  f["t"] = Json(t_s_);
  Json truth = Json::object();
  truth["n"] = Json(truth_state_.position_ned_m.x);
  truth["e"] = Json(truth_state_.position_ned_m.y);
  truth["d"] = Json(truth_state_.position_ned_m.z);
  truth["vn"] = Json(truth_state_.velocity_ned_mps.x);
  truth["ve"] = Json(truth_state_.velocity_ned_mps.y);
  truth["vd"] = Json(truth_state_.velocity_ned_mps.z);
  truth["roll_deg"] = Json(truth_state_.attitude_body_to_ned.roll() * kRadToDeg);
  truth["pitch_deg"] = Json(truth_state_.attitude_body_to_ned.pitch() * kRadToDeg);
  truth["yaw_deg"] = Json(truth_state_.attitude_body_to_ned.yaw() * kRadToDeg);
  truth["phase"] = Json(toString(truth_state_.phase));
  f["truth"] = truth;

  Json sensors = Json::object();
  const SensorId ids[] = {SensorId::kGnss, SensorId::kImu, SensorId::kBaro, SensorId::kVision};
  // Sensor states come from the first channel that actually runs a policy, so
  // the UI shows a real decision rather than an average of several.
  const Channel* reference = nullptr;
  for (const Channel& c : channels_) {
    if (c.spec.integrity.enabled && !c.spec.integrity.monitor_only) {
      reference = &c;
      break;
    }
  }
  if (reference != nullptr) {
    for (SensorId id : ids) {
      const SourceStatus& s = reference->integrity.statusOf(id);
      Json entry = Json::object();
      entry["state"] = Json(toString(s.state));
      entry["age_ms"] = Json(s.age_s * 1000.0);
      entry["nis"] = Json(s.last_nis);
      entry["threshold"] = Json(s.last_threshold);
      entry["quality"] = Json(s.quality);
      entry["trust"] = Json(reference->integrity.trustScore(id, t_s_));
      entry["reason"] = Json(toString(s.last_reason));
      sensors[toString(id)] = entry;
    }
  }
  f["sensors"] = sensors;

  Json solutions = Json::object();
  for (const Channel& c : channels_) {
    Json entry = Json::object();
    entry["err_m"] = Json(c.error_m);
    entry["mode"] = Json(modeName(c.mode));
    const NavSolution s = c.estimator->solution();
    entry["n"] = Json(s.position_ned_m.x);
    entry["e"] = Json(s.position_ned_m.y);
    entry["d"] = Json(s.position_ned_m.z);
    entry["sigma_h_m"] = Json(s.horizontalSigma_m());
    solutions[toString(c.spec.estimator)] = entry;
  }
  f["solutions"] = solutions;

  // Everything that happened since the previous frame, not just this tick.
  Json events = Json::array();
  for (const auto& entry : pending_integrity_events_) {
    const IntegrityEvent& e = entry.second;
    Json ev = Json::object();
    ev["t"] = Json(e.t_s);
    ev["estimator"] = Json(toString(entry.first));
    ev["sensor"] = Json(toString(e.sensor));
    ev["reason"] = Json(toString(e.reason));
    ev["from"] = Json(toString(e.from));
    ev["to"] = Json(toString(e.to));
    ev["statistic"] = Json(e.statistic);
    ev["threshold"] = Json(e.threshold);
    events.push(ev);
  }
  pending_integrity_events_.clear();
  f["events"] = events;

  Json faults = Json::array();
  for (const FaultEvent& e : pending_fault_events_) {
    Json ev = Json::object();
    ev["t"] = Json(e.t_s);
    ev["id"] = Json(e.fault_id);
    ev["type"] = Json(toString(e.type));
    ev["target"] = Json(toString(e.target));
    ev["activated"] = Json(e.activated);
    faults.push(ev);
  }
  pending_fault_events_.clear();
  f["faults"] = faults;

  if (scenario_.sensors.pseudorange.enabled && last_raim_.computed) {
    Json r = Json::object();
    r["statistic"] = Json(last_raim_.statistic);
    r["threshold"] = Json(last_raim_.threshold);
    r["detected"] = Json(last_raim_.detected);
    r["excluded"] = Json(last_raim_.excluded);
    r["excluded_satellite"] = Json(last_raim_.excluded_satellite);
    f["raim"] = r;
  }
  return f;
}

RunResult SimulationRunner::finishStreaming() {
  if (telemetry_open_) {
    telemetry_stream_.flush();
    telemetry_stream_.close();
    telemetry_open_ = false;
  }
  RunResult result;
  result.completed = true;
  result.scenario_id = scenario_.id;
  result.seed = seed_;
  result.fault_start_s = scenario_.faultWindowStart_s();
  result.fault_end_s = scenario_.faultWindowEnd_s();
  result.fault_events = fault_events_;
  result.last_raim = last_raim_;
  result.wall_time_s = wallClockSeconds() - wall_start_s_;

  for (Channel& c : channels_) {
    ChannelResult cr;
    cr.estimator = c.spec.estimator;
    cr.metrics = c.metrics.finalize(scenario_.duration_s);
    cr.events = c.events;
    cr.final_mode = c.mode;
    cr.final_gnss_state = c.integrity.stateOf(SensorId::kGnss);
    cr.healthy = c.estimator->healthy();
    cr.diagnostic = c.estimator->diagnostic();
    cr.final_error_m = c.error_m;
    if (!cr.healthy) result.completed = false;
    result.channels.push_back(cr);
  }
  evaluateVerdict(result);
  result.manifest = buildManifest(result);
  return result;
}

RunResult SimulationRunner::run() {
  RunResult result;
  std::string error;
  if (!beginStreaming(error)) {
    result.completed = false;
    result.error = error;
    return result;
  }
  while (tick_ <= total_ticks_) {
    if (!stepOnce()) break;
  }
  return finishStreaming();
}

void SimulationRunner::evaluateVerdict(RunResult& result) const {
  result.verdict_pass = true;
  result.verdict_failures.clear();

  // SYS-017 / NAV-014: an unhealthy channel is a failed run, recorded as such
  // rather than dropped from the campaign.
  for (const ChannelResult& c : result.channels) {
    if (!c.healthy) {
      result.verdict_pass = false;
      result.verdict_failures.push_back(std::string(toString(c.estimator)) +
                                        ": estimator reported unhealthy - " + c.diagnostic);
    }
    if (!std::isfinite(c.metrics.position_rmse_m)) {
      result.verdict_pass = false;
      result.verdict_failures.push_back(std::string(toString(c.estimator)) +
                                        ": non-finite position RMSE");
    }
  }

  for (const AcceptanceCriterion& a : scenario_.acceptance) {
    for (const ChannelResult& c : result.channels) {
      const std::string name = toString(c.estimator);
      if (a.estimator != "any" && a.estimator != name) continue;
      const MetricsSummary& m = c.metrics;
      const std::string prefix = a.id + " [" + name + "] ";

      if (a.has_max_position_rmse && m.position_rmse_m > a.max_position_rmse_m) {
        result.verdict_pass = false;
        result.verdict_failures.push_back(prefix + "position RMSE " +
                                          std::to_string(m.position_rmse_m) + " m exceeds " +
                                          std::to_string(a.max_position_rmse_m) + " m");
      }
      if (a.has_max_error && m.position_error_m.max > a.max_error_m) {
        result.verdict_pass = false;
        result.verdict_failures.push_back(prefix + "max error " +
                                          std::to_string(m.position_error_m.max) + " m exceeds " +
                                          std::to_string(a.max_error_m) + " m");
      }
      if (a.has_max_horizontal_p95 && m.horizontal_error_m.p95 > a.max_horizontal_p95_m) {
        result.verdict_pass = false;
        result.verdict_failures.push_back(prefix + "horizontal P95 " +
                                          std::to_string(m.horizontal_error_m.p95) + " m exceeds " +
                                          std::to_string(a.max_horizontal_p95_m));
      }
      if (a.require_detection && !m.fault_detected) {
        result.verdict_pass = false;
        result.verdict_failures.push_back(prefix + "no detection was raised");
      }
      if (a.require_isolation && !m.fault_isolated) {
        result.verdict_pass = false;
        result.verdict_failures.push_back(prefix + "no isolation was raised");
      }
      if (a.has_max_time_to_detect &&
          (m.time_to_detect_s < 0.0 || m.time_to_detect_s > a.max_time_to_detect_s)) {
        result.verdict_pass = false;
        result.verdict_failures.push_back(
            prefix + "time to detect " +
            (m.time_to_detect_s < 0.0 ? std::string("never") : std::to_string(m.time_to_detect_s)) +
            " exceeds " + std::to_string(a.max_time_to_detect_s) + " s");
      }
      if (a.has_max_time_to_isolate &&
          (m.time_to_isolate_s < 0.0 || m.time_to_isolate_s > a.max_time_to_isolate_s)) {
        result.verdict_pass = false;
        result.verdict_failures.push_back(prefix + "time to isolate exceeds budget");
      }
      if (a.has_max_false_isolations &&
          static_cast<int>(m.false_isolation_count) > a.max_false_isolations) {
        result.verdict_pass = false;
        result.verdict_failures.push_back(prefix + "false isolations " +
                                          std::to_string(m.false_isolation_count) + " exceed " +
                                          std::to_string(a.max_false_isolations));
      }
      if (a.has_min_availability && m.availability < a.min_availability) {
        result.verdict_pass = false;
        result.verdict_failures.push_back(prefix + "availability " +
                                          std::to_string(m.availability) + " below " +
                                          std::to_string(a.min_availability));
      }
      if (a.has_forbidden_end_mode && c.final_mode == a.forbidden_end_mode) {
        result.verdict_pass = false;
        result.verdict_failures.push_back(prefix + "final mode is " + modeName(c.final_mode) +
                                          ", which is forbidden here");
      }
      if (a.has_required_end_mode && c.final_mode != a.required_end_mode) {
        result.verdict_pass = false;
        result.verdict_failures.push_back(prefix + "final mode is " + modeName(c.final_mode) +
                                          ", expected " + modeName(a.required_end_mode));
      }
    }
  }
}

Json SimulationRunner::buildManifest(const RunResult& result) const {
  Json m = Json::object();
  m["schema"] = Json(kTelemetrySchemaVersion);
  m["scenario_id"] = Json(scenario_.id);
  m["scenario_name"] = Json(scenario_.name);
  m["scenario_hash"] = Json(scenario_.scenario_hash);
  m["config_hash"] = Json(scenario_.config_hash);
  m["source_path"] = Json(scenario_.source_path);
  m["seed"] = Json(static_cast<unsigned long long>(seed_));
  m["duration_s"] = Json(scenario_.duration_s);
  m["dt_s"] = Json(scenario_.dt_s);
  m["truth_profile"] = Json(scenario_.profile.name);
  m["commit"] = Json(buildGitCommit());
  m["version"] = Json(buildVersion());
  m["build_type"] = Json(buildType());
  m["compiler"] = Json(buildCompiler());
  m["wall_time_s"] = Json(result.wall_time_s);
  m["completed"] = Json(result.completed);
  m["verdict"] = Json(result.verdict_pass ? "PASS" : "FAIL");
  if (result.fault_start_s >= 0.0) {
    m["fault_start_s"] = Json(result.fault_start_s);
    m["fault_end_s"] = Json(result.fault_end_s);
  }

  Json channels = Json::array();
  for (const ChannelResult& c : result.channels) {
    const MetricsSummary& s = c.metrics;
    Json j = Json::object();
    j["estimator"] = Json(toString(c.estimator));
    j["healthy"] = Json(c.healthy);
    j["final_mode"] = Json(modeName(c.final_mode));
    j["final_gnss_state"] = Json(toString(c.final_gnss_state));
    j["position_rmse_m"] = Json(s.position_rmse_m);
    j["horizontal_rmse_m"] = Json(s.horizontal_rmse_m);
    j["vertical_rmse_m"] = Json(s.vertical_rmse_m);
    j["error_max_m"] = Json(s.position_error_m.max);
    j["error_p95_m"] = Json(s.position_error_m.p95);
    j["error_median_m"] = Json(s.position_error_m.median);
    j["error_mean_m"] = Json(s.position_error_m.mean);
    j["horizontal_p95_m"] = Json(s.horizontal_error_m.p95);
    j["time_to_detect_s"] = s.time_to_detect_s < 0.0 ? Json() : Json(s.time_to_detect_s);
    j["time_to_isolate_s"] = s.time_to_isolate_s < 0.0 ? Json() : Json(s.time_to_isolate_s);
    j["recovery_time_s"] = s.recovery_time_s < 0.0 ? Json() : Json(s.recovery_time_s);
    j["fault_present"] = Json(s.fault_present);
    j["fault_detected"] = Json(s.fault_detected);
    j["fault_isolated"] = Json(s.fault_isolated);
    j["gate_evaluations"] = Json(static_cast<unsigned long long>(s.gate_evaluations));
    j["false_alert_count"] = Json(static_cast<unsigned long long>(s.false_alert_count));
    j["false_isolation_count"] = Json(static_cast<unsigned long long>(s.false_isolation_count));
    j["false_alert_rate"] = Json(s.false_alert_rate);
    j["availability"] = Json(s.availability);
    j["continuity"] = Json(s.continuity);
    j["interruption_count"] = Json(static_cast<unsigned long long>(s.interruption_count));
    j["nis_mean_normalised"] = Json(s.nis_mean_normalised);
    j["nis_fraction_within_gate"] = Json(s.nis_fraction_within_gate);
    j["nis_samples"] = Json(static_cast<unsigned long long>(s.nis_samples));
    j["max_error_during_fault_m"] = Json(s.max_error_during_fault_m);
    j["tick_ms_p95"] = Json(s.tick_time_ms.p95);
    j["tick_ms_median"] = Json(s.tick_time_ms.median);
    j["tick_ms_max"] = Json(s.tick_time_ms.max);
    j["peak_memory_mb"] = Json(s.peak_memory_mb);
    j["determinism_hash"] = Json(s.determinism_hash);
    j["integrity_event_count"] = Json(static_cast<unsigned long long>(c.events.size()));
    if (!c.diagnostic.empty()) j["diagnostic"] = Json(c.diagnostic);
    channels.push(j);
  }
  m["channels"] = channels;

  Json failures = Json::array();
  for (const std::string& f : result.verdict_failures) failures.push(Json(f));
  m["verdict_failures"] = failures;

  Json fault_events = Json::array();
  for (const FaultEvent& e : result.fault_events) {
    Json j = Json::object();
    j["t_s"] = Json(e.t_s);
    j["id"] = Json(e.fault_id);
    j["type"] = Json(toString(e.type));
    j["target"] = Json(toString(e.target));
    j["activated"] = Json(e.activated);
    fault_events.push(j);
  }
  m["fault_events"] = fault_events;
  return m;
}

}  // namespace aerolab
