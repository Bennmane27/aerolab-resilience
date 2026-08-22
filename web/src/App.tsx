// AEROLAB RESILIENCE - Web Lab shell.
//
// Screens UI-01 .. UI-08 of section 4.2. The C++ core runs in WebAssembly in
// this page; nothing is recomputed in TypeScript and no server is involved
// (SYS-009, D-011).
//
// DEVIATION DEV-010 (docs/deviations.md) - manual fault injection.
// UI-010 asks for manual injection of the faults a scenario permits. It is
// implemented here as a scenario parameter (the fault start instant) plus a
// "trigger now" control that restarts the run with that instant set to the
// current simulated time - NOT as a runtime command that mutates a running
// engine. The reason is BEN-014: a run must be exactly reconstructible from its
// manifest. A button that injected a fault mid-run at an operator-chosen
// instant would produce a run no manifest could describe, and every number the
// page then displayed would be unreproducible.
import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import { AerolabCore, type Frame, type ScenarioInfo } from "./core/session";
import { ESTIMATOR_COLORS, ESTIMATOR_LABELS } from "./core/session";
import { Scene3D } from "./components/Scene3D";
import { EventLog, Panel, SensorHealth, SolutionTable, reasonHelp, type LogEntry } from "./components/Panels";
import { TimeChart, type Series } from "./components/Chart";
import { MethodologyView } from "./views/Methodology";
import { FailureCatalogView } from "./views/FailureCatalog";

type View = "landing" | "scenarios" | "lab" | "compare" | "engineering" | "report" | "methodology" | "failures";

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
  text: string;
}

const REPO_URL = "https://github.com/your-account/aerolab-resilience";

