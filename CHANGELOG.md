# Changelog

All notable changes are recorded here. Because the evaluation configuration is
frozen (section 8.1 of the cahier des charges), **any change to
`configs/evaluation.json` invalidates every published figure and must appear in
this file before the next campaign runs.**

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
