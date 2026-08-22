# Integrity monitoring: what is tested, and what each test cannot see

This is the part of the project that decides whether a measurement may be used.
It is deliberately not part of the estimator: it sees only what `prepareUpdate()`
produced — the innovation, its covariance, the NIS, the measurement age and the
reported quality — and answers one question. May this be fused?

Keeping the two apart is what lets the benchmark run the same filter under
several policies and attribute the difference to the policy alone.

---

## 1. The innovation gate

For a measurement `z` with model `h(x)`, the innovation and its covariance are

```
y = z - h(x)
S = H P H^T + R
NIS = y^T S^-1 y
```

Under the hypothesis that the filter is consistent and the measurement is
fault-free, `NIS` follows a chi-square distribution with as many degrees of
freedom as the measurement has components. The gate is the quantile of that
distribution at a stated probability of false alert.

### Why the threshold is a probability, not a number of sigmas

"Three sigma" means nothing for a three-degree-of-freedom statistic. A gate at
P_fa = 10⁻³ with 3 dof means the fault-free run should cross it about once every
thousand updates, which is a number the benchmark can actually verify. At 5 Hz
over a 90 s run the GNSS position channel sees roughly 450 updates, so the
prediction is about 0.45 crossings per run — and AT-003 measures whether that
holds.

`chiSquareQuantile` inverts the chi-square CDF by bisection at configuration
time. It is slower than a tabulated threshold and exact for any dof and any P_fa
a scenario asks for, which matters because scenarios do ask for different ones.

### What the gate cannot see

**A slow drift.** This is the central limitation and the reason NAV-F exists.

As a spoofed position ramps away, the filter follows it. The innovation is the
difference between the measurement and the *filter's own prediction*, and the
prediction has been dragged along by the previous measurements. The disagreement
therefore never accumulates: the state absorbs it. Below some drift rate the NIS
never crosses its gate, and the architecture reports a clean result while the
position error grows without bound.

That rate is not a constant. It depends on how strongly the other measurements
hold the filter back — on this approach profile the barometric channel constrains
the vertical, the runway-relative vision measurement constrains the horizontal
once it is in range, and the inertial channel constrains the short term. Which is
why the project measures the rate rather than asserting it:
`tools/analysis/drift_sweep.sh` sweeps the drift rate and reports where each
policy stops detecting.

---

## 2. Persistence, isolation and recovery

A single outlier must not isolate a source: at P_fa = 10⁻³ with several channels,
outliers happen. The state machine therefore requires

* **N consecutive exceedances** before a source becomes SUSPECT (default 3),
* **a persistence window** in SUSPECT before it becomes ISOLATED (default 1.2 s),
* **sustained normal residuals** before a SUSPECT source clears (default 2.0 s),
* **a recovery window** before an ISOLATED source is used again (default 6.0 s).

The asymmetry is intentional. Suspecting is cheap and reversible; isolating costs
a source, and un-isolating too eagerly produces an oscillation between two states
that are each individually defensible.

### ISOLATED is not UNAVAILABLE

They are separate states because they are separate facts.

* **ISOLATED** — this software decided to stop using a source that is still
  transmitting. It is a claim, and it has to be justified: every transition
  carries a reason code, the statistic and the threshold that produced it.
* **UNAVAILABLE** — the source stopped producing. It is an observation.

Reporting one as the other would erase the only distinction worth auditing. It
also matters for the metrics: an alert raised without a fault is charged to the
false alert budget, while a source going unavailable is not — on every approach
the vision sensor legitimately loses sight of the runway once the aircraft has
rolled past it, and putting that geometric certainty on the integrity budget
would corrupt the number.

---

## 3. Checks that are not innovation tests

The innovation gate is one detector among several, and it is the weakest against
exactly the case that matters most.

| Check | What it catches that the gate does not |
| --- | --- |
| **Age** (INT-018) | A frozen source. The value stays perfectly plausible; only the sample timestamp betrays it. The fault engine is not allowed to rewrite `sample_time`, which is what makes this detectable at all. |
| **Repeated sequence** (INT-019) | A source replaying itself, even where the age happens to look acceptable. |
| **Velocity consistency** (INT-016) | A position that is plausible while the reported velocity is not. Gets its own reason code so SCN-005 is diagnosable rather than showing a generic outlier. |
| **GNSS against vision** (INT-017) | A slow drift, well before the innovation gate notices — provided the aircraft is close enough for the runway-relative measurement to be usable. On SCN-008 this is what fires first, 0.48 s after the freeze begins. |
| **Solution separation** (DEV-005) | A slow drift, without needing vision, and without losing power as the fault grows. |
| **Residual test on pseudoranges** (INT-021) | A single biased satellite, where the redundancy allows it. |

