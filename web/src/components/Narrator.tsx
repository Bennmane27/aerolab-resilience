// AEROLAB RESILIENCE - live commentary, over the 3D view.
//
// Two things this is not.
//
// It is not a status line. It accumulates: every entry stays, and the panel
// scrolls, because the run moves faster than anyone reads and the moment worth
// understanding is usually the one that just went past. It follows the newest
// entry only while you are already at the bottom; scroll up and it holds still.
//
// It is not a translation of the screen. The first version said "X flagged GNSS
// 0.3 s after the injection", which is what the event log already said in its
// own vocabulary — reading it taught you nothing. Every entry now carries the
// MECHANISM: which test fired, why that test and not another, what firing it
// actually proves, and where the same test is known to fail. The engine gives a
// reason code with every event, which is what makes that possible without
// guessing.
import { useEffect, useLayoutEffect, useRef, useState } from "react";
import type { Frame, ScenarioInfo } from "../core/session";
import { ESTIMATOR_COLORS } from "../core/session";
import type { LogEntry } from "./Panels";
import { useLang } from "../i18n";
import type { Explained } from "../i18n/narration";

type Tone = "idle" | "nominal" | "fault" | "acting" | "done";

interface Entry extends Explained {
  key: string;
  t: number | null;
  tone: Tone;
}

/**
 * Builds the whole commentary from the scenario and the event log.
 *
 * Derived rather than accumulated, so it survives a restart, a language change
 * and scrubbing without keeping any state of its own.
 */
