// AEROLAB RESILIENCE - Web Lab shell.
//
// Screens UI-01 .. UI-08 of section 4.2. The C++ core runs in WebAssembly in
// this page; nothing is recomputed in TypeScript and no server is involved
// (SYS-009, D-011).
//
// Two performance rules hold this together, both learned the hard way:
//
//  1. The samples that feed the charts are accumulated in a REF, not in state.
//     Pushing to a state array on every animation frame copies the whole array
//     each time; at x4 speed that is a few hundred copies a second of an array
//     that grows to thousands of entries, and the page visibly stalls. State is
//     bumped on a timer instead, so React re-renders at a fixed rate whatever
//     the simulation speed is.
//
//  2. The animation loop does NOT depend on the speed or on any other control.
//     It reads them from a ref. An effect that lists `speed` as a dependency
//     tears the loop down and rebuilds it on every speed change, which is
//     exactly the stutter that used to appear when switching x1 to x4.
import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import { AerolabCore, type Frame, type ScenarioInfo } from "./core/session";
import { ESTIMATOR_COLORS, estimatorLabels } from "./core/session";
import { Scene3D, type CameraMode } from "./components/Scene3D";
import { EventLog, Panel, SensorHealth, SolutionTable, type LogEntry } from "./components/Panels";
import { Narrator } from "./components/Narrator";
import { TimeChart, type Series } from "./components/Chart";
import { ErrorBoundary } from "./components/ErrorBoundary";
import { MethodologyView } from "./views/Methodology";
import { FailureCatalogView } from "./views/FailureCatalog";
import { GuideSections } from "./views/Guide";
import { useLang } from "./i18n";
import type { Lang } from "./i18n";

type View =
  | "landing"
  | "scenarios"
  | "lab"
  | "compare"
  | "engineering"
  | "report"
  | "methodology"
  | "failures";

interface Sample {
  t: number;
  errors: Record<string, number>;
  sigma: Record<string, number>;
  nis: Record<string, number>;
  threshold: Record<string, number>;
}

interface CatalogEntry {
  file: string;
  id: string;
  name: string;
  description: string;
  objective: string;
  faultCount: number;
  durationS: number;
  text: string;
}

const REPO_URL = "https://github.com/Bennmane27/aerolab-resilience";
const CHART_SAMPLE_STEP_S = 0.05;  // one chart sample per 50 ms of simulated time
const UI_REFRESH_MS = 120;         // chart / table refresh cadence, speed independent

