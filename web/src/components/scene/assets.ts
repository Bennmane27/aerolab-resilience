// AEROLAB RESILIENCE - procedural 3D assets.
//
// Everything the 3D view draws is generated here at runtime. No external model,
// no texture file, no CDN.
//
// That is a constraint rather than a preference: SYS-009 requires the demo to
// keep working once loaded with no network, and the published page makes no
// external request at all. A licensed airliner model would have to be fetched
// or vendored, and the good free ones (Poly Pizza, Kenney) ship as files under
// terms that would need carrying and attributing. Generating the geometry keeps
// the page self-contained and the repository free of third-party assets.
//
// The runway is the part that pays off most: a canvas-generated texture with
// real ICAO-style markings reads as a runway from any distance, where coloured
// rectangles never did.
import * as THREE from "three";

/**
 * Runway surface texture with standard markings, drawn along the strip:
 * threshold bars, designator number, aiming point, touchdown zone bars,
 * centreline dashes and edge lines.
 *
 * `u` runs across the width, `v` from the threshold towards the far end.
 */
export function makeRunwayTexture(headingDeg: number, lengthM: number, widthM: number): THREE.Texture {
  // One pixel per 0.5 m across, and a proportional length, capped so the
  // texture stays inside what a modest GPU will accept.
  const px = 512;
  const py = Math.min(4096, Math.round((px * lengthM) / widthM));
  const canvas = document.createElement("canvas");
  canvas.width = px;
  canvas.height = py;
  const ctx = canvas.getContext("2d");
  if (!ctx) return new THREE.CanvasTexture(canvas);

  const mToPxX = px / widthM;
  const mToPxY = py / lengthM;

  // --- asphalt ---------------------------------------------------------------
  ctx.fillStyle = "#5c6878";
  ctx.fillRect(0, 0, px, py);
  // Subtle longitudinal streaking, as laid asphalt has.
  for (let i = 0; i < 240; ++i) {
    const x = Math.random() * px;
    const y = Math.random() * py;
    const h = 30 + Math.random() * 260;
    ctx.fillStyle = `rgba(255,255,255,${0.008 + Math.random() * 0.016})`;
    ctx.fillRect(x, y, 1 + Math.random() * 3, h);
  }
  // Rubber deposits in the touchdown zone.
  const tdz = 300 * mToPxY;
  const rubber = ctx.createLinearGradient(0, tdz * 0.4, 0, tdz * 2.2);
  rubber.addColorStop(0, "rgba(18,22,27,0)");
  rubber.addColorStop(0.5, "rgba(18,22,27,0.45)");
  rubber.addColorStop(1, "rgba(18,22,27,0)");
  ctx.fillStyle = rubber;
  ctx.fillRect(px * 0.16, tdz * 0.4, px * 0.68, tdz * 1.8);

  const white = "#eef4fa";
  ctx.fillStyle = white;

  // --- edge lines ------------------------------------------------------------
  const edge = 0.9 * mToPxX;
  ctx.fillRect(px * 0.035, 0, edge, py);
  ctx.fillRect(px * 0.965 - edge, 0, edge, py);

  // --- threshold bars (piano keys) ------------------------------------------
  const barW = 1.8 * mToPxX;
  const barL = 30 * mToPxY;
  const barTop = 6 * mToPxY;
  const bars = 8;
  const span = px * 0.72;
  for (let i = 0; i < bars; ++i) {
    const gap = span / bars;
    const x = px * 0.14 + i * gap + (gap - barW) / 2;
    ctx.fillRect(x, barTop, barW, barL);
  }

  // --- runway designator -----------------------------------------------------
  // The number is the magnetic heading rounded to the nearest ten degrees,
  // divided by ten, as painted on a real runway.
  const designator = String(Math.round(((headingDeg % 360) + 360) % 360 / 10) || 36).padStart(2, "0");
  ctx.save();
  ctx.translate(px / 2, barTop + barL + 34 * mToPxY);
  ctx.scale(1, 1);
  ctx.fillStyle = white;
  ctx.font = `700 ${Math.round(26 * mToPxY)}px ui-monospace, monospace`;
  ctx.textAlign = "center";
  ctx.textBaseline = "top";
  ctx.fillText(designator, 0, 0);
  ctx.restore();

  // --- aiming point blocks ---------------------------------------------------
  const aimY = 300 * mToPxY;
  const aimW = 6 * mToPxX;
  const aimL = 45 * mToPxY;
  ctx.fillRect(px / 2 - 11 * mToPxX - aimW, aimY, aimW, aimL);
  ctx.fillRect(px / 2 + 11 * mToPxX, aimY, aimW, aimL);

  // --- touchdown zone bars ---------------------------------------------------
  const tdzL = 22 * mToPxY;
  const tdzW = 2.6 * mToPxX;
  for (const [distance, count] of [
    [150, 3],
    [450, 2],
    [600, 2],
    [750, 1],
    [900, 1],
  ] as Array<[number, number]>) {
    const y = distance * mToPxY;
    if (y > py - tdzL) continue;
    for (let side of [-1, 1]) {
      for (let k = 0; k < count; ++k) {
        const x = px / 2 + side * (20 * mToPxX + k * 4 * mToPxX);
        ctx.fillRect(side < 0 ? x - tdzW : x, y, tdzW, tdzL);
      }
    }
  }

  // --- centreline ------------------------------------------------------------
  const clW = 0.9 * mToPxX;
  const dash = 30 * mToPxY;
  const gap = 20 * mToPxY;
  for (let y = 60 * mToPxY; y < py - dash; y += dash + gap) {
    ctx.fillRect(px / 2 - clW / 2, y, clW, dash);
  }

  const texture = new THREE.CanvasTexture(canvas);
  texture.colorSpace = THREE.SRGBColorSpace;
  texture.anisotropy = 8;
  texture.needsUpdate = true;
  return texture;
}

