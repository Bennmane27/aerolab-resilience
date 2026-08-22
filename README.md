# AEROLAB RESILIENCE

**Navigation Integrity & Resilience Validation Platform**

> *Break the navigation. Measure what survives.*

A simulation-only test bench for aircraft navigation integrity. It generates a
deterministic trajectory, feeds synthetic GNSS, inertial, barometric and
runway-relative vision measurements to five navigation architectures, breaks
those measurements on purpose, and measures which architectures still know where
the aircraft is — how fast they notice, how often they cry wolf, and where each
one stops working.

The interesting output is not that it detects things. It is the **detectability
floor**: the drift rate below which each integrity policy stops noticing that a
source is lying — and, as it turned out, which detector was doing the work all
along.

---

## What this is not

Simulation only. Every "attack" in this repository is arithmetic applied to an
array of doubles inside one process. There is no radio, no signal generator, no
receiver model and no procedure for interfering with anything real. Not certified,
not airworthy, not affiliated with any manufacturer or authority. See
[SECURITY_AND_SAFETY.md](SECURITY_AND_SAFETY.md).

---

## Quick start

```bash
cmake --preset release && cmake --build build/release -j
./build/release/bin/aerolab_cli --scenario scenarios/SCN-003.yaml --config configs/evaluation.json --out results
```

```
SCN-003  GNSS step spoof
seed 103   duration 90.0 s
fault window 30.00 s .. 90.00 s

estimator              RMSE m      P95 m      max m    TTD s    TTI s   avail  final mode
------------------------------------------------------------------------------------------------
gnss_only              77.499    100.390    103.491        -        -   99.9%  NORMAL
ins_dr                554.308   1105.515   1219.969        -        -  100.0%  DEAD_RECKONING
ekf                    12.819     28.145     33.913        -        -  100.0%  NORMAL
integrity_ekf           7.832     17.116     20.896     0.28     1.68   91.7%  DEGRADED
solsep_ekf              9.896     27.723     36.661     0.28     1.50   99.5%  DEGRADED

VERDICT: PASS
```

Read that table as follows. The naive satellite-only navigator is off by 77 m
because it fused a 100 m spoof and never questioned it. The pure inertial
solution drifts to half a kilometre — that is what coasting costs. The filter
with no integrity layer absorbs part of the spoof and keeps calling the result
NORMAL. The two with an integrity layer notice within 0.28 s, isolate the source,
and report DEGRADED rather than pretending nothing happened. One of them pays for
that with availability: it spends 8 % of the run declining to answer.

The full campaign runs the same thing over a thousand seeds per scenario:

```bash
./build/release/bin/aerolab_bench --suite benchmark/core.yaml --out results/campaign --jobs 8
```

---

## The Web Lab

The same C++ core, compiled to WebAssembly, runs in the browser. Nothing is
recomputed in JavaScript and no server is involved — the numbers on the page come
out of the same filters that produce the published results.

```bash
emcmake cmake --preset wasm && cmake --build build/wasm -j
cd web && npm ci && npm run dev
```

---

## Architecture

```
truth ──► sensors ──► fault engine ──► delivery-ordered bus ──┬─► GNSS only
 (closed   (noise,     (arithmetic     (sample vs delivery)   ├─► INS dead reckoning
  form)     bias,       on measurements                       ├─► EKF
            latency)    only)                                 ├─► EKF + innovation gating
                                                              └─► EKF + solution separation
```

Two properties hold the whole thing up.

**The fault engine is never given the truth.** Not by convention — by the
signature of the function, which takes a vector of measurements and nothing else.
That is what makes every number in this project mean something, so it has its own
acceptance test (AT-002) which checks that a GNSS spoof leaves the inertial
channel bit-for-bit unchanged.

**The integrity gate runs before fusion, not after.** The estimator interface
splits the update in two: `prepareUpdate` computes the innovation and changes no
state, the policy decides, and only then does `applyUpdate` commit. The cahier
des charges specified the opposite order, which would have fused a spoofed fix
before deciding to reject it. See [DEV-004](docs/deviations.md).

| Component | Where |
| --- | --- |
| Closed-form trajectory, no integration drift | `core/src/truth/` |
| Sensor models, sample and delivery timestamps | `core/src/sensors/` |
| Thirteen fault families | `core/src/faults/` |
| 15-state error-state EKF with rollback reprocessing | `core/src/navigation/error_state_ekf.cpp` |
| Solution separation against a GNSS-free sub-filter | `core/src/navigation/solution_separation.cpp` |
| Integrity state machine, chi-square gating, RAIM-like residual test | `core/src/integrity/` |
| Metrics M-01..M-15 | `core/src/metrics/` |

---

## The headline result

Twelve drift rates, 150 seeds each, three configurations. Detection rate for the
two architectures with an integrity layer:

| drift rate | full config |  | cross check disabled |  |
| ---: | ---: | ---: | ---: | ---: |
| | gating | separation | gating | separation |
| 0.50 m/s | 23 % | 34 % | 9 % | 21 % |
| 0.75 | 97 % | 99 % | 11 % | **58 %** |
| 1.00 | 100 % | 100 % | 15 % | **94 %** |
| 1.50 | 100 % | 100 % | 27 % | **100 %** |
| 4.50 | 100 % | 100 % | 99 % | 100 % |

