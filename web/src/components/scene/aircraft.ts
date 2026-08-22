// AEROLAB RESILIENCE - the truth aircraft.
//
// Generated at runtime. Nothing is downloaded: SYS-009 requires the page to keep
// working with no network, and the published site makes no external request, so
// a glTF from a CDN is not an option and a vendored one would mean carrying a
// third-party asset and its attribution through the repository.
//
// SHAPE, not primitives. An earlier version was a capsule with slabs and
// cylinders stuck to it, and it read exactly like that: the giveaway is never
// the material, it is the silhouette and the junctions. The surfaces here are
// lofted through aerofoil sections and revolved from profiles, and every place
// two of them meet is covered by a fairing - which is what a real airframe does,
// for the same reason.
//
// ORIGIN. The model's origin is the WHEEL CONTACT POINT, not the fuselage
// centreline. The simulation reports altitude zero while taxiing, so its
// reference point is where the tyres meet the pavement; with the origin on the
// fuselage axis and no landing gear, the aircraft sat almost four metres into
// the runway. That was a modelling artefact and nothing to do with navigation
// error - unlike an ESTIMATE below the ground, which is the divergence signal
// this view exists to show.
import * as THREE from "three";
import {
  bodyOfRevolution,
  loftSurface,
  makeFuselageTexture,
  revolve,
  type Station,
} from "./loft";

/** Height of the wheel contact point below the fuselage axis, in metres. */
export const GEAR_HEIGHT_M = 4.6;

const NOSE = new THREE.Vector3(1, 0, 0);
const UP = new THREE.Vector3(0, 1, 0);
const STARBOARD = new THREE.Vector3(0, 0, 1);

/**
 * Fuselage: ONE textured body of revolution.
 *
 * Windows, cockpit glazing, cheatline, belly and radome are all painted into
 * the livery rather than modelled. They used to be a box, a squashed sphere and
 * a separate cone intersecting the barrel, and three shapes poking through a
 * fourth is most of what made the aircraft look like assembled primitives.
 */
function buildFuselage(material: THREE.Material): THREE.Mesh {
  // (station along the body, radius). Tail at -23 m, nose at +19 m.
  const profile: Array<[number, number]> = [
    [-23.0, 0.02],
    [-21.6, 0.5],
    [-19.8, 0.95],
    [-17.4, 1.42],
    [-15.2, 1.7],
    [-12.8, 1.86],
    [-10.0, 1.9],
    [-4.0, 1.9],
    [4.0, 1.9],
    [10.4, 1.9],
    [12.6, 1.85],
    [14.6, 1.7],
    [16.2, 1.44],
    [17.6, 1.06],
    [18.6, 0.58],
    [19.1, 0.02],
  ];
  const geometry = bodyOfRevolution(profile, 48);

  // Tail upsweep, on a quadratic. A body of revolution cannot produce it, and
  // without it the rear is a plain cone.
  const position = geometry.attributes.position as THREE.BufferAttribute;
  for (let i = 0; i < position.count; ++i) {
    const x = position.getX(i);
    if (x < -10) {
      const t = (-10 - x) / 13;
      position.setY(i, position.getY(i) + 3.6 * t * t);
    }
  }
  position.needsUpdate = true;
  geometry.computeVertexNormals();
  return new THREE.Mesh(geometry, material);
}

/** Wing, tailplane or fin, lofted through its stations. */
function buildSurface(
  stations: Station[],
  chord: THREE.Vector3,
  span: THREE.Vector3,
  up: THREE.Vector3,
  origin: THREE.Vector3,
  material: THREE.Material
): THREE.Mesh {
  return new THREE.Mesh(loftSurface(stations, chord, span, up, origin), material);
}

/**
 * Engine cowl: revolved, with the profile folding back on itself at the front
 * so the inlet lip is a real rounded lip rather than a torus laid on the end of
 * a tube.
 */
