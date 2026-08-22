# Deviations from the cahier des charges

The cahier des charges v1.0 is the source of truth for this project. Its closing
rule is explicit: where an implementation conflicts with the document, the
decision must be recorded here or in a new version of the specification, and the
objectives must never be changed quietly after seeing results.

This file is that record. Each entry states what the specification asked for,
what was built instead, and why. Ten of them exist; none changes what the project
claims, and four of them exist because building the thing revealed that the
specification could not be satisfied as written.

---

## DEV-001 — Own linear algebra instead of Eigen

**Specified.** Section 9 freezes Eigen 3.x as the linear algebra layer.

**Built.** `core/include/aerolab/math/matrix.hpp`, a fixed-capacity dense matrix
with runtime dimensions, roughly 250 lines.

**Why.** Three reasons, in order of weight.

1. NFR-002 forbids dynamic allocation inside the simulation loop. The type used
   here is entirely stack resident: a 16×16 double array, 2 KiB, no heap.
2. Eigen selects different SIMD kernels on x86-64 (AVX2) and on WebAssembly
   (simd128 or scalar). Different reduction orders change rounding, and rounding
   divergence between the native and wasm builds is precisely what AT-009 has to
   bound. A single scalar kernel removes that variable from the parity budget.
3. The largest object in the project is a 15×15 covariance. Eigen's value —
   expression templates, blocked kernels, vectorised reductions — does not apply
   at that size.

**Cost.** The project owns and must test its own Cholesky. That is covered by
`tests/unit/test_math.cpp`, which checks the factorisation, the solve, the
refusal of non-SPD input, and the quadratic form against closed-form values.

---

## DEV-002 — A delivery-ordered measurement bus, not a per-tick bundle

**Specified.** Section 5.2 orders the tick as
`injected = fault_engine.apply(sensors.sample(truth, t), t)` — one bundle of
measurements produced and consumed within a single tick.

**Built.** `core/include/aerolab/sensors/measurement.hpp` defines a
`MeasurementBus`: an event queue ordered by `(delivery_time, sensor, sequence)`.
Sensors push at sample time, the fault engine may rewrite the delivery time, and
the tick loop pops everything whose delivery time has arrived.

**Why.** The bundle model cannot express SCN-009. A measurement sampled at *t*
and delivered at *t* + 1.5 s does not belong to tick *t*, and there is no slot
for it in tick *t* + 150 either. With a bundle, the only implementable policy is
to discard it, which would make the latency scenario vacuous.

The tie-breakers in the ordering are not cosmetic: two sensors can be scheduled
on the same tick, and the order in which their updates are applied changes the
filter output in the last bits. A total order that depends only on recorded
fields is what keeps a run reproducible.

---

## DEV-003 — The determinism hash is an intra-build contract

**Specified.** M-15 defines a determinism hash of the replay-relevant output;
AT-009 asks for native and WebAssembly parity; VNV-010 asks for golden tests
comparing the two builds.

**Built.** Two separate contracts:

* **Within one build** — SYS-004, AT-001, M-15: the same binary, scenario and
  seed produce bit-identical output. Checked by hash equality.
* **Across builds** — AT-009, VNV-010: agreement within a stated numeric
  tolerance, plus equality of the integrity events, their order, their sources,
  their reason codes and the acceptance verdict. Checked by
  `tools/analysis/wasm_parity.mjs`.

**Why.** Bit-identical output across toolchains is not attainable, for reasons
that are structural rather than fixable:

* The last bits of `log` and `sqrt` are not specified by IEEE-754. glibc and the
  Emscripten libm genuinely disagree on them, and every Gaussian draw goes
  through both.
* x86-64 may fuse `a*b + c` into a single FMA where WebAssembly cannot. The build
  passes `-ffp-contract=off` to forbid it, which removes the largest source of
  divergence but not the libm one.
* Reduction orders differ under different vectorisation.

Claiming bit-exact cross-build parity would have meant either a test that fails
for reasons nobody can fix, or a tolerance quietly hidden inside a "hash"
comparison. The split above is the honest version, and the tolerance is stated in
metres where a reader can argue with it.

---

## DEV-004 — Split update: the integrity gate runs before fusion

**Specified.** Section 5.2 orders the tick as