In the default configuration the two policies are indistinguishable, and the
detectability floor sits around 0.5–0.75 m/s for both. That looked like solution
separation buying nothing — until the control experiment showed why: a plain
threshold on the distance between the satellite fix and the runway-relative vision
fix was catching the drift first, and both architectures were inheriting its
performance.

Disable that cross check and they separate by roughly a factor of six. Remove the
vision sensor entirely and both collapse to identical numbers, because the
satellite-free sub-filter then has no absolute reference and drifts faster than
the spoof does.

**Solution separation is not a way to get integrity out of nothing.** It converts
an independent absolute reference into detection power. Given one it lowers the
floor substantially; given none it is an extra 15-state filter that reports what
the innovation gate already reported. That statement is more useful than the one
the project set out to prove, and it took a control experiment to get to it —
[the full write-up is here](docs/methodology/detectability-floor.md).

## What was learned building it

The failure catalog is not an appendix. Two of its entries are cases where the
platform caught its own simulator, which is what a test bench is actually for.

- **The ground truth was not physically realisable.** On the turning approach,
  every architecture with an integrity layer diverged to kilometres on a
  *fault-free* run. The horizontal track was a chain of constant-curvature
  segments, so the coordinated-turn bank angle stepped from level to 20° in zero
  time. That is an infinite roll rate; no gyroscope can report it, so the filter
  inherited a permanent attitude error. Nothing was wrong with the estimator.

- **The detectability sweep reported 100 % detection at every drift rate**, down
  to 0.1 m/s — four metres of total offset against a two-metre sensor. The tell
  was that time-to-detect was a constant 34.2 s at every low rate, which is not
  how a detector behaves: 25 s of fault start plus 34.2 s is 59.2 s, and 59.16 s
  is exactly when the vision sensor loses sight of the runway at touchdown. The
  metric was crediting *any* source leaving ACTIVE. Detection is now attributed to
  the faulted source only.

- **The project's own central hypothesis did not survive measurement.** Solution
  separation was promoted to a first-class architecture because innovation gating
  should be blind to slow drift. It is — but a simple cross check was masking the
  difference entirely, and without an independent absolute reference solution
  separation is no better at all.

- **Isolating a source can make the solution worse.** On the dual-fault scenario
  the architecture that isolates ends with a higher position error than the one
  that fuses everything blindly. It is still the only one of the two that tells
  you not to trust it.

- **The naive baseline is limited by fix age, not fix noise.** Twelve metres of
  error from a two-metre sensor, because at 5 Hz and 80 ms of latency the last
  fix is 0.18 s old and the aircraft has moved 12.6 m.

Full list with reproduction commands: [docs/failures/known_failures.md](docs/failures/known_failures.md).

---

## Verification

| | |
| --- | --- |
| Tests | 164, covering unit, component, integration, regression and acceptance |
| Sanitizers | AddressSanitizer and UndefinedBehaviorSanitizer clean, leak detection on |
| Scenarios | 14, each with a machine-readable acceptance block, all passing |
| Determinism | same binary, scenario and seed → bit-identical output, checked by hash |
| Native / wasm | agreement within a stated tolerance and on every integrity event |
| Warnings | zero, at `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion` |

```bash
ctest --preset release --output-on-failure          # the suite
cmake --preset asan && ctest --preset asan          # AT-010
bash tools/analysis/drift_sweep.sh                  # the headline experiment
bash tools/analysis/calibrate_separation.sh         # the one calibrated parameter
node tools/analysis/wasm_parity.mjs                 # AT-009
```

---

## Honesty rules this project holds itself to

Taken from section 8.1 and section 19 of the specification, and enforced in the
code rather than in a promise:

- **No mean is published alone.** Every distribution reports median, P95 and worst
  case beside it.
- **A missed detection is recorded as a miss**, not omitted from the average. The
  metric returns "never", and the report prints a dash rather than a zero.
- **Failed runs are kept.** A campaign records them and continues.
- **The tuning and evaluation seed sets are separate**, and the evaluation
  configuration is frozen before the campaign that reports on it. Exactly one
  parameter in this project was calibrated against measured results, and
  [it says so, with the measurement](docs/methodology/integrity.md).
- **An acceptance criterion states what the run must produce, never that a
  particular algorithm must win.** SCN-004 is allowed to defeat the innovation
  gate, and its acceptance block says so.

---

## Documentation

| | |
| --- | --- |
| [docs/deviations.md](docs/deviations.md) | Ten departures from the specification, each with its reason |
| [docs/methodology/integrity.md](docs/methodology/integrity.md) | What each test catches, and what it structurally cannot |
| [docs/failures/known_failures.md](docs/failures/known_failures.md) | What goes wrong, with reproduction commands |
| [docs/requirements/requirements.csv](docs/requirements/requirements.csv) | 233 requirements, exported from the specification |
| [docs/traceability.md](docs/traceability.md) | Requirement → code → test → evidence |
| [docs/industrial_evidence.md](docs/industrial_evidence.md) | What each cited publication does and does not support |
| [BENCHMARK_REPORT.md](BENCHMARK_REPORT.md) | Campaign results with the configuration that produced them |
| [SECURITY_AND_SAFETY.md](SECURITY_AND_SAFETY.md) | The safety boundary, in detail |

---

## Licence

MIT. See [LICENSE](LICENSE).