export function App() {
  const { t, lang, setLang } = useLang();

  const [view, setView] = useState<View>("landing");
  const [core, setCore] = useState<AerolabCore | null>(null);
  const [loadError, setLoadError] = useState<string | null>(null);
  const [catalog, setCatalog] = useState<CatalogEntry[]>([]);
  const [configText, setConfigText] = useState("");
  const [scenario, setScenario] = useState<ScenarioInfo | null>(null);
  const [selectedFile, setSelectedFile] = useState<string | null>(null);
  const [frame, setFrame] = useState<Frame | null>(null);
  const [running, setRunning] = useState(false);
  const [speed, setSpeed] = useState(1);
  const [seed, setSeed] = useState(1);
  const [report, setReport] = useState<Record<string, unknown> | null>(null);
  const [visible, setVisible] = useState<string[]>(Object.keys(ESTIMATOR_COLORS));
  const [camera, setCamera] = useState<CameraMode>("chase");
  const [follow, setFollow] = useState(true);
  const [showLegend, setShowLegend] = useState(true);
  const [expanded, setExpanded] = useState(false);
  const [scrub, setScrub] = useState<number | null>(null);
  const [runKey, setRunKey] = useState(0);
  const [tick, setTick] = useState(0);  // bumped on a timer to refresh the views

  // The 3D view reads THIS, every animation frame, and never a React prop.
  //
  // Calling setFrame on every step of the run loop re-rendered the whole tree —
  // narrator, sensor table, solutions, event log, legend and the Scene3D props
  // — sixty times a second. Measured, that cost 71 ms per frame: 14.5 fps, with
  // the aircraft jumping about five metres between two rendered images and up
  // to ten. At any real zoom that is not motion, it is a slideshow.
  //
  // So the two clocks are now separate. The scene follows the simulation at
  // display rate through this ref; the panels refresh on the fixed UI tick,
  // which is all a number being read by a human needs.
  const liveFrameRef = useRef<Frame | null>(null);
  const samplesRef = useRef<Sample[]>([]);
  const logRef = useRef<LogEntry[]>([]);
  const framesRef = useRef<Frame[]>([]);
  const speedRef = useRef(speed);
  const runningRef = useRef(running);
  const scenarioRef = useRef<ScenarioInfo | null>(scenario);
  const coreRef = useRef<AerolabCore | null>(core);
  const lastSampleTRef = useRef(-1);

  speedRef.current = speed;
  runningRef.current = running;
  scenarioRef.current = scenario;
  coreRef.current = core;

  const labels = useMemo(() => estimatorLabels(lang), [lang]);

  // ------------------------------------------------------------ bootstrap --
  useEffect(() => {
    let cancelled = false;
    (async () => {
      const loaded = await AerolabCore.load();
      if (cancelled) return;
      if (!loaded.ok) {
        setLoadError(`${t.errors.coreLoad}\n${loaded.error}`);
        return;
      }
      setCore(loaded.value);

      try {
        const base = import.meta.env.BASE_URL;
        const index = (await (await fetch(`${base}data/index.json`)).json()) as {
          scenarios: string[];
          config: string;
        };
        const config = await (await fetch(`${base}data/${index.config}`)).text();
        if (cancelled) return;
        setConfigText(config);

        const entries: CatalogEntry[] = [];
        for (const file of index.scenarios) {
          const text = await (await fetch(`${base}data/scenarios/${file}`)).text();
          entries.push({
            file,
            text,
            id: readField(text, "id") ?? file.replace(".yaml", ""),
            name: readField(text, "name") ?? "",
            description: readField(text, "description") ?? "",
            objective: readField(text, "objective") ?? "",
            faultCount: (text.match(/^\s{2}- id: F-/gm) ?? []).length,
            durationS: Number(readField(text, "duration_s") ?? "0"),
          });
        }
        if (!cancelled) setCatalog(entries);
      } catch (e) {
        if (!cancelled) setLoadError(`${t.errors.catalogLoad}\n${String(e)}`);
      }
    })();
    return () => {
      cancelled = true;
    };
    // Intentionally runs once: re-running it on a language change would reload
    // the whole WebAssembly module for a label.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  // Browser fullscreen and the expanded layout are kept in step: leaving
  // fullscreen with Escape has to bring the strip back, or the page is left in
  // a state the button no longer describes.
  const toggleExpanded = useCallback(() => {
    const next = !document.fullscreenElement;
    if (next) {
      document.documentElement.requestFullscreen?.().catch(() => setExpanded(true));
    } else {
      document.exitFullscreen?.().catch(() => setExpanded(false));
    }
    setExpanded(next);
  }, []);

  useEffect(() => {
    const sync = () => setExpanded(document.fullscreenElement !== null);
    document.addEventListener("fullscreenchange", sync);
    return () => document.removeEventListener("fullscreenchange", sync);
  }, []);

  const clearRunState = useCallback(() => {
    samplesRef.current = [];
    logRef.current = [];
    framesRef.current = [];
    lastSampleTRef.current = -1;
    setReport(null);
    setScrub(null);
    liveFrameRef.current = null;
    setFrame(null);
    setRunKey((k) => k + 1);
  }, []);

  const openScenario = useCallback(
    (entry: CatalogEntry) => {
      const c = coreRef.current;
      if (!c) return;
      c.recreateSession();
      const loaded = c.loadScenario(entry.text, entry.file, configText);
      if (!loaded.ok) {
        setLoadError(loaded.error);
        return;
      }
      const info = c.scenarioInfo();
      if (!info.ok) {
        setLoadError(info.error);
        return;
      }
      const r = c.reset(info.value.seed);
      if (!r.ok) {
        setLoadError(r.error);
        return;
      }
      setSeed(info.value.seed);
      setScenario(info.value);
      setSelectedFile(entry.file);
      clearRunState();
      const f = c.frame();
      if (f.ok) {
        liveFrameRef.current = f.value;
        setFrame(f.value);
      }
      setRunning(true);
      setView("lab");
    },
    [configText, clearRunState]
  );

  // NOTE: takes an explicit optional number. The button below calls it as
  // `() => restart()`, never as `onClick={restart}` — passing the MouseEvent as
  // the seed is what used to abort the WebAssembly module and blank the page.
  const restart = useCallback(
    (nextSeed?: number) => {
      const c = coreRef.current;
      if (!c || !scenarioRef.current) return;
      const useSeed = typeof nextSeed === "number" && Number.isFinite(nextSeed) ? nextSeed : seed;
      const r = c.reset(useSeed);
      if (!r.ok) {
        setLoadError(r.error);
        return;
      }
      setSeed(useSeed);
      clearRunState();
      const f = c.frame();
      if (f.ok) {
        liveFrameRef.current = f.value;
        setFrame(f.value);
      }
      setRunning(true);
    },
    [seed, clearRunState]
  );

  // ------------------------------------------------------- the run loop -----
  // Depends only on `core` and `runKey`. Speed and pause are read from refs.
  useEffect(() => {
    if (!core || !scenario) return;
    let raf = 0;
    let previous = 0;
    let accumulator = 0;

    const step = (now: number) => {
      raf = requestAnimationFrame(step);
      const currentScenario = scenarioRef.current;
      if (!currentScenario) return;
      if (!runningRef.current) {
        previous = now;
        return;
      }
      const elapsed = previous ? Math.min((now - previous) / 1000, 0.2) : 0;
      previous = now;
      accumulator += elapsed * speedRef.current;

      const dt = currentScenario.dt_s;
      let steps = Math.floor(accumulator / dt);
      if (steps <= 0) return;
      accumulator -= steps * dt;
      steps = Math.min(steps, 600);

      core.step(steps);
      const f = core.frame();
      if (!f.ok) return;
      const value = f.value;
      liveFrameRef.current = value;
      framesRef.current.push(value);

      // Charts are sampled on simulated time, so the density of the plot does
      // not depend on how fast the run is being played.
      if (value.t - lastSampleTRef.current >= CHART_SAMPLE_STEP_S) {
        lastSampleTRef.current = value.t;
        const errors: Record<string, number> = {};
        const sigma: Record<string, number> = {};
        for (const [id, s] of Object.entries(value.solutions)) {
          errors[id] = s.err_m;
          sigma[id] = s.sigma_h_m;
        }
        const nis: Record<string, number> = {};
        const threshold: Record<string, number> = {};
        for (const [id, s] of Object.entries(value.sensors)) {
          nis[id] = s.nis;
          threshold[id] = s.threshold;
        }
        samplesRef.current.push({ t: value.t, errors, sigma, nis, threshold });
      }

      if (value.events.length > 0 || value.faults.length > 0) {
        const additions: LogEntry[] = [];
        for (const e of value.faults) {
          const ev = eventsRef.current;
          // The fault type and the target keep their engine identifiers: they
          // are what the telemetry and the scenario file say. The sentence
          // beside them is what gets translated, and it names the fault in
          // words rather than repeating the token.
          additions.push({
            t: e.t ?? value.t,
            kind: "fault",
            headline: `${e.activated ? ev.faultArmed : ev.faultEnded} — ${e.type} · ${e.target}`,
            faultType: e.type,
            sensor: e.target,
            activated: e.activated,
            why: e.activated
              ? ev.faultArmedWhy(ev.faultTypes[e.type] ?? e.type, e.target)
              : ev.faultEndedWhy(ev.faultTypes[e.type] ?? e.type, e.target),
          });
        }
        for (const e of value.events) {
          additions.push({
            t: e.t ?? value.t,
            kind: "integrity",
            headline: `${labelsRef.current[e.estimator] ?? e.estimator} · ${eventsRef.current.sensorNames[e.sensor] ?? e.sensor} : ${e.from} → ${e.to}`,
            estimator: e.estimator,
            sensor: e.sensor,
            to: e.to,
            reason: e.reason,
            why: `${reasonRef.current[e.reason] ?? e.reason} (${e.statistic.toPrecision(3)} / ${e.threshold.toPrecision(3)})`,
          });
        }
        logRef.current = [...logRef.current, ...additions].slice(-500);
      }

      if (core.finished()) {
        runningRef.current = false;
        setRunning(false);
        const r = core.report();
        if (r.ok) setReport(r.value);
      }
    };

    raf = requestAnimationFrame(step);
    return () => cancelAnimationFrame(raf);
  }, [core, scenario, runKey]);

  // The loop reads translated labels through refs so that changing language
  // does not restart it.
  const labelsRef = useRef(labels);
  const reasonRef = useRef(t.reasonHelp);
  const eventsRef = useRef(t.eventLog);
  labelsRef.current = labels;
  reasonRef.current = t.reasonHelp;
  eventsRef.current = t.eventLog;

  // Fixed-rate refresh: decouples what the eye sees from how fast the core runs.
  // This is also what publishes the frame to the panels — see liveFrameRef.
  useEffect(() => {
    const id = window.setInterval(() => {
      if (liveFrameRef.current) setFrame(liveFrameRef.current);
      setTick((n) => n + 1);
    }, UI_REFRESH_MS);
    return () => window.clearInterval(id);
  }, []);

  // Keyboard transport (UI-021).
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if (view !== "lab") return;
      const target = e.target as HTMLElement | null;
      if (target && ["INPUT", "SELECT", "TEXTAREA", "BUTTON"].includes(target.tagName)) return;
      if (e.code === "Space") {
        e.preventDefault();
        setRunning((r) => !r);
      } else if (e.key === "r" || e.key === "R") {
        restart();
      }
    };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, [view, restart]);

  const samples = samplesRef.current;
  const displayFrame =
    scrub !== null && framesRef.current[scrub] ? framesRef.current[scrub] : frame;
  const faultWindow =
    scenario && scenario.fault_start_s >= 0
      ? { start: scenario.fault_start_s, end: scenario.fault_end_s }
      : null;

  const errorSeries = useMemo<Series[]>(
    () =>
      visible.map((id) => ({
        id,
        label: labels[id] ?? id,
        color: ESTIMATOR_COLORS[id] ?? "#888",
        unit: "m",
        points: samples.map((s) => [s.t, s.errors[id] ?? 0] as [number, number]),
      })),
    // `tick` is the refresh trigger; `samples` is a ref array mutated in place.
    // eslint-disable-next-line react-hooks/exhaustive-deps
    [visible, labels, tick]
  );

  return (
    <div className="app">
      <header className="topbar">
        <div className="brand">
          AEROLAB <span>RESILIENCE</span>
        </div>
        <div className="tagline">{t.tagline}</div>
        <nav className="nav" aria-label="Main">
          {(
            [
              ["landing", t.nav.landing],
              ["scenarios", t.nav.scenarios],
              ["lab", t.nav.lab],
              ["compare", t.nav.compare],
              ["engineering", t.nav.engineering],
              ["report", t.nav.report],
              ["methodology", t.nav.methodology],
              ["failures", t.nav.failures],
            ] as Array<[View, string]>
          ).map(([id, label]) => (
            <button
              key={id}
              type="button"
              className={view === id ? "active" : ""}
              onClick={() => setView(id)}
              aria-current={view === id ? "page" : undefined}
              disabled={!core && !["landing", "methodology", "failures"].includes(id)}
            >
              {label}
            </button>
          ))}
        </nav>
        <div className="lang-switch" role="group" aria-label={t.langLabel}>
          {(["fr", "en"] as Lang[]).map((l) => (
            <button
              key={l}
              type="button"
              className={lang === l ? "active" : ""}
              onClick={() => setLang(l)}
              aria-pressed={lang === l}
            >
              {l.toUpperCase()}
            </button>
          ))}
        </div>
      </header>

      {/* UI-022: stated on every screen, not buried. */}
      <div className="disclaimer">{t.disclaimer}</div>

      <main className="content">
        {loadError && (
          <div className="pad">
            <div className="banner-error">{loadError}</div>
          </div>
        )}

        <ErrorBoundary
          message={t.errors.recovered}
          resetLabel={t.errors.reload}
          onReset={() => setView("landing")}
        >
          {view === "landing" && (
            <Landing onStart={() => setView("scenarios")} ready={core !== null} core={core} />
          )}

          {view === "scenarios" && (
            <ScenarioSelect catalog={catalog} selected={selectedFile} onOpen={openScenario} />
          )}

          {view === "lab" && (
            <LiveLab
              frame={displayFrame}
              liveFrame={liveFrameRef}
              scenario={scenario}
              log={logRef.current}
              running={running}
              speed={speed}
              seed={seed}
              visible={visible}
              camera={camera}
              follow={follow}
              showLegend={showLegend}
              expanded={expanded}
              labels={labels}
              runKey={runKey}
              frameCount={framesRef.current.length}
              scrub={scrub}
              errorSeries={errorSeries}
              faultWindow={faultWindow}
              onToggleRun={() => setRunning((r) => !r)}
              onRestart={restart}
              onSpeed={setSpeed}
              onSeed={(s) => restart(s)}
              onVisible={setVisible}
              onCamera={setCamera}
              onFollow={setFollow}
              onShowLegend={setShowLegend}
              onToggleExpanded={toggleExpanded}
              onScrub={setScrub}
            />
          )}

          {view === "compare" && (
            <ComparePanel
              samples={samples}
              scenario={scenario}
              faultWindow={faultWindow}
              frame={displayFrame}
              labels={labels}
            />
          )}

          {view === "engineering" && (
            <EngineeringPanel
              samples={samples}
              faultWindow={faultWindow}
              frame={displayFrame}
              labels={labels}
            />
          )}

          {view === "report" && (
            <ReportPanel report={report} scenario={scenario} running={running} labels={labels} />
          )}

          {view === "methodology" && (
            <MethodologyView repoUrl={REPO_URL} build={core?.build ?? null} />
          )}

          {view === "failures" && <FailureCatalogView />}
        </ErrorBoundary>
      </main>
    </div>
  );
}