export function App() {
  const [view, setView] = useState<View>("landing");
  const [core, setCore] = useState<AerolabCore | null>(null);
  const [loadError, setLoadError] = useState<string | null>(null);
  const [catalog, setCatalog] = useState<CatalogEntry[]>([]);
  const [configText, setConfigText] = useState("");
  const [scenario, setScenario] = useState<ScenarioInfo | null>(null);
  const [selectedFile, setSelectedFile] = useState<string | null>(null);
  const [frame, setFrame] = useState<Frame | null>(null);
  const [samples, setSamples] = useState<Sample[]>([]);
  const [log, setLog] = useState<LogEntry[]>([]);
  const [running, setRunning] = useState(false);
  const [speed, setSpeed] = useState(1);
  const [seed, setSeed] = useState(1);
  const [report, setReport] = useState<Record<string, unknown> | null>(null);
  const [visible, setVisible] = useState<string[]>(Object.keys(ESTIMATOR_COLORS));
  const [camera, setCamera] = useState<"chase" | "map" | "runway">("chase");
  const [scrub, setScrub] = useState<number | null>(null);

  const framesRef = useRef<Frame[]>([]);
  const rafRef = useRef<number | null>(null);
  const lastTimeRef = useRef<number>(0);
  const accumulatorRef = useRef<number>(0);

  // --- bootstrap -----------------------------------------------------------
  useEffect(() => {
    let cancelled = false;
    (async () => {
      const loaded = await AerolabCore.load();
      if (cancelled) return;
      if (!loaded.ok) {
        setLoadError(loaded.error);
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
          });
        }
        if (!cancelled) setCatalog(entries);
      } catch (e) {
        if (!cancelled) setLoadError(`could not load the scenario catalogue: ${String(e)}`);
      }
    })();
    return () => {
      cancelled = true;
    };
  }, []);

  const resetRunState = useCallback(() => {
    framesRef.current = [];
    setSamples([]);
    setLog([]);
    setReport(null);
    setScrub(null);
    setFrame(null);
  }, []);

  const openScenario = useCallback(
    (entry: CatalogEntry, withSeed?: number) => {
      if (!core) return;
      core.recreateSession();
      const loaded = core.loadScenario(entry.text, entry.file, configText);
      if (!loaded.ok) {
        setLoadError(loaded.error);
        return;
      }
      const info = core.scenarioInfo();
      if (!info.ok) {
        setLoadError(info.error);
        return;
      }
      const useSeed = withSeed ?? info.value.seed;
      const r = core.reset(useSeed);
      if (!r.ok) {
        setLoadError(r.error);
        return;
      }
      setSeed(useSeed);
      setScenario(info.value);
      setSelectedFile(entry.file);
      resetRunState();
      const f = core.frame();
      if (f.ok) setFrame(f.value);
      setRunning(true);
      setView("lab");
    },
    [core, configText, resetRunState]
  );

  const restart = useCallback(
    (nextSeed?: number) => {
      if (!core || !scenario) return;
      const useSeed = nextSeed ?? seed;
      const r = core.reset(useSeed);
      if (!r.ok) {
        setLoadError(r.error);
        return;
      }
      setSeed(useSeed);
      resetRunState();
      const f = core.frame();
      if (f.ok) setFrame(f.value);
      setRunning(true);
    },
    [core, scenario, seed, resetRunState]
  );

  // --- the run loop --------------------------------------------------------
  useEffect(() => {
    if (!core || !running || !scenario) return;

    const tick = (now: number) => {
      const previous = lastTimeRef.current || now;
      lastTimeRef.current = now;
      const elapsed = Math.min((now - previous) / 1000, 0.25);
      accumulatorRef.current += elapsed * speed;

      const dt = scenario.dt_s;
      let steps = Math.floor(accumulatorRef.current / dt);
      accumulatorRef.current -= steps * dt;
      steps = Math.min(steps, 800);  // never let a stalled tab burn a whole run

      if (steps > 0) {
        core.step(steps);
        const f = core.frame();
        if (f.ok) {
          const value = f.value;
          setFrame(value);
          framesRef.current.push(value);

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
          setSamples((prev) => [...prev, { t: value.t, errors, sigma, nis, threshold }]);

          if (value.events.length > 0 || value.faults.length > 0) {
            setLog((prev) => {
              const additions: LogEntry[] = [];
              for (const e of value.faults) {
                additions.push({
                  t: value.t,
                  kind: "fault",
                  headline: `FAULT ${e.activated ? "ARMED" : "ENDED"} — ${e.type} on ${e.target}`,
                  why: e.activated
                    ? "A synthetic transformation is now being applied to this source's measurements."
                    : "The injected transformation has stopped; the source is nominal again.",
                });
              }
              for (const e of value.events) {
                additions.push({
                  t: value.t,
                  kind: "integrity",
                  headline: `${ESTIMATOR_LABELS[e.estimator] ?? e.estimator}: ${e.sensor} ${e.from} → ${e.to}`,
                  why: `${reasonHelp(e.reason)} (statistic ${e.statistic.toPrecision(3)}, threshold ${e.threshold.toPrecision(3)})`,
                });
              }
              return [...prev, ...additions].slice(-400);
            });
          }
        }
        if (core.finished()) {
          setRunning(false);
          const r = core.report();
          if (r.ok) setReport(r.value);
          return;
        }
      }
      rafRef.current = requestAnimationFrame(tick);
    };

    lastTimeRef.current = 0;
    accumulatorRef.current = 0;
    rafRef.current = requestAnimationFrame(tick);
    return () => {
      if (rafRef.current !== null) cancelAnimationFrame(rafRef.current);
      rafRef.current = null;
    };
  }, [core, running, speed, scenario]);

  // Keyboard access to the main transport controls (UI-021).
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if (view !== "lab") return;
      const target = e.target as HTMLElement | null;
      if (target && ["INPUT", "SELECT", "TEXTAREA"].includes(target.tagName)) return;
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

  const displayFrame = scrub !== null && framesRef.current[scrub] ? framesRef.current[scrub] : frame;
  const faultWindow = scenario && scenario.fault_start_s >= 0
    ? { start: scenario.fault_start_s, end: scenario.fault_end_s }
    : null;

  const errorSeries = useMemo<Series[]>(
    () =>
      visible.map((id) => ({
        id,
        label: ESTIMATOR_LABELS[id] ?? id,
        color: ESTIMATOR_COLORS[id] ?? "#888",
        points: samples.map((s) => [s.t, s.errors[id] ?? 0] as [number, number]),
      })),
    [samples, visible]
  );

  return (
    <div className="app">
      <header className="topbar">
        <div className="brand">
          AEROLAB <span>RESILIENCE</span>
        </div>
        <div className="tagline">Break the navigation. Measure what survives.</div>
        <nav className="nav" aria-label="Main">
          {(
            [
              ["landing", "Overview"],
              ["scenarios", "Scenarios"],
              ["lab", "Live Lab"],
              ["compare", "Compare"],
              ["engineering", "Engineering"],
              ["report", "Report"],
              ["methodology", "Methodology"],
              ["failures", "Failure catalog"],
            ] as Array<[View, string]>
          ).map(([id, label]) => (
            <button
              key={id}
              className={view === id ? "active" : ""}
              onClick={() => setView(id)}
              aria-current={view === id ? "page" : undefined}
              disabled={!core && id !== "landing" && id !== "methodology" && id !== "failures"}
            >
              {label}
            </button>
          ))}
        </nav>
      </header>

      {/* UI-022: the nature of the thing is stated on every screen, not buried. */}
      <div className="disclaimer">
        Simulation only. Every "attack" here is arithmetic applied to synthetic measurements inside
        this page — no radio, no signal, no receiver. Not a certified system, not affiliated with any
        manufacturer or authority.
      </div>

      <main className="content">
        {loadError && (
          <div className="pad">
            <div className="banner-error">{loadError}</div>
          </div>
        )}

        {view === "landing" && <Landing onStart={() => setView("scenarios")} ready={core !== null} core={core} />}

        {view === "scenarios" && (
          <ScenarioSelect
            catalog={catalog}
            selected={selectedFile}
            onOpen={(entry) => openScenario(entry)}
          />
        )}

        {view === "lab" && (
          <LiveLab
            frame={displayFrame}
            scenario={scenario}
            log={log}
            running={running}
            speed={speed}
            seed={seed}
            visible={visible}
            camera={camera}
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
            onScrub={setScrub}
          />
        )}

        {view === "compare" && (
          <ComparePanel samples={samples} scenario={scenario} faultWindow={faultWindow} frame={displayFrame} />
        )}

        {view === "engineering" && (
          <EngineeringPanel samples={samples} faultWindow={faultWindow} frame={displayFrame} />
        )}

        {view === "report" && <ReportPanel report={report} scenario={scenario} running={running} />}

        {view === "methodology" && <MethodologyView repoUrl={REPO_URL} build={core?.build ?? null} />}

        {view === "failures" && <FailureCatalogView />}
      </main>
    </div>
  );
}

