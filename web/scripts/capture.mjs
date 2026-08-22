// AEROLAB RESILIENCE - capture screenshots of the Web Lab.
//
// Used to produce the images in the README and to check the interface without
// having to sit and watch it. Runs against the PRODUCTION build served by
// `vite preview`, which is the artefact that actually ships.
//
// Usage: node scripts/capture.mjs [baseURL] [outputDir]
import { chromium } from "@playwright/test";
import { mkdirSync } from "node:fs";
import { join } from "node:path";

const BASE = process.argv[2] ?? "http://127.0.0.1:4173";
const OUT = process.argv[3] ?? "captures";
mkdirSync(OUT, { recursive: true });

const browser = await chromium.launch();
const page = await browser.newPage({ viewport: { width: 1600, height: 950 } });

async function openScenario(id, lang) {
  await page.goto(BASE);
  await page.getByRole("button", { name: lang === "fr" ? "FR" : "EN", exact: true }).click();
  const start = lang === "fr" ? "Choisir un scénario" : "Choose a scenario";
  await page.getByRole("button", { name: start }).click();
  await page.getByText(id, { exact: true }).click();
}

async function run(seconds, speed = "×4") {
  await page.getByRole("button", { name: speed }).click();
  await page.waitForTimeout(seconds * 1000);
}

// 1. Live Lab in French, chase camera, after the spoof has been isolated.
await openScenario("SCN-003", "fr");
await run(14);
await page.getByRole("button", { name: "Pause" }).click();
await page.screenshot({ path: join(OUT, "01-live-lab-fr.png") });

// 2. Top view: the runway, the range rings and the error vectors.
await page.getByRole("button", { name: "Dessus", exact: true }).click();
await page.waitForTimeout(900);
await page.screenshot({ path: join(OUT, "02-top-view.png") });

// 3. Chase view close in, with the error lines to each estimate.
await page.getByRole("button", { name: "Poursuite", exact: true }).click();
await page.waitForTimeout(900);
await page.screenshot({ path: join(OUT, "03-chase-view.png") });

// 4. The chart readout: crosshair pinned on one instant.
const chart = page.locator(".lab .strip .chart").first();
const box = await chart.boundingBox();
if (box) {
  await page.mouse.move(box.x + box.width * 0.72, box.y + box.height * 0.45);
  await page.mouse.click(box.x + box.width * 0.72, box.y + box.height * 0.45);
  await page.waitForTimeout(300);
}
await page.screenshot({ path: join(OUT, "04-chart-readout.png") });

// 5. English, engineering view: the statistic the policy decides on.
await page.getByRole("button", { name: "EN", exact: true }).click();
await page.getByRole("button", { name: "Engineering" }).click();
await page.waitForTimeout(600);
await page.screenshot({ path: join(OUT, "05-engineering-en.png") });

// 6. A divergent solution below the ground plane.
await openScenario("SCN-002", "fr");
await run(22);
await page.getByRole("button", { name: "Pause" }).click();
await page.getByRole("button", { name: "Dessus", exact: true }).click();
await page.waitForTimeout(700);
await page.screenshot({ path: join(OUT, "06-blackout-top.png") });

await browser.close();
console.log(`captured into ${OUT}/`);