// --------------------------------------------------------------- landing ---

function Landing({
  onStart,
  ready,
  core,
}: {
  onStart: () => void;
  ready: boolean;
  core: AerolabCore | null;
}) {
  const { t } = useLang();
  return (
    <div className="pad">
      <div className="hero">
        <h1>{t.landing.title}</h1>
        <div className="quote">{t.landing.quote}</div>
        <p>{t.landing.lead1}</p>
        <p>{t.landing.lead2}</p>
        <div className="cta">
          <button type="button" className="primary" onClick={onStart} disabled={!ready}>
            {ready ? t.landing.start : t.landing.loading}
          </button>
          <a href={REPO_URL}>
            <button type="button">{t.landing.source}</button>
          </a>
        </div>
      </div>

      <div className="ladder">
        {[
          [t.landing.audience.publicLabel, t.landing.audience.publicText],
          [t.landing.audience.recruiterLabel, t.landing.audience.recruiterText],
          [t.landing.audience.engineerLabel, t.landing.audience.engineerText],
        ].map(([label, text]) => (
          <div className="panel step" key={label}>
            <div className="who">{label}</div>
            <div>{text}</div>
          </div>
        ))}
      </div>

      {/* The explanation used to be a separate page. Someone arriving here got
          three sentences and a button, and had to guess that the rest was one
          menu item away. */}
      <GuideSections onStart={onStart} />

      {core && (
        <p className="footer">
          core {core.build.version} · commit {core.build.commit} · {core.build.compiler} · schema v
          {core.build.telemetry_schema}
        </p>
      )}
    </div>
  );
}

