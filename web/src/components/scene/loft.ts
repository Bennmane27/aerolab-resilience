// AEROLAB RESILIENCE - lofted surfaces for the aircraft.
//
// The previous model was primitives glued together, and it looked like it: a
// wing was a flat slab with a bevel, an engine was a cylinder with a torus on
// the front, and every junction was a hard intersection where two shapes
// happened to overlap. No amount of material tuning hides that, because the
// silhouette is what gives it away.
//
// This builds the surfaces the way they are actually shaped. A lifting surface
// is lofted through aerofoil sections, so it has a rounded leading edge, a
// sharp trailing edge, taper, sweep, dihedral and a real thickness
// distribution. A nacelle is a surface of revolution with an inlet lip that
// curves back on itself. The junctions are then covered by fairings, which is
// what a real airframe does for the same reason.
import * as THREE from "three";

/**
 * NACA four-digit symmetric half-thickness, as a fraction of chord.
 *
 * `u` runs 0 at the leading edge to 1 at the trailing edge. The last
 * coefficient is the closed-trailing-edge variant (-0.1036 rather than
 * -0.1015), because an open trailing edge shows as a visible slit.
 */
function halfThickness(u: number, ratio: number): number {
  const s = Math.sqrt(Math.max(u, 0));
  return (
    5 *
    ratio *
    (0.2969 * s - 0.126 * u - 0.3516 * u * u + 0.2843 * u ** 3 - 0.1036 * u ** 4)
  );
}

/** One spanwise station of a lifting surface. */
export interface Station {
  /** Distance along the span from the root. */
  span: number;
  /** Leading-edge position along the chord axis. */
  leadingEdge: number;
  chord: number;
  /** Offset along the "up" axis at this station: dihedral. */
  rise: number;
  /** Nose-down twist in radians. Small, and it catches the light. */
  twist?: number;
  /** Thickness as a fraction of chord; thins towards the tip on a real wing. */
  ratio?: number;
}

const CHORDWISE = 22;

/**
 * Lofts a closed surface through the stations.
 *
 * Axes are given as unit vectors so the same function builds a wing (span +z,
 * chord +x, up +y), a fin (span +y, chord +x, up +z) and a tailplane without
 * any rotation afterwards - rotating a lofted surface into place is how
 * dihedral and twist end up wrong.
 */
export function loftSurface(
  stations: Station[],
  chordAxis: THREE.Vector3,
  spanAxis: THREE.Vector3,
  upAxis: THREE.Vector3,
  origin: THREE.Vector3
): THREE.BufferGeometry {
  // Cosine spacing: points bunch at the leading edge where the curvature is.
  const us: number[] = [];
  for (let i = 0; i <= CHORDWISE; ++i) us.push(0.5 * (1 - Math.cos((Math.PI * i) / CHORDWISE)));

  const ring = (st: Station): THREE.Vector3[] => {
    const ratio = st.ratio ?? 0.12;
    const twist = st.twist ?? 0;
    const cos = Math.cos(twist);
    const sin = Math.sin(twist);
    const pts: THREE.Vector3[] = [];
    const add = (u: number, sign: number) => {
      // Chordwise distance aft of the leading edge, and the surface offset.
      const c = -u * st.chord;
      const h = sign * halfThickness(u, ratio) * st.chord;
      // Twist about the leading edge.
      const cT = c * cos - h * sin;
      const hT = c * sin + h * cos;
      pts.push(
        new THREE.Vector3()
          .copy(origin)
          .addScaledVector(chordAxis, st.leadingEdge + cT)
          .addScaledVector(spanAxis, st.span)
          .addScaledVector(upAxis, st.rise + hT)
      );
    };
    for (let i = 0; i <= CHORDWISE; ++i) add(us[i], +1);          // upper, LE -> TE
    for (let i = CHORDWISE - 1; i >= 1; --i) add(us[i], -1);      // lower, TE -> LE
    return pts;
  };

  const rings = stations.map(ring);
  const perRing = rings[0].length;

  // A mirrored surface is built on a LEFT-handed basis - the port wing takes
  // span -z where the starboard takes +z - which reverses the triangle winding
  // and turns every normal inward. The material hides it, the lighting does
  // not: one wing comes out lit and the other flat. Detect the handedness and
  // reverse the winding rather than leaving it to a DoubleSide material.
  const mirrored =
    new THREE.Vector3().crossVectors(chordAxis, spanAxis).dot(upAxis) > 0;
  const tri = (a: number, b: number, c: number) =>
    mirrored ? indices.push(a, c, b) : indices.push(a, b, c);
  const positions: number[] = [];
  const indices: number[] = [];

  for (const r of rings) for (const p of r) positions.push(p.x, p.y, p.z);

  for (let s = 0; s < rings.length - 1; ++s) {
    const a = s * perRing;
    const b = (s + 1) * perRing;
    for (let i = 0; i < perRing; ++i) {
      const j = (i + 1) % perRing;
      tri(a + i, b + i, a + j);
      tri(a + j, b + i, b + j);
    }
  }

  // Cap the tip with a fan around its centroid, so the surface is closed and
  // the tip catches light instead of showing the inside of the loft.
  const tip = rings[rings.length - 1];
  const centre = new THREE.Vector3();
  for (const p of tip) centre.add(p);
  centre.multiplyScalar(1 / tip.length);
  const centreIndex = positions.length / 3;
  positions.push(centre.x, centre.y, centre.z);
  const tipStart = (rings.length - 1) * perRing;
  for (let i = 0; i < perRing; ++i) {
    tri(centreIndex, tipStart + i, tipStart + ((i + 1) % perRing));
  }

  const geometry = new THREE.BufferGeometry();
  geometry.setAttribute("position", new THREE.Float32BufferAttribute(positions, 3));
  geometry.setIndex(indices);
  geometry.computeVertexNormals();
  return geometry;
}

