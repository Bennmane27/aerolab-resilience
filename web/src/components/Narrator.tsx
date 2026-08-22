// AEROLAB RESILIENCE - live commentary panel.
//
// Derives, from the frame and the scenario file alone, which moment of the run
// is on screen, and says so in plain language. It invents nothing: the fault
// window comes from the scenario, the detections come from the integrity events
// the engine emitted, and the errors are the ones in the frame.
//
// It exists because the integrity log answers "what did the policy decide" and
// not "why is this the interesting second of the run". Someone who does not
// already know what a NIS gate is watches a nominal approach, then a number
// changes, and nothing tells them that was the point.
import type { Frame, ScenarioInfo } from "../core/session";
import { ESTIMATOR_COLORS } from "../core/session";
import type { LogEntry } from "./Panels";
import { useLang } from "../i18n";
import type { Beat } from "../i18n/narration";

type Tone = "idle" | "nominal" | "alert" | "acting" | "done";

/** The beat, plus how loudly to draw it. */
function chooseBeat(
  frame: Frame | null,
  scenario: ScenarioInfo,
  log: LogEntry[],
  labels: Record<string, string>,
  n: ReturnType<typeof useLang>["narration"],
  t: ReturnType<typeof useLang>["t"],
  num: (v: number, d?: number) => string,
  finished: boolean
): { beat: Beat; tone: Tone } {
  if (!frame) return { beat: n.waiting, tone: "idle" };

  const now = frame.t;
  const faults = scenario.faults ?? [];
  if (faults.length === 0) return { beat: n.nominal, tone: "nominal" };

  const faultedTargets = new Set(faults.map((f) => f.target));
  const named = (target: string) => t.eventLog.sensorNames[target] ?? target;
  const faultWords = (type: string) => t.eventLog.faultTypes[type] ?? type;

  // Detections and isolations, restricted to sources the scenario actually
  // faulted. Crediting any source leaving ACTIVE is exactly the mistake KF-007
  // records, and it would be just as wrong in a sentence as in a metric.
  const onFaulted = log.filter(
    (e) => e.kind === "integrity" && e.sensor !== undefined && faultedTargets.has(e.sensor)
  );
  const firstDetection = onFaulted.find((e) => e.to !== undefined && e.to !== "ACTIVE");
  const firstIsolation = onFaulted.find((e) => e.to === "ISOLATED");

  // Best and worst of the architectures that produced a solution this frame.
  const scored = Object.keys(ESTIMATOR_COLORS)
    .map((id) => ({ id, err: frame.solutions[id]?.err_m }))
    .filter((s): s is { id: string; err: number } => Number.isFinite(s.err))
    .sort((a, b) => a.err - b.err);
  const best = scored[0];
  const worst = scored[scored.length - 1];
  const spread = (): [string, string, string, string] => [
    labels[best?.id ?? ""] ?? "—",
    best ? num(best.err, 1) : "—",
    labels[worst?.id ?? ""] ?? "—",
    worst ? num(worst.err, 1) : "—",
  ];

  if (finished) return { beat: n.finished(...spread()), tone: "done" };

  const start = scenario.fault_start_s;
  const end = scenario.fault_end_s;
  const next = faults.find((f) => f.start_s >= start) ?? faults[0];

  if (now < start) {
    return {
      beat: n.beforeFault(num(Math.max(0, start - now), 0), faultWords(next.type), named(next.target)),
      tone: "nominal",
    };
  }

  if (now <= end) {
    if (firstIsolation) {
      return {
        beat: n.isolated(
          labels[firstIsolation.estimator ?? ""] ?? firstIsolation.estimator ?? "—",
          named(firstIsolation.sensor ?? ""),
          num(Math.max(0, (firstDetection ?? firstIsolation).t - start), 1)
        ),
        tone: "acting",
      };
    }
    if (firstDetection) {
      return {
        beat: n.detected(
          labels[firstDetection.estimator ?? ""] ?? firstDetection.estimator ?? "—",
          num(Math.max(0, firstDetection.t - start), 1),
          named(firstDetection.sensor ?? "")
        ),
        tone: "acting",
      };
    }
    return {
      beat: n.undetected(faultWords(next.type), named(next.target), num(now - start, 0)),
      tone: "alert",
    };
  }

  return { beat: n.over(...spread()), tone: "done" };
}

export function Narrator({
  frame,
  scenario,
  log,
  labels,
  finished,
}: {
  frame: Frame | null;
  scenario: ScenarioInfo;
  log: LogEntry[];
  labels: Record<string, string>;
  finished: boolean;
}) {
  const { t, num, narration, scenarioText } = useLang();
  const { beat, tone } = chooseBeat(frame, scenario, log, labels, narration, t, num, finished);

  return (
    <div className={`narrator tone-${tone}`}>
      <p className="narrator-objective">
        <b>{narration.objectiveLabel}</b>{" "}
        — {scenarioText(scenario.id)?.objective ?? scenario.objective ?? scenario.description}
      </p>
      <h4 className="narrator-headline">{beat.headline}</h4>
      <p className="narrator-text">{beat.text}</p>
      <p className="narrator-watch">
        <b>{narration.watchLabel}</b> {beat.watch}
      </p>
    </div>
  );
}