// ------------------------------------------------------- scenario select ---

function ScenarioSelect({
  catalog,
  selected,
  onOpen,
}: {
  catalog: CatalogEntry[];
  selected: string | null;
  onOpen: (entry: CatalogEntry) => void;
}) {
  const { t, num, scenarioText } = useLang();
  if (catalog.length === 0) {
    return (
      <div className="pad">
        <p className="empty">{t.scenarios.loading}</p>
      </div>
    );
  }
  return (
    <div className="pad">
      <h2 style={{ marginTop: 0 }}>{t.scenarios.title}</h2>
      <p className="lead">{t.scenarios.lead}</p>
      <div className="scenario-grid">
        {catalog.map((entry) => {
          // The scenario files are technical English; the interface is not.
          const localised = scenarioText(entry.id);
          return (
          <button
            type="button"
            key={entry.file}
            className={`panel scenario-card ${selected === entry.file ? "active" : ""}`}
            onClick={() => onOpen(entry)}
          >
            <span className="id">{entry.id}</span>
            <span className="name">{localised?.name ?? entry.name}</span>
            <span className="desc">{localised?.description ?? entry.description}</span>
            <span className="tags">
              <span className="tag">
                {entry.faultCount === 0
                  ? t.scenarios.nominal
                  : t.scenarios.faultCount(entry.faultCount)}
              </span>
              <span className="tag">
                {t.scenarios.duration} {num(entry.durationS, 0)} s
              </span>
            </span>
          </button>
          );
        })}
      </div>
    </div>
  );
}

