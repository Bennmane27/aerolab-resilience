# Traceability: requirement → code → test → evidence

Section 14 of the cahier des charges asks for critical requirements to be linked
to the code that implements them, the test that checks them, and the artefact
that proves it. The full requirement catalogue is exported to
[requirements/requirements.csv](requirements/requirements.csv) — 233 entries,
213 MUST, 14 SHOULD, 6 COULD.

This file traces the ones that carry the project. A requirement is listed here if
breaking it would invalidate results rather than merely degrade them.

---

## The properties everything else rests on

| Requirement | Implementation | Test | Evidence |
| --- | --- | --- | --- |
| **SYS-001** truth, measurements, injected measurements and estimates are separate | `core/src/faults/fault_engine.cpp` — `apply()` takes only a measurement vector | `Acceptance.AT002_GroundTruthIsolation` | The dead-reckoning channel hashes identically with and without a GNSS spoof |
| **FI-017** the fault engine never modifies the truth | Enforced by the signature, not by convention | `FaultEngine.TruthIsNeverModified` | Truth re-sampled from a clean simulator, compared bit-for-bit over 2000 ticks |
| **NAV-006** no estimator reads the truth in benchmark mode | The estimator interface has no truth parameter; the alignment seed is produced once by the runner | `Pipeline.AllChannelsShareTheSameTruth` | — |
| **SYS-004** an explicit seed reproduces the same noise | `core/include/aerolab/core/rng.hpp`, per-stream derivation | `Rng.StreamsAreIndependentAndSeedDerived`, `Acceptance.AT001_DeterministicReset` | Determinism hash equal across two runs of every channel |
| **BEN-002** every architecture sees the same seeds | Structural: the truth and the sensor stream are generated once per tick and shared | `Pipeline.EveryChannelSeesTheSameFaultWindow` | Identical sample counts across channels |

---

## Estimation

| Requirement | Implementation | Test | Evidence |
| --- | --- | --- | --- |
| **NAV-003** documented EKF | `core/src/navigation/error_state_ekf.cpp` | `Ekf.LevelFlightPropagationIsExact` | Exact state hold under a level, bias-free IMU |
| **NAV-005** common estimator interface | `core/include/aerolab/navigation/estimator.hpp` | every estimator test | — |
| **NAV-009** sample and delivery timestamps handled | Snapshot rollback and replay | `Ekf.DelayedMeasurementIsReprocessedByRollback` | A fix 1 s old moves the state by its own implication, not by the stale difference |
| **NAV-010** covariance stays symmetric and valid | Joseph form plus `symmetrize()` | `Ekf.CovarianceGrowsUnderPropagationAndStaysSymmetric` | Symmetry to 1e-15 after 500 propagations |
| **NAV-014** NaN or Inf invalidates the run | `checkHealth()` on every propagation and update | `Matrix.CholeskySolvesAndRejectsNonSpd` | Non-SPD input refused rather than silently inverted |
| **NAV-015** reset restores the initial state | `reset()` restores the stored nominal and P | `Ekf.ResetRestoresTheInitialState` | Exact equality on all 15 diagonal entries |
| **VNV-011** EKF tested on analytic cases | — | `Ekf.NisMatchesTheClosedForm` | S = P + R = 25, NIS = 1 exactly |

---

## Integrity

| Requirement | Implementation | Test | Evidence |
| --- | --- | --- | --- |
| **INT-003** NIS computed | `quadraticFormInv` in `matrix.hpp` | `Matrix.QuadraticFormMatchesManualNis` | Closed-form agreement to 1e-12 |
| **INT-004** thresholds configurable and documented | `chi_square.hpp`, derived from a stated P_fa | `ChiSquare.QuantilesMatchPublishedTables` | Agreement with published tables to 1e-3 |
| **INT-005/006/007** persistence, isolation, recovery | `IntegrityManager` state machine | `Integrity.FullTransitionCycle` | The full ACTIVE→SUSPECT→ISOLATED→ACTIVE cycle with timing |
| **INT-008** reason code on every transition | `transition()` records reason, statistic, threshold | `Integrity.FullTransitionCycle` | No event carries `kNone` |
| **INT-009** ISOLATED distinct from UNAVAILABLE | Separate enumerators and separate paths | `Integrity.UnavailableIsDistinctFromIsolated` | — |
| **INT-013/014** honest mode degradation | `navigationMode()` | `Integrity.NavigationModeDegradesHonestly`, `Acceptance.AT008_DualFaultFailSafe` | SCN-012 ends UNSAFE on every seed |
| **INT-015** trust score is not a decision | Computed on demand by telemetry; no core branch reads it | `Integrity.TrustScoreIsBoundedAndFollowsState` | — |
| **INT-018** staleness by age | Age from `sample_time`, which no fault may rewrite | `Integrity.StaleMeasurementIsCaughtByAge` | A frozen source caught on a perfectly plausible value |
| **INT-021** RAIM-like exclusion | `core/src/integrity/raim.cpp` | `Raim.DetectsAndExcludesASingleOutlier` | The biased satellite is identified by index |
| **INT-022 → DEV-005** solution separation | `core/src/navigation/solution_separation.cpp` | `SolutionSeparation.StatisticGrowsWhenGnssDisagrees` | Separation grows with injected drift |

---

## Fault injection

