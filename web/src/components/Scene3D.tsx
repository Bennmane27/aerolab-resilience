// AEROLAB RESILIENCE - Live Lab 3D view (UI-003, UI-004, UI-026).
//
// The scene shows one white aircraft on the SIMULATION TRUTH trajectory and one
// coloured marker per estimator. UI-004 is enforced here and in the badge above
// the canvas: the truth track is drawn in white, dashed, and labelled as
// simulation truth - it is a display series, not something any estimator has
// ever seen.
import { useEffect, useRef } from "react";
import * as THREE from "three";
import type { Frame, ScenarioInfo } from "../core/session";
import { ESTIMATOR_COLORS } from "../core/session";

interface Props {
  frame: Frame | null;
  scenario: ScenarioInfo | null;
  visibleEstimators: string[];
  cameraMode: "chase" | "map" | "runway";
}

const MAX_TRAIL_POINTS = 4000;

export function Scene3D({ frame, scenario, visibleEstimators, cameraMode }: Props) {
  const mountRef = useRef<HTMLDivElement>(null);
  const stateRef = useRef<{
    renderer: THREE.WebGLRenderer;
    scene: THREE.Scene;
    camera: THREE.PerspectiveCamera;
    truthTrail: THREE.Line;
    truthPositions: Float32Array;
    truthCount: number;
    estimatorTrails: Map<string, { line: THREE.Line; positions: Float32Array; count: number }>;
    truthMarker: THREE.Object3D;
    estimatorMarkers: Map<string, THREE.Object3D>;
    dispose: () => void;
  } | null>(null);

  // NED -> three.js world: x = East, y = altitude (up), z = North.
  const toWorld = (n: number, e: number, d: number) => new THREE.Vector3(e, -d, n);

  useEffect(() => {
    const mount = mountRef.current;
    if (!mount) return;

    const renderer = new THREE.WebGLRenderer({ antialias: true, alpha: false });
    renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    renderer.setClearColor(0x0b0f14, 1);
    mount.appendChild(renderer.domElement);

    const scene = new THREE.Scene();
    scene.fog = new THREE.Fog(0x0b0f14, 2000, 9000);

    const camera = new THREE.PerspectiveCamera(52, 1, 1, 40000);

    scene.add(new THREE.AmbientLight(0xffffff, 0.65));
    const key = new THREE.DirectionalLight(0xffffff, 0.9);
    key.position.set(-1500, 2500, 1200);
    scene.add(key);

    // Ground and a grid, sized for a 10 km footprint.
    const ground = new THREE.Mesh(
      new THREE.PlaneGeometry(24000, 24000),
      new THREE.MeshLambertMaterial({ color: 0x121a23 })
    );
    ground.rotation.x = -Math.PI / 2;
    ground.position.y = -0.5;
    scene.add(ground);

    const grid = new THREE.GridHelper(20000, 40, 0x22303f, 0x1a242f);
    grid.position.y = -0.4;
    scene.add(grid);

    const truthPositions = new Float32Array(MAX_TRAIL_POINTS * 3);
    const truthGeometry = new THREE.BufferGeometry();
    truthGeometry.setAttribute("position", new THREE.BufferAttribute(truthPositions, 3));
    truthGeometry.setDrawRange(0, 0);
    const truthTrail = new THREE.Line(
      truthGeometry,
      new THREE.LineDashedMaterial({ color: 0xffffff, dashSize: 26, gapSize: 16, opacity: 0.9, transparent: true })
    );
    scene.add(truthTrail);

    const truthMarker = new THREE.Mesh(
      new THREE.ConeGeometry(16, 52, 4),
      new THREE.MeshBasicMaterial({ color: 0xffffff })
    );
    scene.add(truthMarker);

    const estimatorTrails = new Map<string, { line: THREE.Line; positions: Float32Array; count: number }>();
    const estimatorMarkers = new Map<string, THREE.Object3D>();
    for (const [id, color] of Object.entries(ESTIMATOR_COLORS)) {
      const positions = new Float32Array(MAX_TRAIL_POINTS * 3);
      const geometry = new THREE.BufferGeometry();
      geometry.setAttribute("position", new THREE.BufferAttribute(positions, 3));
      geometry.setDrawRange(0, 0);
      const line = new THREE.Line(geometry, new THREE.LineBasicMaterial({ color }));
      scene.add(line);
      estimatorTrails.set(id, { line, positions, count: 0 });

      const marker = new THREE.Mesh(
        new THREE.SphereGeometry(15, 12, 10),
        new THREE.MeshBasicMaterial({ color })
      );
      scene.add(marker);
      estimatorMarkers.set(id, marker);
    }

    const resize = () => {
      const w = mount.clientWidth || 1;
      const h = mount.clientHeight || 1;
      renderer.setSize(w, h, false);
      camera.aspect = w / h;
      camera.updateProjectionMatrix();
    };
    resize();
    const observer = new ResizeObserver(resize);
    observer.observe(mount);

    stateRef.current = {
      renderer,
      scene,
      camera,
      truthTrail,
      truthPositions,
      truthCount: 0,
      estimatorTrails,
      truthMarker,
      estimatorMarkers,
      dispose: () => {
        observer.disconnect();
        renderer.dispose();
        if (renderer.domElement.parentElement === mount) mount.removeChild(renderer.domElement);
      },
    };

    return () => {
      stateRef.current?.dispose();
      stateRef.current = null;
    };
  }, []);

  // Runway geometry, rebuilt whenever the scenario changes.
  useEffect(() => {
    const s = stateRef.current;
    if (!s || !scenario) return;
    const existing = s.scene.getObjectByName("runway");
    if (existing) s.scene.remove(existing);

    const group = new THREE.Group();
    group.name = "runway";
    const heading = (scenario.scene.runway_heading_deg * Math.PI) / 180;
    const length = scenario.scene.runway_length_m;
    const width = scenario.scene.runway_width_m;

    const strip = new THREE.Mesh(
      new THREE.PlaneGeometry(width, length),
      new THREE.MeshLambertMaterial({ color: 0x2c3947 })
    );
    strip.rotation.x = -Math.PI / 2;
    strip.rotation.z = -heading;
    const [tn, te] = scenario.scene.threshold_ned_m;
    strip.position.set(te + (length / 2) * Math.sin(heading), 0.1, tn + (length / 2) * Math.cos(heading));
    group.add(strip);

    // Centreline, drawn from the threshold along the runway heading.
    const centreline = new THREE.Line(
      new THREE.BufferGeometry().setFromPoints([
        toWorld(tn, te, 0),
        toWorld(tn + length * Math.cos(heading), te + length * Math.sin(heading), 0),
      ]),
      new THREE.LineBasicMaterial({ color: 0x91a4b8 })
    );
    centreline.position.y = 0.3;
    group.add(centreline);

    // Threshold bar.
    const bar = new THREE.Mesh(
      new THREE.BoxGeometry(width, 1, 6),
      new THREE.MeshBasicMaterial({ color: 0xd8e4f0 })
    );
    bar.rotation.y = -heading;
    bar.position.copy(toWorld(tn, te, 0)).setY(0.4);
    group.add(bar);

    s.scene.add(group);
  }, [scenario]);

  // Per-frame update.
  useEffect(() => {
    const s = stateRef.current;
    if (!s || !frame) return;

    const truthPos = toWorld(frame.truth.n, frame.truth.e, frame.truth.d);
    if (s.truthCount < MAX_TRAIL_POINTS) {
      s.truthPositions.set([truthPos.x, truthPos.y, truthPos.z], s.truthCount * 3);
      s.truthCount += 1;
      s.truthTrail.geometry.setDrawRange(0, s.truthCount);
      s.truthTrail.geometry.attributes.position.needsUpdate = true;
      s.truthTrail.geometry.computeBoundingSphere();
      s.truthTrail.computeLineDistances();
    }
    s.truthMarker.position.copy(truthPos);
    s.truthMarker.rotation.set(0, -((frame.truth.yaw_deg * Math.PI) / 180), Math.PI / 2);

    for (const [id, trail] of s.estimatorTrails) {
      const marker = s.estimatorMarkers.get(id);
      const solution = frame.solutions[id];
      const visible = visibleEstimators.includes(id) && solution !== undefined;
      trail.line.visible = visible;
      if (marker) marker.visible = visible;
      if (!visible || !solution) continue;

      const p = toWorld(solution.n, solution.e, solution.d);
      if (trail.count < MAX_TRAIL_POINTS) {
        trail.positions.set([p.x, p.y, p.z], trail.count * 3);
        trail.count += 1;
        trail.line.geometry.setDrawRange(0, trail.count);
        trail.line.geometry.attributes.position.needsUpdate = true;
        trail.line.geometry.computeBoundingSphere();
      }
      marker?.position.copy(p);
    }

    // Camera.
    const cam = s.camera;
    const yaw = (frame.truth.yaw_deg * Math.PI) / 180;
    if (cameraMode === "chase") {
      const back = new THREE.Vector3(-Math.sin(yaw), 0, -Math.cos(yaw)).multiplyScalar(430);
      cam.position.copy(truthPos).add(back).add(new THREE.Vector3(0, 165, 0));
      cam.lookAt(truthPos);
    } else if (cameraMode === "runway" && scenario) {
      const [tn, te] = scenario.scene.threshold_ned_m;
      const h = (scenario.scene.runway_heading_deg * Math.PI) / 180;
      cam.position.copy(toWorld(tn + 260 * Math.cos(h), te + 260 * Math.sin(h), -22));
      cam.lookAt(truthPos);
    } else {
      cam.position.set(truthPos.x, 2100, truthPos.z + 320);
      cam.lookAt(truthPos.x, 0, truthPos.z);
    }

    s.renderer.render(s.scene, s.camera);
  }, [frame, visibleEstimators, cameraMode, scenario]);

  // Clear the trails when the run restarts (t goes back to zero).
  useEffect(() => {
    const s = stateRef.current;
    if (!s || !frame || frame.t > 0.05) return;
    s.truthCount = 0;
    s.truthTrail.geometry.setDrawRange(0, 0);
    for (const trail of s.estimatorTrails.values()) {
      trail.count = 0;
      trail.line.geometry.setDrawRange(0, 0);
    }
  }, [frame?.t === 0]);

  return <div ref={mountRef} style={{ width: "100%", height: "100%" }} aria-label="Three dimensional view of the simulated approach" role="img" />;
}