// -------------------------------------------------------------- live lab ---

interface LiveLabProps {
  frame: Frame | null;
  /** Live pose for the 3D view, updated every step. See App. */
  liveFrame: React.RefObject<Frame | null>;
  scenario: ScenarioInfo | null;
  log: LogEntry[];
  running: boolean;
  speed: number;
  seed: number;
  visible: string[];
  camera: CameraMode;
  follow: boolean;
  showLegend: boolean;
  expanded: boolean;
  labels: Record<string, string>;
  runKey: number;
  frameCount: number;
  scrub: number | null;
  errorSeries: Series[];
  faultWindow: { start: number; end: number } | null;
  onToggleRun: () => void;
  onRestart: (seed?: number) => void;
  onSpeed: (s: number) => void;
  onSeed: (s: number) => void;
  onVisible: (v: string[]) => void;
  onCamera: (c: CameraMode) => void;
  onShowLegend: (v: boolean) => void;
  onToggleExpanded: () => void;
  onFollow: (f: boolean) => void;
  onScrub: (i: number | null) => void;
}

function LiveLab(props: LiveLabProps) {
  const { t, num } = useLang();
  const { frame, scenario } = props;
  if (!scenario) {
    return (
      <div className="pad">
        <p className="empty">{t.lab.chooseFirst}</p>
      </div>
    );
  }

  return (
    <div className={props.expanded ? "lab is-expanded" : "lab"}>
      <div className="panel viewport">
        <Scene3D
          scenario={scenario}
          visibleEstimators={props.visible}
          liveFrame={props.liveFrame}
          scrubFrame={props.scrub !== null ? frame : null}
          speed={props.running ? props.speed : 0}
          cameraMode={props.camera}
          follow={props.follow}
          labels={props.labels}
          runKey={props.runKey}
        />

        {props.showLegend && (
          <ViewportLegend frame={frame} labels={props.labels} visible={props.visible} />
        )}

        {/* UI-004, on the view itself and not only in a legend. */}
        <div className="truth-badge">
          <b>— — {t.lab.truthBadge}</b> — {t.lab.truthNotAvailable}
          <br />
          {scenario.id} · {t.lab.seed} {props.seed} · t = {num(frame?.t ?? 0, 2)} s ·{" "}
          {frame?.truth.phase ?? "—"}
        </div>

        <div className="viewport-readout">
          <span>
            {t.lab.altitude} <b>{num(-(frame?.truth.d ?? 0), 0)}</b>
            <span className="unit"> m</span>
          </span>
          <span>
            {t.lab.groundSpeed}{" "}
            <b>
              {num(
                Math.hypot(frame?.truth.vn ?? 0, frame?.truth.ve ?? 0),
                0
              )}
            </b>
            <span className="unit"> m/s</span>
          </span>
          <span>
            {t.lab.roll} <b>{num(frame?.truth.roll_deg ?? 0, 1)}</b>
            <span className="unit">°</span>
          </span>
          <span>
            {t.lab.pitch} <b>{num(frame?.truth.pitch_deg ?? 0, 1)}</b>
            <span className="unit">°</span>
          </span>
        </div>

        <div className="viewport-hint">{t.lab.cameraHint}</div>

        {/* On the view rather than in the side column: this is the reading of
            what the 3D is showing, and it belongs next to it. */}
        <Narrator
          frame={frame}
          scenario={scenario}
          log={props.log}
          labels={props.labels}
          finished={
            !props.running && props.frameCount > 0 && (frame?.t ?? 0) >= scenario.duration_s - 0.01
          }
        />

        <button
          type="button"
          className="viewport-expand"
          onClick={props.onToggleExpanded}
          title={props.expanded ? t.lab.collapseHint : t.lab.expandHint}
        >
          {props.expanded ? `⤡ ${t.lab.collapse}` : `⤢ ${t.lab.expand}`}
        </button>

      </div>

      <div className="side">
        <Panel title={t.lab.transport}>
          <div className="transport">
            <button type="button" className="primary" onClick={() => props.onToggleRun()}>
              {props.running ? t.lab.pause : t.lab.resume}
            </button>
            {/* Wrapped on purpose: passing the handler directly would hand the
                MouseEvent to the seed argument. */}
            <button type="button" onClick={() => props.onRestart()}>
              {t.lab.restart}
            </button>
            <span className="clock">
              {num(frame?.t ?? 0, 2)} / {num(scenario.duration_s, 0)} s
            </span>
          </div>

          <div className="control-row">
            <span className="control-label">{t.lab.speed}</span>
            {[0.25, 1, 4].map((s) => (
              <button
                type="button"
                key={s}
                className={props.speed === s ? "active" : ""}
                onClick={() => props.onSpeed(s)}
              >
                ×{s}
              </button>
            ))}
          </div>

          <div className="control-row">
            <label className="control-label" htmlFor="seed">
              {t.lab.seed}
            </label>
            <input
              id="seed"
              type="number"
              defaultValue={props.seed}
              key={props.seed}
              style={{ width: 120 }}
              onKeyDown={(e) => {
                if (e.key === "Enter") {
                  const v = Number((e.target as HTMLInputElement).value);
                  if (Number.isFinite(v)) props.onSeed(v);
                }
              }}
            />
            <span className="control-hint">{t.lab.seedHint}</span>
          </div>

          <div className="control-row" style={{ display: "block" }}>
            <label className="sr-only" htmlFor="scrub">
              {t.lab.scrub}
            </label>
            <input
              id="scrub"
              type="range"
              min={0}
              max={Math.max(0, props.frameCount - 1)}
              value={props.scrub ?? Math.max(0, props.frameCount - 1)}
              disabled={props.running || props.frameCount === 0}
              onChange={(e) => props.onScrub(Number(e.target.value))}
              style={{ width: "100%" }}
            />
            <div className="control-hint">
              {props.running
                ? t.lab.scrubHint
                : t.lab.scrubFrame((props.scrub ?? props.frameCount - 1) + 1, props.frameCount)}
            </div>
          </div>

          <div className="control-row">
            <span className="control-label">{t.lab.camera}</span>
            {(
              [
                ["chase", t.lab.cameraChase],
                ["map", t.lab.cameraMap],
                ["runway", t.lab.cameraRunway],
                ["free", t.lab.cameraFree],
              ] as Array<[CameraMode, string]>
            ).map(([mode, label]) => (
              <button
                type="button"
                key={mode}
                className={props.camera === mode ? "active" : ""}
                onClick={() => props.onCamera(mode)}
              >
                {label}
              </button>
            ))}
          </div>

          <div className="control-row">
            <label className="checkbox">
              <input
                type="checkbox"
                checked={props.follow}
                onChange={(e) => props.onFollow(e.target.checked)}
              />
              {t.lab.followAircraft}
            </label>
            <label className="checkbox">
              <input
                type="checkbox"
                checked={props.showLegend}
                onChange={(e) => props.onShowLegend(e.target.checked)}
              />
              {t.lab.showLegend}
            </label>
          </div>

          <div className="control-hint">{t.lab.cameraModeHint}</div>
        </Panel>

        {/* No per-panel cap: the side column is the scroll container, so every
            panel keeps its full height and one scrollbar reaches the end. */}
        <Panel title={t.lab.sensorHealth}>
          <SensorHealth frame={frame} />
        </Panel>

        <Panel title={t.lab.solutions}>
          <SolutionTable frame={frame} labels={props.labels} />
          <div className="chip-row">
            {Object.keys(ESTIMATOR_COLORS).map((id) => (
              <button
                type="button"
                key={id}
                className={`chip ${props.visible.includes(id) ? "active" : ""}`}
                onClick={() =>
                  props.onVisible(
                    props.visible.includes(id)
                      ? props.visible.filter((x) => x !== id)
                      : [...props.visible, id]
                  )
                }
              >
                <i style={{ background: ESTIMATOR_COLORS[id] }} />
                {props.labels[id]}
              </button>
            ))}
          </div>
        </Panel>
      </div>

      <div className="strip">
        <Panel title={t.lab.errorChart} scroll="fill">
          <TimeChart
            series={props.errorSeries}
            yLabel={t.chart.positionError}
            faultWindow={props.faultWindow}
            height={230}
            logY
          />
        </Panel>
        <Panel title={t.lab.events} scroll="fill">
          <EventLog entries={props.log} empty={t.lab.noEvents} />
        </Panel>
      </div>
    </div>
  );
}