// --------------------------------------------------------------- landing --

function Landing({ onStart, ready, core }: { onStart: () => void; ready: boolean; core: AerolabCore | null }) {
  return (
    <div className="pad">
      <div className="hero">
        <h1>Can you make an aircraft lose its position?</h1>
        <div className="quote">“Break the navigation. Measure what survives.”</div>
        <p>
          This page breaks the navigation sensors of a simulated aircraft on purpose — the satellite
          fix disappears, or freezes, or quietly starts lying — and then measures which strategies
          still know where the aircraft is, how fast they notice, and how often they cry wolf.
        </p>
        <p>
          The engine running below is the same C++ core the offline benchmark uses, compiled to
          WebAssembly. Nothing is faked for the demo: every number on screen comes out of the same
          filters and the same integrity logic that produce the published results.
        </p>
        <div className="cta">
          <button className="primary" onClick={onStart} disabled={!ready}>
            {ready ? "Choose a scenario" : "Loading the core…"}
          </button>
          <a href={REPO_URL}>
            <button>Source and benchmark report</button>
          </a>
        </div>
      </div>

      <div className="ladder">
        <div className="panel step">
          <div className="who">If you are not an engineer</div>
          <div>
            An aircraft works out where it is by combining several instruments. Some of them can be
            fooled. This lab breaks one on purpose and shows you which combinations survive.
          </div>
        </div>
        <div className="panel step">
          <div className="who">If you are a recruiter</div>
          <div>
            C++17 core compiled native and to WebAssembly, deterministic simulation, controlled fault
            injection, error-state Kalman filtering, integrity monitoring, solution separation, Monte
            Carlo benchmarking, requirement traceability and a published failure catalog.
          </div>
        </div>
        <div className="panel step">
          <div className="who">If you work in navigation</div>
          <div>
            Closed-form truth, per-sensor noise models with sample and delivery timestamps, a
            delivery-ordered measurement bus with rollback reprocessing, a 15-state error-state EKF,
            NIS gating with persistence and recovery hysteresis, chi-square solution separation
            against a GNSS-free sub-filter, and a snapshot RAIM-like residual test.
          </div>
        </div>
      </div>

      {core && (
        <p className="footer">
          core {core.build.version} · commit {core.build.commit} · {core.build.compiler} · telemetry
          schema v{core.build.telemetry_schema}
        </p>
      )}
    </div>
  );
}