/** Soft blob shadow that grows with altitude, cheaper and steadier than a shadow map. */
export function makeBlobShadow(): THREE.Mesh {
  const size = 128;
  const canvas = document.createElement("canvas");
  canvas.width = canvas.height = size;
  const ctx = canvas.getContext("2d");
  if (ctx) {
    const gradient = ctx.createRadialGradient(size / 2, size / 2, 0, size / 2, size / 2, size / 2);
    gradient.addColorStop(0, "rgba(0,0,0,0.55)");
    gradient.addColorStop(0.55, "rgba(0,0,0,0.22)");
    gradient.addColorStop(1, "rgba(0,0,0,0)");
    ctx.fillStyle = gradient;
    ctx.fillRect(0, 0, size, size);
  }
  const texture = new THREE.CanvasTexture(canvas);
  const mesh = new THREE.Mesh(
    new THREE.PlaneGeometry(1, 1),
    new THREE.MeshBasicMaterial({ map: texture, transparent: true, depthWrite: false })
  );
  mesh.rotation.x = -Math.PI / 2;
  mesh.renderOrder = 1;
  return mesh;
}

/** Viewport height in world units at one metre from the camera. */
export function unitViewHeight(camera: THREE.PerspectiveCamera): number {
  return 2 * Math.tan((camera.fov * Math.PI) / 360);
}

/**
 * World size of something that should occupy `fraction` of the viewport height
 * at the given distance, clamped so it never disappears or fills the screen.
 */
export function screenSizedWorld(
  distance: number,
  unitHeight: number,
  fraction: number,
  min: number,
  max: number
): number {
  return Math.min(max, Math.max(min, distance * unitHeight * fraction));
}

/**
 * Billboarded reticle for one estimated position.
 *
 * A sphere was the wrong symbol. A sphere is an OBJECT with a size and a
 * volume, and these are none of those things: each one is a single point that
 * one architecture believes the aircraft occupies. Every navigation display in
 * existence draws that as a flat mark - a ring, a cross, a diamond - because a
 * mark reads as a coordinate and a ball reads as a thing. The balls also hid
 * the aircraft, which is the one object in the scene whose size means anything.
 *
 * Drawn on a canvas so the ring stays one pixel wide at every distance, and
 * kept facing the camera by the render loop.
 */
export function makeReticle(color: string, size = 128): THREE.Sprite {
  const canvas = document.createElement("canvas");
  canvas.width = canvas.height = size;
  const ctx = canvas.getContext("2d");
  if (ctx) {
    const c = size / 2;
    const r = size * 0.30;
    ctx.strokeStyle = color;
    ctx.fillStyle = color;
    ctx.lineWidth = size * 0.035;
    ctx.lineCap = "butt";

    // Outer ring, left open at the four ticks so the mark reads as a reticle
    // rather than as a filled disc.
    for (let k = 0; k < 4; ++k) {
      const a0 = k * (Math.PI / 2) + 0.26;
      ctx.beginPath();
      ctx.arc(c, c, r, a0, a0 + Math.PI / 2 - 0.52);
      ctx.stroke();
    }
    // Ticks into the gaps.
    for (let k = 0; k < 4; ++k) {
      const a = k * (Math.PI / 2);
      ctx.beginPath();
      ctx.moveTo(c + Math.cos(a) * r * 0.62, c + Math.sin(a) * r * 0.62);
      ctx.lineTo(c + Math.cos(a) * r * 1.28, c + Math.sin(a) * r * 1.28);
      ctx.stroke();
    }
    // Centre dot: the point itself.
    ctx.beginPath();
    ctx.arc(c, c, size * 0.055, 0, Math.PI * 2);
    ctx.fill();
  }
  const texture = new THREE.CanvasTexture(canvas);
  texture.colorSpace = THREE.SRGBColorSpace;
  const sprite = new THREE.Sprite(
    new THREE.SpriteMaterial({ map: texture, transparent: true, depthWrite: false })
  );
  return sprite;
}

/** The same mark, hollow and doubled, for an estimate below the ground. */
export function makeBelowGroundReticle(color: string, size = 128): THREE.Sprite {
  const canvas = document.createElement("canvas");
  canvas.width = canvas.height = size;
  const ctx = canvas.getContext("2d");
  if (ctx) {
    const c = size / 2;
    ctx.strokeStyle = color;
    ctx.lineWidth = size * 0.05;
    for (const r of [size * 0.30, size * 0.19]) {
      ctx.beginPath();
      ctx.arc(c, c, r, 0, Math.PI * 2);
      ctx.stroke();
    }
    // Downward chevron: the estimate is under the surface.
    ctx.beginPath();
    ctx.moveTo(c - size * 0.11, c - size * 0.05);
    ctx.lineTo(c, c + size * 0.08);
    ctx.lineTo(c + size * 0.11, c - size * 0.05);
    ctx.stroke();
  }
  const texture = new THREE.CanvasTexture(canvas);
  texture.colorSpace = THREE.SRGBColorSpace;
  const sprite = new THREE.Sprite(
    new THREE.SpriteMaterial({ map: texture, transparent: true, depthTest: false, depthWrite: false })
  );
  sprite.renderOrder = 30;
  return sprite;
}