```
estimator.update(injected_measurements)
integrity.evaluate(estimator, injected_measurements)
```

**Built.** `INavigationEstimator` splits the update in two:

```
prepareUpdate(m, info)   // computes y, S, H, R and the NIS. Changes no state.
<the integrity policy decides>
applyUpdate(m, info)     // commits the correction, only if accepted.
```

**Why.** The specified order runs the integrity check *after* the measurement has
already been fused. For NAV-D that is the wrong order twice over: a spoofed fix
is already in the state by the time the policy decides to reject it, and the
innovation the policy inspects is the post-update one, which is smaller than the
one that mattered.

It is also the only way to express two other requirements. INT-010 (monitor-only
mode) means "compute the statistic, take no action" — meaningless if the action
has already happened. Section 11.5 asks for the same estimator to be run under
different integrity policies, which requires the policy to sit between the two
halves of the update rather than after the whole thing.

---

## DEV-005 — Solution separation promoted from COULD to MUST

**Specified.** INT-022 lists solution separation and ARAIM-like study as a COULD,
an optional educational extension.

**Built.** A first-class architecture, NAV-F (`solsep_ekf`), running in every
scenario alongside the innovation-gating architecture.

**Why.** A chi-square gate on the innovation is structurally weak against a slow
drift: as a spoofed position ramps away, the filter state follows it, so the
innovation stays small and the gate never fires. SCN-004 exists to expose exactly
that. An integrity architecture that only ever tests innovations could therefore
report a clean result while the position error grew without bound — the most
misleading outcome this project could publish.

Solution separation compares the all-sources solution against a sub-filter that
never receives the suspected source. The sub-filter cannot follow the spoof, so
the separation grows with the injected error rather than being absorbed by it.
The cost is one extra 15-state filter; the benefit is that the project can
measure where each policy stops working instead of asserting it.

**What the measurement showed, and the correction it forces.** The promotion was
justified, but the justification above is incomplete as written. Measured over
12 drift rates and 150 seeds each, solution separation is indistinguishable from
innovation gating in the default configuration — because a simple
GNSS-versus-vision cross check catches the drift first and both architectures
inherit its performance. Disabling that cross check separates them by roughly a
factor of six in detectability floor; removing the vision sensor entirely collapses
both to identical numbers, because the GNSS-free sub-filter then has no absolute
reference and drifts faster than the spoof.

The correct statement is: **solution separation converts an independent absolute
reference into detection power.** Given one it lowers the floor substantially;
given none it adds nothing. The reasoning above is left as originally written, and
this paragraph appended, because the sequence — hypothesis, measurement,
contradiction, control experiment, revised claim — is the part worth keeping.
See `docs/methodology/detectability-floor.md` and KF-008.

No conformance to RAIM or ARAIM is claimed. See `docs/methodology/integrity.md`
for the assumptions and for the calibration this required (DEV-009).

---

## DEV-006 — A YAML subset, not full YAML

**Specified.** Section 7.1 writes scenarios in YAML.

**Built.** `core/src/io/yaml.cpp` accepts block mappings, block sequences, flow
collections, scalars, comments, and block scalars (`|` and `>`). It rejects
anchors, aliases, tags and multi-document streams with a line number.

**Why.** A conforming YAML implementation is a dependency an order of magnitude
larger than this project's core, for a feature set no scenario needs. The subset
is stated in the header of the parser, and anything outside it is a hard parse
error rather than a silent misinterpretation — including an anchor in value
position, which a naive reader would happily turn into the string `&anchor value`.

Block scalars are supported rather than rejected because the acceptance block of
each scenario carries several sentences of reasoning, and folding those onto one
line would make the auditable record of what a scenario asserts unreadable.

---

## DEV-007 — Machine-readable acceptance blocks

**Specified.** SYS-016 requires each run to export a verdict of conformance to
the criteria of its suite. Section 7 states the expected outcome of each scenario
in prose, in the "Attendu" column.

**Built.** Every scenario file carries an `acceptance:` block: a list of criteria,
each bound to one estimator channel, each evaluated against the metrics of the
run. The manifest verdict is their conjunction.

**Why.** Prose is not computable. Without this the verdict could only mean "the
run finished", which is what section 13.2 gives every regression test from AT-013
onward — a smoke test, not an acceptance criterion.

