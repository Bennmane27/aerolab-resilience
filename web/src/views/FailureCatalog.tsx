// AEROLAB RESILIENCE - UI-08 Failure catalog (UI-025).
//
// Section 22 of the cahier des charges makes publishing the known failures a
// condition of calling V1 finished. Each entry below is a real, reproducible
// finding from this project, with the command that reproduces it.

interface Entry {
  id: string;
  title: string;
  what: string;
  why: string;
  status: string;
  reproduce: string;
}

const ENTRIES: Entry[] = [
  {
    id: "KF-001",
    title: "Isolating the satellite source can make the solution worse, not better",
    what:
      "On the dual-fault scenario (a position spoof and an accelerometer bias arriving together), the architecture that gates and isolates ends the run with a HIGHER position error than the plain filter that fuses everything blindly: roughly 25 m RMSE against 16 m on the reference seed.",
    why:
      "Isolation moves the solution onto the inertial channel. When the inertial channel is the other fault, that is a move onto worse data. The architecture is doing the right thing by its own criteria — it correctly declares the result unusable, and its availability drops to about 70 % — but the accuracy number alone would suggest the integrity layer is harmful. It is not: it is the only one of the two that tells you not to trust it.",
    status:
      "Open by design. It is a real limit of single-fault reasoning under a double fault, and it is why the scenario asserts the reported MODE rather than the error.",
    reproduce: "./aerolab_cli --scenario scenarios/SCN-012.yaml --config configs/evaluation.json",
  },
  {
    id: "KF-008",
    title: "Solution separation added nothing measurable until the cross check was removed",
    what:
      "The detectability sweep showed the two integrity architectures performing almost identically in the default configuration: a floor around 0.5 to 0.75 m/s for both, and 100 % detection above 1 m/s for both. Solution separation, promoted to a first-class architecture specifically because it should beat innovation gating on a slow drift, appeared to buy nothing.",
    why:
      "The detector that was firing was neither of them. A simple threshold on the distance between the satellite fix and the runway-relative vision fix caught the drift first in almost every case, so both architectures inherited its performance. Disabling that cross check while leaving vision feeding the filter separated them sharply - 58 % against 11 % at 0.75 m/s, 94 % against 15 % at 1.0 m/s - roughly a sixfold difference in detectability floor. Removing vision entirely collapsed both to identical numbers, because the satellite-free sub-filter then has no absolute reference and drifts faster than the spoof does.",
    status:
      "Understood, and the justification revised. Solution separation is not a way to get integrity out of nothing: it converts an independent absolute reference into detection power. Given one it lowers the floor substantially; given none it is an extra filter that reports what the innovation gate already reported.",
    reproduce: "tools/analysis/drift_sweep.sh 150 results/sweep_nocc nocrosscheck",
  },
  {
    id: "KF-002",
    title: "The innovation gate is not blind to a slow drift at this ramp rate",
    what:
      "The slow-drift scenario ramps the satellite position by 150 m over 45 s, about 3.3 m/s. The project's own starting hypothesis was that a chi-square gate on the innovation would miss it. It does not: the gate detects on every seed tested, with a time to detect around 8.5 s.",
    why:
      "The filter cannot follow the ramp freely, because the barometric and runway-relative vision measurements hold it back, and the cross-check against the vision fix fires as well. The structural weakness of innovation gating against slow drift is real, but it only bites below a drift rate this experiment has to find rather than assume.",
    status:
      "The hypothesis as originally stated is not supported at 3.3 m/s. The measured floor is around 0.5 to 0.75 m/s in the default configuration. See KF-008 for what was actually doing the detecting.",
    reproduce: "tools/analysis/drift_sweep.sh",
  },
  {
    id: "KF-007",
    title: "The detectability sweep reported detection that had not happened",
    what:
      "The first run of the sweep reported 100 % detection at every drift rate, including 0.1 m/s - four metres of total offset against a sensor with two metres of noise. Time to detect was a constant 34.2 s at every low rate, which is not how a detector behaves.",
    why:
      "25 s of fault start plus 34.2 s is 59.2 s, and 59.16 s is exactly when the vision sensor goes unavailable on this profile because the aircraft has rolled past the runway. The detection metric was crediting any source leaving the active state, so a geometric certainty was being scored as a detection of a spoof.",
    status:
      "Fixed. Detection is credited only for a source the scenario actually faulted, so a fault-free run cannot produce one at all.",
    reproduce: "aerolab_tests --gtest_filter=Metrics.DetectionIsCreditedOnlyOnTheFaultedSource",
  },
  {
    id: "KF-003",
    title: "A discontinuous bank angle in the truth silently destroyed the filter",
    what:
      "On the turning approach profile, every architecture with an integrity layer diverged to kilometres of error on a fault-free run, and isolated three sources while doing it.",
    why:
      "The horizontal track was a chain of constant-curvature segments, so curvature stepped at the arc entry. A coordinated-turn bank angle is a function of curvature, so the bank stepped too — from level to 20 degrees instantaneously. That is an infinite roll rate. No gyroscope can report it, so the filter inherited a permanent attitude error at the turn entry and everything downstream followed. Nothing was wrong with the estimator; the ground truth was not physically realisable.",
    status:
      "Fixed. The bank angle is now derived from a curvature blended over a roll-in distance, which is what an aircraft actually does. A regression test bounds the body rates over the whole profile.",
    reproduce: "aerolab_tests --gtest_filter=GroundTruth.BodyRatesStayBoundedThroughTheTurn",
  },
  {
    id: "KF-004",
    title: "The theoretical solution-separation covariance is optimistic in practice",
    what:
      "Applied literally, the identity cov(x_full − x_sub) = P_sub − P_full produced false isolations on 12 % of fault-free runs, later reduced to 4 % by evaluating the test at the measurement rate instead of every tick.",
    why:
      "The identity holds for two optimal filters. Neither of these is: both are linearised, they share a non-white inertial error, and the main filter may refuse an update its policy rejected. Every one of those effects reduces the cross-covariance, which makes the true covariance of the difference larger than the theory and the statistic biased high.",
    status:
      "Mitigated by a covariance inflation factor of 2.0, calibrated on the tuning seed set and frozen before the evaluation campaign. Zero false isolations over 400 fault-free runs, with unchanged detection rate and time to detect on the fault scenarios.",
    reproduce: "tools/analysis/calibrate_separation.sh",
  },
  {
    id: "KF-005",
    title: "The naive baseline is limited by fix age, not by fix noise",
    what:
      "The GNSS-only architecture shows about 12 m of position error on a fault-free approach, with a 2 m satellite noise model. That is six times the noise.",
    why:
      "At 5 Hz with 80 ms of transport latency, the last available fix is on average about 0.18 s old, and the aircraft covers roughly 12.6 m in that time. The error is dominated by the age of the measurement, not by its precision. It is a useful reminder that quoting a sensor's accuracy says little about the accuracy of a navigator built on it.",
    status: "Not a defect. Reported because the number is otherwise easy to misread.",
    reproduce: "./aerolab_cli --scenario scenarios/SCN-001.yaml --config configs/evaluation.json",
  },
  {
    id: "KF-006",
    title: "Recovering from a fault can trip the gate a second time",
    what:
      "On the slow-drift scenario, isolations are observed after the fault window has closed, and the metric counts them as false alerts.",
    why:
      "While the source is isolated the solution coasts and drifts. When the source returns to nominal, the honest measurement now disagrees with the drifted filter, so the gate fires again. The alert is not wrong — the two really do disagree — but the cause is the recovery transient, not a new fault.",
    status:
      "Open. Handling it properly means resetting the filter towards the returning source rather than treating its first measurements as suspect; that is a design change, not a threshold change, and it is listed as post-V1 work.",
    reproduce: "./aerolab_bench --scenario scenarios/SCN-004.yaml --seeds 1:200 --out results/kf006",
  },
];

export function FailureCatalogView() {
  return (
    <div className="pad prose">
      <h2 style={{ marginTop: 0 }}>Failure catalog</h2>
      <p>
        Every entry here is something that went wrong, or still goes wrong, in this project. They are
        published because a benchmark that only reports its successes is not a benchmark. Two of
        these were found by the platform catching its own simulator, which is the outcome a test
        bench is actually for.
      </p>

      {ENTRIES.map((e) => (
        <section className="panel" key={e.id} style={{ marginBottom: 14 }}>
          <h3 style={{ margin: 0, padding: "8px 12px", fontSize: 12, letterSpacing: "0.08em", textTransform: "uppercase", color: "var(--text-dim)", background: "var(--bg-panel-2)", borderBottom: "1px solid var(--line)" }}>
            {e.id} — {e.title}
          </h3>
          <div className="panel-body">
            <p style={{ marginTop: 0 }}>
              <b>What happens.</b> {e.what}
            </p>
            <p>
              <b>Why.</b> {e.why}
            </p>
            <p>
              <b>Status.</b> {e.status}
            </p>
            <pre style={{ marginBottom: 0 }}>{e.reproduce}</pre>
          </div>
        </section>
      ))}
    </div>
  );
}
