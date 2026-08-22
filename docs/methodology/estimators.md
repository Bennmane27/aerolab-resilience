# Estimation: the filter, and why it is shaped this way

## Why an error-state filter and not a direct one

Section 6.1 of the cahier des charges writes the state vector with Euler angles:

```
x = [pN pE pD  vN vE vD  roll pitch yaw  bax bay baz  bgx bgy bgz]
```

The implementation uses a quaternion for the nominal attitude and a
three-component **error** vector for its uncertainty. The reason is not
preference.

1. **The Euler parameterisation is singular at pitch = ±90°.** The V1 profiles
   never go there, so this alone would not force the change.
2. **The covariance of a wrapped angle is not well defined.** A yaw of +179° and
   one of −179° are two degrees apart, and any filter that stores yaw as a scalar
   with a variance has to special-case that. Every such special case is a place
   for a subtle error to live.
3. **A quaternion has four components and three degrees of freedom.** Putting it
   directly in the state makes the covariance rank-deficient and the
   normalisation constraint has to be re-imposed by hand after every update.

The error-state (indirect, multiplicative) formulation removes all three at once.
The nominal state carries a unit quaternion and is propagated exactly; the filter
estimates a small rotation vector `dtheta` that has three components, no
singularity and no constraint. After each update the correction is injected
multiplicatively and the error is reset to zero.

This is also what the target audience expects: the roles cited in
`docs/industrial_evidence.md` name Kalman filtering and GNSS/IMU hybridisation
explicitly, and a direct Euler EKF would read as a first attempt.

## The formulation

Nominal state, propagated exactly:

```
p       position, NED, m
v       velocity, NED, m/s
q       attitude, body to NED, unit quaternion, Hamilton, scalar first
b_a     accelerometer bias, m/s^2
b_g     gyroscope bias, rad/s
```

Error state, 15 components, what the covariance actually describes:

```
dx = [dp  dv  dtheta  db_a  db_g]
```

with the **local** convention `q_true = q_nominal ⊗ dq(dtheta)`, `dtheta`
expressed in body axes.

Continuous error dynamics, with `a_b = f_b − b_a` and `w = w_b − b_g` the
bias-compensated inertial signals:

```
d(dp)/dt     = dv
d(dv)/dt     = −R [a_b]× dtheta − R db_a + n_a
d(dtheta)/dt = −[w]× dtheta − db_g + n_g
d(db_a)/dt   = n_ba
d(db_g)/dt   = n_bg
```

Discretisation is first order, `F = I + A dt`. At 100 Hz the second-order term is
about 5·10⁻⁵ of the first.

The covariance update is Joseph form:

```
P = (I − KH) P (I − KH)^T + K R K^T
```

rather than `(I − KH) P`. The two are algebraically identical and numerically are
not: Joseph preserves symmetry and positive definiteness far better under
accumulated rounding, which matters over 9000 updates per run. `symmetrize()` runs
after every propagation and every update, and `hasPositiveDiagonal()` is checked
as part of the health test (NAV-010, NAV-014).

## Measurement models

| Measurement | h(x) | Jacobian | Notes |
| --- | --- | --- | --- |
| GNSS position | `p` | `[I 0 0 0 0]` | dim 3 |
| GNSS velocity | `v` | `[0 I 0 0 0]` | dim 3, gated separately so SCN-005 gets its own reason code |
| Barometric altitude | `−p_D` | `[0 0 −1 | 0 …]` | dim 1; see the bias note below |
| Vision, runway-relative | lateral and along-track offsets in the runway frame, plus relative heading | position rows from the runway rotation; the heading row is the third row of `R` | dim 3; `R` is measurement noise inflated by `1/quality²` |

### The barometric bias

The V1 state vector does not estimate it. The truth applies a bias of
`1.5 + 0.01·t` metres, and the filter has no state for it, so the update is
biased.

The honest treatment — and what is implemented — is to absorb it as extra
measurement uncertainty: `R = sigma² + bias_uncertainty²` with
`bias_uncertainty_m = 3.0`. That is suboptimal and it is documented as such
(NFR-024). The alternative, a 16th state, was not added because the vertical
channel is not what any scenario tests and a state that no scenario exercises is
a state nobody will notice is wrong.

### The vision heading row

To first order, `d(yaw)/d(dtheta_body)` is the third row of the body-to-NED
rotation matrix. That is exact for the local error convention used here and
accurate to first order in the attitude error, which is what the filter assumes
everywhere else.

## Delayed measurements

`SCN-009` injects 1.4 s of extra delivery delay. Three policies were available:

1. **Reject anything older than a threshold.** Simple, and it makes the scenario
   vacuous: nothing is measured except the rejection.
2. **Apply the measurement at the current time with inflated R.** Cheap and
   approximate; the position it refers to is no longer where the aircraft is.
3. **Reprocess.** Restore the filter to the state it had at the measurement's
   sample instant, apply the update there, and replay everything since.

The filter implements (3). It keeps a snapshot per IMU sample over
`rollback_window_s` (2.5 s by default), plus a log of the inertial samples and of
the updates it accepted. On a delayed measurement it restores the snapshot that
brackets the sample time, merges the buffered inertial samples and accepted
updates with the new measurement into one timestamp-ordered sequence, and replays
it forward. That is exact reprocessing, not an approximation.

The innovation the integrity policy is shown is built at the snapshot nearest the
sample instant — at most half an IMU period away — rather than at the current
state, so the gate judges the measurement against the state it actually refers
to. Anything older than the buffer is refused with `kMeasurementStale`.

`Ekf.DelayedMeasurementIsReprocessedByRollback` checks the arithmetic: a fix
sampled 1 s ago saying "you were at 60 m", on an aircraft that was at 50 m then
and is at 100 m now, must move the current estimate towards 110 m — not towards
60 m.

## Initialisation

Every channel is handed the same seed solution, produced once before the run by
the runner: the truth at t = 0 perturbed by the configured alignment sigmas
(8 m position, 0.5 m/s velocity, 1° attitude), with the bias estimates at zero.

This is the single controlled exception to NAV-006, and it is the standard way a
navigation benchmark is initialised — a real system aligns on the ground and
starts from a known point with a known uncertainty. Using the same seed solution
for every architecture is what keeps the comparison fair.

The bias estimates start at zero while the true biases are not, which is why the
dead-reckoning baseline drifts. That is the point of having it.

## Why the baselines exist

**NAV-A, GNSS only.** Position is the last valid fix. It exists to show what
happens when one absolute source is trusted without question: a 100 m spoof
becomes a 100 m error, immediately, with nothing to indicate anything is wrong.
It also produces the most instructive nominal number in the project — 12.4 m of
error from a 2 m sensor, because at 5 Hz with 80 ms of latency the fix is 0.18 s
old and the aircraft has moved 12.6 m.

**NAV-B, inertial dead reckoning.** No absolute update, ever. It bounds how long
a solution can coast, and its failure mode is instructive in its own right: over
90 s it reaches 554 m, dominated not by the accelerometer bias but by the 1°
attitude alignment error, which tilts gravity by 0.17 m/s² and integrates
quadratically.

**NAV-C, EKF with no integrity layer.** The control case. It is not a
"monitor-only" configuration — monitor-only exists as a mode of the integrity
manager and is unit tested, but giving NAV-C that mode would emit SUSPECT
transitions and make a baseline look as though it had detected something. NAV-C
has no integrity architecture at all, which is what section 6.4 specifies and
what makes the comparison against NAV-D measure the presence of a policy rather
than the willingness to act on one.
