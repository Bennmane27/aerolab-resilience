// AEROLAB RESILIENCE - record the header animation for the README.
//
// Frames are captured by SCRUBBING the recorded run rather than by filming it
// live. A screenshot takes a variable hundred-odd milliseconds, so filming a
// running simulation gives unevenly spaced samples and the motion judders; the
// transport slider steps through exact, evenly spaced instants instead, and the
// camera path is evaluated per frame from the aircraft's own position.
//
// Usage: node scripts/record-demo.mjs [baseURL] [outDir]
import { chromium } from "@playwright/test";
import { mkdirSync } from "node:fs";
import { join } from "node:path";

const BASE = process.argv[2] ?? "http://127.0.0.1:5199";
const OUT = process.argv[3] ?? "../docs/media/frames";
mkdirSync(OUT, { recursive: true });

const SCENARIO = "SCN-003";     // a step spoof: something visibly happens
const FRAMES = 150;             // 12.5 s at 12 fps
const FROM_S = 22;              // before the injection at t = 30 s
const TO_S = 48;                // well after it, with the source isolated

// Captured at the size it will be SHOWN. Recording at 1184 px and scaling the
// GIF down to 700 turned every panel into grey mush: the interface text is the
// first thing a downscale destroys, and a README animation is read at whatever
// width the page gives it.
const VIEW = { width: 1180, height: 780 };

const browser = await chromium.launch();
const page = await browser.newPage({ viewport: VIEW });
page.on("pageerror", (e) => console.log("PAGE ERROR:", e.message));

await page.goto(BASE);
await page.getByRole("button", { name: "FR", exact: true }).click();
await page.locator(".hero").getByRole("button", { name: "Choisir un scénario" }).click();
await page.locator(".scenario-card", { hasText: SCENARIO }).click();

// Run past the window we want, then pause: everything after this is replay.
// At x1 the loop records roughly one frame per rendered image, which gives the
// scrub enough resolution to land near any instant we ask for.
await page.getByRole("button", { name: "×1" }).click();
const clock = page.locator(".transport .clock");
for (let i = 0; i < 200; ++i) {
  const t = Number(((await clock.textContent()) ?? "0").split("/")[0].trim().replace(",", "."));
  if (t >= TO_S + 1) break;
  await page.waitForTimeout(250);
}
await page.getByRole("button", { name: "Pause" }).click();
await page.getByRole("button", { name: "Libre", exact: true }).click();
// The commentary collapses to its header: it shows the feature exists without
// covering a third of the frame with prose nobody can read at this size.
await page.locator(".narrator-toggle").click();
await page.evaluate(() => {
  for (const sel of [".viewport-hint", ".viewport-expand"]) {
    const el = document.querySelector(sel);
    if (el) el.style.display = "none";
  }
});
await page.waitForTimeout(400);

const total = Number(await page.locator("#scrub").getAttribute("max"));

// Recorded frames are NOT spaced by the simulation step: the run loop stores one
// per rendered image, and how much simulated time that covers depends on the
// speed and on how fast the machine was going. Probing the clock at a dozen
// indices and interpolating between them is the only honest way to ask for an
// instant. Assuming index = t / dt silently clamped every request to the last
// frame, and the aircraft sat still while the camera moved around it.
const clockAt = async (index) => {
  await page.locator("#scrub").fill(String(index));
  await page.waitForTimeout(30);
  const text = (await clock.textContent()) ?? "0";
  return Number(text.split("/")[0].trim().replace(",", "."));
};
const probes = [];
for (let k = 0; k <= 12; ++k) {
  const index = Math.round((total * k) / 12);
  probes.push({ index, t: await clockAt(index) });
}
console.log(
  `recorded frames: ${total}, covering t = ${probes[0].t.toFixed(1)}s to ${probes[probes.length - 1].t.toFixed(1)}s`
);
const indexForTime = (t) => {
  if (t <= probes[0].t) return probes[0].index;
  for (let k = 1; k < probes.length; ++k) {
    if (t <= probes[k].t) {
      const a = probes[k - 1];
      const b = probes[k];
      const f = b.t > a.t ? (t - a.t) / (b.t - a.t) : 0;
      return Math.round(a.index + f * (b.index - a.index));
    }
  }
  return probes[probes.length - 1].index;
};

const viewport = page.locator(".panel.viewport");
const easeOut = (u) => 1 - Math.pow(1 - u, 2.4);

for (let i = 0; i < FRAMES; ++i) {
  const u = i / (FRAMES - 1);
  const simT = FROM_S + (TO_S - FROM_S) * u;
  await page.locator("#scrub").fill(String(indexForTime(simT)));

  // Dolly in while drifting round the aircraft, and settle the elevation.
  await page.evaluate(
    ({ u, ease }) => {
      const s = window.__aerolab3d;
      const p = s.truthAircraft.position;
      const distance = 300 - (300 - 78) * ease;
      const azimuth = (-0.58 + 0.86 * u) * Math.PI;   // a little over half a turn
      const elevation = ((18 - 10 * ease) * Math.PI) / 180;
      const horizontal = distance * Math.cos(elevation);
      s.controls.target.set(p.x, p.y + 4, p.z);
      s.camera.position.set(
        p.x + horizontal * Math.sin(azimuth),
        p.y + 4 + distance * Math.sin(elevation),
        p.z + horizontal * Math.cos(azimuth)
      );
      s.controls.update();
    },
    { u, ease: easeOut(u) }
  );
  await page.waitForTimeout(60);
  await viewport.screenshot({ path: join(OUT, `f${String(i).padStart(4, "0")}.png`) });
  if (i % 25 === 0) {
    const shown = ((await clock.textContent()) ?? "").split("/")[0].trim();
    console.log(`  ${i}/${FRAMES}  asked t=${simT.toFixed(1)}s  shown ${shown}`);
  }
}

await browser.close();
console.log(`captured ${FRAMES} frames into ${OUT}`);