function buildEntries(
  frame: Frame | null,
  scenario: ScenarioInfo,
  log: LogEntry[],
  labels: Record<string, string>,
  n: ReturnType<typeof useLang>["narration"],
  t: ReturnType<typeof useLang>["t"],
  num: (v: number, d?: number) => string,
  finished: boolean
): Entry[] {
  const entries: Entry[] = [];
  const faults = scenario.faults ?? [];
  const named = (id: string) => t.eventLog.sensorNames[id] ?? id;
  const faultWords = (type: string) => t.eventLog.faultTypes[type] ?? type;
  const start = scenario.fault_start_s;
  const now = frame?.t ?? 0;

  // --- opening -------------------------------------------------------------
  if (faults.length === 0) {
    entries.push({ key: "open", t: 0, tone: "nominal", ...n.openingNominal });
  } else {
    const first = faults.find((f) => f.start_s >= start) ?? faults[0];
    entries.push({
      key: "open",
      t: 0,
      tone: "nominal",
      ...n.opening(faultWords(first.type), named(first.target), num(Math.max(0, start - now), 0)),
    });
  }

  // --- what the fault engine did ------------------------------------------
  for (const e of log) {
    if (e.kind !== "fault") continue;
    const target = named(e.sensor ?? "");
    if (e.activated) {
      const base = n.faultArmed(faultWords(e.faultType ?? ""), target);
      const mechanism = n.faultMechanism[e.faultType ?? ""];
      entries.push({
        key: `fault-on-${e.faultType}-${e.t}`,
        t: e.t,
        tone: "fault",
        what: base.what,
        why: mechanism ? `${mechanism} ${base.why}` : base.why,
      });
    } else {
      entries.push({ key: `fault-off-${e.t}`, t: e.t, tone: "nominal", ...n.faultEnded(target) });
    }
  }

  // --- how each architecture reacted, once per architecture and state ------
  // One entry per (architecture, target, new state): the engine can emit the
  // same transition repeatedly, and repeating the explanation would bury the
  // entries that say something new.
  const seen = new Set<string>();
  for (const e of log) {
    if (e.kind !== "integrity" || !e.to || e.to === "ACTIVE") continue;
    const key = `${e.estimator}-${e.sensor}-${e.to}`;
    if (seen.has(key)) continue;
    seen.add(key);
    const base = n.reaction(
      labels[e.estimator ?? ""] ?? e.estimator ?? "—",
      named(e.sensor ?? ""),
      e.to,
      num(Math.max(0, e.t - start), 1)
    );
    entries.push({
      key: `react-${key}`,
      t: e.t,
      tone: "acting",
      what: base.what,
      why: n.reasonMechanism[e.reason ?? ""] ?? base.why,
    });
  }

  // --- silence is a result too --------------------------------------------
  const reacted = log.some((e) => e.kind === "integrity" && e.to && e.to !== "ACTIVE");
  if (faults.length > 0 && now > start + 6 && !reacted) {
    entries.push({
      key: "silence",
      t: null,
      tone: "fault",
      ...n.silence(num(now - start, 0)),
    });
  }

  // --- an estimate that has left the possible ------------------------------
  if (frame) {
    for (const [id, s] of Object.entries(frame.solutions)) {
      if (-s.d >= 0) continue;
      entries.push({
        key: `below-${id}`,
        t: null,
        tone: "fault",
        ...n.belowGround(labels[id] ?? id),
      });
    }
  }

  // --- the comparison ------------------------------------------------------
  if (finished && frame) {
    const scored = Object.keys(ESTIMATOR_COLORS)
      .map((id) => ({ id, err: frame.solutions[id]?.err_m }))
      .filter((x): x is { id: string; err: number } => Number.isFinite(x.err))
      .sort((a, b) => a.err - b.err);
    if (scored.length > 1) {
      const best = scored[0];
      const worst = scored[scored.length - 1];
      entries.push({
        key: "done",
        t: frame.t,
        tone: "done",
        ...n.finished(
          labels[best.id] ?? best.id,
          num(best.err, 1),
          labels[worst.id] ?? worst.id,
          num(worst.err, 1)
        ),
      });
    }
  }

  entries.sort((a, b) => (a.t ?? Number.MAX_SAFE_INTEGER) - (b.t ?? Number.MAX_SAFE_INTEGER));
  return entries;
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
  const entries = buildEntries(frame, scenario, log, labels, narration, t, num, finished);

  const listRef = useRef<HTMLDivElement>(null);
  const [collapsed, setCollapsed] = useState(false);
  // Follow the newest entry, but only while the reader is already at the
  // bottom. Scrolling up to read what happened is the whole point of keeping a
  // history, and yanking the view back would defeat it.
  const [pinned, setPinned] = useState(true);
  const onScroll = () => {
    const el = listRef.current;
    if (!el) return;
    setPinned(el.scrollHeight - el.clientHeight - el.scrollTop < 24);
  };
  useLayoutEffect(() => {
    const el = listRef.current;
    if (el && pinned) el.scrollTop = el.scrollHeight;
  }, [entries.length, pinned]);
  useEffect(() => setPinned(true), [scenario.id]);

  const objective = scenarioText(scenario.id)?.objective ?? scenario.objective;

  return (
    <section
      className={collapsed ? "narrator is-collapsed" : "narrator"}
      aria-label={narration.title}
    >
      <header className="narrator-head">
        <button
          type="button"
          className="narrator-toggle"
          onClick={() => setCollapsed((c) => !c)}
          aria-expanded={!collapsed}
          title={collapsed ? narration.expand : narration.collapse}
        >
          <span aria-hidden="true">{collapsed ? "▸" : "▾"}</span>
          {narration.title}
        </button>
        {!collapsed && !pinned && (
          <span className="narrator-hint">{narration.historyHint}</span>
        )}
        {collapsed && <span className="narrator-count">{entries.length}</span>}
      </header>
      <p className="narrator-objective">
        <b>{narration.objectiveLabel}</b> — {objective}
      </p>
      <div className="narrator-list" ref={listRef} onScroll={onScroll} role="log">
        {entries.length === 0 && <p className="empty">{narration.empty}</p>}
        {entries.map((e, i) => (
          <article className={`narrator-entry tone-${e.tone}`} key={e.key}>
            <div className="narrator-entry-head">
              {e.t !== null && <span className="narrator-t">{num(e.t, 1)} s</span>}
              <b>{e.what}</b>
            </div>
            <p>{e.why}</p>
            {i === entries.length - 1 && <span className="narrator-latest" aria-hidden="true" />}
          </article>
        ))}
      </div>
    </section>
  );
}
