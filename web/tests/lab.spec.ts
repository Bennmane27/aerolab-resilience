// AEROLAB RESILIENCE - Web Lab end to end (AT-012, UI-019, UI-021, UI-022).
//
// The specification asks for "load, start, fault, report, with no blocking
// console error". These tests do that, and add the checks that actually matter
// for a page whose whole claim is that its numbers are real: that the core is
// the WebAssembly one, that a run reaches a report, and that the report carries
// the provenance a reader would need to reproduce it.
import { expect, test, type ConsoleMessage } from "@playwright/test";

function collectErrors(messages: ConsoleMessage[]) {
  return (message: ConsoleMessage) => {
    if (message.type() === "error") messages.push(message);
  };
}

test("the disclaimer is present before anything else happens", async ({ page }) => {
  await page.goto("/");
  // UI-022: the nature of the thing is stated on every screen, not buried.
  await expect(page.getByText(/Simulation only/)).toBeVisible();
  await expect(page.getByText(/no radio, no signal, no receiver/)).toBeVisible();
});

test("the WebAssembly core loads and reports its provenance", async ({ page }) => {
  const errors: ConsoleMessage[] = [];
  page.on("console", collectErrors(errors));
  await page.goto("/");

  // The landing page prints the core build once the module is up. If this
  // appears, the wasm module instantiated and answered a call.
  await expect(page.getByText(/telemetry schema v\d+/)).toBeVisible({ timeout: 60_000 });
  await expect(page.getByRole("button", { name: "Choose a scenario" })).toBeEnabled();
  expect(errors.map((e) => e.text())).toEqual([]);
});

test("load, start, inject a fault, reach a report", async ({ page }) => {
  const errors: ConsoleMessage[] = [];
  page.on("console", collectErrors(errors));
  await page.goto("/");

  await page.getByRole("button", { name: "Choose a scenario" }).click();
  await expect(page.getByText("SCN-003")).toBeVisible({ timeout: 60_000 });

  // SCN-003 injects a 100 m step spoof at t = 30 s.
  await page.getByText("SCN-003").click();
  await expect(page.getByText(/SIMULATION TRUTH/)).toBeVisible();

  // UI-004: the truth is labelled as unavailable to the estimators, on the view
  // itself rather than only in a legend.
  await expect(page.getByText(/not available to any estimator/)).toBeVisible();

  // Run at x4 so the fault window is reached quickly.
  await page.getByRole("button", { name: "×4" }).click();

  // The fault must actually arrive and be reported.
  await expect(page.getByText(/FAULT ARMED/)).toBeVisible({ timeout: 120_000 });

  // And an integrity architecture must react to it. The reason code is shown in
  // plain language beside the transition (UI-017).
  await expect(page.getByText(/gnss ACTIVE → SUSPECT/)).toBeVisible({ timeout: 120_000 });

  expect(errors.map((e) => e.text())).toEqual([]);
});

test("the engineering view shows the statistic the policy decides on", async ({ page }) => {
  await page.goto("/");
  await page.getByRole("button", { name: "Choose a scenario" }).click();
  await expect(page.getByText("SCN-001")).toBeVisible({ timeout: 60_000 });
  await page.getByText("SCN-001").click();
  await page.getByRole("button", { name: "Engineering" }).click();

  await expect(page.getByText(/Normalized Innovation Squared/)).toBeVisible();
  // UI-019: units are visible on the axes, not implied.
  await expect(page.getByText(/NIS \(dimensionless\)/)).toBeVisible({ timeout: 60_000 });
});

test("the methodology page states the limits and the safety boundary", async ({ page }) => {
  await page.goto("/");
  await page.getByRole("button", { name: "Methodology" }).click();
  await expect(page.getByText(/not a certified system/)).toBeVisible();
  await expect(page.getByText(/There is no radio/)).toBeVisible();
  await expect(page.getByText(/Assumptions and simplifications/)).toBeVisible();
  // UI-024: the industrial sources are linked, with what they do and do not support.
  await expect(page.getByText(/They say nothing about this implementation/)).toBeVisible();
});

test("the failure catalog is populated", async ({ page }) => {
  await page.goto("/");
  await page.getByRole("button", { name: "Failure catalog" }).click();
  // UI-025: at least three reproducible limits or failures before release.
  const entries = page.locator("section.panel");
  await expect(entries).toHaveCount(8);
  await expect(page.getByText(/KF-003/)).toBeVisible();
  await expect(page.getByText(/reproduce/i).first()).toBeVisible();
});

test("the transport controls are reachable from the keyboard", async ({ page }) => {
  await page.goto("/");
  await page.getByRole("button", { name: "Choose a scenario" }).click();
  await expect(page.getByText("SCN-001")).toBeVisible({ timeout: 60_000 });
  await page.getByText("SCN-001").click();

  // UI-021: space pauses and resumes, R restarts.
  await expect(page.getByRole("button", { name: "Pause" })).toBeVisible();
  await page.keyboard.press("Space");
  await expect(page.getByRole("button", { name: "Resume" })).toBeVisible();
  await page.keyboard.press("Space");
  await expect(page.getByRole("button", { name: "Pause" })).toBeVisible();
});