---

## 4. Solution separation

The all-sources solution is compared against a sub-filter that never receives
satellite data at all. The sub-filter cannot follow a spoof, so the separation
grows with the injected error instead of being absorbed by it.

For two optimal filters where one uses a subset of the other's measurements, the
covariance of their difference is the difference of their covariances:

```
cov(x_full - x_sub) = P_sub - P_full
T = d^T (P_sub - P_full)^-1 d,    d = p_full - p_sub
```

distributed as chi-square with two degrees of freedom in the horizontal plane.
This is the mechanism behind RAIM solution separation and the baseline of ARAIM.
**No conformance to either is claimed**: there is no protection level computation,
no integrity risk allocation, no constellation fault model and no continuity
budget here.

### The identity is optimistic in practice, and by how much

Neither filter is exactly optimal. Both are linearised. They share an inertial
error that is not white. The main one may refuse an update its own policy
rejected. Each of those reduces the true cross-covariance below `P_full`, which
makes the true covariance of the difference *larger* than `P_sub − P_full`, which
makes the statistic biased high.

Measured on the tuning seed set (`configs/tuning.json`, seed base 3 000 000):

| Configuration | Fault-free runs isolating GNSS |
| --- | --- |
| Raw identity, evaluated every tick | 12 % |
| Raw identity, evaluated at the GNSS update rate | 4 % |
| Inflation 1.5, at the GNSS update rate | 0 / 400 |
| Inflation 2.0, at the GNSS update rate | 0 / 400 |
| Inflation 3.0, at the GNSS update rate | 0 / 400 |

Evaluating a 5 Hz statistic at 100 Hz multiplies highly correlated threshold
crossings without adding information; fixing that alone removed two thirds of the
false alarms.

**2.0 is frozen.** 1.5 is the smallest value reaching zero, but it sits exactly at
the boundary of the tuning sample, and a boundary value calibrated on 400 seeds
will not hold on a different thousand. 2.0 gives margin at identical detection
performance — 100 % detection on both SCN-003 and SCN-004, with time-to-detect
P95 unchanged at 0.28 s and 8.48 s — while 3.0 begins to cost post-fault
re-isolations for no gain.

This is the **only** parameter in the project calibrated against measured results.
It was fixed on the tuning seed set and frozen before the evaluation campaign,
which is the separation section 8.1 of the cahier des charges requires.
`tools/analysis/calibrate_separation.sh` reproduces the table above.

---

## 5. Declaring the solution unusable

INT-013 and INT-014 exist because the worst possible behaviour for an integrity
architecture is to keep publishing a position it can no longer support.

```
absolute sources in use = 0 and last fix older than unsafe_fix_age_s  -> UNSAFE
absolute sources in use = 0 and last fix older than low_confidence... -> LOW_CONFIDENCE
absolute sources in use = 0                                          -> DEAD_RECKONING
any source isolated or unavailable                                   -> DEGRADED
otherwise                                                            -> NORMAL
```

LOW_CONFIDENCE and UNSAFE count as unavailable in the availability metric. That is
deliberate: refusing to answer is better than answering wrongly, but it is still
an outage and the metric has to say so. SCN-012 is the scenario built around this
— with two simultaneous faults the architecture cannot arbitrate, and the correct
result is to say so rather than to force a solution. Its acceptance block asserts
the reported mode, not the error.

That produces one of the more uncomfortable results in the failure catalog: on
SCN-012 the architecture with integrity ends the run with a *worse* position error
than the one without. It is doing the right thing — it is the only one of the two
that tells you not to trust it — but the accuracy number alone would suggest the
opposite. See `docs/failures/known_failures.md`, KF-001.

---

## 6. The trust score is not a decision

`IntegrityManager::trustScore` combines freshness, innovation consistency, state
stability and reported quality into a number between 0 and 1 for the interface.

No branch anywhere in the engine reads it. Section 6.7 of the cahier des charges
is explicit that critical decisions must never depend on a cosmetic percentage,
and the cheapest way to guarantee that is for the value to have no consumer
inside the core. It is computed on demand by the telemetry layer and used only for
display.
