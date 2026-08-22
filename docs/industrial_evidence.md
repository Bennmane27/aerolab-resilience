# Industrial and regulatory evidence

## How to read this file

Every source below is cited for one thing: **evidence that the problem this
project simulates is a real, currently active concern, and that the methods it
uses are the methods the field uses.**

None of them says anything about this implementation. No organisation listed here
has reviewed, endorsed, validated or is affiliated with this project. No logo or
branding belonging to any of them appears anywhere in this repository. The
distinction matters enough that the cahier des charges (section 19) enumerates
the formulations that are permitted and the ones that are not, and it is repeated
here because a portfolio project that blurs it is worse than one with no citations
at all.

For each entry: what it is, what it supports, and — the column that keeps this
honest — what it explicitly does **not** support.

---

## Regulatory context

### S01 — EASA, Safety Information Bulletin on GNSS interference, revision 4 (3 July 2026)

<https://www.easa.europa.eu/en/newsroom-and-events/news/easa-updates-safety-information-bulletin-gnss-interference>

- **Supports.** That jamming and spoofing of satellite navigation are recurring
  operational occurrences serious enough for a European authority to maintain and
  revise a safety bulletin about them; that the distinction between denial and
  falsification is operationally meaningful.
- **Does not support.** Any statement about detection performance, about
  thresholds, or about this project.

### S02 — EASA and EUROCONTROL, joint action plan (25 March 2026)

<https://www.easa.europa.eu/en/newsroom-and-events/press-releases/easa-and-eurocontrol-publish-joint-action-plan-ensure-safe>

- **Supports.** That operational resilience to interference is an institutional
  priority with coordinated work behind it, rather than a hypothetical.
- **Does not support.** Any specific mitigation architecture, and nothing about
  this one.

### S12 — EASA and IATA, GNSS interference mitigation plan (18 June 2025)

<https://www.easa.europa.eu/en/newsroom-and-events/press-releases/easa-and-iata-outline-comprehensive-plan-mitigate-gnss>

- **Supports.** That mitigation is treated as a coordinated programme spanning
  information collection, preparation and operational procedure.
- **Does not support.** Anything technical about the approach taken here.

---

## Industrial context

### S03 — Airbus, computer vision automated landing and embedded AI (10 June 2026)

<https://www.airbus.com/en/newsroom/stories/2026-06-computer-vision-automated-landing-and-embedded-ai-for-tomorrows-cockpits>

- **Supports.** That an *independent, non-satellite* source of position for the
  approach phase is an actively pursued line of work, and that runway-relative
  vision is one of the forms it takes. That is the justification for the
  runway-relative vision sensor being part of this simulation at all, rather than
  an invented convenience.
- **Does not support.** The sensor model used here, its noise figures, its range
  envelope, or anything about the filters that consume it. The publication itself
  states the work is research and far from commercial certification.

### S04 — Airbus UpNext, Optimate test bench (22 May 2024)

<https://www.airbus.com/en/newsroom/stories/2024-05-meet-optimate-an-extra-pair-of-eyes-and-ears-for-pilots>

- **Supports.** That validating navigation architectures on a ground test bench
  before flight is the standard method, and that multi-sensor fusion across
  vision, inertial, satellite, lidar and radar is the architecture being
  validated. This is the closest published analogue to what this project is: a
  bench, not a product.
- **Does not support.** Any comparison of capability. Optimate is instrumented
  hardware; this is a few thousand lines of simulation.

### S05 — Safran Electronics & Defense, BlackNaute resilient PNT (17 June 2025)

<https://www.safran-group.com/fr/espace-presse/blacknaute-revolution-du-pnt-resilient-face-aux-menaces-contre-systemes-navigation-satellite-gnss-2025-06-17>

- **Supports.** That the industrial answer to interference is architectural —
  combining inertial navigation, a multi-mode receiver, an independent clock, and
  algorithms that detect compromised signals and fall back to autonomous sources.
  The *principle* of redundant, independent sources with an explicit fallback
  policy is what this project models.
- **Does not support.** Any performance comparison. This project does not
  reproduce, approximate or benchmark against a proprietary product, and the
  operational figures quoted in that release have no counterpart here.

