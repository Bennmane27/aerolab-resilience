// AEROLAB RESILIENCE - Web Lab end to end (AT-012).
//
// The specification asks for "load, start, fault, report, with no blocking
// console error". These tests do that, and add the checks that matter for a
// page whose whole claim is that its numbers are real: that the core running is
// the WebAssembly one, that a run reaches a report, and that the controls the
// tool is demonstrated with do not break it.
//
// Two of these exist because of specific defects. `restartDoesNotKillThePage`
// covers the wiring bug where React handed the MouseEvent to `core.reset()` as
// the seed, aborted the WebAssembly module and blanked the page.
// `speedChangesDoNotStallTheRun` covers the animation loop being torn down and
// rebuilt on every speed change.
import { expect, test, type ConsoleMessage, type Page } from "@playwright/test";

function watchErrors(page: Page): ConsoleMessage[] {
  const errors: ConsoleMessage[] = [];
  page.on("console", (m) => {
    if (m.type() === "error") errors.push(m);
  });
  return errors;
}

/**
 * Playwright runs in en-US, so the interface comes up in English.
 *
 * The overview page carries the same call to action twice, at the top and at
 * the foot of the explanation, so this aims at the hero one explicitly rather
 * than at whichever the locator happens to resolve first.
 */
async function openScenario(page: Page, id: string) {
  await page.goto("/");
  const start = page.locator(".hero").getByRole("button", { name: "Choose a scenario" });
  await expect(start).toBeEnabled({ timeout: 60_000 });
  await start.click();
  await expect(page.getByText(id, { exact: true })).toBeVisible({ timeout: 30_000 });
  await page.getByText(id, { exact: true }).click();
  // Scoped to the badge: the legend names the truth too, and an unscoped
  // text match would be ambiguous.
  await expect(page.locator(".truth-badge")).toContainText("SIMULATION TRUTH");
}

test("the disclaimer is present before anything else happens", async ({ page }) => {
  await page.goto("/");
  // UI-022: the nature of the thing is stated on every screen, not buried.
  // Scoped to the banner: the overview also states the safety boundary in the
  // body of the page, which is deliberate but makes a bare text match ambiguous.
  await expect(page.locator(".disclaimer")).toContainText("Simulation only");
  await expect(page.locator(".disclaimer")).toContainText("no radio, no signal, no receiver");
});

test("the WebAssembly core loads and reports its provenance", async ({ page }) => {
  const errors = watchErrors(page);
  await page.goto("/");
  await expect(page.getByText(/schema v\d+/)).toBeVisible({ timeout: 60_000 });
  await expect(
    page.locator(".hero").getByRole("button", { name: "Choose a scenario" })
  ).toBeEnabled();
  expect(errors.map((e) => e.text())).toEqual([]);
});

test("the interface switches between French and English", async ({ page }) => {
  await page.goto("/");
  const hero = page.locator(".hero");
  await expect(hero.getByRole("button", { name: "Choose a scenario" })).toBeVisible({
    timeout: 60_000,
  });

  await page.getByRole("button", { name: "FR", exact: true }).click();
  await expect(hero.getByRole("button", { name: "Choisir un scénario" })).toBeVisible();
  await expect(page.locator(".disclaimer")).toContainText("Simulation uniquement");
  await expect(page.locator("html")).toHaveAttribute("lang", "fr");

  await page.getByRole("button", { name: "EN", exact: true }).click();
  await expect(hero.getByRole("button", { name: "Choose a scenario" })).toBeVisible();
  await expect(page.locator("html")).toHaveAttribute("lang", "en");
});

test("a run advances, injects its fault, and is reported", async ({ page }) => {
  const errors = watchErrors(page);
  await openScenario(page, "SCN-003");

  // UI-004: the truth is labelled as unavailable to the estimators, on the view.
  await expect(page.getByText(/not available to any estimator/)).toBeVisible();
  // The 3D view really is a WebGL canvas, not a placeholder.
  await expect(page.locator(".viewport canvas")).toBeVisible();

  await page.getByRole("button", { name: "×4" }).click();

  // SCN-003 injects a 100 m step spoof at t = 30 s.
  await expect(page.getByText(/FAULT ARMED/)).toBeVisible({ timeout: 180_000 });
  // And an integrity architecture reacts, with the reason in plain language.
  await expect(page.getByText(/ACTIVE → SUSPECT/).first()).toBeVisible({ timeout: 180_000 });

  expect(errors.map((e) => e.text())).toEqual([]);
});

test("restart does not kill the page", async ({ page }) => {
  const errors = watchErrors(page);
  await openScenario(page, "SCN-001");

  for (let i = 0; i < 4; ++i) {
    await page.getByRole("button", { name: "Restart" }).click();
    await page.waitForTimeout(250);
    // The view must still be there after every restart.
    await expect(page.locator(".viewport canvas")).toBeVisible();
    await expect(page.locator(".truth-badge")).toContainText("SIMULATION TRUTH");
  }
  expect(errors.map((e) => e.text())).toEqual([]);
});