function buildNacelle(material: THREE.Material, dark: THREE.Material): THREE.Group {
  const g = new THREE.Group();
  // Aft to forward, ending in the lip folding back into the inlet.
  const cowl: Array<[number, number]> = [
    [-2.7, 0.92],
    [-2.4, 1.04],
    [-1.6, 1.2],
    [-0.4, 1.34],
    [1.2, 1.4],
    [2.2, 1.38],
    [2.68, 1.33],
    [2.78, 1.26],
    [2.72, 1.14],
    [2.55, 0.98],
  ];
  g.add(new THREE.Mesh(revolve(cowl, 24), material));

  // The dark inlet, seen through the lip.
  const inlet = new THREE.Mesh(
    new THREE.CylinderGeometry(0.98, 0.86, 1.5, 22, 1, true),
    dark
  );
  inlet.rotation.z = Math.PI / 2;
  inlet.position.x = 1.9;
  g.add(inlet);

  // Exhaust plug.
  const plug: Array<[number, number]> = [
    [-4.7, 0.0],
    [-4.2, 0.42],
    [-3.4, 0.74],
    [-2.7, 0.9],
  ];
  g.add(new THREE.Mesh(revolve(plug, 20), dark));
  return g;
}

/** One leg: oleo strut, axles and wheels, measured UP from the contact patch. */
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
  const oleo = new THREE.Mesh(new THREE.CylinderGeometry(0.2, 0.28, strutLength, 12), strut);
  oleo.position.set(x, wheelRadius + strutLength / 2, z);
  leg.add(oleo);

  const halfTrack = 0.44 + wheelRadius * 0.34;
  for (let i = 0; i < axles; ++i) {
    const ax = x + (i - (axles - 1) / 2) * (wheelRadius * 2.1);
    const axle = new THREE.Mesh(
      new THREE.CylinderGeometry(0.11, 0.11, halfTrack * 2 + 0.3, 8),
      strut
    );
    axle.rotation.x = Math.PI / 2;
    axle.position.set(ax, wheelRadius, z);
    leg.add(axle);
    for (const side of [-1, 1]) {
      const wheel = new THREE.Mesh(
        new THREE.CylinderGeometry(wheelRadius, wheelRadius, 0.36, 20),
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
 * metres of error, so the one object of known size has to be the size it claims.
 */
export function buildAirliner(): THREE.Group {
  const root = new THREE.Group();
  const body = new THREE.Group();
  body.position.y = GEAR_HEIGHT_M;
  root.add(body);

  const painted = new THREE.MeshStandardMaterial({
    map: makeFuselageTexture(),
    roughness: 0.34,
    metalness: 0.3,
  });
  const skin = new THREE.MeshStandardMaterial({
    color: 0xeef2f7,
    roughness: 0.34,
    metalness: 0.38,
    side: THREE.DoubleSide,
  });
  const cowl = new THREE.MeshStandardMaterial({ color: 0xdfe5ec, roughness: 0.26, metalness: 0.6 });
  const strut = new THREE.MeshStandardMaterial({ color: 0x8d9aa9, roughness: 0.32, metalness: 0.7 });
  const dark = new THREE.MeshStandardMaterial({ color: 0x1b222c, roughness: 0.7 });
  const rubber = new THREE.MeshStandardMaterial({ color: 0x14181e, roughness: 0.95 });

  body.add(buildFuselage(painted));

  // ---- wing ---------------------------------------------------------------
  // Six stations: aft sweep about 24 degrees, taper 3.1 : 1, five degrees of
  // dihedral, a little washout at the tip, and a thickness ratio that thins
  // outboard the way a transport wing does.
  const wingStations: Station[] = [
    { span: 0.0, leadingEdge: 5.0, chord: 7.6, rise: -1.05, ratio: 0.145 },
    { span: 2.4, leadingEdge: 4.2, chord: 6.9, rise: -0.85, ratio: 0.135 },
    { span: 6.6, leadingEdge: 2.4, chord: 5.5, rise: -0.48, ratio: 0.122 },
    { span: 11.0, leadingEdge: 0.4, chord: 4.2, rise: -0.1, ratio: 0.112, twist: -0.012 },
    { span: 15.2, leadingEdge: -1.4, chord: 3.0, rise: 0.27, ratio: 0.104, twist: -0.026 },
    { span: 17.0, leadingEdge: -2.2, chord: 2.45, rise: 0.43, ratio: 0.1, twist: -0.034 },
  ];
  // Winglet: the wing carried on, turned up. Continuous with the tip, which is
  // what makes it read as part of the wing rather than a fin taped to it.
  const wingletStations: Station[] = [
    { span: 0.0, leadingEdge: -2.2, chord: 2.45, rise: 0.0, ratio: 0.1 },
    { span: 1.1, leadingEdge: -2.75, chord: 1.9, rise: 0.28, ratio: 0.095 },
    { span: 2.6, leadingEdge: -3.5, chord: 1.15, rise: 0.7, ratio: 0.09 },
  ];

  for (const side of [1, -1]) {
    const span = new THREE.Vector3(0, 0, side);
    body.add(
      buildSurface(wingStations, NOSE, span, UP, new THREE.Vector3(0, 0, 0), skin)
    );
    // The winglet grows out of the tip: span up, chord aft, thickness across.
    body.add(
      buildSurface(
        wingletStations,
        NOSE,
        UP,
        new THREE.Vector3(0, 0, side),
        new THREE.Vector3(0, 0.43, side * 17.0),
        skin
      )
    );

    // ---- engine -----------------------------------------------------------
    const engine = buildNacelle(cowl, dark);
    engine.position.set(0.9, -2.55, side * 6.6);
    body.add(engine);
    // Pylon, lofted so it blends into both the cowl and the wing underside.
    body.add(
      buildSurface(
        [
          { span: 0.0, leadingEdge: 2.0, chord: 4.0, rise: 0, ratio: 0.13 },
          { span: 1.4, leadingEdge: 1.5, chord: 4.2, rise: 0, ratio: 0.12 },
          { span: 2.4, leadingEdge: 1.1, chord: 4.0, rise: 0, ratio: 0.11 },
        ],
        NOSE,
        UP,
        new THREE.Vector3(0, 0, side),
        new THREE.Vector3(0, -2.5, side * 6.6),
        skin
      )
    );

    // ---- tailplane --------------------------------------------------------
    body.add(
      buildSurface(
        [
          { span: 0.0, leadingEdge: -14.6, chord: 3.8, rise: 1.9, ratio: 0.11 },
          { span: 3.2, leadingEdge: -16.0, chord: 2.7, rise: 2.1, ratio: 0.1 },
          { span: 6.3, leadingEdge: -17.4, chord: 1.5, rise: 2.35, ratio: 0.095 },
        ],
        NOSE,
        new THREE.Vector3(0, 0, side),
        UP,
        new THREE.Vector3(0, 0, 0),
        skin
      )
    );
  }

  // ---- fin ----------------------------------------------------------------
  body.add(
    buildSurface(
      [
        { span: 0.0, leadingEdge: -12.2, chord: 7.0, rise: 0, ratio: 0.13 },
        { span: 3.0, leadingEdge: -14.4, chord: 5.4, rise: 0, ratio: 0.12 },
        { span: 5.6, leadingEdge: -16.3, chord: 4.0, rise: 0, ratio: 0.11 },
        { span: 7.6, leadingEdge: -17.8, chord: 2.9, rise: 0, ratio: 0.1 },
      ],
      NOSE,
      UP,
      STARBOARD,
      new THREE.Vector3(0, 1.7, 0),
      skin
    )
  );

  // ---- fairings -----------------------------------------------------------
  // These are the difference between an aircraft and a pile of parts. Every one
  // of them covers a junction that would otherwise be a hard intersection.

  // Wing-body fairing: the long belly blister over the wing box.
  const fairing = new THREE.Mesh(
    revolve(
      [
        [-8.2, 0.0],
        [-6.8, 1.1],
        [-4.2, 1.82],
        [-1.4, 2.05],
        [1.8, 1.9],
        [4.6, 1.25],
        [6.2, 0.0],
      ],
      24
    ),
    new THREE.MeshStandardMaterial({ color: 0xc9d2dc, roughness: 0.4, metalness: 0.28 })
  );
  // Hugging the belly, not hanging off it: this covers the wing box junction,
  // it is not a pod.
  fairing.scale.set(1, 0.52, 1.3);
  fairing.position.set(-1.4, -1.1, 0);
  body.add(fairing);

  // Dorsal fillet: the fin leading edge running forward into the fuselage.
  const dorsal = buildSurface(
    [
      { span: 0.0, leadingEdge: -8.6, chord: 4.6, rise: 0, ratio: 0.34 },
      { span: 1.2, leadingEdge: -11.0, chord: 2.6, rise: 0, ratio: 0.24 },
      { span: 1.9, leadingEdge: -12.1, chord: 1.6, rise: 0, ratio: 0.18 },
    ],
    NOSE,
    UP,
    STARBOARD,
    new THREE.Vector3(0, 0.9, 0),
    skin
  );
  body.add(dorsal);

  // ---- landing gear -------------------------------------------------------
  root.add(buildGear(11.2, 0, 0.46, 1, strut, rubber));
  root.add(buildGear(-1.8, 3.8, 0.6, 2, strut, rubber));
  root.add(buildGear(-1.8, -3.8, 0.6, 2, strut, rubber));

  return root;
}