/**
 * Legend for the 3D view.
 *
 * This replaces the labels that used to float beside each marker. With five
 * architectures a couple of metres apart — the normal case, and the one worth
 * reading — those labels covered each other and the aircraft, and abbreviating
 * them did not fix it: the problem was text in the scene at all. The scene now
 * carries shape, colour and position; this carries the words and the numbers,
 * in one place that never moves and never overlaps anything.
 */
function ViewportLegend({
  frame,
  labels,
  visible,
}: {
  frame: Frame | null;
  labels: Record<string, string>;
  visible: string[];
}) {
  const { t, num } = useLang();
  return (
    <div className="viewport-legend">
      <h4>{t.lab.legend}</h4>
      <div className="legend-row legend-truth">
        <span className="legend-swatch legend-aircraft" aria-hidden="true">
          ✈
        </span>
        <span className="legend-name">{t.lab.truthShort}</span>
      </div>
      {Object.keys(ESTIMATOR_COLORS).map((id) => {
        const solution = frame?.solutions[id];
        const shown = visible.includes(id);
        const belowGround = solution !== undefined && -solution.d < 0;
        return (
          <div className={shown ? "legend-row" : "legend-row is-hidden"} key={id}>
            <span
              className="legend-swatch"
              style={{ background: ESTIMATOR_COLORS[id] }}
              aria-hidden="true"
            />
            <span className="legend-name">{labels[id] ?? id}</span>
            <span className="legend-value">
              {solution ? num(solution.err_m, 1) : "—"}
              <span className="unit"> m</span>
              {belowGround && <b className="legend-below"> ▼</b>}
            </span>
          </div>
        );
      })}
      <p className="legend-note">{t.lab.legendScale}</p>
    </div>
  );
}

// ---------------------------------------------------------------- compare --