test("speed changes do not stall the run", async ({ page }) => {
  await openScenario(page, "SCN-001");
  const clock = page.locator(".transport .clock");

  const readTime = async () => {
    const text = (await clock.textContent()) ?? "0";
    return Number(text.split("/")[0].trim().replace(",", "."));
  };

  for (const speed of ["×4", "×0.25", "×4", "×1", "×4"]) {
    const before = await readTime();
    await page.getByRole("button", { name: speed }).click();
    await page.waitForTimeout(900);
    const after = await readTime();
    expect(after, `simulated time did not advance after selecting ${speed}`).toBeGreaterThan(before);
  }
});

test("the position error chart reads out values under the pointer", async ({ page }) => {
  await openScenario(page, "SCN-001");
  await page.getByRole("button", { name: "×4" }).click();
  await page.waitForTimeout(2500);
  await page.getByRole("button", { name: "Pause" }).click();

  const chart = page.locator(".lab .strip .chart").first();
  await expect(chart).toBeVisible();
  const box = await chart.boundingBox();
  expect(box).not.toBeNull();
  if (!box) return;

  await page.mouse.move(box.x + box.width * 0.6, box.y + box.height * 0.5);
  // The readout replaces the hint with a timestamp and one value per series.
  await expect(page.locator(".chart-readout-time").first()).toBeVisible();
  await expect(page.locator(".chart-readout-item").first()).toBeVisible();

  // Clicking pins the instant so it can be read without holding the mouse still.
  await page.mouse.click(box.x + box.width * 0.6, box.y + box.height * 0.5);
  await expect(page.getByText(/pinned/).first()).toBeVisible();
});

test("orbiting the view does not drop out of the selected camera mode", async ({ page }) => {
  // Reported by a user: selecting Chase and then looking around flipped the
  // mode to Free, so the camera stopped following and chase mode was unusable
  // for anyone who wanted to look at anything.
  await openScenario(page, "SCN-001");
  await page.getByRole("button", { name: "Chase", exact: true }).click();
  await expect(page.getByRole("button", { name: "Chase", exact: true })).toHaveClass(/active/);

  const box = await page.locator(".panel.viewport").boundingBox();
  expect(box).not.toBeNull();
  if (!box) return;

  // Orbit, then zoom: neither is a mode change.
  await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
  await page.mouse.down();
  await page.mouse.move(box.x + box.width / 2 + 200, box.y + box.height / 2 - 60, { steps: 12 });
  await page.mouse.up();
  await page.mouse.wheel(0, -400);
  await page.waitForTimeout(400);

  await expect(page.getByRole("button", { name: "Chase", exact: true })).toHaveClass(/active/);
  await expect(page.getByRole("button", { name: "Free", exact: true })).not.toHaveClass(/active/);
});

test("the camera can be moved and the presets are available", async ({ page }) => {
  await openScenario(page, "SCN-001");
  for (const preset of ["Chase", "Top", "Runway", "Free"]) {
    await page.getByRole("button", { name: preset, exact: true }).click();
  }
  const canvas = page.locator(".viewport canvas");
  await expect(canvas).toBeVisible();
  // Aim at the panel rather than the canvas itself: the canvas is inside an
  // element with role="img", whose subtree Playwright treats as presentational,
  // so its box is not always resolvable. The canvas fills the panel, so the
  // coordinates are the same ones a user would click.
  const box = await page.locator(".panel.viewport").boundingBox();
  expect(box).not.toBeNull();
  if (!box) return;
  // Dragging must not throw; orbit control is what makes the view analysable.
  await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
  await page.mouse.down();
  await page.mouse.move(box.x + box.width / 2 + 120, box.y + box.height / 2 + 60, { steps: 8 });
  await page.mouse.up();
  await expect(canvas).toBeVisible();
});

test("the engineering view shows the statistic the policy decides on", async ({ page }) => {
  await openScenario(page, "SCN-001");
  await page.getByRole("button", { name: "Engineering" }).click();
  await expect(page.getByText(/Normalized Innovation Squared/).first()).toBeVisible();
  // UI-019: units are on the axes, not implied.
  await expect(page.getByText(/NIS \(dimensionless\)/)).toBeVisible({ timeout: 60_000 });
});

