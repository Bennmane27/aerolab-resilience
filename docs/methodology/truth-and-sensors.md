# Ground truth and sensor models

## The truth carries no state

The trajectory is available in closed form at any instant. `sampleAt(t)` is a pure
function of the profile: there is no integrator, no accumulated position, and
therefore nothing that can drift. Two consequences follow.

First, a run can be evaluated at any time without replaying what came before,
which is what makes the finite-difference checks in the test suite possible.
Second, and more importantly, the reference against which every error is measured
cannot itself be wrong by accumulation. If the truth were integrated, a long run
would compare estimates against a reference that had quietly drifted, and every
number would be off by an amount nobody could see.

The only quantity not in closed form is the set of Euler rates, obtained by a
five-point central difference of the analytic attitude angles with h = 10⁻³ s.
The resulting error is around 10⁻¹¹ rad/s, nine orders of magnitude below the
gyroscope noise floor.

## Construction

**Horizontal track.** A chain of constant-curvature segments. Each admits an exact
closed form in arc length:

```
heading(s) = h0 + k s
k = 0 :  p(s) = p0 + s (cos h0, sin h0)
k ≠ 0 :  p(s) = p0 + (1/k) (sin(h0 + k s) − sin h0,  cos h0 − cos(h0 + k s))
```

**Speed schedule.** Constant on the approach, then a smoothstep deceleration to
taxi speed. The smoothstep has zero derivative at both ends, so acceleration is
continuous, and its integral is a closed-form polynomial so distance stays exact.

**Vertical profile.** A glideslope aimed at a point beyond the threshold, then a
cubic Hermite flare with `h(0) = h_f`, `h'(0) = −V tan γ`, `h(T) = 0`, `h'(T) = 0`.
With `T = 1.75 h_f / (V tan γ)` the derivative has a single root at touchdown, so
the height decreases monotonically for any speed and any glideslope — that is
checked by a test rather than by choosing lucky constants.

## The bank angle, and the defect it caused

A coordinated-turn bank angle is a function of curvature:

```
phi = atan(V psi_dot / g),   psi_dot = k V
```

Curvature is piecewise constant, so it **steps** at every segment boundary. For
position and heading that is harmless. For bank it is not: it means the aircraft
rolls from level to 20 degrees in zero time, which is an infinite roll rate.

No gyroscope can report an infinite rate. An estimator fed by such a truth
therefore has no way to know the aircraft rolled, and inherits a permanent 20
degree attitude error at the turn entry. On the turning profile this diverged
every architecture with an integrity layer to kilometres of error on a
**fault-free** run, while the architecture with no integrity layer was fine — the
signature that pointed at the truth rather than at the filters.

The bank is now derived from a curvature blended over a roll-in distance
(`roll_in_time_s`, default 3 s). The consequence is that the turn is not perfectly
coordinated during roll-in, so bank and lateral acceleration briefly disagree —
which is also what happens in a real aircraft. `HorizontalPath::curvatureSmoothed`
does the blending; `GroundTruth.BodyRatesStayBoundedThroughTheTurn` bounds the
result. See KF-003.

## Simplifications

Stated because a reader needs them to disagree with any number here (NFR-024).

| Simplification | What it costs |
| --- | --- |
| Zero angle of attack: pitch equals flight path angle | A real aircraft holds a few degrees of alpha on approach. The specific force is self-consistent with whatever attitude the truth reports, so this affects realism, not correctness. |
| No wind, therefore no crab | Yaw equals track heading exactly. |
| Flat earth, local NED | No Coriolis or transport rate. At a 10 km footprint over 10 minutes these are below the noise floor of every sensor modelled. |
| Open loop | The truth receives no feedback from any estimator. Continuity and availability therefore describe the estimator, not an aircraft that would have reacted. |

---

# Sensors

Every sensor here is **nominal**. Anything abnormal — loss, freeze, bias, spoof,
latency, drop — belongs to the fault engine. Keeping the split sharp is what makes
AT-002 checkable: the sensor layer reads the truth, the fault layer never does.

## Noise parameterisation

Inertial noise is given as a spectral density (m/s²/√Hz, rad/s/√Hz); the discrete
standard deviation at rate f is `density · √f`. Bias random walk is given in
units/√s and integrated as `b += sigma √dt · n`. GNSS, barometric and vision noise
are given directly as discrete standard deviations, because those sensors deliver
an already-filtered estimate rather than a raw sample.

| Sensor | Rate | Model |
| --- | --- | --- |
| GNSS | 5 Hz | 3D position and velocity, white noise, configurable bias, 80 ms latency |
| IMU | 100 Hz | Specific force and angular rate, white noise plus bias with random walk |
| Barometer | 20 Hz | Altitude with its own slowly drifting bias, independent of the GNSS vertical channel (SIM-011) |
| Vision | 20 Hz | Runway-frame lateral and along-track offsets plus relative heading, with an availability envelope and a quality that degrades with range |
| Pseudorange | 1 Hz | Per-satellite ranges against a frozen constellation geometry, for the residual test |

## Sample time and delivery time are different things

Every measurement carries both. The distinction is not bookkeeping: it is what
makes half the integrity monitoring possible.

A fault may rewrite `delivery_time` — that is what the latency fault does — but
**never** `sample_time`. So a frozen source keeps reporting a plausible value on a
fresh delivery, while its sample timestamp stays where it was, and age-based
detection catches it (INT-018). Without the split, a freeze is undetectable by
anything except its eventual disagreement with the rest of the solution, which is
exactly what a slow drift avoids.

## Independent random streams

Each sensor draws from its own PCG32 stream, derived from the run seed through a
SplitMix64 finaliser and a stable stream identifier. Two properties follow: the
streams are independent, so adding a sensor never shifts the sequence an existing
one consumes; and everything is determined by one 64-bit seed.

`std::mt19937` with the standard distributions was not used, because the C++
standard does not specify the sequence a distribution produces. libstdc++ and the
Emscripten library genuinely differ, which would have broken the native/wasm
comparison before the numerics got a chance to. Gaussian sampling uses the
Marsaglia polar method, which needs only `log` and `sqrt` from libm rather than
`sin` and `cos` as well — one fewer function whose last bit is unspecified.

## Vision quality is not availability

The vision sensor reports a quality between 0 and 1 alongside its measurement,
degrading with range before the source is lost. The estimator inflates `R` by
`1/quality²`; the integrity policy refuses anything below a floor.

Below that floor the source is marked **UNAVAILABLE**, not SUSPECT. That
distinction was a fix: on every approach the runway enters the sensor envelope at
essentially zero quality and improves as the range closes, and calling that
SUSPECT put a geometric certainty on the false alert budget and made the interface
claim a fault where there was none.