function ComparePanel({
  samples,
  scenario,
  faultWindow,
  frame,
  labels,
}: {
  samples: Sample[];
  scenario: ScenarioInfo | null;
  faultWindow: { start: number; end: number } | null;
  frame: Frame | null;
  labels: Record<string, string>;
}) {
  const { t, num, scenarioText } = useLang();
  if (!scenario) {
    return (
      <div className="pad">
        <p className="empty">{t.lab.chooseFirst}</p>
      </div>
    );
  }
  const ids = Object.keys(ESTIMATOR_COLORS);
  const stats = ids.map((id) => {
    const values = samples.map((s) => s.errors[id]).filter((v) => v !== undefined);
    const sorted = [...values].sort((a, b) => a - b);
    const rms = Math.sqrt(values.reduce((acc, v) => acc + v * v, 0) / Math.max(1, values.length));
    return {
      id,
      rms,
      p95: sorted.length ? sorted[Math.max(0, Math.ceil(0.95 * sorted.length) - 1)] : 0,
      max: sorted.length ? sorted[sorted.length - 1] : 0,
      mode: frame?.solutions[id]?.mode ?? "—",
    };
  });

  return (
    <div className="pad">
      <h2 style={{ marginTop: 0 }}>
        {t.compare.title} — {scenario.id} {scenarioText(scenario.id)?.name ?? scenario.name}
      </h2>
      <p className="lead">{t.compare.lead}</p>

      <Panel title={t.compare.chartTitle}>
        <TimeChart
          series={ids.map((id) => ({
            id,
            label: labels[id] ?? id,
            color: ESTIMATOR_COLORS[id],
            unit: "m",
            points: samples.map((s) => [s.t, s.errors[id] ?? 0] as [number, number]),
          }))}
          yLabel={t.chart.positionError}
          faultWindow={faultWindow}
          height={280}
          logY
        />
      </Panel>

      <Panel title={t.compare.metricsTitle} style={{ marginTop: 12 }}>
        <table>
          <thead>
            <tr>
              <th scope="col">{t.panels.architecture}</th>
              <th scope="col">
                {t.compare.rmse} <span className="unit">m</span>
              </th>
              <th scope="col">
                {t.compare.p95} <span className="unit">m</span>
              </th>
              <th scope="col">
                {t.compare.max} <span className="unit">m</span>
              </th>
              <th scope="col">{t.compare.modeNow}</th>
            </tr>
          </thead>
          <tbody>
            {stats.map((s) => (
              <tr key={s.id}>
                <td>
                  <span className="swatch" style={{ background: ESTIMATOR_COLORS[s.id] }} />
                  {labels[s.id]}
                </td>
                <td className="num">{num(s.rms, 2)}</td>
                <td className="num">{num(s.p95, 2)}</td>
                <td className="num">{num(s.max, 2)}</td>
                <td className={`num mode-${s.mode}`}>{s.mode}</td>
              </tr>
            ))}
          </tbody>
        </table>
        <p className="caveat">{t.compare.caveat}</p>
      </Panel>
    </div>
  );
}

// ------------------------------------------------------------ engineering --

function EngineeringPanel({
  samples,
  faultWindow,
  frame,
  labels,
}: {
  samples: Sample[];
  faultWindow: { start: number; end: number } | null;
  frame: Frame | null;
  labels: Record<string, string>;
}) {
  const { t, num } = useLang();
  const sensorIds = frame ? Object.keys(frame.sensors) : [];
  const gnssThreshold = frame?.sensors.gnss?.threshold ?? 0;
  const sensorColors = ["#4d9be0", "#3fbf8f", "#e8c95a", "#cf5b7f", "#b58cd8"];

  return (
    <div className="pad">
      <h2 style={{ marginTop: 0 }}>{t.engineering.title}</h2>
      <p className="lead">{t.engineering.lead}</p>

      <Panel title={t.engineering.nisTitle}>
        <TimeChart
          series={sensorIds.map((id, i) => ({
            id,
            label: t.panels.sensorNames[id] ?? id,
            color: sensorColors[i % sensorColors.length],
            points: samples.map((s) => [s.t, s.nis[id] ?? 0] as [number, number]),
          }))}
          yLabel={t.chart.nis}
          faultWindow={faultWindow}
          threshold={
            gnssThreshold > 0
              ? { value: gnssThreshold, label: `${t.panels.threshold} ${num(gnssThreshold, 1)}` }
              : undefined
          }
          height={250}
          logY
        />
      </Panel>

      <Panel title={t.engineering.sigmaTitle} style={{ marginTop: 12 }}>
        <TimeChart
          series={Object.keys(ESTIMATOR_COLORS)
            .filter((id) => id !== "gnss_only")
            .map((id) => ({
              id,
              label: labels[id] ?? id,
              color: ESTIMATOR_COLORS[id],
              unit: "m",
              points: samples.map((s) => [s.t, s.sigma[id] ?? 0] as [number, number]),
            }))}
          yLabel={t.chart.sigmaHorizontal}
          faultWindow={faultWindow}
          height={250}
          logY
        />
        <p className="caveat">{t.engineering.sigmaCaveat}</p>
      </Panel>

      {frame?.raim && (
        <Panel title={t.engineering.raimTitle} style={{ marginTop: 12 }}>
          <dl className="kv">
            <dt>{t.engineering.raimStatistic}</dt>
            <dd>{num(frame.raim.statistic, 2)}</dd>
            <dt>{t.engineering.raimThreshold}</dt>
            <dd>{num(frame.raim.threshold, 2)}</dd>
            <dt>{t.engineering.raimDetected}</dt>
            <dd>{frame.raim.detected ? t.engineering.yes : t.engineering.no}</dd>
            <dt>{t.engineering.raimExcluded}</dt>
            <dd>
              {frame.raim.excluded ? `#${frame.raim.excluded_satellite}` : t.engineering.raimNone}
            </dd>
          </dl>
          <p className="caveat">{t.engineering.raimCaveat}</p>
        </Panel>
      )}
    </div>
  );
}

