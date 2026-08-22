// AEROLAB RESILIENCE - the truth aircraft.
//
// Built from three.js geometry primitives and the official addons that ship
// with the package. Nothing is downloaded: SYS-009 requires the page to keep
// working with no network, and the published site makes no external request, so
// a glTF fetched from a CDN is not an option and a vendored one would mean
// carrying a third-party asset and its attribution through the repository.
// `three/examples/jsm` is MIT, already installed, and is where the quality
// actually comes from - LatheGeometry for a real surface of revolution, an
// environment map for the specular response, thick lines, DOM labels.
//
// ORIGIN. The model's origin is the WHEEL CONTACT POINT, not the fuselage
// centreline. The simulation reports altitude zero while taxiing, so its
// reference point is where the tyres meet the pavement; with the origin on the
// fuselage axis and no landing gear, the aircraft sat almost four metres into
// the runway during the taxi phase. That was a modelling artefact and nothing
// to do with navigation error - unlike an ESTIMATE below the ground, which is
// the divergence signal this view exists to show.
import * as THREE from "three";

/** Height of the wheel contact point below the fuselage axis, in metres. */
export const GEAR_HEIGHT_M = 4.6;

/**
 * Fuselage as a surface of revolution: a real profile with a tapered nose and a
 * swept-up tail, rather than a capsule with a cone stuck on the back.
 */
function buildFuselage(material: THREE.Material): THREE.Mesh {
  // (radius, station) along the body axis, tail at -23 m, nose at +19 m.
  const profile: Array<[number, number]> = [
    [0.0, -23.0],
    [0.55, -21.5],
    [1.05, -19.5],
    [1.5, -17.0],
    [1.78, -15.0],
    [1.9, -12.5],
    [1.9, 10.5],
    [1.86, 12.5],
    [1.72, 14.5],
    [1.45, 16.3],
    [1.0, 17.9],
    [0.45, 18.8],
    [0.0, 19.1],
  ];
  const geometry = new THREE.LatheGeometry(
    profile.map(([r, y]) => new THREE.Vector2(Math.max(r, 0.001), y)),
    28
  );
  // Lathe spins about +y; the model's body axis is +x.
  geometry.rotateZ(-Math.PI / 2);

  // Tail upsweep. A lathe cannot produce it, so shear the aft vertices upward
  // on a quadratic, which is close to how a real tail cone is drawn.
  const position = geometry.attributes.position as THREE.BufferAttribute;
  for (let i = 0; i < position.count; ++i) {
    const x = position.getX(i);
    if (x < -11) {
      const t = (-11 - x) / 12;
      position.setY(i, position.getY(i) + 3.4 * t * t);
    }
  }
  position.needsUpdate = true;
  geometry.computeVertexNormals();
  return new THREE.Mesh(geometry, material);
}

/**
 * Swept planform with dihedral, extruded to a thin slab.
 *
 * Local axes: +x forward, +y spanwise before the rotation below turns it into
 * +z. `sweep` is NEGATIVE for the aft sweep every transport wing has.
 */
function sweptSurface(
  rootChord: number,
  tipChord: number,
  span: number,
  sweep: number,
  thickness: number,
  material: THREE.Material
): THREE.Mesh {
  const shape = new THREE.Shape();
  shape.moveTo(0, 0);
  shape.lineTo(rootChord, 0);
  shape.lineTo(sweep + tipChord, span);
  shape.lineTo(sweep, span);
  shape.closePath();
  const geometry = new THREE.ExtrudeGeometry(shape, {
    depth: thickness,
    bevelEnabled: true,
    bevelThickness: thickness * 0.45,
    bevelSize: thickness * 0.35,
    bevelSegments: 2,
  });
  geometry.rotateX(Math.PI / 2);
  geometry.computeVertexNormals();
  return new THREE.Mesh(geometry, material);
}

/**
 * One leg: oleo strut, axles and wheels.
 *
 * Everything is measured UP from y = 0, because y = 0 is the contact patch.
 * Placing the strut below the origin instead — which is what an earlier version
 * did — left the wheels dangling four metres under an aircraft they were no
 * longer attached to.
 */
function buildGear(
  x: number,
  z: number,
  wheelRadius: number,
  axles: number,
  strut: THREE.Material,
  tyre: THREE.Material
): THREE.Group {
  const leg = new THREE.Group();
  const strutLength = GEAR_HEIGHT_M - wheelRadius;
  const oleo = new THREE.Mesh(new THREE.CylinderGeometry(0.22, 0.3, strutLength, 10), strut);
  oleo.position.set(x, wheelRadius + strutLength / 2, z);
  leg.add(oleo);

  const halfTrack = 0.46 + wheelRadius * 0.35;
  for (let i = 0; i < axles; ++i) {
    const ax = x + (i - (axles - 1) / 2) * (wheelRadius * 2.1);
    const axle = new THREE.Mesh(
      new THREE.CylinderGeometry(0.12, 0.12, halfTrack * 2 + 0.3, 8),
      strut
    );
    axle.rotation.x = Math.PI / 2;
    axle.position.set(ax, wheelRadius, z);
    leg.add(axle);
    for (const side of [-1, 1]) {
      const wheel = new THREE.Mesh(
        new THREE.CylinderGeometry(wheelRadius, wheelRadius, 0.36, 18),
        tyre
      );
      wheel.rotation.x = Math.PI / 2;
      wheel.position.set(ax, wheelRadius, z + side * halfTrack);
      leg.add(wheel);
    }
  }
  return leg;
}

