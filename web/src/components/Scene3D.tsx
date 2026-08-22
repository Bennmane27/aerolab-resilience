// AEROLAB RESILIENCE - Live Lab 3D view (UI-003, UI-004, UI-026).
//
// What this view has to make legible, in order of importance:
//
//  1. WHERE THE ERROR IS. Not five dots that happen to be near each other, but
//     an explicit line from the true position to each estimate, labelled in
//     metres. The error is the subject of this project; it should be the thing
//     you see first.
// NO TEXT IN THE SCENE. Every number is read from the legend overlay and the
// solutions table, not from labels floating beside the markers. Labels anchored
// to five estimates a couple of metres apart — which is the normal case, and the
// case worth reading — pile on top of each other and on top of the aircraft, and
// shortening them does not help: the problem is that there is text there at all.
// The scene carries shape, colour and position; the legend carries the words.
//
//  2. WHICH DOT IS THE TRUTH. UI-004 requires the truth to be identifiable as
//     simulation truth and never mistakable for something an estimator had. It
//     is the only aircraft-shaped object, the only white one, and it carries a
//     dashed trail.
//  3. WHEN AN ESTIMATE HAS LEFT REALITY. A solution that places the aircraft
//     under the ground has diverged, and that is worth seeing rather than
//     inferring from a number. A marker below the surface turns into a hollow
//     ring and is drawn THROUGH the opaque ground, with a dashed stem back up
//     to it.
//  4. SCALE. Range rings at 1, 2 and 5 km from the threshold, a runway drawn at
//     its real dimensions with real markings, and an aircraft drawn at its real
//     40 m length, so that "20 m of error" has a size you can compare against.
//
// CAMERA. The mode and the orbit are independent, which is the point:
//
//     mode   = WHAT the camera watches and where it sits relative to it
//     orbit  = WHERE YOU have dragged it, kept as an offset from that
//
// Dragging, zooming and panning therefore never change the mode. In chase and
// top-down the camera is carried along with the aircraft with your offset
// preserved; in runway view the camera stays on the ground like a tower and
// only turns to track. Earlier this view switched itself to "free" the moment
// you touched it, which made chase mode unusable: one drag to look at something
// and you had lost the mode.
import { useEffect, useRef } from "react";
import * as THREE from "three";
import { OrbitControls } from "three/examples/jsm/controls/OrbitControls.js";
import type { Frame, ScenarioInfo } from "../core/session";
import { ESTIMATOR_COLORS } from "../core/session";
import { Sky } from "three/examples/jsm/objects/Sky.js";
import { Line2 } from "three/examples/jsm/lines/Line2.js";
import { LineGeometry } from "three/examples/jsm/lines/LineGeometry.js";
import { LineMaterial } from "three/examples/jsm/lines/LineMaterial.js";
import { RoomEnvironment } from "three/examples/jsm/environments/RoomEnvironment.js";
import {
  makeBlobShadow,
  makeRunwayTexture,
  screenSizedWorld,
  unitViewHeight,
} from "./scene/assets";
import { buildAirliner } from "./scene/aircraft";
import { attitudeFromEuler } from "./scene/attitude";

export type CameraMode = "chase" | "map" | "runway" | "free";

interface Props {
  frame: Frame | null;
  scenario: ScenarioInfo | null;
  visibleEstimators: string[];
  cameraMode: CameraMode;
  follow: boolean;
  labels: Record<string, string>;
  /** Changes on every restart; resets the trails without guessing from t. */
  runKey: number;
}

/**
 * Per mode: does the orbit target track the aircraft, and is the camera carried
 * along with it?
 *
 * Runway does neither. It is framed on the runway and stays there while the
 * aircraft flies into it — which is the point of a view called "runway". An
 * earlier version tracked the aircraft from the threshold, so the camera turned
 * its back on the runway the moment the run started and the one view meant to
 * show the runway never showed it.
 */
const CAMERA_BEHAVIOUR: Record<CameraMode, { track: boolean; carry: boolean }> = {
  chase: { track: true, carry: true },
  map: { track: true, carry: true },
  runway: { track: false, carry: false },
  free: { track: false, carry: false },
};

const MAX_TRAIL_POINTS = 6000;
const MIN_TRAIL_STEP_M = 0.4;