// ------------------------------------------------------- scenario select --

function ScenarioSelect({
  catalog,
  selected,
  onOpen,
}: {
  catalog: CatalogEntry[];
  selected: string | null;
  onOpen: (entry: CatalogEntry) => void;
}) {
  if (catalog.length === 0) {
    return (
      <div className="pad">
        <p className="empty">Loading the scenario catalogue…</p>
      </div>
    );
  }
  return (
    <div className="pad">
      <h2 style={{ marginTop: 0 }}>Scenario catalogue</h2>
      <p style={{ color: "var(--text-dim)", maxWidth: "76ch" }}>
        Fourteen scenarios, each a versioned file with its own machine-readable acceptance block.
        The same files drive the Monte Carlo campaign; this page reads them directly rather than a
        copy, so what you run here is what the benchmark ran.
      </p>
      <div className="scenario-grid">
        {catalog.map((entry) => (
          <button
            key={entry.file}
            className={`panel scenario-card ${selected === entry.file ? "active" : ""}`}
            onClick={() => onOpen(entry)}
          >
            <span className="id">{entry.id}</span>
            <span className="name">{entry.name}</span>
            <span className="desc">{entry.description}</span>
            <span className="tags">
              <span className="tag">{entry.objective.slice(0, 64)}{entry.objective.length > 64 ? "…" : ""}</span>
            </span>
          </button>
        ))}
      </div>
    </div>
  );
}

// -------------------------------------------------------------- live lab --

