// AEROLAB RESILIENCE - shared display components.
//
// UI-005, UI-006, UI-007, UI-017, UI-018, UI-019.
import type { ReactNode } from "react";
import type { Frame, NavMode, SensorState } from "../core/session";
import {
  ESTIMATOR_COLORS,
  ESTIMATOR_LABELS,
  NAV_MODE_HELP,
  SENSOR_STATE_GLYPH,
} from "../core/session";

export function Panel({ title, children, style }: { title: string; children: ReactNode; style?: React.CSSProperties }) {
  return (
    <section className="panel" style={style}>
      <h3>{title}</h3>
      <div className="panel-body">{children}</div>
    </section>
  );
}

const SENSOR_LABELS: Record<string, string> = {
  gnss: "GNSS",
  imu: "IMU",
  baro: "Baro",
  vision: "Vision",
};

// UI-017: every state carries a plain-language explanation of what it means and
// what follows from it. These strings are keyed on the reason codes the C++
// integrity manager emits, so the UI cannot invent a status the engine did not
// produce (section 4.3).
const REASON_HELP: Record<string, string> = {
  NONE: "Nominal.",
  NIS_ABOVE_THRESHOLD:
    "The measurement disagrees with the filter prediction by more than the gate allows for this source.",
  NIS_PERSISTENT: "The disagreement persisted long enough to rule out a single outlier.",
  NIS_NORMAL_CLEARED: "Residuals have been normal long enough for the source to be trusted again.",
  RECOVERY_WINDOW_ELAPSED: "The recovery window has elapsed and the source is consistent again.",
  MEASUREMENT_STALE:
    "The sample timestamp is older than the freshness limit: the value may look plausible but it is not current.",
  SEQUENCE_REPEATED: "The source is repeating a previous sample rather than producing a new one.",
  SOURCE_UNAVAILABLE: "The source stopped delivering usable measurements.",
  SOURCE_RETURNED: "The source is delivering again.",
  CROSS_CHECK_INERTIAL:
    "GNSS disagrees with where the inertial solution says the aircraft should be.",
  CROSS_CHECK_VISION: "GNSS and the runway-relative vision fix disagree about the position.",
  SOLUTION_SEPARATION:
    "The all-sources solution has drifted away from the GNSS-free sub-filter by more than their combined uncertainty allows.",
  INNOVATION_COVARIANCE_INVALID: "The innovation covariance was not usable; the update was refused.",
  VELOCITY_INCONSISTENT:
    "The reported velocity is not consistent with the rest of the solution, even though the position looks plausible.",
  QUALITY_BELOW_THRESHOLD: "The source reports a quality too low for its measurement to be usable.",
  REDUNDANCY_INSUFFICIENT: "Not enough independent sources remain to support an integrity claim.",
  MANUAL_ISOLATION: "Isolated by an operator request.",
};

export function reasonHelp(reason: string): string {
  return REASON_HELP[reason] ?? reason;
}

export function SensorHealth({ frame }: { frame: Frame | null }) {
  const sensors = frame?.sensors ?? {};
  const ids = Object.keys(sensors);
  if (ids.length === 0) return <p className="empty">Waiting for the first measurements.</p>;
  return (
    <div>
      {ids.map((id) => {
        const s = sensors[id];
        return (
          <div className="sensor-row" key={id}>
            {/* UI-018: glyph + word + colour, never colour alone. */}
            <span className={`glyph state-${s.state}`} aria-hidden="true">
              {SENSOR_STATE_GLYPH[s.state as SensorState] ?? "?"}
            </span>
            <span className="sensor-name">{SENSOR_LABELS[id] ?? id}</span>
            <span className={`sensor-state state-${s.state}`}>{s.state}</span>
            <span className="sensor-meta">
              age {s.age_ms.toFixed(0)}<span className="unit"> ms</span>
              {" · NIS "}
              {s.nis.toFixed(2)}
              {s.threshold > 0 ? ` / ${s.threshold.toFixed(1)}` : ""}
              {id === "vision" ? ` · quality ${(s.quality * 100).toFixed(0)}%` : ""}
            </span>
            {s.reason !== "NONE" && (
              <span className="sensor-meta" style={{ color: "var(--text-dim)" }}>
                {reasonHelp(s.reason)}
              </span>
            )}
          </div>
        );
      })}
    </div>
  );
}

export function SolutionTable({ frame, highlight }: { frame: Frame | null; highlight?: string }) {
  const solutions = frame?.solutions ?? {};
  const ids = Object.keys(solutions);
  if (ids.length === 0) return <p className="empty">No solution yet.</p>;
  return (
    <table>
      <thead>
        <tr>
          <th scope="col">Architecture</th>
          <th scope="col">
            Error <span className="unit">m</span>
          </th>
          <th scope="col">
            σ<sub>h</sub> <span className="unit">m</span>
          </th>
          <th scope="col">Mode</th>
        </tr>
      </thead>
      <tbody>
        {ids.map((id) => {
          const s = solutions[id];
          return (
            <tr key={id} className={highlight === id ? "highlight" : undefined}>
              <td>
                <span
                  aria-hidden="true"
                  style={{
                    display: "inline-block",
                    width: 9,
                    height: 9,
                    borderRadius: 2,
                    background: ESTIMATOR_COLORS[id] ?? "#888",
                    marginRight: 7,
                  }}
                />
                {ESTIMATOR_LABELS[id] ?? id}
              </td>
              <td className="num">{s.err_m.toFixed(2)}</td>
              <td className="num">{s.sigma_h_m.toFixed(2)}</td>
              <td className={`num mode-${s.mode}`} title={NAV_MODE_HELP[s.mode as NavMode]}>
                {s.mode}
              </td>
            </tr>
          );
        })}
      </tbody>
    </table>
  );
}

export interface LogEntry {
  t: number;
  kind: "integrity" | "fault";
  headline: string;
  why: string;
}

export function EventLog({ entries }: { entries: LogEntry[] }) {
  if (entries.length === 0) {
    return <p className="empty">No integrity event yet. A nominal run should stay empty.</p>;
  }
  return (
    <div className="event-log" role="log" aria-live="polite">
      {entries
        .slice()
        .reverse()
        .map((e, i) => (
          <div className={`event ${e.kind}`} key={`${e.t}-${i}`}>
            <span className="t">{e.t.toFixed(2)}s</span>
            <span>{e.headline}</span>
            <span className="why">{e.why}</span>
          </div>
        ))}
    </div>
  );
}
