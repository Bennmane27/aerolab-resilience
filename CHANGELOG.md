# Changelog

All notable changes are recorded here. Because the evaluation configuration is
frozen (section 8.1 of the cahier des charges), **any change to
`configs/evaluation.json` invalidates every published figure and must appear in
this file before the next campaign runs.**

## [Unreleased]

Web Lab only. No change to the core, the scenarios, the evaluation configuration
or any published figure.

### Added

- **Live commentary on the run.** The integrity log answers "what did the policy
  decide"; it does not answer "why is this the interesting second". A panel at
  the top of the Live Lab now says, in plain language, which moment of the
  scenario is on screen — counting down to the injection, naming the fault and
  the source it is applied to, naming the first architecture to react and after
  how long, and what to watch next. It derives everything from the frame, the
  scenario file and the integrity events; it states no number the engine did not
  produce. Detections are credited only on sources the scenario actually
  faulted, which is the same rule KF-007 forced on the metric.
- **French for the scenario catalogue.** The scenario files stay in technical
  English — they carry the requirement identifiers and the campaign reads them —
  but the fourteen names, descriptions and objectives now have French text in
  the interface, falling back to the file for anything without an entry.
- **The overview page now carries the whole explanation.** The problem the
  bench addresses, the five-stage chain from trajectory to metrics, a
  walkthrough of every panel in the Live Lab, the safety boundary, what the
  campaign produced — including the two results that contradicted the project's
  own expectations — and why it was built. Bilingual, like the rest of the
  interface. It was briefly a page of its own beside the overview; two entries
  in the navigation each telling half the story is worse than one telling all
  of it, and a visitor landing on three sentences and a button had no reason to
  guess the rest was one menu item away.
- **Full screen.** One button gives the 3D view the whole window and keeps the
  controls in their column; the chart strip is hidden rather than squeezed,
  because a sixty-pixel chart is worse than no chart. Browser fullscreen and the
  layout are kept in step, so leaving with Escape restores the strip.
- **A legend in the viewport**, replacing the labels that used to float beside
  each marker: colour, architecture and current error, in a fixed corner that
  never overlaps anything.
- Landing gear on the truth aircraft, and a switch for the legend.
- French and English interface, switchable at any time and remembered. The
  identifiers the engine emits — reason codes, sensor states, navigation modes —
  are never translated, because a reader comparing the screen against a
  telemetry file has to see the same token.
- Charts read out every series under the pointer, and an instant can be pinned
  by clicking it.
- An error boundary around the interface, so a rendering fault resets the view
  instead of blanking the page.
- Procedurally generated 3D assets under `web/src/components/scene/`: an
  airliner at its real 42 m length with swept wings, winglets, podded engines
  and landing gear; a runway whose ICAO-style markings are painted into a
  generated texture; edge and approach lighting; and a ground shadow that reads
  as altitude. Nothing is fetched at runtime, because the page has to keep
  working with no network (SYS-009).

### Fixed

- **The aircraft was drawn on a mirrored heading, and banked out of its turns.**
  `rotation.y = -yaw + PI/2` produces a nose vector of (sin ψ, 0, −cos ψ), which
  is the heading reflected about north: on a runway at 140° the aircraft was
  drawn tracking 40°, a hundred degrees across its own path. The correction is
  not a sign, either — the NED-to-world map this view uses (x = East, y = Up,
  z = North) is orientation-reversing, so building a basis from (nose, up,
  starboard) yields a determinant −1 *reflection*. three.js accepts it silently:
  `setFromRotationMatrix` returned a quaternion of norm 0.707 and the model sat
  nearly unrotated while the truth turned through ninety degrees. The attitude
  now comes from an explicit 3-2-1 body basis in
  `web/src/components/scene/attitude.ts`, with the third axis the PORT wing, and
  five unit tests pin the determinant, the heading, the pitch, the bank
  direction and the orthonormality.
- **The runway was drawn about a hundred degrees off its own centreline**, from
  the same class of mistake: Euler order XYZ applies the heading rotation before
  the tip about X, and that tip reverses its sense. It sat on the correct centre
  point, which is why it read as a pale streak across the horizon rather than as
  an obviously misplaced runway.
- **The aircraft sank into the runway while taxiing.** Its origin was the
  fuselage centreline and it had no landing gear, so at altitude zero the belly
  was 1.9 m under the pavement. The model's origin is now the wheel contact
  point. This was a modelling artefact; an ESTIMATE below the ground remains the
  divergence signal the view exists to show.
- **The terrain washed out to a flat pale grey.** The ground was drawn at 0.85
  opacity so a marker underneath stayed visible, which worked only while the
  background was near black; against the atmospheric sky, fifteen percent of a
  bright sky bled through every square metre. The ground is opaque now and a
  below-ground marker is drawn *through* it instead — a cleaner image and a
  stronger signal, since exactly one thing shows through the surface.
- **Restart blanked the page.** `onClick={props.onRestart}` handed React's
  MouseEvent to `core.reset()` as the seed, which aborted the WebAssembly
  module. Covered by an end-to-end test.
- **Camera modes did not survive being looked around from.** Orbiting switched
  the view to Free, so chase mode was unusable for anyone who wanted to look at
  anything. Mode and orbit are now independent: presets decide what the camera
  watches, the user decides from where. Covered by an end-to-end test.
