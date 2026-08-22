// AEROLAB RESILIENCE - TypeScript wrapper around the WebAssembly core.
//
// API-003: every call that can fail returns a discriminated union, never throws
// an opaque Emscripten exception. The C++ side already hands back a JSON object
// with an `ok` flag; this file turns that into a type the UI cannot ignore.

export interface BuildInfo {
  version: string;
  commit: string;
  build_type: string;
  compiler: string;
  telemetry_schema: number;
  scenario_schema: number;
}

export type SensorState = "ACTIVE" | "SUSPECT" | "ISOLATED" | "UNAVAILABLE";
export type NavMode =
  | "INITIALIZING"
  | "NORMAL"
  | "DEGRADED"
  | "DEAD_RECKONING"
  | "LOW_CONFIDENCE"
  | "UNSAFE";

export interface SensorStatus {
  state: SensorState;
  age_ms: number;
  nis: number;
  threshold: number;
  quality: number;
  trust: number;
  reason: string;
}

export interface SolutionStatus {
  err_m: number;
  mode: NavMode;
  n: number;
  e: number;
  d: number;
  sigma_h_m: number;
}

export interface IntegrityEventFrame {
  t: number;
  estimator: string;
  sensor: string;
  reason: string;
  from: SensorState;
  to: SensorState;
  statistic: number;
  threshold: number;
}

export interface FaultEventFrame {
  t: number;
  id: string;
  type: string;
  target: string;
  activated: boolean;
}

export interface Frame {
  t: number;
  truth: {
    n: number;
    e: number;
    d: number;
    vn: number;
    ve: number;
    vd: number;
    roll_deg: number;
    pitch_deg: number;
    yaw_deg: number;
    phase: string;
  };
  sensors: Record<string, SensorStatus>;
  solutions: Record<string, SolutionStatus>;
  events: IntegrityEventFrame[];
  faults: FaultEventFrame[];
  raim?: {
    statistic: number;
    threshold: number;
    detected: boolean;
    excluded: boolean;
    excluded_satellite: number;
  };
}

export interface ScenarioInfo {
  id: string;
  name: string;
  description: string;
  objective: string;
  seed: number;
  duration_s: number;
  dt_s: number;
  truth_profile: string;
  scenario_hash: string;
  config_hash: string;
  scene: {
    runway_heading_deg: number;
    threshold_ned_m: number[];
    runway_length_m: number;
    runway_width_m: number;
  };
  faults: Array<{
    id: string;
    type: string;
    target: string;
    start_s: number;
    duration_s: number;
    amplitude: number[];
  }>;
  estimators: string[];
  fault_start_s: number;
  fault_end_s: number;
  acceptance: Array<{ id: string; estimator: string; description: string }>;
}

export type Result<T> = { ok: true; value: T } | { ok: false; error: string };

interface WasmSession {
  loadScenario(scenarioText: string, label: string, configText: string): string;
  reset(seed: number): string;
  step(ticks: number): boolean;
  frame(): string;
  finished(): boolean;
  report(): string;
  scenarioInfo(): string;
  delete(): void;
}

interface WasmModule {
  AerolabSession: {
    new (): WasmSession;
    buildInfo(): string;
  };
}

function parse<T>(text: string): Result<T> {
  let raw: unknown;
  try {
    raw = JSON.parse(text);
  } catch (e) {
    return { ok: false, error: `core returned malformed JSON: ${String(e)}` };
  }
  const obj = raw as Record<string, unknown>;
  if (obj && obj.ok === false) {
    return { ok: false, error: String(obj.error ?? "unspecified core error") };
  }
  return { ok: true, value: raw as T };
}

export class AerolabCore {
  private constructor(
    private readonly module: WasmModule,
    private session: WasmSession,
    readonly build: BuildInfo
  ) {}

  static async load(): Promise<Result<AerolabCore>> {
    try {
      // The glue is emitted by Emscripten with EXPORT_ES6, so it is a normal
      // dynamic import. The path is relative to the deployed site root, which
      // keeps the build working from any sub-path on GitHub Pages.
      const factory = (await import(/* @vite-ignore */ `${import.meta.env.BASE_URL}wasm/aerolab.js`))
        .default as (options?: object) => Promise<WasmModule>;
      const module = await factory({});
      const info = parse<BuildInfo>(module.AerolabSession.buildInfo());
      if (!info.ok) return info;
      return { ok: true, value: new AerolabCore(module, new module.AerolabSession(), info.value) };
    } catch (e) {
      return {
        ok: false,
        error:
          `could not load the WebAssembly core: ${String(e)}. ` +
          `Build it with: emcmake cmake --preset wasm && cmake --build build/wasm`,
      };
    }
  }

  loadScenario(scenarioText: string, label: string, configText: string): Result<null> {
    const r = parse<{ ok: boolean }>(this.session.loadScenario(scenarioText, label, configText));
    return r.ok ? { ok: true, value: null } : r;
  }

  reset(seed: number): Result<null> {
    const r = parse<{ ok: boolean }>(this.session.reset(seed));
    return r.ok ? { ok: true, value: null } : r;
  }

  step(ticks: number): boolean {
    return this.session.step(ticks);
  }

  frame(): Result<Frame> {
    return parse<Frame>(this.session.frame());
  }

  finished(): boolean {
    return this.session.finished();
  }

  report(): Result<Record<string, unknown>> {
    return parse<Record<string, unknown>>(this.session.report());
  }

  scenarioInfo(): Result<ScenarioInfo> {
    return parse<ScenarioInfo>(this.session.scenarioInfo());
  }

  // A fresh session per scenario: the C++ side keeps buffers sized for the
  // previous configuration otherwise.
  recreateSession(): void {
    this.session.delete();
    this.session = new this.module.AerolabSession();
  }
}

export const ESTIMATOR_LABELS: Record<string, string> = {
  gnss_only: "GNSS only",
  ins_dr: "INS dead reckoning",
  ekf: "EKF (no integrity)",
  integrity_ekf: "EKF + innovation gating",
  solsep_ekf: "EKF + solution separation",
};

export const ESTIMATOR_COLORS: Record<string, string> = {
  gnss_only: "#e0663d",
  ins_dr: "#b58cd8",
  ekf: "#4d9be0",
  integrity_ekf: "#3fbf8f",
  solsep_ekf: "#e8c95a",
};

// UI-018: colour is never the only signal. Every state also carries a glyph and
// a word wherever it is displayed.
export const SENSOR_STATE_GLYPH: Record<SensorState, string> = {
  ACTIVE: "●",
  SUSPECT: "▲",
  ISOLATED: "✖",
  UNAVAILABLE: "○",
};

export const NAV_MODE_HELP: Record<NavMode, string> = {
  INITIALIZING: "The filter has not yet converged on a solution.",
  NORMAL: "Every source is consistent and in use.",
  DEGRADED: "At least one source has been isolated or is unavailable; the solution continues on the rest.",
  DEAD_RECKONING: "No absolute position source is in use; the solution is coasting on inertial data alone.",
  LOW_CONFIDENCE: "Too little redundancy remains to support an integrity claim. The position is still published but must not be trusted.",
  UNSAFE: "The policy criteria are no longer met. The solution is not usable.",
};