Every fault family has a test showing it changes the measurement (VNV-002); the
truth half is structural. `tests/unit/test_faults.cpp` covers all thirteen:
`SourceUnavailable`, `BiasStepShiftsEveryComponent`,
`RampGrowsLinearlyAndSaturates`, `FreezeRepeatsValueAndTimestamp`,
`LatencyShiftsDeliveryButNotSampleTime`, `NoiseBurstIncreasesScatterWithoutBias`,
`DropProbabilisticRampsTowardsTheFinalRate`,
`ImuAccelAndGyroBiasesHitTheRightTriad`,
`VelocityInconsistencyOnlyTouchesTheVelocityChannel`,
`VisionDegradeLowersQualityWithoutRemovingTheSource`,
`PseudorangeOutlierTargetsOneSatellite`, `MultipleFaultsCombine`.

| Requirement | Test | Evidence |
| --- | --- | --- |
| **FI-015** start and end events recorded | `FaultEngine.EmitsStartAndEndEvents` | M-05 and M-06 have an unambiguous t0 |
| **FI-016** deterministic at identical seed | `FaultEngine.IsDeterministic` | 100 identical samples across two passes |
| **FI-018** incoherent amplitudes refused before the run | `FaultEngine.RejectsIncoherentSpecifications` | Seven distinct rejection paths, each with a message |
| **FI-020** phase trigger | `FaultEngine.PhaseTriggerArmsOnTheMissionPhase` | — |

---

## Metrics and benchmark

| Requirement | Implementation | Test | Evidence |
| --- | --- | --- | --- |
| **M-05/M-06** time to detect and isolate | `MetricsAccumulator::noteIntegrityEvent` | `Metrics.TimeToDetectAndIsolate` | Measured from the fault instant |
| **M-08** missed detections counted, not dropped | Negative sentinel, never zero | `Metrics.MissedDetectionIsNegativeNotZero` | Report prints a dash |
| **M-07** false alerts | Attributed to policy decisions only | `Metrics.FalseAlertsAreCountedOutsideTheFaultWindow` | `Acceptance.AT003_NominalFalseAlertBudget`: zero over 30 nominal runs |
| **M-12** NIS consistency | Normalised by degrees of freedom | `Metrics.NisConsistencyNormalisesByDegreesOfFreedom` | — |
| **M-15** determinism hash | `DeterminismHasher`, IEEE-754 bit patterns | `DeterminismHasher.IsSensitiveToTheLastBit` | One ulp changes the digest |
| **BEN-016** median, P95 and worst beside every mean | `Distribution::from` | `Distribution.NearestRankPercentiles` | Enforced by the report generator |
| **BEN-017** tuning and evaluation sets separate | Different seed bases in the two suite files | — | `benchmark/tuning.yaml` vs `benchmark/core.yaml` |
| **SYS-016 → DEV-007** conformance verdict | `evaluateVerdict()` against the acceptance block | `Acceptance.FailingCriterionProducesAReadableFailNotACrash` | A deliberately impossible criterion produces a readable FAIL, not a crash |

---

## Data and interfaces

| Requirement | Implementation | Test | Evidence |
| --- | --- | --- | --- |
| **DATA-002/008** versioned schema, incompatible refused | `loadScenarioFromJson` checks the version first | `Scenario.RejectsAnUnknownSchemaVersion` | Refusal message names the versions |
| **DATA-005** manifest carries every hash | `buildManifest()` | `Pipeline.ManifestCarriesProvenanceAndVerdict` | 64-character scenario and config hashes present |
| **DATA-009** float precision sufficient for replay | `%.17g` | `Json.RoundTripsEveryScalarType` | `0.1 + 0.2` round-trips exactly |
| **VNV-016** parsers fuzzed on malformed input | — | `Parsers.MalformedInputNeverCrashes` | 3000 mutations across both parsers |
| **API-003** C++ errors become structured TypeScript errors | `web/wasm/binding.cpp`, `web/src/core/session.ts` | — | Discriminated union the UI cannot ignore |

---

## Verification of the verification

| Requirement | How |
| --- | --- |
| **VNV-004/005**, **AT-010** | `asan` and `ubsan` presets; CI fails on any finding |
| **VNV-008**, **AT-011** | `coverage` preset; CI enforces a 90 % line floor on `core/` |
| **VNV-010**, **AT-009** | `tools/analysis/wasm_parity.mjs`, tolerance-based (see DEV-003) |
| **VNV-020** | Every catalogue scenario runs in `ci-native.yml` before any release |
| **VNV-022** | `docs/deviations.md`, ten entries |
| **VNV-024** | `tools/analysis/calibrate_separation.sh` — the sensitivity measurement behind the one calibrated parameter |
| **UI-025** | `docs/failures/known_failures.md`, seven entries |

---

## Requirements deliberately not met in V1

Recording these is the point of VNV-022; an unmet requirement that nobody wrote
down is a defect, one that is documented is a scope decision.

| Requirement | Status | Why |
| --- | --- | --- |
| **NAV-017** (SHOULD) Student-t or particle filter | Not implemented | The comparison the project needed was between integrity *policies* on the same filter, not between filters. NAV-E remains available through the estimator interface; adding one is a new class, not a change to anything else. |
| **UI-010** manual injection | Implemented as a scenario parameter | See DEV-010: a runtime injection button produces runs no manifest can describe. |
| **SYS-020** (COULD) JSBSim ground truth | Not implemented | Listed as a post-V1 extension in section 23. |
| **NFR-026** (SHOULD) Docker benchmark image | Not implemented | The build has no dependency beyond a compiler and CMake, so the image would add a layer without removing one. |
| **INT-022** ARAIM study | Superseded | Promoted to a working architecture instead (DEV-005). |