/**
 * A twin-engine airliner about 42 m long over a 35 m span - an A320 or a 737,
 * at real scale. Deliberately NOT exaggerated: the whole view is about judging
 * metres of error, so the one object of known size has to be the size it
 * claims to be.
 */
export function buildAirliner(bodyColor: number): THREE.Group {
  const root = new THREE.Group();
  // Everything is authored about the fuselage axis, then lifted so the origin
  // of the group is the wheel contact point. See the note at the top.
  const body = new THREE.Group();
  body.position.y = GEAR_HEIGHT_M;
  root.add(body);

  // DoubleSide because the port wing, tailplane and nacelle are the starboard
  // ones with `scale.z = -1`, which reverses their triangle winding.
  const skin = new THREE.MeshStandardMaterial({
    color: bodyColor,
    roughness: 0.34,
    metalness: 0.55,
    side: THREE.DoubleSide,
  });
  const accent = new THREE.MeshStandardMaterial({
    color: 0x93a6bb,
    roughness: 0.3,
    metalness: 0.7,
  });
  const dark = new THREE.MeshStandardMaterial({ color: 0x222a35, roughness: 0.75 });
  const rubber = new THREE.MeshStandardMaterial({ color: 0x14181e, roughness: 0.95 });

  body.add(buildFuselage(skin));

  // Cockpit glazing and the cabin window strip.
  const glass = new THREE.Mesh(new THREE.BoxGeometry(2.6, 0.7, 2.4), dark);
  glass.position.set(15.2, 0.95, 0);
  body.add(glass);
  for (const side of [1, -1]) {
    const windows = new THREE.Mesh(new THREE.BoxGeometry(23, 0.5, 0.08), dark);
    windows.position.set(0.5, 0.75, side * 1.85);
    body.add(windows);
  }

  for (const side of [1, -1]) {
    // Wing: 17 m half-span, swept aft about 23 degrees, 5 degrees of dihedral.
    const wing = sweptSurface(7.5, 2.4, 17, -7.2, 0.62, skin);
    wing.position.set(-2.5, -1.0, 0);
    wing.scale.z = side;
    wing.rotation.x = side * THREE.MathUtils.degToRad(-5);
    body.add(wing);

    // Winglet, which is most of what makes a modern airliner recognisable.
    const winglet = sweptSurface(2.4, 1.0, 2.6, -1.3, 0.3, skin);
    winglet.rotation.x = -Math.PI / 2 + side * 0.25;
    winglet.position.set(-9.7, 0.5, side * 16.8);
    body.add(winglet);

    // Engine: nacelle, dark intake lip, exhaust cone and pylon.
    const nacelle = new THREE.Mesh(new THREE.CylinderGeometry(1.3, 1.1, 5.2, 18), accent);
    nacelle.rotation.z = Math.PI / 2;
    nacelle.position.set(0.6, -2.6, side * 6.6);
    body.add(nacelle);
    const lip = new THREE.Mesh(new THREE.TorusGeometry(1.3, 0.16, 8, 20), dark);
    lip.rotation.y = Math.PI / 2;
    lip.position.set(3.2, -2.6, side * 6.6);
    body.add(lip);
    const exhaust = new THREE.Mesh(new THREE.ConeGeometry(0.75, 2.2, 14), dark);
    exhaust.rotation.z = -Math.PI / 2;
    exhaust.position.set(-3.1, -2.6, side * 6.6);
    body.add(exhaust);
    const pylon = new THREE.Mesh(new THREE.BoxGeometry(3.6, 2.0, 0.45), skin);
    pylon.position.set(0.4, -1.8, side * 6.6);
    body.add(pylon);

    // Horizontal stabiliser, up on the swept tail cone.
    const tailplane = sweptSurface(3.4, 1.3, 6.2, -2.6, 0.4, skin);
    tailplane.position.set(-17.5, 1.9, 0);
    tailplane.scale.z = side;
    body.add(tailplane);
  }

  // Vertical fin. Rotating about -x turns the spanwise axis from +z into +y.
  const fin = sweptSurface(6.4, 2.4, 7.6, -4.6, 0.45, skin);
  fin.rotation.x = -Math.PI / 2;
  fin.position.set(-16.5, 2.2, 0);
  body.add(fin);

  // Landing gear, on the root group so the tyres reach exactly to y = 0.
  // Nose leg well forward, main legs on a 7.6 m track just aft of the wing box,
  // which is the geometry of the aircraft this model is sized for.
  root.add(buildGear(11.2, 0, 0.46, 1, accent, rubber));
  root.add(buildGear(-1.8, 3.8, 0.6, 2, accent, rubber));
  root.add(buildGear(-1.8, -3.8, 0.6, 2, accent, rubber));

  return root;
}
