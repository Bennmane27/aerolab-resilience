// AEROLAB RESILIENCE - the drawn aircraft points where the truth says.
//
// Runs without a browser: it exercises the same pure function the 3D view uses.
//
// The determinant check is the important one. Both times this has been wrong,
// the symptom was a matrix that three.js accepted silently — a reflection whose
// quaternion came out non-unit, leaving the aircraft nearly unrotated while the
// truth turned through ninety degrees. Nothing threw, nothing logged, and the
// only evidence was that the model looked odd.
import { expect, test } from "@playwright/test";
import * as THREE from "three";
import { attitudeFromEuler, bodyAxesNED } from "../src/components/scene/attitude";

/** World frame of the 3D view: x = East, y = Up, z = North. */
function axes(yaw: number, pitch: number, roll: number) {
  const m = attitudeFromEuler(yaw, pitch, roll).clone();
  const e = m.elements;
  return {
    matrix: m,
    nose: new THREE.Vector3(e[0], e[1], e[2]),
    up: new THREE.Vector3(e[4], e[5], e[6]),
    port: new THREE.Vector3(e[8], e[9], e[10]),
  };
}

/** Compass heading of a world direction, degrees from north through east. */
function headingOf(v: THREE.Vector3): number {
  return ((Math.atan2(v.x, v.z) * 180) / Math.PI + 360) % 360;
}

test("the attitude basis is a rotation, never a reflection", () => {
  for (const yaw of [0, 45, 140, 230, 359]) {
    for (const pitch of [-12, -3, 0, 8]) {
      for (const roll of [-25, 0, 19.6, 30]) {
        const { matrix } = axes(yaw, pitch, roll);
        // A determinant of -1 is the reflection bug; anything other than +1 is
        // not an orientation at all.
        expect(matrix.determinant(), `det at ${yaw}/${pitch}/${roll}`).toBeCloseTo(1, 6);
        // And it has to survive the conversion three.js actually performs.
        const q = new THREE.Quaternion().setFromRotationMatrix(matrix);
        expect(q.length(), `unit quaternion at ${yaw}/${pitch}/${roll}`).toBeCloseTo(1, 6);
      }
    }
  }
});

test("the nose follows the commanded heading, not its mirror", () => {
  // 140 degrees is the runway heading every scenario uses. The mirrored version
  // of this bug drew it at 40.
  for (const yaw of [0, 40, 90, 140, 200, 315]) {
    const { nose } = axes(yaw, 0, 0);
    expect(headingOf(nose), `heading at yaw ${yaw}`).toBeCloseTo(yaw, 4);
  }
});

test("pitch raises the nose and roll drops the correct wing", () => {
  // Positive pitch is nose up.
  expect(axes(140, 8, 0).nose.y).toBeGreaterThan(0);
  expect(axes(140, -3, 0).nose.y).toBeLessThan(0);
  expect((Math.asin(axes(140, -3, 0).nose.y) * 180) / Math.PI).toBeCloseTo(-3, 4);

  // Positive roll is right wing down, so the PORT wing goes up.
  const banked = axes(0, 0, 20);
  expect(banked.port.y).toBeGreaterThan(0);
  expect((Math.asin(banked.port.y) * 180) / Math.PI).toBeCloseTo(20, 4);

  // Heading north, banked right: lift tilts east.
  expect(banked.up.x).toBeGreaterThan(0);
});

test("the body axes stay orthonormal", () => {
  const { nose, up, port } = axes(217, -6, 14);
  expect(nose.length()).toBeCloseTo(1, 9);
  expect(up.length()).toBeCloseTo(1, 9);
  expect(port.length()).toBeCloseTo(1, 9);
  expect(nose.dot(up)).toBeCloseTo(0, 9);
  expect(nose.dot(port)).toBeCloseTo(0, 9);
  expect(up.dot(port)).toBeCloseTo(0, 9);
  // Right handed: nose x up = port.
  expect(new THREE.Vector3().crossVectors(nose, up).distanceTo(port)).toBeCloseTo(0, 9);
});

test("the NED body axes match the 3-2-1 definition", () => {
  // Level flight due east: forward is east, right is south, down is down.
  const { forward, right, down } = bodyAxesNED(90, 0, 0);
  expect(forward.n).toBeCloseTo(0, 9);
  expect(forward.e).toBeCloseTo(1, 9);
  expect(right.n).toBeCloseTo(-1, 9);
  expect(down.d).toBeCloseTo(1, 9);
});