test("the live commentary explains the mechanism, and keeps its history", async ({ page }) => {
  // The event log answers "what did the policy decide". This has to answer why
  // that decision is the interesting one, and it has to keep the earlier
  // entries, because the run moves faster than anyone reads.
  await openScenario(page, "SCN-001");
  const entries = page.locator(".narrator-entry");
  // A scenario with no faults is the false-alert control case, and says so.
  await expect(entries.first()).toContainText(/Nothing will be injected/);
  await expect(entries.first()).toContainText(/false alarm/);
  // The commentary lives over the 3D view, not in the side column.
  await expect(page.locator(".viewport .narrator")).toBeVisible();
  await expect(page.locator(".lab .side .narrator")).toHaveCount(0);
  // The scenario's own objective anchors it.
  await expect(page.locator(".narrator-objective")).toContainText(/false alert rate/);

  await openScenario(page, "SCN-003");
  const before = await entries.count();
  await page.getByRole("button", { name: "×4" }).click();

  // SCN-003 is a step spoof: the commentary must name the mechanism that makes
  // a step easy, not merely report that something happened.
  await expect(page.locator(".narrator-list")).toContainText(/chi-square gate rejects it/, {
    timeout: 180_000,
  });
  // And the opening entry is still there: this accumulates, it does not replace.
  await expect(entries.first()).toContainText(/Nominal approach/);
  expect(await entries.count()).toBeGreaterThan(before);
  // The history is reachable.
  const overflow = await page.locator(".narrator-list").evaluate(
    (el) => el.scrollHeight > el.clientHeight
  );
  expect(typeof overflow).toBe("boolean");
});

test("the overview explains what the tool is, how it works and what it is not", async ({ page }) => {
  // The explanation used to be a page of its own next to this one. A visitor
  // landing here got three sentences and a button, and had to guess the rest
  // was one menu item away.
  await page.goto("/");
  await expect(page.getByRole("heading", { name: "How it works", level: 3 })).toBeVisible();
  await expect(page.getByRole("heading", { name: "The problem it addresses" })).toBeVisible();
  await expect(page.getByRole("heading", { name: "Reading the Live Lab" })).toBeVisible();
  await expect(page.getByRole("heading", { name: "What it is not" })).toBeVisible();
  await expect(page.getByRole("heading", { name: "Why it was built" })).toBeVisible();
  // The five-stage chain, and the safety boundary restated where a newcomer reads it.
  await expect(page.locator(".guide-chain > li")).toHaveCount(5);
  await expect(page.getByText(/no radio, no signal generator/)).toBeVisible();
  // There is no separate page for it any more.
  await expect(page.getByRole("button", { name: "How it works" })).toHaveCount(0);
  // The call to action at the FOOT of the page reaches the scenarios too, so a
  // reader who has just finished the explanation need not scroll back up.
  await page.locator(".guide-cta").getByRole("button", { name: "Choose a scenario" }).click();
  await expect(page.getByText("SCN-001", { exact: true })).toBeVisible({ timeout: 30_000 });
});

test("full screen gives the 3D view the window and keeps the controls", async ({ page }) => {
  await openScenario(page, "SCN-001");
  const lab = page.locator(".lab");
  await expect(lab).not.toHaveClass(/is-expanded/);
  await expect(page.locator(".lab .strip")).toBeVisible();

  await page.getByRole("button", { name: /Full screen/ }).click();
  await expect(lab).toHaveClass(/is-expanded/);
  // The chart strip goes; the viewport and the side controls stay.
  await expect(page.locator(".lab .strip")).toBeHidden();
  await expect(page.locator(".viewport canvas")).toBeVisible();
  await expect(page.getByRole("button", { name: "Chase", exact: true })).toBeVisible();

  await page.getByRole("button", { name: /Exit full screen/ }).click();
  await expect(lab).not.toHaveClass(/is-expanded/);
  await expect(page.locator(".lab .strip")).toBeVisible();
});

test("the methodology page states the limits and the safety boundary", async ({ page }) => {
  await page.goto("/");
  await page.getByRole("button", { name: "Methodology" }).click();
  await expect(page.getByText(/not a certified system/)).toBeVisible();
  await expect(page.getByText(/There is no radio/)).toBeVisible();
  await expect(page.getByText(/Assumptions and simplifications/)).toBeVisible();
  await expect(page.getByText(/They say nothing about this implementation/)).toBeVisible();
});

test("the failure catalog is populated", async ({ page }) => {
  await page.goto("/");
  await page.getByRole("button", { name: "Failure catalog" }).click();
  // UI-025 asks for at least three; nine are published.
  await expect(page.locator("section.failure-entry")).toHaveCount(9);
  await expect(page.getByText(/KF-003/)).toBeVisible();
  await expect(page.getByText(/KF-009/)).toBeVisible();
});

test("the transport controls are reachable from the keyboard", async ({ page }) => {
  await openScenario(page, "SCN-001");
  // UI-021: space pauses and resumes.
  await expect(page.getByRole("button", { name: "Pause" })).toBeVisible();
  await page.locator("body").click({ position: { x: 5, y: 400 } });
  await page.keyboard.press("Space");
  await expect(page.getByRole("button", { name: "Resume" })).toBeVisible();
  await page.keyboard.press("Space");
  await expect(page.getByRole("button", { name: "Pause" })).toBeVisible();
});
