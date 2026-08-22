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
      // dynamic import. The path must be resolved against the PAGE, not against
      // this module: with base "./" the bundle lives under /assets/, and a
      // relative specifier would look for /assets/wasm/aerolab.js and 404. This
      // exact mistake shipped once — it worked in dev, where BASE_URL is "/",
      // and broke only in the production build, which is why the end-to-end
      // suite runs against `vite preview` rather than the dev server.
      const wasmUrl = new URL(`${import.meta.env.BASE_URL}wasm/aerolab.js`, document.baseURI).href;
      const factory = (await import(/* @vite-ignore */ wasmUrl))
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

// The estimator identifiers are the ones the C++ engine emits and the manifests
// record; only their display names are translated.
const ESTIMATOR_LABELS_BY_LANG: Record<string, Record<string, string>> = {
  en: {
    gnss_only: "GNSS only",
    ins_dr: "INS dead reckoning",
    ekf: "EKF (no integrity)",
    integrity_ekf: "EKF + innovation gating",
    solsep_ekf: "EKF + solution separation",
  },
  fr: {
    gnss_only: "GNSS seul",
    ins_dr: "Navigation à l’estime",
    ekf: "EKF (sans intégrité)",
    // "gating" is the English term; the French term of art for rejecting a
    // measurement on its innovation test is "rejet sur innovation".
    integrity_ekf: "EKF + rejet sur innovation",
    solsep_ekf: "EKF + séparation de solutions",
  },
};

export function estimatorLabels(lang: string): Record<string, string> {
  return ESTIMATOR_LABELS_BY_LANG[lang] ?? ESTIMATOR_LABELS_BY_LANG.en;
}

export const ESTIMATOR_LABELS = ESTIMATOR_LABELS_BY_LANG.en;

/**
 * Short codes for the 3D view.
 *
 * The full names are too long to float beside a marker: with five architectures
 * a few metres apart — which is the normal case early in a run, and the case
 * worth reading — the labels overlapped each other and buried the aircraft.
 * These are the same in both languages on purpose; they are technical tags, and
 * the colour matches the solutions table and the chips.
 */
export const ESTIMATOR_SHORT: Record<string, string> = {
  gnss_only: "GNSS",
  ins_dr: "INS",
  ekf: "EKF",
  integrity_ekf: "EKF·G",
  solsep_ekf: "EKF·SS",
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