- **3D labels were sized in metres**, so they were unreadable from the top-down
  view and swallowed the screen up close. They are now held at a constant
  on-screen size, and staggered per architecture so that two estimates a couple
  of metres apart — the interesting case — remain separately readable.
- **Side-column panels were capped at a fixed pixel height inside a column that
  already scrolled**, giving two nested scrollbars where the inner one stopped
  short of the content. The column is now the only scroll container.
- Speed changes no longer rebuild the animation loop, and samples no longer go
  through React state on every frame; both showed up as stutter at ×4.
- The published build resolved the WebAssembly module against the bundle chunk
  instead of the page, so the built site never loaded its core.

### Changed

- **No text in the 3D scene at all.** Labels anchored to five estimates a couple
  of metres apart — the normal case, and the one worth reading — pile on top of
  each other and on top of the aircraft. Shortening them did not help, because
  the problem was text being there at all. The scene now carries shape, colour
  and position; the legend carries the words and the numbers.
- **The scene is built with three's own addons** rather than hand-rolled
  substitutes: `Sky` for atmospheric scattering, `Line2` for error vectors that
  are actually visible (a plain `THREE.Line` is one device pixel wide on every
  desktop GPU, whatever `linewidth` says), `RoomEnvironment` for image-based
  lighting, and `LatheGeometry` for a fuselage that is a real surface of
  revolution. All MIT, all already installed, none of it fetched at runtime.
- French rewritten against the terms actually used in the field rather than
  word-for-word: *essai*, *compilation* and *rejet sur innovation* in place of
  the untranslated *run*, *build* and *gating* the first pass had left in place.
  Fault types, integrity events and the failure catalogue are translated too.

## [1.0.0] — 2026-08-21

First complete implementation of the cahier des charges v1.0.

### Core

- Closed-form ground truth: constant-curvature horizontal track, glideslope with
  a Hermite flare, analytic speed schedule. It carries no integration state and
  therefore cannot drift.
- Sensor suite with per-sensor deterministic streams, distinct sample and
  delivery timestamps, and quality reported separately from value.
- Delivery-ordered measurement bus with a total order depending only on recorded
  fields.
- Fault injection engine covering thirteen families, with no access to the
  ground truth by construction.
- 15-state error-state Kalman filter with a local attitude error, Joseph-form
  covariance update, and exact reprocessing of delayed measurements by snapshot
  rollback.
- Integrity manager: chi-square gating derived from a stated probability of false
  alert, persistence, isolation, recovery hysteresis, staleness and
  repeated-sequence detection, velocity consistency, GNSS-versus-vision cross
  check, stable reason codes.
- Solution separation against a GNSS-free sub-filter.
- Snapshot RAIM-like residual test with leave-one-out exclusion.
- Metrics M-01 to M-15, reported as distributions rather than bare means.

### Deviations from the specification

Ten, recorded in `docs/deviations.md`. The four that changed behaviour rather
than implementation:

- **DEV-004** — the integrity gate runs *before* fusion, not after. The specified
  order would have fused a spoofed fix before deciding to reject it.
- **DEV-002** — a delivery-ordered measurement bus replaces the per-tick bundle,
  without which SCN-009 cannot be expressed at all.
- **DEV-005** — solution separation promoted from COULD to a first-class
  architecture.
- **DEV-007** — machine-readable acceptance blocks, without which SYS-016 can
  only mean "the run finished".

### Configuration

- `configs/evaluation.json` frozen on 2026-08-21.
- `solution_separation_covariance_inflation` calibrated at **2.0** on the tuning
  seed set. This is the only parameter in the project fitted to measured results;
  the measurement is in `docs/methodology/integrity.md`.

### Defects found and fixed during construction

- **The ground truth was not physically realisable on the turning profile.** A
  curvature step made the coordinated-turn bank angle discontinuous, implying an
  infinite roll rate. Every architecture with an integrity layer diverged to
  kilometres on a fault-free run. Fixed by blending curvature over a roll-in
  distance. (KF-003)
- **The detection metric credited any source leaving ACTIVE**, so the vision
  sensor losing sight of the runway at touchdown was scored as detecting a GNSS
  drift. The first detectability sweep therefore reported 100 % detection at
  every drift rate, down to 0.1 m/s. Fixed by attributing detection to the
  faulted source only. (KF-007)
- **Integrity events were only visible on the exact tick they occurred**, so
  telemetry at a decimation of 5 dropped four out of five of them and the browser
  dropped most of them at ×4 speed. Fixed by draining accumulated events into
  each emitted frame.
- **The telemetry recorder reopened the output file per frame** in append mode,
  which raced with the still-buffered header and corrupted the first frame.
- **Isolation reason codes were hard coded to `NIS_PERSISTENT`** regardless of
  what had actually escalated, mislabelling age-driven and sequence-driven
  isolations as innovation failures.
- **The false alert metric double counted** the ACTIVE→SUSPECT→ISOLATED
  escalation as two alerts, and counted a source going UNAVAILABLE for geometric
  reasons as a policy alert.

### Known limits

Six entries in `docs/failures/known_failures.md`, including one where isolating a
source measurably worsens accuracy under a double fault, and one where a stated
project hypothesis was not supported by measurement and was replaced by a
measurement.