/** Estimator markers are drawn at this share of the viewport height. */
const MARKER_SCREEN_FRACTION = 0.017;
/** Radius the marker geometry is authored at, before the screen-size scaling. */
const MARKER_BASE_RADIUS_M = 5;

// NED -> three.js world: x = East, y = altitude (up), z = North.
function toWorld(n: number, e: number, d: number): THREE.Vector3 {
  return new THREE.Vector3(e, -d, n);
}

interface TrailHandle {
  line: THREE.Line;
  positions: Float32Array;
  count: number;
  last: THREE.Vector3 | null;
}

function makeTrail(scene: THREE.Scene, material: THREE.Material): TrailHandle {
  const positions = new Float32Array(MAX_TRAIL_POINTS * 3);
  const geometry = new THREE.BufferGeometry();
  geometry.setAttribute("position", new THREE.BufferAttribute(positions, 3));
  geometry.setDrawRange(0, 0);
  const line = new THREE.Line(geometry, material);
  line.frustumCulled = false;
  scene.add(line);
  return { line, positions, count: 0, last: null };
}

function pushTrail(trail: TrailHandle, point: THREE.Vector3, dashed: boolean) {
  if (trail.count >= MAX_TRAIL_POINTS) return;
  if (trail.last && trail.last.distanceTo(point) < MIN_TRAIL_STEP_M) return;
  trail.positions.set([point.x, point.y, point.z], trail.count * 3);
  trail.count += 1;
  trail.last = point.clone();
  trail.line.geometry.setDrawRange(0, trail.count);
  trail.line.geometry.attributes.position.needsUpdate = true;
  if (dashed) trail.line.computeLineDistances();
}

function resetTrail(trail: TrailHandle) {
  trail.count = 0;
  trail.last = null;
  trail.line.geometry.setDrawRange(0, 0);
}

/** Disposes a subtree's geometries, materials and textures. */
function disposeTree(root: THREE.Object3D) {
  root.traverse((object) => {
    const mesh = object as THREE.Mesh & { material?: THREE.Material | THREE.Material[] };
    mesh.geometry?.dispose?.();
    const materials = Array.isArray(mesh.material) ? mesh.material : mesh.material ? [mesh.material] : [];
    for (const material of materials) {
      const map = (material as THREE.MeshBasicMaterial).map;
      map?.dispose();
      material.dispose();
    }
  });
}