The blocks follow one rule taken from section 8.1: a criterion states what the
**run** must produce — finite metrics, journaled events, an honest reported mode —
never that a particular algorithm must win. SCN-004 is expected to be able to
defeat the innovation gate, and its acceptance block says so explicitly.

---

## DEV-008 — Selective trace retention promoted from SHOULD to MUST

**Specified.** DATA-011 (SHOULD) suggests that heavy campaigns might store full
traces only for selected runs.

**Built.** `aerolab_bench` always keeps aggregate metrics for every run, and a
full trace only for the first N seeds of each scenario plus every run that
failed. The policy and what it dropped are recorded in the campaign manifest.

**Why.** Arithmetic. A telemetry frame is about 350 bytes; at 100 Hz over 90 s
that is roughly 3 MB per run, and fourteen scenarios times a thousand seeds is
about 40 GB. That is not a storage inconvenience, it is a design error. Making
the retention policy optional would have meant the default configuration of the
tool could not run the campaign the specification asks for.

---

## DEV-009 — A calibrated inflation on the solution separation covariance

**Specified.** Nothing: this is a parameter the specification does not mention,
introduced by DEV-005.

**Built.** `solution_separation_covariance_inflation`, frozen at 2.0 in
`configs/evaluation.json`, plus evaluation of the separation test at the GNSS
update rate rather than every tick.

**Why.** The identity cov(x_full − x_sub) = P_sub − P_full holds for two optimal
filters. Neither of these is exactly optimal: both are linearised, they share a
non-white inertial error, and the main filter may refuse an update its policy
rejected. Every one of those reduces the cross-covariance, so the true covariance
of the difference is larger than the theory and the raw statistic is biased high.

Measured on the tuning seed set: the raw form isolated the satellite source on
12 % of fault-free runs, falling to 4 % once the test was evaluated at the
measurement rate. 1.5 is the smallest inflation reaching zero over 400 fault-free
runs; 2.0 was frozen for margin because it gives identical detection performance
(100 % on SCN-003 and SCN-004, unchanged time-to-detect P95 of 0.28 s and 8.48 s)
while 3.0 begins to cost post-fault re-isolations for no gain.

This is the only parameter in the project calibrated against measured results. It
was fixed on the tuning seed set and frozen before the evaluation campaign, which
is the separation section 8.1 requires. `tools/analysis/calibrate_separation.sh`
reproduces the measurement.

---

## DEV-010 — Manual fault injection is a scenario parameter, not a runtime command

**Specified.** UI-010 asks the Live Lab to allow manual injection of the faults a
scenario permits.

**Built.** The fault instant is a scenario parameter, and the Live Lab exposes
the seed and a restart. There is no control that mutates a running engine.

**Why.** BEN-014 requires a run to be exactly reconstructible from its manifest.
A button that injected a fault mid-run at an operator-chosen instant would
produce a run that no manifest could describe, and every number the page then
displayed would be unreproducible. Given a choice between an interaction and the
property that makes the numbers mean anything, the property wins.

---

## Corrections to the document itself

These are defects in the cahier des charges rather than deviations from it. They
are listed here because the document declares itself the single source of truth
and these would otherwise propagate.

| Where | Problem |
| --- | --- |
| Sommaire vs body | Two different numbering plans coexist from section 7 onward. The table of contents announces "7 Capteurs simulés / 8 Fault Injection / 9 Solutions de navigation"; the body has "7 Catalogue de scénarios / 8 Métriques / 9 Stack technique". Citing "§12" in an issue is ambiguous as a result. |
| §13.2, AT-027..AT-030 | Eighteen regression tests for fourteen scenarios; the last four duplicate SCN-001..004. |
| §13.2, AT-013..AT-030 | All eighteen share one generic pass criterion ("the run completes and produces telemetry and a manifest"). That is a smoke test. DEV-007 exists to give them real criteria. |
| §4.2 UI-04 vs §12.8 UI-011 | One says the Compare view superimposes "2 to 4" solutions, the other "at least three". |
| §6.1 vs §10.1 | The state vector lists Euler angles while `TruthState` carries a quaternion. The implementation uses a quaternion with a local error state throughout; see `docs/methodology/estimators.md`. |
