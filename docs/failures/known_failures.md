# Known failures and limits

Section 22 of the cahier des charges makes publishing this file a condition of
calling V1 finished, and section 8.1 is explicit that a scenario the algorithm
loses is a result rather than a bug. Everything below is real, reproducible, and
was found by running the thing.

Two of these are cases where the platform caught its own simulator. That is the
outcome a test bench is actually for, and it is the reason they are listed first.

---

## KF-003 — A discontinuous bank angle in the ground truth destroyed the filter

**Severity.** Would have invalidated every result on the turning profile.
**Status.** Fixed. Regression test in place.

**What happened.** On `approach_turn`, a *fault-free* run diverged to kilometres
of position error on every architecture with an integrity layer, and isolated
three sources on the way. The plain filter, with no integrity layer, was fine at
23 m.

```
integrity_ekf   RMSE 8123.959 m   avail 32.6%   final mode UNSAFE
solsep_ekf      RMSE 6427.601 m   avail 30.0%   final mode UNSAFE
ekf             RMSE   23.001 m   avail 100.0%  final mode NORMAL
```

**Why.** The horizontal track was a chain of constant-curvature segments, so
curvature stepped at the arc entry. A coordinated-turn bank angle is a function
of curvature — `phi = atan(V * psi_dot / g)` — so the bank stepped too, from
level to 20 degrees in zero time. That is an infinite roll rate. No gyroscope can
report it, so the filter had no way to know the aircraft had rolled, inherited a
permanent 20 degree attitude error at the turn entry, and everything downstream
followed: the specific force was rotated into the wrong frame, the velocity
diverged, the innovation exploded, the gate isolated GNSS, and the solution
coasted on an inertial channel that was itself wrong.

**Diagnosis path.** The tell was that the *ungated* filter was healthy. If the
data had been bad, it would have suffered too. The event log then showed the
first isolation was `VELOCITY_INCONSISTENT` — a velocity error, not a position
error — which pointed at attitude rather than at the satellite channel.

**Fix.** The bank angle is now derived from a curvature blended over a roll-in
distance (`roll_in_time_s`, default 3 s), which is what an aircraft actually does.
The consequence is that the turn is not perfectly coordinated during roll-in, so
there is a brief mismatch between bank and lateral acceleration — which is also
what happens in reality.

```bash
build/release/bin/aerolab_tests --gtest_filter=GroundTruth.BodyRatesStayBoundedThroughTheTurn
build/release/bin/aerolab_cli --scenario scenarios/SCN-014.yaml --config configs/evaluation.json --out results
```

---

## KF-007 — The detectability sweep reported detection that had not happened

**Severity.** Would have published a false headline result.
**Status.** Fixed. Detection is now attributed to the faulted source.

**What happened.** The first run of the drift-rate sweep reported 100 %
detection at every rate, including 0.1 m/s — four metres of total offset injected
over 45 seconds, against a sensor with two metres of noise. That is not
detectable, and reporting that it was would have been the most misleading number
in the project.

The tell was in the times. Time-to-detect was a constant 34.2 s at every low
rate, which is not how a detector behaves. 25 s (fault start) plus 34.2 s is
59.2 s — and 59.16 s is exactly when the vision sensor goes UNAVAILABLE on this
profile, because the aircraft has rolled past the runway and there is nothing
left to look at.

**Why.** The detection metric credited *any* source leaving the ACTIVE state
after the fault start. The vision sensor dropping out for geometric reasons was
being scored as a detection of a GNSS drift.

**Fix.** `MetricsAccumulator` is given the set of sources the scenario actually
faults, and credits a detection only for those. A fault-free scenario faults
nothing, so no detection is possible on one, which is also correct.

```bash
bash tools/analysis/drift_sweep.sh 150 results/drift_sweep
```

---

## KF-009 - The native and WebAssembly builds disagree by 190 m on dead reckoning

**Severity.** None, once understood. **Status.** Measured, published, and the
parity contract rewritten to say what it can honestly say.

**What happens.** The native and WebAssembly builds of the same source, given the
same scenario and the same seed, produce inertial dead-reckoning results that
differ by tens of percent:

```
SCN-001 / ins_dr: native  490.3 m, wasm 300.8 m   (39 % apart)
SCN-012 / ins_dr: native  780.0 m, wasm 591.6 m   (24 % apart)
SCN-014 / ins_dr: native 1041.9 m, wasm 363.7 m   (65 % apart)
```

Every other architecture agrees to within centimetres on the same runs. The worst
disagreement across all six parity scenarios is 0.5 m, and most are under a
centimetre.

