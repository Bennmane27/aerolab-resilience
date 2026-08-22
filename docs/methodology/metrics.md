# Metrics: definitions, and the ways they can lie

Fifteen metrics, M-01 to M-15. The definitions matter less than the conventions
around them, because a metric with a defensible definition and a careless
convention will still produce a flattering benchmark.

## Definitions

| | Metric | Definition |
| --- | --- | --- |
| M-01 | Position RMSE | `sqrt(mean(‖p_est − p_true‖²))`, m |
| M-02 | Horizontal error | `sqrt(N² + E²)`, m |
| M-03 | Maximum error | `max ‖e‖`, m |
| M-04 | P95 error | 95th percentile of `‖e‖`, nearest rank, m |
| M-05 | Time to detect | first departure from ACTIVE **on a faulted source**, minus the fault instant, s |
| M-06 | Time to isolate | first transition to ISOLATED on a faulted source, minus the fault instant, s |
| M-07 | False alert rate | policy alerts raised outside a fault window, per gate evaluation |
| M-08 | Missed detection rate | faulted runs with no detection, over faulted runs |
| M-09 | Availability | fraction of time the reported mode is one a consumer could act on |
| M-10 | Continuity | binary per run: no unscheduled interruption, or one |
| M-11 | Recovery time | return to ACTIVE minus the fault end, s |
| M-12 | NIS consistency | `E[NIS]/dof`; 1.0 for a consistent filter |
| M-13 | CPU tick | p50, p95, p99 of the per-tick wall clock, ms |
| M-14 | Peak memory | maximum resident set size, MiB |
| M-15 | Determinism hash | SHA-256 over the replay-relevant output |

## The conventions, and what each one prevents

### A detection must belong to the source that was faulted

M-05 credits a detection only when the architecture flags a source the scenario
actually injected a fault into.

This was not the original implementation, and the original was wrong in a way that
produced a spectacular result. The first run of the detectability sweep reported
100 % detection at every drift rate, down to 0.1 m/s — four metres of injected
offset against a two-metre sensor. The tell was that time-to-detect was a constant
34.2 s at every low rate, which is not how a detector behaves: 25 s of fault start
plus 34.2 s is 59.2 s, and 59.16 s is exactly when the vision sensor goes
unavailable on this profile because the aircraft has rolled past the runway.

The metric was crediting a geometric certainty as a detection of a spoof. See
KF-007.

### A missed detection is never a zero

When no detection was raised, `time_to_detect_s` is negative and the report prints
a dash. It is never averaged in as zero, and the run is never dropped from the
denominator.

Averaging a missed detection as an instantaneous response is the single easiest
way to publish a flattering benchmark, and it is easy to do by accident — a
`std::vector<double>` of detection times that only ever gets pushed on success
looks perfectly innocent.

### An alert is raised once

M-07 counts the first departure from ACTIVE. The ACTIVE → SUSPECT → ISOLATED
escalation is one alert getting worse, not two.

### A source going away is not an alert

A transition to UNAVAILABLE is an observation, not a policy claim, and is excluded
from the false alert budget. Otherwise the vision sensor legitimately losing sight
of the runway at touchdown would be charged against the integrity architecture on
every single approach.

Isolation, by contrast, always counts: it is a decision, and a decision taken
without a fault is a false alert regardless of what prompted it.

### Refusing to answer is still an outage

M-09 counts LOW_CONFIDENCE and UNSAFE as unavailable. Refusing to answer is better
than answering wrongly, but it is not free, and a metric that treated an honest
refusal as availability would let an architecture score perfectly by declaring
itself unsafe and stopping.

This is visible in the results: on the step-spoof scenario the gating architecture
has the best accuracy *and* the worst availability of the fused solutions, because
it spends 8 % of the run declining to answer while it coasts. Both numbers are
true and they belong side by side.

### Continuity is binary per run

Either the service was provided without unscheduled interruption for the whole
run, or it was not. The campaign-level figure is the fraction of runs scoring 1.

A per-run fraction would blur a single 30-second outage into "97 % continuous" and
hide precisely the event that matters.

### Every distribution reports median, P95 and worst

`Distribution::from` computes them together and the report generator prints them
together (BEN-016). A mean alone hides the tail, and the tail is where a
navigation architecture fails.

Percentiles use nearest rank, stated here because the interpolated convention
gives different answers on small samples and the difference is not negligible at
n = 150.

### The determinism hash is sensitive to the last bit

Doubles are folded through their exact IEEE-754 bit pattern, so one ulp changes
the digest. That is the point: a hash that tolerated small differences would not
detect the class of change it exists to detect.

Its scope is one build. See DEV-003 for why cross-toolchain bit equality is not
attainable and what is checked instead.

### CPU time is measured but does not discriminate

M-13 is reported, and on the reference configuration it is around 0.007 ms per
tick at p95 — roughly 300 times under the 2 ms budget of NFR-009. With five
architectures running 15-state filters at 100 Hz, the budget is simply not the
binding constraint, and saying so is more useful than presenting a comfortable
margin as an achievement. The metric would start to discriminate against a
particle filter, which is what it is there for.
