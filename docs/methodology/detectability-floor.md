# The detectability floor

This is the result the project exists to produce. It is also the result that
contradicted the project's own starting hypothesis, which is why it is written up
in full rather than summarised.

## The question

Not "does the integrity layer work" — a 100 m step is caught by anything at
0.28 s. The question is: **how slowly does a source have to lie before each policy
stops noticing?**

## The hypothesis, and why it was wrong as stated

`docs/deviations.md` DEV-005 promoted solution separation from an optional
extension to a first-class architecture on the following reasoning:

> A chi-square gate on the innovation is structurally weak against a slow drift.
> As the spoofed position ramps away, the filter state follows it, so the
> innovation stays small and the gate never fires. Solution separation compares
> against a sub-filter that never receives the drifting source, so the separation
> grows with the injected error instead of being absorbed by it.

The mechanism is correct. The conclusion drawn from it — that solution separation
would show a measurably lower detectability floor — turned out to depend entirely
on a condition the reasoning did not mention.

## The experiment

`tools/analysis/drift_sweep.sh` generates one scenario per drift rate, identical
except for the ramp amplitude, and runs the same 150-seed set through every one.
Three configurations were run, each 12 rates × 150 seeds × 5 architectures.

| Configuration | Vision as a filter measurement | Explicit GNSS-vs-vision cross check |
| --- | --- | --- |
| `full` | yes | yes |
| `nocrosscheck` | yes | **no** |
| `novision` | **no** | no |

## Results

Detection rate against drift rate, for the two architectures with an integrity
layer. The architecture with no integrity layer detects nothing at any rate, by
construction, and is omitted.

### Configuration `full` — everything enabled

| drift rate | total offset | innovation gating | solution separation |
| ---: | ---: | ---: | ---: |
| 0.10 m/s | 4 m | 0 % | 0 % |
| 0.20 | 9 m | 1 % | 1 % |
| 0.35 | 16 m | 8 % | 9 % |
| 0.50 | 22 m | 23 % | 34 % |
| 0.75 | 34 m | 97 % | 99 % |
| 1.00 | 45 m | 100 % | 100 % |
| ≥ 1.50 | ≥ 68 m | 100 % | 100 % |

**Detectability floor ≈ 0.5–0.75 m/s for both.** The two policies are almost
indistinguishable, and solution separation appears to add nothing.

### Configuration `nocrosscheck` — vision still feeds the filter, cross check off

| drift rate | innovation gating | solution separation |
| ---: | ---: | ---: |
| 0.50 m/s | 9 % | 21 % |
| 0.75 | 11 % | **58 %** |
| 1.00 | 15 % | **94 %** |
| 1.50 | 27 % | **100 %** |
| 2.00 | 41 % | **100 %** |
| 3.00 | 67 % | 100 % |
| 4.50 | 99 % | 100 % |

**Now the two separate sharply.** Solution separation holds a floor around
0.75–1.0 m/s; innovation gating does not reach reliable detection until about
4.5 m/s. That is roughly a **sixfold** difference in detectability floor, and the
median time to detect at 1.5 m/s is 21.5 s against 29.1 s.

### Configuration `novision` — vision removed entirely

| drift rate | innovation gating | solution separation |
| ---: | ---: | ---: |
| 1.00 m/s | 19 % | 19 % |
| 3.00 | 34 % | 34 % |
| 6.00 | 65 % | 65 % |
| 9.00 | 97 % | 97 % |

**Both collapse, and become identical to the digit.** Neither reaches reliable
detection anywhere in the swept range.

## What this actually says

Three findings, in order of how much they change the conclusion.

**1. In the full configuration, the detector that fires is the cross check, not
either gate.** With a simple geometric comparison between the satellite fix and
the runway-relative vision fix available, both policies detect everything above
1 m/s and neither is distinguishable from the other. The sophisticated statistic
is not what caught the fault; a threshold on a distance was.

That is worth stating plainly because it is the kind of result a project is
tempted to leave out. Measured against a well-chosen cross check, the elaborate
architecture bought nothing on this profile.

**2. Solution separation is genuinely and substantially better — when it is the
mechanism actually doing the work.** Remove the cross check and the sixfold gap
appears exactly where the theory predicted. The mechanism in DEV-005 is real; it
was simply masked.

**3. Solution separation's power depends entirely on the sub-filter having an
independent absolute reference.** Remove vision altogether and the GNSS-free
sub-filter has only inertial data and a barometer. It drifts faster than the spoof
does. Its covariance `P_sub` grows so large that the threshold derived from
`P_sub − P_full` grows with it, and the test loses power at exactly the same rate
the innovation gate does — which is why the two configurations agree to the digit.

That third finding is the useful engineering statement. **Solution separation is
not a way to get integrity out of nothing.** It converts an independent source
into detection power. Given one, it lowers the floor by about a factor of six.
Given none, it is an extra 15-state filter that tells you what the innovation gate
already told you.

## Consequences for the project's own claims

DEV-005 justified promoting solution separation to a first-class architecture. The
promotion was still correct — the sixfold improvement in the `nocrosscheck`
configuration is real, and it could not have been measured without implementing
it — but the justification as originally written was incomplete. It should have
read: *given an independent absolute reference for the sub-filter*.

The failure catalog records this as KF-002 and KF-008 rather than quietly
rewriting the deviation, because the sequence — hypothesis, measurement,
contradiction, control experiment, revised statement — is the part worth keeping.

## Reproducing

```bash
cmake --preset release && cmake --build build/release -j
bash tools/analysis/drift_sweep.sh 150 results/drift_sweep              # full
bash tools/analysis/drift_sweep.sh 150 results/drift_sweep_nocc nocrosscheck
bash tools/analysis/drift_sweep.sh 150 results/drift_sweep_nv   novision
```

Each writes `sweep.csv` and `sweep.json` alongside the per-rate campaign
directories, so the numbers above can be checked line by line.
