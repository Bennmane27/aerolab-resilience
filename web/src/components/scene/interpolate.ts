// AEROLAB RESILIENCE - render-time interpolation for the 3D view.
//
// The simulation advances in fixed steps of dt (10 ms by default). The display
// refreshes on its own clock, and the two never line up: at x1 speed the
// aircraft covers 1.17 m in a 16.7 ms display frame, but it can only ever be
// drawn at a multiple of 0.7 m. Sixty percent jitter in the per-frame
// displacement is exactly the fragmented motion this fixes, and no amount of
// GPU makes it go away, because it is a sampling problem rather than a
// throughput one.
//
// So the scene keeps the two samples that bracket the instant being drawn and
// interpolates between them. The endpoints are exact, there is no
// extrapolation, and the drawn pose lags the newest sample by at most one dt -
// 0.7 m at approach speed.
//
// THAT LAG IS APPLIED TO EVERYTHING, on purpose. Truth and estimates are
// interpolated to the SAME render time, so the line drawn between them is the
// error at that instant and not a mixture of two. The numbers in the legend and
// the tables come from the sampled frame, never from the interpolated pose.
import * as THREE from "three";
import type { Frame } from "../../core/session";
import { attitudeFromEuler } from "./attitude";

/** NED -> world for a position: x = East, y = up, z = North. */
export function toWorld(n: number, e: number, d: number): THREE.Vector3 {
  return new THREE.Vector3(e, -d, n);
}

/** One simulation instant, reduced to what the scene actually draws. */
export interface PoseSample {
  t: number;
  truth: THREE.Vector3;
  attitude: THREE.Quaternion;
  /** World position per estimator id; absent when that solution had none. */
  solutions: Map<string, THREE.Vector3>;
}

export function samplePose(frame: Frame): PoseSample {
  const solutions = new Map<string, THREE.Vector3>();
  for (const [id, s] of Object.entries(frame.solutions)) {
    solutions.set(id, toWorld(s.n, s.e, s.d));
  }
  return {
    t: frame.t,
    truth: toWorld(frame.truth.n, frame.truth.e, frame.truth.d),
    attitude: new THREE.Quaternion().setFromRotationMatrix(
      attitudeFromEuler(frame.truth.yaw_deg, frame.truth.pitch_deg, frame.truth.roll_deg)
    ),
    solutions,
  };
}

/**
 * Keeps the bracketing pair and paces a render clock across it.
 *
 * The clock is advanced by wall time scaled by the transport speed, then
 * clamped into the bracket. If it drifts outside - a tab coming back from the
 * background, a speed change, a scrub, a restart - it snaps to the newest
 * sample rather than trying to catch up, because a visible jump once is better
 * than a slow slide that misreports where the aircraft was.
 */
export class PoseInterpolator {
  private a: PoseSample | null = null;
  private b: PoseSample | null = null;
  private clock = 0;
  private readonly out: PoseSample = {
    t: 0,
    truth: new THREE.Vector3(),
    attitude: new THREE.Quaternion(),
    solutions: new Map(),
  };

  reset() {
    this.a = null;
    this.b = null;
    this.clock = 0;
  }

  /** Feeds a frame. Ignores one that is not newer than the current bracket. */
  push(frame: Frame) {
    if (this.b && frame.t === this.b.t) return;
    const sample = samplePose(frame);
    // Going backwards means a scrub or a restart: start a fresh bracket.
    if (this.b && frame.t < this.b.t) {
      this.a = null;
      this.b = sample;
      this.clock = sample.t;
      return;
    }
    this.a = this.b;
    this.b = sample;
    if (!this.a) this.clock = sample.t;
  }

  /**
   * Advances the render clock and returns the pose to draw, or null before the
   * first sample. `dtWall` is seconds of real time since the previous call.
   */
  advance(dtWall: number, speed: number): PoseSample | null {
    const { a, b } = this;
    if (!b) return null;
    if (!a) return b;

    this.clock += dtWall * speed;
    const span = b.t - a.t;
    // Outside the bracket by more than one span: the feed jumped, so do not
    // pretend to interpolate.
    if (this.clock < a.t - span || this.clock > b.t + span) this.clock = b.t;
    const alpha = span > 1e-9 ? Math.min(1, Math.max(0, (this.clock - a.t) / span)) : 1;

    const out = this.out;
    out.t = a.t + alpha * span;
    out.truth.lerpVectors(a.truth, b.truth, alpha);
    out.attitude.slerpQuaternions(a.attitude, b.attitude, alpha);
    out.solutions.clear();
    for (const [id, pb] of b.solutions) {
      const pa = a.solutions.get(id);
      const v = new THREE.Vector3();
      if (pa) v.lerpVectors(pa, pb, alpha);
      else v.copy(pb);
      out.solutions.set(id, v);
    }
    return out;
  }
}
