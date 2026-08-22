// AEROLAB RESILIENCE - shared display components.
//
// UI-005, UI-006, UI-007, UI-017, UI-018, UI-019.
//
// The reason codes, sensor states and navigation modes are the identifiers the
// C++ engine emits and the telemetry records. They are never translated: a
// reader comparing this screen against a JSON file has to see the same token.
// Only the explanation beside them is.
import type { ReactNode } from "react";
import type { Frame, NavMode, SensorState } from "../core/session";
import { ESTIMATOR_COLORS, SENSOR_STATE_GLYPH } from "../core/session";
import { useLang } from "../i18n";

export function Panel({
  title,
  children,
  actions,
  scroll,
  style,
}: {
  title: string;
  children: ReactNode;
  actions?: ReactNode;
  /**
   * `"fill"` makes the body take whatever height the panel was given and scroll
   * inside it. A number caps the body at that many pixels.
   *
   * Prefer `"fill"`, and prefer neither. A fixed cap inside a column that
   * already scrolls gives you two nested scrollbars where the inner one stops
   * short of the content, which is a good way to make a panel look broken: the
   * side column panels therefore set nothing at all and let the column scroll
   * as one piece.
   */
  scroll?: number | "fill";
  style?: React.CSSProperties;
}) {
  const fill = scroll === "fill";
  return (
    <section className={fill ? "panel panel-fill" : "panel"} style={style}>
      <h3>
        <span>{title}</span>
        {actions && <span className="panel-actions">{actions}</span>}
      </h3>
      <div
        className="panel-body"
        style={typeof scroll === "number" ? { maxHeight: scroll, overflowY: "auto" } : undefined}
      >
        {children}
      </div>
    </section>
  );
}

export function SensorHealth({ frame }: { frame: Frame | null }) {
  const { t, num } = useLang();
  const sensors = frame?.sensors ?? {};
  const ids = Object.keys(sensors);
  if (ids.length === 0) return <p className="empty">{t.panels.waiting}</p>;
  return (
    <div>
      {ids.map((id) => {
        const s = sensors[id];
        const state = s.state as SensorState;
        return (
          <div className="sensor-row" key={id} title={t.stateHelp[state]}>
            {/* UI-018: glyph + word + colour, never colour alone. */}
            <span className={`glyph state-${state}`} aria-hidden="true">
              {SENSOR_STATE_GLYPH[state] ?? "?"}
            </span>
            <span className="sensor-name">{t.panels.sensorNames[id] ?? id}</span>
            <span className={`sensor-state state-${state}`}>{state}</span>
            <span className="sensor-meta">
              {t.panels.age} {num(s.age_ms, 0)}
              <span className="unit"> ms</span>
              {" · NIS "}
              {num(s.nis, 2)}
              {s.threshold > 0 ? ` / ${num(s.threshold, 1)}` : ""}
              {id === "vision" ? ` · ${t.panels.quality} ${num(s.quality * 100, 0)} %` : ""}
            </span>
            {s.reason !== "NONE" && (
              <span className="sensor-reason">{t.reasonHelp[s.reason] ?? s.reason}</span>
            )}
          </div>
        );
      })}
    </div>
  );
}

export function SolutionTable({
  frame,
  labels,
  highlight,
}: {
  frame: Frame | null;
  labels: Record<string, string>;
  highlight?: string;
}) {
  const { t, num } = useLang();
  const solutions = frame?.solutions ?? {};
  const ids = Object.keys(solutions);
  if (ids.length === 0) return <p className="empty">{t.panels.noSolution}</p>;
  return (
    <table>
      <thead>
        <tr>
          <th scope="col">{t.panels.architecture}</th>
          <th scope="col">
            {t.panels.error} <span className="unit">m</span>
          </th>
          <th scope="col">
            {t.panels.sigmaH} <span className="unit">m</span>
          </th>
          <th scope="col">{t.panels.mode}</th>
        </tr>
      </thead>
      <tbody>
        {ids.map((id) => {
          const s = solutions[id];
          const mode = s.mode as NavMode;
          return (
            <tr key={id} className={highlight === id ? "highlight" : undefined}>
              <td>
                <span
                  aria-hidden="true"
                  className="swatch"
                  style={{ background: ESTIMATOR_COLORS[id] ?? "#888" }}
                />
                {labels[id] ?? id}
              </td>
              <td className="num">{num(s.err_m, 2)}</td>
              <td className="num">{num(s.sigma_h_m, 2)}</td>
              <td className={`num mode-${mode}`} title={t.modeHelp[mode]}>
                {mode}
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
  /** Structured copy of the event, for the live commentary. Display uses the
      pre-rendered strings above; the narrator needs the identifiers. */
  estimator?: string;
  sensor?: string;
  to?: string;
  /** Engine reason code: which check actually fired. */
  reason?: string;
  /** Fault type identifier, for entries of kind "fault". */
  faultType?: string;
  activated?: boolean;
}

export function EventLog({ entries, empty }: { entries: LogEntry[]; empty: string }) {
  const { num } = useLang();
  if (entries.length === 0) return <p className="empty">{empty}</p>;
  return (
    <div className="event-log" role="log" aria-live="polite">
      {entries
        .slice()
        .reverse()
        .map((e, i) => (
          <div className={`event ${e.kind}`} key={`${e.t}-${i}`}>
            <span className="t">{num(e.t, 2)}s</span>
            <span>{e.headline}</span>
            <span className="why">{e.why}</span>
          </div>
        ))}
    </div>
  );
}