**Why.** Emscripten and glibc disagree in the last bits of `log`. IEEE-754 does
not specify them. Every Gaussian draw goes through `log` (Marsaglia polar), and
the alignment error handed to every estimator is drawn from the very first draws,
so the two builds start from measurably different alignment errors.

For an architecture with an absolute reference that does not matter: the filter is
contracting, and every measurement pulls the difference back towards zero. For
pure dead reckoning it matters enormously, because an attitude error integrates
roughly as `(1/6) g b t^3`. Over 90 s the cube law turns a last-bit difference in
the initial draw into hundreds of metres.

**What it nearly cost.** The first parity check applied one tolerance to every
architecture and failed on `ins_dr` in every scenario. The tempting fix is to
widen the tolerance until it passes, which would have hidden a real defect
anywhere else. The contract is now stated per dynamics - 0.75 m or 10 % for
contracting channels, same order of magnitude for divergent ones - with the
reasoning in the script and the actual divergent numbers printed on every run
rather than suppressed.

**Why it is worth publishing.** This is the most concrete demonstration in the
project of why DEV-003 refuses to claim cross-toolchain bit equality. It is also a
reminder that "reproducible" is not one property: the same code, the same seed and
the same inputs can produce answers that differ by a factor of three, and whether
that is a defect depends entirely on whether the system being simulated amplifies
or suppresses its initial conditions.

```bash
node tools/analysis/wasm_parity.mjs
```

---

## KF-001 — Isolating a source can make the solution worse

**Severity.** A real limit of single-fault reasoning. **Status.** Open by design.

**What happens.** On the dual-fault scenario — a position spoof and an
accelerometer bias arriving together at t = 30 s — the architecture that gates and
isolates ends the run with a *higher* position error than the plain filter that
fuses everything blindly.

```
ekf             RMSE 16.123 m   avail 100.0%  final mode NORMAL
integrity_ekf   RMSE 25.435 m   avail  70.1%  final mode UNSAFE
solsep_ekf      RMSE 26.005 m   avail  70.1%  final mode UNSAFE
```

**Why.** Isolation moves the solution onto the inertial channel. When the
inertial channel is the *other* fault, that is a move onto worse data. The
architecture is behaving correctly by its own criteria — it detects, it isolates,
and it declares the result unusable — but a reader looking only at accuracy would
conclude the integrity layer is harmful.

It is not. The plain filter ends the run reporting NORMAL while carrying a 40 m
excursion it knows nothing about. The gated one ends reporting UNSAFE, having
spent 30 % of the run declining to answer. Only one of those two is honest, and
it is not the accurate one.

**Why it stays open.** Handling it properly requires reasoning about which of two
simultaneous faults to believe, which needs a redundancy the V1 architecture does
not have. The scenario therefore asserts the reported **mode**, not the error.

```bash
build/release/bin/aerolab_cli --scenario scenarios/SCN-012.yaml --config configs/evaluation.json --out results
```

---

## KF-002 — The innovation gate is not blind to a slow drift at the catalogue rate

**Severity.** A stated project hypothesis, not supported at the rate tested.
**Status.** Replaced by a measurement.

**What happens.** SCN-004 ramps the satellite position by 150 m over 45 s, about
3.3 m/s. The project's starting hypothesis — the reason NAV-F was promoted from a
COULD to a first-class architecture — was that innovation gating would miss it.
It does not. The gate detects on every seed tested, at around 8.5 s.

**Why.** The filter cannot follow the ramp freely. The barometric channel holds
the vertical, the runway-relative vision measurement holds the horizontal once
the aircraft is inside its range, and the explicit GNSS-versus-vision cross-check
fires independently of the gate. The structural weakness of innovation gating
against slow drift is real; it simply bites below a rate this experiment has to
find rather than assume.

**What replaced it.** `tools/analysis/drift_sweep.sh` sweeps the drift rate and
reports detection rate and time-to-detect for each policy, so the floor is
measured. The result is in [BENCHMARK_REPORT.md](../../BENCHMARK_REPORT.md).

---

## KF-008 — Solution separation added nothing measurable until the cross check was removed

**Severity.** Contradicted the justification for a first-class architecture.
**Status.** Understood, and the justification revised.

**What happened.** The detectability sweep in the full configuration showed the
two integrity architectures performing almost identically: floor around
0.5-0.75 m/s for both, 100 % detection above 1 m/s for both. Solution separation,
promoted from a COULD to a first-class architecture specifically because it should
beat innovation gating on a slow drift, appeared to buy nothing.