export function Scene3D({
  frame,
  scenario,
  visibleEstimators,
  cameraMode,
  follow,
  labels,
  runKey,
}: Props) {
  const mountRef = useRef<HTMLDivElement>(null);
  const stateRef = useRef<{
    renderer: THREE.WebGLRenderer;
    scene: THREE.Scene;
    camera: THREE.PerspectiveCamera;
    controls: OrbitControls;
    truthTrail: TrailHandle;
    truthAircraft: THREE.Group;
    truthShadow: THREE.Mesh;
    estimatorTrails: Map<string, TrailHandle>;
    estimatorMarkers: Map<string, THREE.Group>;
    errorLines: Map<string, Line2>;
    errorMaterials: LineMaterial[];
    dropLines: Map<string, THREE.Line>;
    ground: THREE.Mesh;
    dispose: () => void;
    lastTruth: THREE.Vector3;
  } | null>(null);

  // Values the animation loop needs but that must not tear the scene down when
  // they change.
  const propsRef = useRef({ frame, visibleEstimators, cameraMode, follow, labels });
  propsRef.current = { frame, visibleEstimators, cameraMode, follow, labels };

  // ---------------------------------------------------------------- setup ---
  useEffect(() => {
    const mount = mountRef.current;
    if (!mount) return;

    const renderer = new THREE.WebGLRenderer({ antialias: true, alpha: false });
    renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    renderer.setClearColor(0x0b1119, 1);
    renderer.outputColorSpace = THREE.SRGBColorSpace;
    // The Preetham sky below is a physical radiance model, so it needs a tone
    // map. Without one it clips to white and takes the runway with it.
    renderer.toneMapping = THREE.ACESFilmicToneMapping;
    renderer.toneMappingExposure = 0.68;
    mount.appendChild(renderer.domElement);
    renderer.domElement.style.touchAction = "none";

    const scene = new THREE.Scene();
    // The fog colour is the sky's horizon colour, so distant geometry dissolves
    // into the horizon instead of ending at a visible edge. It has to START far
    // out: at 5 km almost everything a camera a few hundred metres up can see
    // was already fogged, which flattened the whole airfield to one pale grey
    // and left the runway reading as a black hole in it.
    scene.fog = new THREE.Fog(0x3a4d60, 13000, 52000);

    // Sky: three's Preetham atmospheric scattering model rather than a hand
    // written gradient. The sun is placed low, which gives the dusk horizon the
    // rest of the interface is toned for, and the key light is aimed along the
    // same vector so the lighting and the sky agree.
    const sky = new Sky();
    sky.scale.setScalar(60000);
    const sunDirection = new THREE.Vector3().setFromSphericalCoords(
      1,
      THREE.MathUtils.degToRad(90 - 9),
      THREE.MathUtils.degToRad(155)
    );
    sky.material.uniforms.turbidity.value = 5;
    sky.material.uniforms.rayleigh.value = 1.8;
    sky.material.uniforms.mieCoefficient.value = 0.006;
    sky.material.uniforms.mieDirectionalG.value = 0.82;
    sky.material.uniforms.sunPosition.value.copy(sunDirection);
    sky.name = "sky";
    scene.add(sky);

    // Image based lighting, so the airliner's skin has a specular response
    // instead of reading as flat plastic. RoomEnvironment is a procedural
    // studio box that ships with three; nothing is loaded.
    const pmrem = new THREE.PMREMGenerator(renderer);
    const environment = pmrem.fromScene(new RoomEnvironment(), 0.04);
    scene.environment = environment.texture;
    scene.environmentIntensity = 0.22;
    pmrem.dispose();

    const camera = new THREE.PerspectiveCamera(50, 1, 1, 80000);
    camera.position.set(600, 500, -700);

    const controls = new OrbitControls(camera, renderer.domElement);
    controls.enableDamping = true;
    controls.dampingFactor = 0.09;
    controls.screenSpacePanning = false;
    controls.minDistance = 25;
    controls.maxDistance = 24000;
    // Allow going slightly below the horizon, so a divergent solution under
    // the surface can be looked at from underneath.
    controls.maxPolarAngle = Math.PI * 0.98;

    scene.add(new THREE.HemisphereLight(0x9dbcdd, 0x1b241d, 1.25));
    const key = new THREE.DirectionalLight(0xffe3c2, 2.6);
    key.position.copy(sunDirection).multiplyScalar(8000);
    scene.add(key);
    const rim = new THREE.DirectionalLight(0x74a6dc, 0.55);
    rim.position.set(2000, 900, -2500);
    scene.add(rim);

    // Ground: OPAQUE.
    //
    // It used to be drawn at 0.85 opacity so an estimate underneath it stayed
    // visible. That worked only while the background was near black. Against
    // the atmospheric sky, fifteen percent of a bright sky bled through every
    // square metre of terrain and washed the whole airfield to a flat pale
    // grey, with the runway reading as a dark hole in the middle of it.
    //
    // A below-ground marker is now drawn THROUGH the ground instead, by
    // switching off its depth test when it goes under. That is both a cleaner
    // image and a stronger signal: the surface stays solid, and exactly one
    // thing shows through it.
    const ground = new THREE.Mesh(
      new THREE.PlaneGeometry(60000, 60000),
      new THREE.MeshStandardMaterial({
        color: 0x25312a,
        roughness: 1,
        side: THREE.DoubleSide,
      })
    );
    ground.rotation.x = -Math.PI / 2;
    ground.renderOrder = -1;
    scene.add(ground);

    const grid = new THREE.GridHelper(40000, 80, 0x2f4657, 0x1d2b38);
    grid.position.y = 0.2;
    (grid.material as THREE.Material).transparent = true;
    (grid.material as THREE.Material).opacity = 0.32;
    scene.add(grid);

    // Truth aircraft, its ground shadow and its dashed trail.
    const truthAircraft = buildAirliner(0xf2f6fb);
    scene.add(truthAircraft);
    const truthShadow = makeBlobShadow();
    scene.add(truthShadow);
    const truthTrail = makeTrail(
      scene,
      new THREE.LineDashedMaterial({
        color: 0xffffff,
        dashSize: 34,
        gapSize: 22,
        transparent: true,
        opacity: 0.85,
      })
    );

    const estimatorTrails = new Map<string, TrailHandle>();
    const estimatorMarkers = new Map<string, THREE.Group>();
    const errorLines = new Map<string, Line2>();
    const errorMaterials: LineMaterial[] = [];
    const dropLines = new Map<string, THREE.Line>();

    for (const [id, colorHex] of Object.entries(ESTIMATOR_COLORS)) {
      const color = new THREE.Color(colorHex);

      estimatorTrails.set(id, makeTrail(scene, new THREE.LineBasicMaterial({ color })));

      // Marker: a lit sphere in a halo above ground, a hollow ring below it.
      const marker = new THREE.Group();
      const solid = new THREE.Group();
      solid.name = "solid";
      // Translucent: an opaque bead sitting on the aircraft hides the thing
      // the error is being measured against.
      const bead = new THREE.Mesh(
        new THREE.SphereGeometry(MARKER_BASE_RADIUS_M, 20, 14),
        new THREE.MeshStandardMaterial({
          color,
          emissive: color,
          emissiveIntensity: 0.45,
          roughness: 0.4,
          transparent: true,
          opacity: 0.82,
        })
      );
      solid.add(bead);
      const halo = new THREE.Mesh(
        new THREE.RingGeometry(8.2, 9.1, 36),
        new THREE.MeshBasicMaterial({ color, transparent: true, opacity: 0.42, side: THREE.DoubleSide })
      );
      halo.name = "halo";
      solid.add(halo);
      marker.add(solid);

      // Drawn through the ground: this is the "the estimate is underground"
      // signal, and it has to be visible from above the surface.
      const hollow = new THREE.Mesh(
        new THREE.TorusGeometry(11, 2.2, 8, 28),
        new THREE.MeshBasicMaterial({ color, depthTest: false })
      );
      hollow.renderOrder = 30;
      hollow.name = "hollow";
      hollow.rotation.x = Math.PI / 2;
      hollow.visible = false;
      marker.add(hollow);
      scene.add(marker);
      estimatorMarkers.set(id, marker);

      // Error vector: truth -> estimate. Drawn with Line2, because a plain
      // THREE.Line is one device pixel wide on every desktop GPU regardless of
      // linewidth — the single reason these read as barely visible hairlines
      // before. Line2 builds the line from instanced quads, so the width is
      // real and is specified in screen pixels.
      const errorGeometry = new LineGeometry();
      errorGeometry.setPositions([0, 0, 0, 0, 0, 0]);
      const errorMaterial = new LineMaterial({
        color: color.getHex(),
        linewidth: 2.6,
        transparent: true,
        opacity: 0.85,
        dashed: false,
      });
      errorMaterials.push(errorMaterial);
      const errorLine = new Line2(errorGeometry, errorMaterial);
      errorLine.frustumCulled = false;
      errorLine.renderOrder = 5;
      scene.add(errorLine);
      errorLines.set(id, errorLine);

      // Drop line: estimate -> ground directly below it.
      const dropGeometry = new THREE.BufferGeometry();
      dropGeometry.setAttribute("position", new THREE.BufferAttribute(new Float32Array(6), 3));
      const dropLine = new THREE.Line(
        dropGeometry,
        new THREE.LineDashedMaterial({
          color,
          dashSize: 12,
          gapSize: 10,
          transparent: true,
          opacity: 0.45,
        })
      );
      dropLine.frustumCulled = false;
      scene.add(dropLine);
      dropLines.set(id, dropLine);
    }

    const resize = () => {
      const w = mount.clientWidth || 1;
      const h = mount.clientHeight || 1;
      renderer.setSize(w, h, false);
      // Line2 needs the viewport size to convert its pixel width into clip
      // space; without this the error vectors change thickness on resize.
      for (const material of errorMaterials) material.resolution.set(w, h);
      camera.aspect = w / h;
      camera.updateProjectionMatrix();
    };
    resize();
    const observer = new ResizeObserver(resize);
    observer.observe(mount);

    // The camera modes move the orbit TARGET, and carry the camera with it by
    // the same translation. The user's own rotation, zoom and pan are an offset
    // from that target, so they survive untouched: dragging changes the angle,
    // never the mode.
    const applyCamera = (s: NonNullable<typeof stateRef.current>) => {
      const p = propsRef.current;
      const behaviour = CAMERA_BEHAVIOUR[p.cameraMode];
      if (!behaviour.track || !p.follow) return;
      const previous = s.controls.target.clone();
      s.controls.target.lerp(s.lastTruth, 0.18);
      if (behaviour.carry) s.camera.position.add(s.controls.target.clone().sub(previous));
    };

    let raf = 0;
    const animate = () => {
      const s = stateRef.current;
      if (s) {
        applyCamera(s);
        s.controls.update();
        // Markers hold a constant size on screen, like a map pin. A marker's
        // size carries no information — only its position does — so letting it
        // grow to twice the aircraft when the camera comes close, which is what
        // a fixed world size does, buys nothing and hides the one object in the
        // scene whose size you are meant to be judging metres against.
        const unitHeight = unitViewHeight(s.camera);
        for (const marker of s.estimatorMarkers.values()) {
          if (!marker.visible) continue;
          const distance = s.camera.position.distanceTo(marker.position);
          const world = screenSizedWorld(distance, unitHeight, MARKER_SCREEN_FRACTION, 2, 260);
          marker.scale.setScalar(world / MARKER_BASE_RADIUS_M);
          const halo = marker.getObjectByName("halo");
          if (halo) halo.quaternion.copy(s.camera.quaternion);
        }
        s.renderer.render(s.scene, s.camera);
      }
      raf = requestAnimationFrame(animate);
    };
    animate();

    stateRef.current = {
      renderer,
      scene,
      camera,
      controls,
      truthTrail,
      truthAircraft,
      truthShadow,
      estimatorTrails,
      estimatorMarkers,
      errorLines,
      dropLines,
      errorMaterials,
      ground,
      lastTruth: new THREE.Vector3(),
      dispose: () => {
        cancelAnimationFrame(raf);
        observer.disconnect();
        controls.dispose();
        disposeTree(scene);
        environment.texture.dispose();
        renderer.dispose();
        if (renderer.domElement.parentElement === mount) mount.removeChild(renderer.domElement);
      },
    };

    // Development only: lets the scene be inspected from the console when the
    // view looks wrong, without shipping a debug hook to the published page.
    if (import.meta.env.DEV) {
      (window as unknown as Record<string, unknown>).__aerolab3d = stateRef.current;
    }

    return () => {
      stateRef.current?.dispose();
      stateRef.current = null;
    };
    // Deliberately empty: the scene is built once for the life of the view.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  // ------------------------------------------------------- runway + scale ---
  useEffect(() => {
    const s = stateRef.current;
    if (!s || !scenario) return;
    const previous = s.scene.getObjectByName("scene-geometry");
    if (previous) {
      s.scene.remove(previous);
      disposeTree(previous);
    }

    const group = new THREE.Group();
    group.name = "scene-geometry";
    const headingDeg = scenario.scene.runway_heading_deg;
    const heading = (headingDeg * Math.PI) / 180;
    const length = scenario.scene.runway_length_m;
    const width = scenario.scene.runway_width_m;
    const [tn, te] = scenario.scene.threshold_ned_m;
    const along = new THREE.Vector3(Math.sin(heading), 0, Math.cos(heading));
    const across = new THREE.Vector3(Math.cos(heading), 0, -Math.sin(heading));
    const threshold = toWorld(tn, te, 0);
    const centre = threshold.clone().addScaledVector(along, length / 2);

    // Graded strip: the cleared area an aerodrome keeps around the pavement.
    // Without it the runway looks like a sticker laid on the terrain.
    const strip = new THREE.Mesh(
      new THREE.PlaneGeometry(width + 150, length + 400),
      new THREE.MeshStandardMaterial({ color: 0x232e24, roughness: 1 })
    );
    strip.rotation.x = -Math.PI / 2;
    // NOT -heading. Euler order XYZ applies Rz first and Rx afterwards, and the
    // -90 degree tip about X reverses the sense of that first rotation. With
    // -heading the pavement ran about a hundred degrees off its own centreline
    // while sitting on the correct centre point, which is why the runway used
    // to appear as a pale streak lying across the horizon.
    strip.rotation.z = heading;
    strip.position.copy(centre).setY(0.3);
    group.add(strip);

    // Pavement, with the markings painted into a generated texture rather than
    // assembled from dozens of white quads: one draw call, and the markings
    // stay sharp because the texture resolution follows the runway length.
    const pavement = new THREE.Mesh(
      new THREE.PlaneGeometry(width, length),
      new THREE.MeshStandardMaterial({
        map: makeRunwayTexture(headingDeg, length, width),
        roughness: 0.92,
        metalness: 0.02,
      })
    );
    pavement.rotation.x = -Math.PI / 2;
    pavement.rotation.z = heading;
    pavement.position.copy(centre).setY(0.6);
    group.add(pavement);

    // Runway edge lights and the approach light bars before the threshold.
    // Instanced, because there are a few hundred of them.
    const lightGeometry = new THREE.SphereGeometry(1.6, 6, 4);
    const edgeLightMaterial = new THREE.MeshBasicMaterial({ color: 0xf5f0d8 });
    const edgeCount = Math.floor(length / 60) + 1;
    const edgeLights = new THREE.InstancedMesh(lightGeometry, edgeLightMaterial, edgeCount * 2);
    const dummy = new THREE.Object3D();
    let index = 0;
    for (let i = 0; i < edgeCount; ++i) {
      for (const side of [-1, 1]) {
        dummy.position
          .copy(threshold)
          .addScaledVector(along, i * 60)
          .addScaledVector(across, (side * width) / 2 + side * 2)
          .setY(1.4);
        dummy.updateMatrix();
        edgeLights.setMatrixAt(index++, dummy.matrix);
      }
    }
    group.add(edgeLights);

    const approachMaterial = new THREE.MeshBasicMaterial({ color: 0xdfe9ff });
    const approachBars = 12;
    const approachLights = new THREE.InstancedMesh(
      lightGeometry,
      approachMaterial,
      approachBars * 5
    );
    index = 0;
    for (let bar = 1; bar <= approachBars; ++bar) {
      for (let k = -2; k <= 2; ++k) {
        dummy.position
          .copy(threshold)
          .addScaledVector(along, -bar * 60)
          .addScaledVector(across, k * 4.5)
          .setY(1.2);
        dummy.updateMatrix();
        approachLights.setMatrixAt(index++, dummy.matrix);
      }
    }
    group.add(approachLights);

    // Approach range rings: scale reference, so an error in metres has a size.
    for (const radius of [1000, 2000, 5000]) {
      const ring = new THREE.Mesh(
        new THREE.RingGeometry(radius - 7, radius + 7, 160),
        new THREE.MeshBasicMaterial({
          color: 0x36506a,
          transparent: true,
          opacity: 0.55,
          side: THREE.DoubleSide,
        })
      );
      ring.rotation.x = -Math.PI / 2;
      ring.position.copy(threshold).setY(0.4);
      group.add(ring);

    }

    // Extended centreline, out to the start of the approach.
    const extended = new THREE.Line(
      new THREE.BufferGeometry().setFromPoints([
        threshold.clone().addScaledVector(along, -9000),
        threshold.clone().addScaledVector(along, length),
      ]),
      new THREE.LineDashedMaterial({
        color: 0x4d6c8c,
        dashSize: 90,
        gapSize: 70,
        transparent: true,
        opacity: 0.7,
      })
    );
    extended.computeLineDistances();
    extended.position.y = 0.5;
    group.add(extended);

    // North arrow, so the view is orientable at a glance.
    const north = new THREE.ArrowHelper(
      new THREE.Vector3(0, 0, 1),
      threshold.clone().addScaledVector(across, -1100).setY(2),
      430,
      0x8fb6da,
      120,
      70
    );
    group.add(north);

    s.scene.add(group);
  }, [scenario]);

  // -------------------------------------------------------------- presets ---
  // Runs only when the MODE changes (or the run restarts), never when the user
  // drags: that is what makes the mode survive being looked around from.
  useEffect(() => {
    const s = stateRef.current;
    if (!s || !scenario) return;
    if (cameraMode === "free") return;

    const heading = (scenario.scene.runway_heading_deg * Math.PI) / 180;
    const along = new THREE.Vector3(Math.sin(heading), 0, Math.cos(heading));
    const across = new THREE.Vector3(Math.cos(heading), 0, -Math.sin(heading));
    const [tn, te] = scenario.scene.threshold_ned_m;
    const threshold = toWorld(tn, te, 0);
    const target = s.lastTruth.lengthSq() > 0 ? s.lastTruth.clone() : threshold.clone();

    if (cameraMode === "chase") {
      // Behind and above, close enough that a 40 m aircraft is a shape rather
      // than a dot, far enough that the error vectors stay in frame.
      s.controls.target.copy(target);
      s.camera.position.copy(target).addScaledVector(along, -230).add(new THREE.Vector3(0, 95, 0));
    } else if (cameraMode === "map") {
      s.controls.target.copy(target);
      s.camera.position.copy(target).add(new THREE.Vector3(0.01, 1600, 0));
    } else if (cameraMode === "runway") {
      // Over the approach, off to one side and high enough to look DOWN on the
      // pavement. Sitting at approach height on the centreline is realistic and
      // useless: from 85 m up a 45 m runway is fourteen pixels of edge-on
      // sliver. A 14-degree depression angle from four hundred metres out is
      // what makes it read as a runway, threshold bars and all.
      s.controls.target.copy(threshold).addScaledVector(along, 450);
      s.camera.position
        .copy(threshold)
        .addScaledVector(along, -900)
        .addScaledVector(across, 650)
        .setY(380);
    }
    s.controls.update();
  }, [cameraMode, scenario, runKey]);

  // ---------------------------------------------------------- run restart ---
  useEffect(() => {
    const s = stateRef.current;
    if (!s) return;
    resetTrail(s.truthTrail);
    for (const trail of s.estimatorTrails.values()) resetTrail(trail);
  }, [runKey]);

  // ----------------------------------------------------------- per frame ----
  useEffect(() => {
    const s = stateRef.current;
    if (!s || !frame) return;

    const truthPos = toWorld(frame.truth.n, frame.truth.e, frame.truth.d);
    s.lastTruth.copy(truthPos);
    pushTrail(s.truthTrail, truthPos, true);

    s.truthAircraft.position.copy(truthPos);
    s.truthAircraft.quaternion.setFromRotationMatrix(
      attitudeFromEuler(frame.truth.yaw_deg, frame.truth.pitch_deg, frame.truth.roll_deg)
    );

    // Ground shadow: it spreads and fades with height, which reads as altitude
    // even in the top-down view where height is otherwise invisible.
    const altitude = Math.max(0, truthPos.y);
    const spread = 70 + altitude * 0.35;
    s.truthShadow.position.set(truthPos.x, 1.2, truthPos.z);
    s.truthShadow.scale.set(spread, spread, 1);
    (s.truthShadow.material as THREE.MeshBasicMaterial).opacity = Math.max(
      0.05,
      0.85 - altitude / 900
    );

    for (const [id, trail] of s.estimatorTrails) {
      const marker = s.estimatorMarkers.get(id);
      const errorLine = s.errorLines.get(id);
      const dropLine = s.dropLines.get(id);
      const solution = frame.solutions[id];
      const visible = visibleEstimators.includes(id) && solution !== undefined;

      trail.line.visible = visible;
      if (marker) marker.visible = visible;
      if (errorLine) errorLine.visible = visible;
      if (dropLine) dropLine.visible = visible;
      if (!visible || !solution) continue;

      const p = toWorld(solution.n, solution.e, solution.d);
      pushTrail(trail, p, false);
      marker?.position.copy(p);

      // Below the ground plane: swap the lit bead for a hollow ring, which
      // reads as "this is not a place an aircraft can be".
      const belowGround = p.y < 0;
      if (marker) {
        const solid = marker.getObjectByName("solid");
        const hollow = marker.getObjectByName("hollow");
        if (solid) solid.visible = !belowGround;
        if (hollow) hollow.visible = belowGround;
      }

      if (errorLine) {
        errorLine.material.depthTest = !belowGround;
        errorLine.renderOrder = belowGround ? 28 : 5;
        errorLine.geometry.setPositions([
          truthPos.x, truthPos.y, truthPos.z,
          p.x, p.y, p.z,
        ]);
        errorLine.geometry.computeBoundingSphere();
      }

      if (dropLine) {
        const attr = dropLine.geometry.attributes.position as THREE.BufferAttribute;
        attr.setXYZ(0, p.x, p.y, p.z);
        attr.setXYZ(1, p.x, 0, p.z);
        attr.needsUpdate = true;
        dropLine.computeLineDistances();
        const dropMaterial = dropLine.material as THREE.LineDashedMaterial;
        dropMaterial.opacity = belowGround ? 0.95 : 0.4;
        dropMaterial.depthTest = !belowGround;
        dropLine.renderOrder = belowGround ? 29 : 0;
      }
    }
  }, [frame, visibleEstimators, labels]);

  return (
    <div
      ref={mountRef}
      style={{ width: "100%", height: "100%", cursor: "grab" }}
      role="img"
      aria-label="Interactive 3D view of the simulated approach"
    />
  );
}
