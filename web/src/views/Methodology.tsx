// AEROLAB RESILIENCE - UI-07 Methodology (UI-024).
//
// Every assumption a reader would need in order to disagree with the numbers.
import type { BuildInfo } from "../core/session";
import { useLang } from "../i18n";

export function MethodologyView({ repoUrl, build }: { repoUrl: string; build: BuildInfo | null }) {
  const { t } = useLang();
  const m = t.methodology;
  return (
    <div className="pad prose">
      <h2 style={{ marginTop: 0 }}>{m.title}</h2>
      <blockquote>{m.blockquote}</blockquote>

      <h3>{m.h_experiment}</h3>
      <p>{m.p_experiment1}</p>
      <p>{m.p_experiment2}</p>

      <h3>{m.h_safety}</h3>
      <p>{m.p_safety}</p>

      <h3>{m.h_arch}</h3>
      <ul>
        {m.arch.map((a) => (
          <li key={a.name}>
            <b>{a.name}</b> — {a.text}
          </li>
        ))}
      </ul>

      <h3>{m.h_why}</h3>
      <p>{m.p_why1}</p>
      <p>{m.p_why2}</p>

      <h3>{m.h_assumptions}</h3>
      <ul>
        {m.assumptions.map((a) => (
          <li key={a}>{a}</li>
        ))}
      </ul>

      <h3>{m.h_repro}</h3>
      <p>{m.p_repro}</p>

      <h3>{m.h_industrial}</h3>
      <p>{m.p_industrial}</p>

      <h3>{m.h_build}</h3>
      {build ? (
        <dl className="kv">
          <dt>{m.buildVersion}</dt>
          <dd>{build.version}</dd>
          <dt>{m.buildCommit}</dt>
          <dd>{build.commit}</dd>
          <dt>{m.buildType}</dt>
          <dd>{build.build_type}</dd>
          <dt>{m.buildCompiler}</dt>
          <dd>{build.compiler}</dd>
          <dt>{m.telemetrySchema}</dt>
          <dd>v{build.telemetry_schema}</dd>
          <dt>{m.scenarioSchema}</dt>
          <dd>v{build.scenario_schema}</dd>
        </dl>
      ) : (
        <p className="empty">{m.notLoaded}</p>
      )}

      <p style={{ marginTop: 24 }}>
        {m.fullDocs} <a href={repoUrl}>{repoUrl}</a>
      </p>
    </div>
  );
}