// ----------------------------------------------------------------- report --

function ReportPanel({
  report,
  scenario,
  running,
  labels,
}: {
  report: Record<string, unknown> | null;
  scenario: ScenarioInfo | null;
  running: boolean;
  labels: Record<string, string>;
}) {
  const { t, num } = useLang();
  if (!scenario) {
    return (
      <div className="pad">
        <p className="empty">{t.lab.chooseFirst}</p>
      </div>
    );
  }
  if (!report) {
    return (
      <div className="pad">
        <p className="empty">{running ? t.report.running : t.report.none}</p>
      </div>
    );
  }

  const channels = (report.channels as Array<Record<string, unknown>>) ?? [];
  const failures = (report.verdict_failures as string[]) ?? [];
  const json = JSON.stringify(report, null, 2);

  const download = () => {
    const blob = new Blob([json], { type: "application/json" });
    const url = URL.createObjectURL(blob);
    const a = document.createElement("a");
    a.href = url;
    a.download = `${String(report.scenario_id)}_seed${String(report.seed)}_manifest.json`;
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
    URL.revokeObjectURL(url);
  };

  return (
    <div className="pad">
      <h2 style={{ marginTop: 0 }}>{t.report.title}</h2>

      <Panel title={t.report.provenance}>
        <dl className="kv">
          <dt>{t.report.scenario}</dt>
          <dd>
            {String(report.scenario_id)} — {String(report.scenario_name)}
          </dd>
          <dt>{t.report.seed}</dt>
          <dd>{String(report.seed)}</dd>
          <dt>{t.report.commit}</dt>
          <dd>{String(report.commit)}</dd>
          <dt>{t.report.compiler}</dt>
          <dd>{String(report.compiler)}</dd>
          <dt>{t.report.scenarioHash}</dt>
          <dd>{String(report.scenario_hash)}</dd>
          <dt>{t.report.configHash}</dt>
          <dd>{String(report.config_hash)}</dd>
          <dt>{t.report.verdict}</dt>
          <dd className={report.verdict === "PASS" ? "mode-NORMAL" : "mode-UNSAFE"}>
            {String(report.verdict)}
          </dd>
        </dl>
      </Panel>

      {failures.length > 0 && (
        <Panel title={t.report.failures} style={{ marginTop: 12 }}>
          <ul className="mono-list">
            {failures.map((f, i) => (
              <li key={i}>{f}</li>
            ))}
          </ul>
        </Panel>
      )}

      <Panel title={t.report.metrics} style={{ marginTop: 12 }}>
        <div style={{ overflowX: "auto" }}>
          <table>
            <thead>
              <tr>
                <th scope="col">{t.panels.architecture}</th>
                <th scope="col">
                  RMSE <span className="unit">m</span>
                </th>
                <th scope="col">
                  P95 <span className="unit">m</span>
                </th>
                <th scope="col">
                  Max <span className="unit">m</span>
                </th>
                <th scope="col">
                  {t.report.ttd} <span className="unit">s</span>
                </th>
                <th scope="col">
                  {t.report.tti} <span className="unit">s</span>
                </th>
                <th scope="col">{t.report.availability}</th>
                <th scope="col">{t.report.nisPerDof}</th>
                <th scope="col">{t.panels.mode}</th>
              </tr>
            </thead>
            <tbody>
              {channels.map((c, i) => (
                <tr key={i}>
                  <td>{labels[String(c.estimator)] ?? String(c.estimator)}</td>
                  <td className="num">{num(Number(c.position_rmse_m), 2)}</td>
                  <td className="num">{num(Number(c.error_p95_m), 2)}</td>
                  <td className="num">{num(Number(c.error_max_m), 2)}</td>
                  <td className="num">
                    {c.time_to_detect_s === null ? "—" : num(Number(c.time_to_detect_s), 2)}
                  </td>
                  <td className="num">
                    {c.time_to_isolate_s === null ? "—" : num(Number(c.time_to_isolate_s), 2)}
                  </td>
                  <td className="num">{num(Number(c.availability) * 100, 1)} %</td>
                  <td className="num">{num(Number(c.nis_mean_normalised), 2)}</td>
                  <td className={`num mode-${String(c.final_mode)}`}>{String(c.final_mode)}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
        <p className="caveat">{t.report.dashHelp}</p>
      </Panel>

      <Panel
        title={t.report.manifest}
        style={{ marginTop: 12 }}
        actions={
          <button type="button" onClick={download}>
            {t.report.download}
          </button>
        }
      >
        <p className="caveat" style={{ marginTop: 0 }}>
          {t.report.manifestHelp}
        </p>
        <pre style={{ maxHeight: 380, overflow: "auto", margin: 0 }}>{json}</pre>
      </Panel>
    </div>
  );
}

// Reads a top-level `key: value` from the raw scenario YAML, for the catalogue
// cards. The authoritative parse happens in C++; this only needs the title.
function readField(text: string, key: string): string | null {
  const match = new RegExp(`^${key}:\\s*(.+)$`, "m").exec(text);
  if (!match) return null;
  return match[1].trim().replace(/^"(.*)"$/, "$1");
}
