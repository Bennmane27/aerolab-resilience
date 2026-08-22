// AEROLAB RESILIENCE - UI-07 Methodology (UI-024).
//
// Every assumption a reader would need in order to disagree with the numbers.
import type { BuildInfo } from "../core/session";

export function MethodologyView({ repoUrl, build }: { repoUrl: string; build: BuildInfo | null }) {
  return (
    <div className="pad prose">
      <h2 style={{ marginTop: 0 }}>Methodology</h2>
      <blockquote>
        This is a research and engineering demonstrator for evaluating navigation integrity and
        resilience. It is not a certified system, it makes no claim of conformance to DO-178C,
        RAIM, ARAIM or any other standard, and it is not affiliated with or endorsed by any
        aircraft manufacturer, equipment supplier or aviation authority.
      </blockquote>

      <h3>What the experiment is</h3>
      <p>
        A deterministic trajectory is generated in closed form. Synthetic sensors read that
        trajectory and add their own noise, bias and latency. A fault engine then applies arithmetic
        transformations to those measurements — and only to those measurements. Five navigation
        architectures consume the identical resulting stream, and their outputs are compared against
        the trajectory that produced it.
      </p>
      <p>
        The fault engine is never given access to the trajectory. That is enforced by the type of the
        function, not by convention: it takes an array of measurements and nothing else. It is the
        property that makes every number here meaningful, so it also has its own acceptance test.
      </p>

      <h3>Safety boundary</h3>
      <p>
        Everything called a spoof, a jam or an attack in this project is a number added to an array
        inside one process. There is no radio, no signal generator, no receiver model, no
        transmission parameter and no operational procedure for interfering with a real system
        anywhere in the source. The word "spoofing" here means: add an offset to a synthetic position
        that never left this program.
      </p>

      <h3>The five architectures</h3>
      <ul>
        <li>
          <b>GNSS only</b> — position is the last valid satellite fix. It exists to show what happens
          when a single absolute source is trusted without question.
        </li>
        <li>
          <b>INS dead reckoning</b> — strapdown inertial integration with no absolute update ever. It
          bounds how long a solution can coast.
        </li>
        <li>
          <b>EKF</b> — a 15-state error-state Kalman filter fusing inertial, satellite, barometric and
          runway-relative vision measurements, with no integrity layer. The control case.
        </li>
        <li>
          <b>EKF + innovation gating</b> — the same filter, plus a chi-square test on each
          measurement's innovation, with a persistence counter before a source is suspected, an
          isolation delay, and a recovery window before it is trusted again.
        </li>
        <li>
          <b>EKF + solution separation</b> — the same filter and the same gate, plus a second,
          independent test: the all-sources solution is compared against a sub-filter that never
          receives satellite data at all.
        </li>
      </ul>

      <h3>Why solution separation exists here</h3>
      <p>
        A chi-square gate on the innovation is structurally weak against a slow drift. As a spoofed
        position ramps away, the filter state follows it, so the innovation — the disagreement
        between the measurement and the prediction — stays small and the gate never fires. A
        sub-filter that does not receive the drifting source cannot follow it, so the separation
        between the two solutions grows with the injected error instead of being absorbed by it.
      </p>
      <p>
        For two optimal filters where one uses a subset of the other's measurements, the covariance
        of their difference is the difference of their covariances. Neither of ours is exactly
        optimal — they are linearised, they share a non-white inertial error, and the main one may
        refuse an update — so the true covariance of the difference is larger than the theory says
        and the raw statistic is biased high. Measured on the tuning seed set, the raw form isolated
        the satellite source on 4 % of fault-free runs. An inflation factor of 2.0, calibrated on
        that tuning set and frozen before the evaluation campaign, brings that to zero over 400
        fault-free runs with no measured loss of detection performance.
      </p>

      <h3>Assumptions and simplifications</h3>
      <ul>
        <li>Flat earth, local north-east-down frame, no Coriolis or transport rate.</li>
        <li>Zero angle of attack: pitch equals flight path angle. A real aircraft holds a few degrees.</li>
        <li>No wind, therefore no crab angle.</li>
        <li>
          The barometric bias is not part of the state vector; it is absorbed as extra measurement
          uncertainty, which is honest but suboptimal.
        </li>
        <li>
          The vision sensor is a synthetic runway-relative measurement, not computer vision on
          rendered images.
        </li>
        <li>
          The satellite constellation used by the residual test is a frozen geometry snapshot, not an
          orbital model.
        </li>
      </ul>

      <h3>Reproducibility</h3>
      <p>
        Every run is fully determined by a scenario file, a configuration file and a seed, and every
        run exports the content hash of both files, the commit, the compiler and the resulting
        metrics. Two runs of the same binary with the same inputs produce bit-identical output, and
        an acceptance test checks it. Bit-identical output across different compilers or across the
        native and WebAssembly builds is <em>not</em> claimed: floating-point library differences and
        vectorisation make it unattainable, so cross-build agreement is checked against a stated
        numeric tolerance and on the equivalence of the integrity events instead.
      </p>

      <h3>Industrial context</h3>
      <p>
        Satellite navigation interference has been the subject of published safety bulletins and
        coordinated action plans from European aviation authorities, and resilient
        positioning-navigation-timing is an active industrial programme area. Those publications
        establish that the problem is real and the methods are relevant. They say nothing about this
        implementation, and are cited in the repository with the date and the specific claim each one
        supports — never as an endorsement.
      </p>

      <h3>Build</h3>
      {build ? (
        <dl className="kv">
          <dt>core version</dt>
          <dd>{build.version}</dd>
          <dt>commit</dt>
          <dd>{build.commit}</dd>
          <dt>build type</dt>
          <dd>{build.build_type}</dd>
          <dt>compiler</dt>
          <dd>{build.compiler}</dd>
          <dt>telemetry schema</dt>
          <dd>v{build.telemetry_schema}</dd>
          <dt>scenario schema</dt>
          <dd>v{build.scenario_schema}</dd>
        </dl>
      ) : (
        <p className="empty">Core not loaded.</p>
      )}

      <p style={{ marginTop: 24 }}>
        Full requirement catalogue, traceability matrix, benchmark report and source:{" "}
        <a href={repoUrl}>{repoUrl}</a>
      </p>
    </div>
  );
}