/**
 * Surface of revolution about +x from a (station, radius) profile.
 *
 * Used for the fuselage, the engine cowls and the fairings. A profile may fold
 * back on itself, which is how the inlet lip is made.
 */
export function revolve(profile: Array<[number, number]>, segments = 26): THREE.BufferGeometry {
  // Ordered nose-negative to nose-positive. A profile listed the other way round
  // lathes inside out: the surface is culled and only whatever sits inside it
  // shows, which is what turned the engine cowls into dark blobs.
  if (profile.length > 1 && profile[0][0] > profile[profile.length - 1][0]) {
    profile = profile.slice().reverse();
  }
  const geometry = new THREE.LatheGeometry(
    profile.map(([x, r]) => new THREE.Vector2(Math.max(r, 1e-4), x)),
    segments
  );
  geometry.rotateZ(-Math.PI / 2);
  geometry.computeVertexNormals();
  return geometry;
}

/**
 * Paints a livery onto a geometry by vertex colour: white over the top, a
 * darker grey under the belly, and a thin cheatline where they meet.
 *
 * Cheaper and sharper than a texture for a surface of revolution, whose UVs run
 * around the body rather than over it.
 */
export function paintLivery(
  geometry: THREE.BufferGeometry,
  upper: THREE.Color,
  lower: THREE.Color,
  cheat: THREE.Color,
  cheatBand: [number, number]
): THREE.BufferGeometry {
  const position = geometry.attributes.position as THREE.BufferAttribute;
  const colors = new Float32Array(position.count * 3);
  const c = new THREE.Color();
  for (let i = 0; i < position.count; ++i) {
    const y = position.getY(i);
    const t = Math.min(1, Math.max(0, (y - cheatBand[1]) / 1.4));
    c.copy(lower).lerp(upper, t);
    if (y >= cheatBand[0] && y <= cheatBand[1]) c.copy(cheat);
    colors[i * 3] = c.r;
    colors[i * 3 + 1] = c.g;
    colors[i * 3 + 2] = c.b;
  }
  geometry.setAttribute("color", new THREE.BufferAttribute(colors, 3));
  return geometry;
}

/**
 * Body of revolution about +x, with UVs we control.
 *
 * LatheGeometry would do the geometry, but its UVs run in whatever direction
 * the lathe happened to sweep, which makes it impossible to paint anything
 * positioned - a window line, a windscreen, a radome boundary. Building it here
 * costs thirty lines and gives an exact mapping:
 *
 *     u = 0 at the TOP of the body, 0.25 starboard, 0.5 belly, 0.75 port
 *     v = 0 at the tail, 1 at the nose, linear in x
 *
 * With that, every piece of livery is a rectangle on a canvas instead of another
 * primitive stuck to the outside. The windows, the cockpit glazing and the
 * radome used to be a box, a squashed sphere and a separate cone; three shapes
 * intersecting a fourth is most of what made the aircraft look assembled.
 */
