// AEROLAB RESILIENCE - UI-08 Failure catalog (UI-025).
//
// Section 22 of the cahier des charges makes publishing the known failures a
// condition of calling V1 finished. The entries mirror
// docs/failures/known_failures.md; the reproduction command is given verbatim so
// a reader can check the claim rather than take it.
import { useLang } from "../i18n";

export function FailureCatalogView() {
  const { t, failures } = useLang();
  return (
    <div className="pad prose">
      <h2 style={{ marginTop: 0 }}>{t.failures.title}</h2>
      <p>{t.failures.lead}</p>

      {failures.map((e) => (
        <section className="panel failure-entry" key={e.id}>
          <h3>
            {e.id} — {e.title}
          </h3>
          <div className="panel-body">
            <p style={{ marginTop: 0 }}>
              <b>{t.failures.what}</b> {e.what}
            </p>
            <p>
              <b>{t.failures.why}</b> {e.why}
            </p>
            <p>
              <b>{t.failures.status}</b> {e.status}
            </p>
            <pre style={{ marginBottom: 0 }}>{e.reproduce}</pre>
          </div>
        </section>
      ))}
    </div>
  );
}