### S06 — Thales, €55 M investment in resilient navigation (12 June 2025)

<https://www.thalesgroup.com/en/news-centre/press-releases/thales-invests-eu55-million-euros-anchor-next-generation-resilient>

- **Supports.** That demand for interference-resistant navigation is sufficient
  to justify sustained industrial investment.
- **Does not support.** Anything technical.

### S07, S08, S09 — Thales Toulouse, published role descriptions (March–April 2026)

- S07: <https://careers.thalesgroup.com/de/de/job/R0320205/Ing%C3%A9nieur-Algorithmes-et-Performances-de-Syst%C3%A8mes-de-Navigation-H-F>
- S08: <https://careers.thalesgroup.com/de/de/job/R0321532/ALTERNANCE-Ing%C3%A9nieur-en-algorithmes-de-filtrage-pour-syst%C3%A8mes-de-navigation-F-H>
- S09: <https://careers.thalesgroup.com/de/de/job/R0321540/ALTERNANCE-Ing%C3%A9nieur-%C3%89tude-d%E2%80%99Algorithmes-d%E2%80%99Int%C3%A9grit%C3%A9-pour-la-Navigation-F-H>

- **Supports.** That the specific working method used here — prototype an
  algorithm, validate it in simulation, inject the events you are afraid of,
  measure, compare architectures, justify the choice — is how the work is
  publicly described by a team doing it. And that the specific techniques
  implemented here are the named ones: Kalman filtering, particle and
  Student-t filters, GNSS/IMU hybridisation, RAIM, ARAIM, solution separation,
  detection and exclusion of erroneous measurements.
- **Does not support.** Anything at all about this implementation, and obviously
  nothing about any hiring outcome. A job posting describes what a team works on;
  it is evidence about the field, not about the reader.

---

## Signal authentication

### S10, S11 — EUSPA, Galileo Open Service Navigation Message Authentication

- <https://www.euspa.europa.eu/galileo-osnma>
- <https://www.euspa.europa.eu/newsroom-events/news/celebrating-one-year-galileo-osnma-milestone-trusted-positioning>

- **Supports.** Two things, and the second is the more important one. First, that
  authenticating navigation data is an operational mitigation against certain
  spoofing scenarios. Second — and this shaped the design directly — that
  authentication **complements** receiver-side consistency checks rather than
  replacing them, and does **not** protect against jamming at all.

  That nuance is why this project separates *authenticity* from *quality* in the
  integrity manager, and why no single indicator is allowed to stand in for
  integrity. A measurement can be authenticated and inconsistent, or
  unauthenticated and perfectly consistent. The state machine reflects that: the
  reason codes distinguish a staleness failure from an innovation failure from a
  cross-check failure, because collapsing them into one "trusted / untrusted" flag
  would lose the only information worth having.
- **Does not support.** Any claim that this project implements OSNMA. It does not.
  There is no cryptography here and no signal-level anything; the extension listed
  in section 23 of the specification would simulate an authentication *status*
  from public logic, never a signal.

---

## Permitted and forbidden formulations

From section 19 of the cahier des charges, reproduced because it is the operative
rule for anything written about this project.

**Permitted**

- "Research and engineering demonstrator for navigation integrity and resilience
  evaluation."
- "Synthetic GNSS fault-injection and multi-sensor navigation benchmark."
- "DO-178C-inspired traceability and verification discipline" — only where the
  method is actually applied, never "DO-178C compliant".
- "RAIM-like educational implementation" — never "certified RAIM".
- "Detects the defined synthetic fault scenarios under the documented assumptions
  with the reported performance."

**Forbidden**

- "I solved GNSS spoofing for aircraft."
- "Airbus / Safran / Thales validated my solution."
- "Flight-ready", "certifiable", "certified".
- "Guaranteed anti-spoofing."
- Any quantitative comparison against an industrial product without publicly
  comparable data — which does not exist for any product cited above.

---

*Sources verified 21 August 2026. Web pages change; each entry records the date,
the URL and the specific claim it was used for, so that a broken link does not
turn a citation into an unsupported assertion.*