function LiveLab(props: {
  frame: Frame | null;
  scenario: ScenarioInfo | null;
  log: LogEntry[];
  running: boolean;
  speed: number;
  seed: number;
  visible: string[];
  camera: "chase" | "map" | "runway";
  frameCount: number;
  scrub: number | null;
  errorSeries: Series[];
  faultWindow: { start: number; end: number } | null;
  onToggleRun: () => void;
  onRestart: () => void;
  onSpeed: (s: number) => void;
  onSeed: (s: number) => void;
  onVisible: (v: string[]) => void;
  onCamera: (c: "chase" | "map" | "runway") => void;
  onScrub: (i: number | null) => void;
}) {
  const { frame, scenario } = props;
  if (!scenario) {
    return (
      <div className="pad">
        <p className="empty">Choose a scenario first.</p>
      </div>
    );
  }
  return (
    <div className="lab">
      <div className="panel viewport">
        <Scene3D frame={frame} scenario={scenario} visibleEstimators={props.visible} cameraMode={props.camera} />
        {/* UI-004, stated on the view itself and not only in the legend. */}
        <div className="truth-badge">
          <b>— — SIMULATION TRUTH</b> — not available to any estimator
          <br />
          {scenario.id} · seed {props.seed} · t = {(frame?.t ?? 0).toFixed(2)} s ·{" "}
          {frame?.truth.phase ?? "—"}
        </div>
      </div>

      <div className="side">
        <Panel title="Transport">
          <div className="transport">
            <button className="primary" onClick={props.onToggleRun}>
              {props.running ? "Pause" : "Resume"}
            </button>
            <button onClick={props.onRestart}>Restart</button>
            {[0.25, 1, 4].map((s) => (
              <button key={s} className={props.speed === s ? "active" : ""} onClick={() => props.onSpeed(s)}>
                ×{s}
              </button>
            ))}
            <span className="clock">
              {(frame?.t ?? 0).toFixed(2)} / {scenario.duration_s.toFixed(0)} s
            </span>
          </div>
          <div style={{ marginTop: 10, display: "flex", gap: 8, alignItems: "center", flexWrap: "wrap" }}>
            <label htmlFor="seed" style={{ fontSize: 12, color: "var(--text-dim)" }}>
              seed
            </label>
            <input
              id="seed"
              type="number"
              defaultValue={props.seed}
              style={{ width: 110 }}
              onKeyDown={(e) => {
                if (e.key === "Enter") props.onSeed(Number((e.target as HTMLInputElement).value));
              }}
            />
            <span style={{ fontSize: 11, color: "var(--text-faint)" }}>press Enter to re-run</span>
          </div>
          <div style={{ marginTop: 10 }}>
            <label htmlFor="scrub" className="sr-only">
              Scrub through recorded frames
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
            <div style={{ fontSize: 11, color: "var(--text-faint)" }}>
              {props.running ? "Pause to scrub through the recorded run." : `frame ${(props.scrub ?? props.frameCount - 1) + 1} / ${props.frameCount}`}
            </div>
          </div>
          <div style={{ marginTop: 10, display: "flex", gap: 6 }}>
            {(["chase", "map", "runway"] as const).map((c) => (
              <button key={c} className={props.camera === c ? "active" : ""} onClick={() => props.onCamera(c)}>
                {c}
              </button>
            ))}
          </div>
        </Panel>

        <Panel title="Sensor health">
          <SensorHealth frame={frame} />
        </Panel>

        <Panel title="Solutions">
          <SolutionTable frame={frame} />
          <div style={{ marginTop: 10, display: "flex", flexWrap: "wrap", gap: 6 }}>
            {Object.keys(ESTIMATOR_COLORS).map((id) => (
              <button
                key={id}
                className={props.visible.includes(id) ? "active" : ""}
                style={{ fontSize: 11, padding: "3px 7px" }}
                onClick={() =>
                  props.onVisible(
                    props.visible.includes(id)
                      ? props.visible.filter((x) => x !== id)
                      : [...props.visible, id]
                  )
                }
              >
                {ESTIMATOR_LABELS[id]}
              </button>
            ))}
          </div>
        </Panel>
      </div>

      <div className="strip grid" style={{ gridTemplateColumns: "minmax(0, 1.4fr) minmax(0, 1fr)" }}>
        <Panel title="Position error against simulation truth">
          <TimeChart
            series={props.errorSeries}
            yLabel="position error (m)"
            faultWindow={props.faultWindow}
            height={190}
            logY
          />
        </Panel>
        <Panel title="Integrity and fault events">
          <EventLog entries={props.log} />
        </Panel>
      </div>
    </div>
  );
}

// --------------------------------------------------------------- compare --

