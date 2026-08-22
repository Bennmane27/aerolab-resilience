// AEROLAB RESILIENCE - aircraft attitude for the 3D view.
//
// Separated from Scene3D so it can be checked numerically without a browser.
// It has earned that: the orientation of the drawn aircraft has been wrong
// twice, both times because an axis convention was reasoned about rather than
// written down and verified.
//
// THE TWO TRAPS, both of which this file walks into deliberately so the
// comments can point at them:
//
//  1. `rotation.y = -yaw + PI/2` gives a nose vector of (sin psi, 0, -cos psi).
//     That is the heading MIRRORED about north. On a runway at 140 degrees the
//     aircraft was drawn tracking 40 - a hundred degrees across its own path.
//     The correct rotation is `yaw - PI/2`, but see trap 2 before trusting it.
//
//  2. The NED -> world map used by this view (x = East, y = Up, z = North) is
//     ORIENTATION-REVERSING. Physical East-North-Up is right handed, so
//     E x N = U; this frame labels the axes E, U, N, and E x U = -N. Feeding
//     the three body axes through that map and calling makeBasis(nose, up,
//     starboard) therefore produces a REFLECTION, not a rotation. Three.js does
//     not complain: Quaternion.setFromRotationMatrix on a determinant -1 matrix
//     returns a non-unit quaternion - measured at 0.707 - and the aircraft sits
//     there barely rotating at all while the truth turns through ninety degrees.
//
//     The fix is not a sign on the roll. In a right handed local frame with +x
//     through the nose and +y up, the third axis is the PORT wing, because
//     forward x up = left. The basis below is built that way and its
//     determinant is +1.
import * as THREE from "three";

/** Body axes in NED, from the standard yaw-pitch-roll (3-2-1) sequence. */
export interface BodyAxesNED {
  forward: { n: number; e: number; d: number };
  right: { n: number; e: number; d: number };
  down: { n: number; e: number; d: number };
}

export function bodyAxesNED(yawDeg: number, pitchDeg: number, rollDeg: number): BodyAxesNED {
  const psi = (yawDeg * Math.PI) / 180;
  const theta = (pitchDeg * Math.PI) / 180;
  const phi = (rollDeg * Math.PI) / 180;
  const cy = Math.cos(psi);
  const sy = Math.sin(psi);
  const cp = Math.cos(theta);
  const sp = Math.sin(theta);
  const cr = Math.cos(phi);
  const sr = Math.sin(phi);
  return {
    forward: { n: cy * cp, e: sy * cp, d: -sp },
    right: { n: cy * sp * sr - sy * cr, e: sy * sp * sr + cy * cr, d: cp * sr },
    down: { n: cy * sp * cr + sy * sr, e: sy * sp * cr - cy * sr, d: cp * cr },
  };
}

const noseAxis = new THREE.Vector3();
const upAxis = new THREE.Vector3();
const portAxis = new THREE.Vector3();
const basis = new THREE.Matrix4();

/**
 * Orientation for a model authored with +x through the nose, +y up and +z out
 * the port wing, in the view's world frame (x = East, y = Up, z = North).
 *
 * Returns a shared matrix: copy it if you need to keep it.
 */
export function attitudeFromEuler(
  yawDeg: number,
  pitchDeg: number,
  rollDeg: number
): THREE.Matrix4 {
  const { forward, right, down } = bodyAxesNED(yawDeg, pitchDeg, rollDeg);
  // NED -> world for a direction: x = East, y = -Down, z = North.
  noseAxis.set(forward.e, -forward.d, forward.n);
  upAxis.set(-down.e, down.d, -down.n);
  // Port, not starboard: see trap 2 above.
  portAxis.set(-right.e, right.d, -right.n);
  return basis.makeBasis(noseAxis, upAxis, portAxis);
}