**Why.** The detector that was firing was neither of them. The explicit
GNSS-versus-vision cross check (INT-017) — a threshold on a distance — caught the
drift first in almost every case, so both architectures inherited its performance
and neither statistic got a chance to distinguish itself.

**The control experiment.** Disabling the cross check while leaving the vision
sensor feeding the filter separated them sharply:

| drift rate | innovation gating | solution separation |
| ---: | ---: | ---: |
| 0.75 m/s | 11 % | 58 % |
| 1.00 | 15 % | 94 % |
| 1.50 | 27 % | 100 % |
| 4.50 | 99 % | 100 % |

A sixfold difference in detectability floor, exactly where the theory predicted
it. The mechanism was real; it was masked.

**The second control.** Removing vision entirely collapsed both to identical
numbers, to the digit. The GNSS-free sub-filter then has only inertial data and a
barometer, drifts faster than the spoof, and its covariance grows fast enough that
the separation threshold grows with it.

**What it means.** Solution separation is not a way to get integrity out of
nothing. It converts an independent absolute reference into detection power.
Given one, it lowers the floor by about a factor of six. Given none, it is an
extra 15-state filter that reports what the innovation gate already reported.
DEV-005 should have said *given an independent absolute reference for the
sub-filter*. Full write-up in
[docs/methodology/detectability-floor.md](../methodology/detectability-floor.md).



---

## KF-004 — The theoretical solution-separation covariance is optimistic

**Severity.** Produced false isolations on fault-free runs. **Status.** Mitigated
by a calibrated inflation.

**What happens.** Applied literally, `cov(x_full − x_sub) = P_sub − P_full`
isolated the satellite source on 12 % of fault-free runs. Evaluating the test at
the measurement rate rather than every tick brought that to 4 %; an inflation of
2.0 brings it to zero over 400 fault-free runs.

**Why.** The identity holds for two *optimal* filters. Neither of these is: both
are linearised, they share an inertial error that is not white, and the main
filter may refuse an update its own policy rejected. Each of those reduces the
true cross-covariance, so the true covariance of the difference is larger than
the theory says and the statistic is biased high.

**Honesty note.** This is the only parameter in the project calibrated against
measured results. It was fixed on the tuning seed set and frozen before the
evaluation campaign. The full table is in
[docs/methodology/integrity.md](../methodology/integrity.md).

```bash
bash tools/analysis/calibrate_separation.sh
```

---

## KF-006 — Recovering from a fault can trip the gate a second time

**Severity.** Inflates the false alert count. **Status.** Open, design change
required.

**What happens.** On the slow-drift scenario, isolations occur *after* the fault
window has closed, and the metric counts them as false alerts.

**Why.** While the source is isolated the solution coasts and drifts. When the
source returns to nominal, the honest measurement now disagrees with the drifted
filter, so the gate fires again. The alert is not wrong — the two really do
disagree — but the cause is the recovery transient, not a new fault.

**Why it stays open.** Fixing it properly means resetting the filter towards the
returning source rather than treating its first measurements as suspect, which is
a design change to the recovery logic rather than a threshold adjustment. Listed
as post-V1 work.

---

## KF-005 — The naive baseline is limited by fix age, not fix noise

**Severity.** None. Listed because the number is easy to misread.

**What happens.** The GNSS-only architecture shows about 12.4 m of position error
on a fault-free approach, with a 2 m satellite noise model. Six times the noise.

**Why.** At 5 Hz with 80 ms of transport latency, the last available fix is on
average about 0.18 s old, and at 70 m/s the aircraft covers 12.6 m in that time.
The error is dominated by the age of the measurement, not by its precision — a
useful reminder that quoting a sensor's accuracy says very little about the
accuracy of a navigator built on it.

---

## Structural limits of V1

These are not defects. They are the boundaries of what the platform can currently
say, listed so that no result is read as covering them.

| Limit | Consequence |
| --- | --- |
| Single-fault reasoning | The architecture cannot arbitrate between two simultaneous faults; see KF-001. |
| No barometric bias state | The vertical channel carries an unmodelled bias absorbed as extra measurement noise. |
| Synthetic vision, not computer vision | The runway-relative measurement is a geometric model with a noise term, not image processing. |
| Frozen constellation geometry | The residual test sees a fixed dilution of precision, not an orbital one. |
| Open-loop benchmark | The truth receives no feedback from any estimator, so continuity and availability describe the estimator, not an aircraft. |
| No wind, no angle of attack | Yaw equals track and pitch equals flight path angle. |
| Flat earth | No Coriolis or transport rate; valid at this footprint and duration, not beyond it. |