function ComparePanel({
  samples,
  scenario,
  faultWindow,
  frame,
}: {
  samples: Sample[];
  scenario: ScenarioInfo | null;
  faultWindow: { start: number; end: number } | null;
  frame: Frame | null;
}) {
  if (!scenario) {
    return (
      <div className="pad">
        <p className="empty">Choose a scenario first.</p>
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
        Compare — {scenario.id} {scenario.name}
      </h2>
      <p style={{ color: "var(--text-dim)", maxWidth: "76ch" }}>
        Every architecture below saw the identical measurement sequence: the truth is generated once
        per tick and the same sensor stream is handed to all five. Any difference between these rows
        is attributable to the architecture, not to luck.
      </p>

      <Panel title="Position error, all architectures, same data">
        <TimeChart
          series={ids.map((id) => ({
            id,
            label: ESTIMATOR_LABELS[id] ?? id,
            color: ESTIMATOR_COLORS[id],
            points: samples.map((s) => [s.t, s.errors[id] ?? 0] as [number, number]),
          }))}
          yLabel="position error (m)"
          faultWindow={faultWindow}
          height={260}
          logY
        />
      </Panel>

      <Panel title="Metrics so far (this run only)" style={{ marginTop: 12 }}>
        <table>
          <thead>
            <tr>
              <th scope="col">Architecture</th>
              <th scope="col">RMSE <span className="unit">m</span></th>
              <th scope="col">P95 <span className="unit">m</span></th>
              <th scope="col">Max <span className="unit">m</span></th>
              <th scope="col">Mode now</th>
            </tr>
          </thead>
          <tbody>
            {stats.map((s) => (
              <tr key={s.id}>
                <td>
                  <span
                    aria-hidden="true"
                    style={{ display: "inline-block", width: 9, height: 9, borderRadius: 2, background: ESTIMATOR_COLORS[s.id], marginRight: 7 }}
                  />
                  {ESTIMATOR_LABELS[s.id]}
                </td>
                <td className="num">{s.rms.toFixed(2)}</td>
                <td className="num">{s.p95.toFixed(2)}</td>
                <td className="num">{s.max.toFixed(2)}</td>
                <td className={`num mode-${s.mode}`}>{s.mode}</td>
              </tr>
            ))}
          </tbody>
        </table>
        <p style={{ fontSize: 12, color: "var(--text-faint)", marginBottom: 0 }}>
          These are single-run numbers on one seed. The published figures are distributions over a
          thousand seeds per scenario; see the benchmark report in the repository.
        </p>
      </Panel>
    </div>
  );
}

// ----------------------------------------------------------- engineering --

function EngineeringPanel({
  samples,
  faultWindow,
  frame,
}: {
  samples: Sample[];
  faultWindow: { start: number; end: number } | null;
  frame: Frame | null;
}) {
  const sensorIds = frame ? Object.keys(frame.sensors) : [];
  const gnssThreshold = frame?.sensors.gnss?.threshold ?? 0;

  return (
    <div className="pad">
      <h2 style={{ marginTop: 0 }}>Engineering view</h2>
      <p style={{ color: "var(--text-dim)", maxWidth: "76ch" }}>
        The statistics the integrity policy actually decides on. The Normalized Innovation Squared is
        the measured disagreement between a measurement and the filter prediction, normalised by the
        uncertainty the filter claims. Under a consistent filter it averages the number of degrees of
        freedom of the measurement; the dashed line is the gate.
      </p>

      <Panel title="Normalized Innovation Squared per source, against the gate">
        <TimeChart
          series={sensorIds.map((id, i) => ({
            id,
            label: id,
            color: ["#4d9be0", "#3fbf8f", "#e8c95a", "#cf5b7f", "#b58cd8"][i % 5],
            points: samples.map((s) => [s.t, s.nis[id] ?? 0] as [number, number]),
          }))}
          yLabel="NIS (dimensionless)"
          faultWindow={faultWindow}
          threshold={gnssThreshold > 0 ? { value: gnssThreshold, label: `gate ${gnssThreshold.toFixed(1)}` } : undefined}
          height={230}
          logY
        />
      </Panel>

      <Panel title="Filter uncertainty: reported horizontal sigma" style={{ marginTop: 12 }}>
        <TimeChart
          series={Object.keys(ESTIMATOR_COLORS)
            .filter((id) => id !== "gnss_only")
            .map((id) => ({
              id,
              label: ESTIMATOR_LABELS[id] ?? id,
              color: ESTIMATOR_COLORS[id],
              points: samples.map((s) => [s.t, s.sigma[id] ?? 0] as [number, number]),
            }))}
          yLabel="sigma horizontal (m)"
          faultWindow={faultWindow}
          height={230}
          logY
        />
        <p style={{ fontSize: 12, color: "var(--text-faint)", marginBottom: 0 }}>
          A filter whose reported sigma stays small while its true error grows is overconfident. That
          gap, not the error alone, is what an integrity architecture has to catch.
        </p>
      </Panel>

      {frame?.raim && (
        <Panel title="RAIM-like residual test (pseudorange scenarios only)" style={{ marginTop: 12 }}>
          <dl className="kv">
            <dt>statistic</dt>
            <dd>{frame.raim.statistic.toFixed(2)}</dd>
            <dt>threshold</dt>
            <dd>{frame.raim.threshold.toFixed(2)}</dd>
            <dt>detected</dt>
            <dd>{frame.raim.detected ? "yes" : "no"}</dd>
            <dt>excluded satellite</dt>
            <dd>{frame.raim.excluded ? `#${frame.raim.excluded_satellite}` : "none"}</dd>
          </dl>
          <p style={{ fontSize: 12, color: "var(--text-faint)", marginBottom: 0 }}>
            Educational residual test. No conformance to any RAIM or ARAIM standard is claimed and no
            protection level is computed.
          </p>
        </Panel>
      )}
    </div>
  );
}

// ---------------------------------------------------------------- report --

function ReportPanel({
  report,
  scenario,
  running,
}: {
  report: Record<string, unknown> | null;
  scenario: ScenarioInfo | null;
  running: boolean;
}) {
  if (!scenario) {
    return (
      <div className="pad">
        <p className="empty">Choose a scenario first.</p>
      </div>
    );
  }
  if (!report) {
    return (
      <div className="pad">
        <p className="empty">
          {running
            ? "The run is still going. The report is produced when it finishes."
            : "No report yet — let a run finish."}
        </p>
      </div>
    );
  }

  const channels = (report.channels as Array<Record<string, unknown>>) ?? [];
  const failures = (report.verdict_failures as string[]) ?? [];
  const json = JSON.stringify(report, null, 2);

  return (
    <div className="pad">
      <h2 style={{ marginTop: 0 }}>Run report</h2>

      <Panel title="Provenance">
        <dl className="kv">
          <dt>scenario</dt>
          <dd>{String(report.scenario_id)} — {String(report.scenario_name)}</dd>
          <dt>seed</dt>
          <dd>{String(report.seed)}</dd>
          <dt>commit</dt>
          <dd>{String(report.commit)}</dd>
          <dt>compiler</dt>
          <dd>{String(report.compiler)}</dd>
          <dt>scenario hash</dt>
          <dd>{String(report.scenario_hash)}</dd>
          <dt>config hash</dt>
          <dd>{String(report.config_hash)}</dd>
          <dt>verdict</dt>
          <dd className={report.verdict === "PASS" ? "mode-NORMAL" : "mode-UNSAFE"}>
            {String(report.verdict)}
          </dd>
        </dl>
      </Panel>

      {failures.length > 0 && (
        <Panel title="Acceptance failures" style={{ marginTop: 12 }}>
          <ul>
            {failures.map((f, i) => (
              <li key={i} style={{ fontFamily: "var(--mono)", fontSize: 12 }}>
                {f}
              </li>
            ))}
          </ul>
        </Panel>
      )}

      <Panel title="Metrics" style={{ marginTop: 12 }}>
        <div style={{ overflowX: "auto" }}>
          <table>
            <thead>
              <tr>
                <th scope="col">Architecture</th>
                <th scope="col">RMSE <span className="unit">m</span></th>
                <th scope="col">P95 <span className="unit">m</span></th>
                <th scope="col">Max <span className="unit">m</span></th>
                <th scope="col">TTD <span className="unit">s</span></th>
                <th scope="col">TTI <span className="unit">s</span></th>
                <th scope="col">Avail</th>
                <th scope="col">NIS/dof</th>
                <th scope="col">Mode</th>
              </tr>
            </thead>
            <tbody>
              {channels.map((c, i) => (
                <tr key={i}>
                  <td>{ESTIMATOR_LABELS[String(c.estimator)] ?? String(c.estimator)}</td>
                  <td className="num">{Number(c.position_rmse_m).toFixed(2)}</td>
                  <td className="num">{Number(c.error_p95_m).toFixed(2)}</td>
                  <td className="num">{Number(c.error_max_m).toFixed(2)}</td>
                  <td className="num">{c.time_to_detect_s === null ? "—" : Number(c.time_to_detect_s).toFixed(2)}</td>
                  <td className="num">{c.time_to_isolate_s === null ? "—" : Number(c.time_to_isolate_s).toFixed(2)}</td>
                  <td className="num">{(Number(c.availability) * 100).toFixed(1)}%</td>
                  <td className="num">{Number(c.nis_mean_normalised).toFixed(2)}</td>
                  <td className={`num mode-${String(c.final_mode)}`}>{String(c.final_mode)}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
        <p style={{ fontSize: 12, color: "var(--text-faint)" }}>
          A dash in time-to-detect means no detection was raised. It is deliberately not shown as
          zero: averaging a missed detection as an instant response is the easiest way to publish a
          flattering benchmark.
        </p>
      </Panel>

      <Panel title="Full manifest (UI-023)" style={{ marginTop: 12 }}>
        <p style={{ fontSize: 12, color: "var(--text-dim)" }}>
          This is the same JSON the native CLI writes next to a run. Select it and copy to keep a
          record of exactly what produced the numbers above.
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
