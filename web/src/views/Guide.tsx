// AEROLAB RESILIENCE - the explanatory body of the overview page.
//
// This used to be a page of its own, next to the overview. Two entries in the
// navigation that each told half the story, and neither told it well: someone
// arriving on the overview got three sentences and a button, and had to guess
// that the real explanation was one menu item away. It is all one page now.
//
// The Methodology page still exists and is still the technical statement of the
// experiment, in the language of the specification. This is the version for
// someone who has just arrived.
import { useLang } from "../i18n";

/**
 * Everything below the hero: the problem, the five-stage chain, how to read the
 * Live Lab, the safety boundary, the results and the rationale.
 *
 * Deliberately does NOT restate what the tool is — the hero above it already
 * does, and saying it twice on one page is how a landing page starts to feel
 * padded.
 */
export function GuideSections({ onStart }: { onStart: () => void }) {
  const { guide: g } = useLang();

  return (
    <div className="guide">
      <h3>{g.h_why}</h3>
      {g.p_why.map((p, i) => (
        <p key={i}>{p}</p>
      ))}

      <h3>{g.h_how}</h3>
      <p>{g.p_how}</p>
      <ol className="guide-chain">
        {g.steps.map((step, i) => (
          <li key={step.title}>
            <span className="guide-step-index" aria-hidden="true">
              {i + 1}
            </span>
            <div>
              <b>{step.title}</b>
              <p>{step.text}</p>
            </div>
          </li>
        ))}
      </ol>

      <h3>{g.h_read}</h3>
      <p>{g.p_read}</p>
      <dl className="guide-panels">
        {g.panels.map((panel) => (
          <div key={panel.name}>
            <dt>{panel.name}</dt>
            <dd>{panel.text}</dd>
          </div>
        ))}
      </dl>

      <h3>{g.h_limits}</h3>
      <ul className="guide-limits">
        {g.p_limits.map((p, i) => (
          <li key={i}>{p}</li>
        ))}
      </ul>

      <h3>{g.h_learned}</h3>
      {g.findings.map((f) => (
        <section className="guide-finding" key={f.title}>
          <h4>{f.title}</h4>
          <p>{f.text}</p>
        </section>
      ))}

      <h3>{g.h_motivation}</h3>
      {g.p_motivation.map((p, i) => (
        <p key={i}>{p}</p>
      ))}

      <div className="guide-cta">
        <p>{g.next}</p>
        <button type="button" className="primary" onClick={onStart}>
          {g.start}
        </button>
      </div>
    </div>
  );
}