export function bodyOfRevolution(
  profile: Array<[number, number]>,
  segments = 48
): THREE.BufferGeometry {
  const ordered =
    profile[0][0] > profile[profile.length - 1][0] ? profile.slice().reverse() : profile;
  const xTail = ordered[0][0];
  const xNose = ordered[ordered.length - 1][0];
  const spanX = xNose - xTail || 1;

  const positions: number[] = [];
  const uvs: number[] = [];
  const indices: number[] = [];

  for (const [x, r] of ordered) {
    for (let j = 0; j <= segments; ++j) {
      const phi = (j / segments) * Math.PI * 2;
      // phi = 0 is straight up, and increases towards the starboard side.
      positions.push(x, r * Math.cos(phi), r * Math.sin(phi));
      uvs.push(j / segments, (x - xTail) / spanX);
    }
  }

  const perRing = segments + 1;
  for (let i = 0; i < ordered.length - 1; ++i) {
    for (let j = 0; j < segments; ++j) {
      const a = i * perRing + j;
      const b = (i + 1) * perRing + j;
      indices.push(a, b, a + 1);
      indices.push(a + 1, b, b + 1);
    }
  }

  const geometry = new THREE.BufferGeometry();
  geometry.setAttribute("position", new THREE.Float32BufferAttribute(positions, 3));
  geometry.setAttribute("uv", new THREE.Float32BufferAttribute(uvs, 2));
  geometry.setIndex(indices);
  geometry.computeVertexNormals();
  return geometry;
}

/**
 * The fuselage livery, drawn flat: white over the top, grey under the belly, a
 * cheatline where they meet, a row of cabin windows down each side, the cockpit
 * glazing, and a radome at the nose.
 *
 * `u` across is the way round the body, `v` down is tail to nose.
 */
export function makeFuselageTexture(): THREE.Texture {
  const w = 2048;
  const h = 512;
  const canvas = document.createElement("canvas");
  canvas.width = w;
  canvas.height = h;
  const ctx = canvas.getContext("2d");
  if (!ctx) return new THREE.CanvasTexture(canvas);

  const U = (u: number) => u * w;
  const V = (v: number) => v * h;

  ctx.fillStyle = "#f2f6fa";
  ctx.fillRect(0, 0, w, h);

  // Belly, with a soft edge so the transition is a curve rather than a seam.
  const belly = ctx.createLinearGradient(U(0.28), 0, U(0.72), 0);
  belly.addColorStop(0, "rgba(150,163,177,0)");
  belly.addColorStop(0.22, "rgba(150,163,177,1)");
  belly.addColorStop(0.78, "rgba(150,163,177,1)");
  belly.addColorStop(1, "rgba(150,163,177,0)");
  ctx.fillStyle = belly;
  ctx.fillRect(U(0.28), 0, U(0.44), h);

  // Cheatline down each side.
  ctx.fillStyle = "#2f6ea8";
  for (const u of [0.288, 0.688]) ctx.fillRect(U(u), 0, U(0.024), h);

  // Cabin windows: a row of them, not a stripe. The gap between them is what
  // makes an airliner read as an airliner at a distance.
  ctx.fillStyle = "#141a22";
  for (const u of [0.208, 0.768]) {
    for (let v = 0.2; v < 0.79; v += 0.0155) {
      ctx.fillRect(U(u), V(v), U(0.016), V(0.0085));
    }
  }
  // Doors, slightly taller, at the usual stations.
  for (const u of [0.2, 0.76]) {
    for (const v of [0.235, 0.4, 0.63, 0.755]) {
      ctx.fillStyle = "rgba(120,132,146,0.55)";
      ctx.fillRect(U(u), V(v), U(0.032), V(0.028));
    }
  }

  // Cockpit glazing, wrapped over the top of the nose.
  ctx.fillStyle = "#10161e";
  ctx.beginPath();
  ctx.ellipse(U(0.0), V(0.9), U(0.055), V(0.035), 0, 0, Math.PI * 2);
  ctx.fill();
  ctx.beginPath();
  ctx.ellipse(U(1.0), V(0.9), U(0.055), V(0.035), 0, 0, Math.PI * 2);
  ctx.fill();
  for (const u of [0.055, 0.945]) {
    ctx.beginPath();
    ctx.ellipse(U(u), V(0.895), U(0.03), V(0.026), 0, 0, Math.PI * 2);
    ctx.fill();
  }

  // Radome: mid grey, NOT black. A black nose disappears against a dark scene,
  // and the aircraft loses the end that says which way it is pointing.
  const radome = ctx.createLinearGradient(0, V(0.945), 0, V(1));
  radome.addColorStop(0, "rgba(122,134,148,0)");
  radome.addColorStop(0.35, "rgba(122,134,148,1)");
  radome.addColorStop(1, "rgba(104,116,130,1)");
  ctx.fillStyle = radome;
  ctx.fillRect(0, V(0.945), w, V(0.055));

  const texture = new THREE.CanvasTexture(canvas);
  texture.colorSpace = THREE.SRGBColorSpace;
  texture.anisotropy = 8;
  texture.wrapS = THREE.RepeatWrapping;
  return texture;
}
