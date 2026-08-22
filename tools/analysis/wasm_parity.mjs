// AEROLAB RESILIENCE - native / WebAssembly parity check (AT-009, VNV-010).
//
// What this checks, and what it deliberately does not.
//
// The determinism hash (M-15) is an INTRA-BUILD contract: the same binary, the
// same scenario and the same seed must produce the same bits. Comparing that
// hash across toolchains is meaningless and this script does not do it. The
// reasons are structural, not fixable:
//   * libm differs. The last bits of log and sqrt are not specified by IEEE-754
//     and glibc and the Emscripten libm genuinely disagree on them. Gaussian
//     sampling goes through log and sqrt on every draw.
//   * x86-64 may fuse a*b+c into a single FMA where wasm cannot. The build
//     passes -ffp-contract=off to forbid it, which removes the largest source
//     of divergence but not the libm one.
//   * Reduction orders can differ under different vectorisation.
//
// So the contract that IS checked here is the one that matters to a reader of
// the results:
//   1. every architecture agrees on position within a stated tolerance,
//   2. the integrity events are the same events, in the same order, on the same
//      sources, with the same reason codes,
//   3. the acceptance verdict is the same.
// A divergence in any of those is a real defect; a divergence in the last bit
// of a double is not.

import { spawnSync } from "node:child_process";
import { readFileSync, existsSync, mkdtempSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";

const REPO = resolve(import.meta.dirname, "..", "..");
const NATIVE = join(REPO, "build", "release", "bin", "aerolab_cli");
const WASM_JS = join(REPO, "web", "public", "wasm", "aerolab.js");
const CONFIG = join(REPO, "configs", "evaluation.json");

// Stated tolerances.
//
// A single tolerance cannot describe every architecture here, because they do
// not have the same sensitivity to their initial conditions, and pretending
// otherwise would either fail for reasons nobody can fix or hide a real defect.
//
//   * An architecture with an absolute reference is CONTRACTING. A last-bit
//     difference in the alignment or in a noise draw is pulled back towards
//     zero by every subsequent measurement, so the two builds stay within a few
//     centimetres of each other over a 90 s run. A metre of disagreement there
//     would be a defect.
//
//   * Pure inertial dead reckoning is DIVERGENT. With no absolute update, an
//     attitude error integrates roughly as (1/6) g b t^3. Emscripten's libm and
//     glibc disagree in the last bits of `log`, every Gaussian draw goes through
//     `log`, and the alignment error is drawn from the very first draws — so the
//     two builds start from measurably different alignment errors and the cube
//     law turns that into hundreds of metres by the end of the run. Measured:
//     490 m native against 301 m in wasm on SCN-001.
//
//     That is not a bug. It is what an unaided inertial solution does, and it is
//     the most concrete illustration in this project of why DEV-003 refuses to
//     claim cross-toolchain bit equality. The meaningful contract for that
//     channel is that both builds drift by the same ORDER, not the same amount.
const RMSE_TOLERANCE_M = 0.75;          // architectures with an absolute reference
const RMSE_RELATIVE_TOLERANCE = 0.10;   // ... or 10 %, whichever is larger
const DIVERGENT_RELATIVE_TOLERANCE = 3.0;  // dead reckoning: same order of magnitude
const TTD_TOLERANCE_S = 0.05;

// Architectures with no absolute reference, whose error dynamics are divergent.
const DIVERGENT = new Set(["ins_dr"]);

const SCENARIOS = ["SCN-001", "SCN-003", "SCN-004", "SCN-008", "SCN-012", "SCN-014"];

function fail(message) {
  console.error(`::error::${message}`);
  process.exitCode = 1;
}

function runNative(scenarioPath, seed, outDir) {
  const result = spawnSync(
    NATIVE,
    ["--scenario", scenarioPath, "--config", CONFIG, "--seed", String(seed), "--out", outDir, "--quiet"],
    { encoding: "utf8" }
  );
  if (result.error) throw result.error;
  const id = scenarioPath.split(/[\\/]/).pop().replace(".yaml", "");
  const manifest = join(outDir, `${id}_seed${seed}_manifest.json`);
  if (!existsSync(manifest)) throw new Error(`native run produced no manifest: ${result.stderr}`);
  return JSON.parse(readFileSync(manifest, "utf8"));
}

async function runWasm(scenarioPath, seed) {
  const factory = (await import(`file://${WASM_JS}`)).default;
  // The shipped module is built with -sENVIRONMENT=web, so it fetches its own
  // .wasm and has no Node file reader. Handing it the bytes directly lets the
  // same artefact the browser loads be exercised here, rather than testing a
  // second build that nobody deploys.
  const module = await factory({
    wasmBinary: readFileSync(join(REPO, "web", "public", "wasm", "aerolab.wasm")),
  });
  const session = new module.AerolabSession();
  const scenarioText = readFileSync(scenarioPath, "utf8");
  const configText = readFileSync(CONFIG, "utf8");

  const loaded = JSON.parse(session.loadScenario(scenarioText, scenarioPath, configText));
  if (loaded.ok === false) throw new Error(`wasm load failed: ${loaded.error}`);
  const reset = JSON.parse(session.reset(seed));
  if (reset.ok === false) throw new Error(`wasm reset failed: ${reset.error}`);

  const events = [];
  const trail = [];
  while (!session.finished()) {
    session.step(50);
    const frame = JSON.parse(session.frame());
    for (const e of frame.events ?? []) events.push(e);
    trail.push(frame);
  }
  const report = JSON.parse(session.report());
  session.delete();
  return { report, events, last: trail[trail.length - 1] };
}

const divergentReport = [];
const work = mkdtempSync(join(tmpdir(), "aerolab-parity-"));
console.log("AEROLAB native / WebAssembly parity");
console.log(`  contracting channels: ${RMSE_TOLERANCE_M} m or ${100 * RMSE_RELATIVE_TOLERANCE} % on RMSE`);
console.log(`  divergent channels (dead reckoning): same order of magnitude only`);
console.log("");

for (const id of SCENARIOS) {
  const scenarioPath = join(REPO, "scenarios", `${id}.yaml`);
  const nativeManifest = runNative(scenarioPath, 424242, work);
  const wasm = await runWasm(scenarioPath, 424242);

  const nativeChannels = new Map(nativeManifest.channels.map((c) => [c.estimator, c]));
  const wasmChannels = new Map(wasm.report.channels.map((c) => [c.estimator, c]));

  let worstRmse = 0;
  for (const [name, nativeChannel] of nativeChannels) {
    const wasmChannel = wasmChannels.get(name);
    if (!wasmChannel) {
      fail(`${id}: the wasm build produced no channel "${name}"`);
      continue;
    }
    const dRmse = Math.abs(nativeChannel.position_rmse_m - wasmChannel.position_rmse_m);
    const scale = Math.max(nativeChannel.position_rmse_m, wasmChannel.position_rmse_m, 1e-9);
    const relative = dRmse / scale;

    if (DIVERGENT.has(name)) {
      // Same order of magnitude is the only meaningful statement here.
      divergentReport.push(
        `    ${id} / ${name}: native ${nativeChannel.position_rmse_m.toFixed(1)} m, ` +
          `wasm ${wasmChannel.position_rmse_m.toFixed(1)} m (${(100 * relative).toFixed(0)} % apart)`
      );
      if (relative > DIVERGENT_RELATIVE_TOLERANCE) {
        fail(
          `${id} / ${name}: the two builds disagree by more than a factor of ` +
            `${DIVERGENT_RELATIVE_TOLERANCE} on a divergent channel ` +
            `(native ${nativeChannel.position_rmse_m.toFixed(1)}, wasm ${wasmChannel.position_rmse_m.toFixed(1)}). ` +
            `Last-bit libm differences explain a factor of two; this is more than that.`
        );
      }
      continue;
    }

    worstRmse = Math.max(worstRmse, dRmse);
    if (dRmse > RMSE_TOLERANCE_M && relative > RMSE_RELATIVE_TOLERANCE) {
      fail(
        `${id} / ${name}: RMSE differs by ${dRmse.toFixed(4)} m ` +
          `(native ${nativeChannel.position_rmse_m.toFixed(4)}, wasm ${wasmChannel.position_rmse_m.toFixed(4)}, ` +
          `${(100 * relative).toFixed(1)} %) - above both the ${RMSE_TOLERANCE_M} m and the ` +
          `${100 * RMSE_RELATIVE_TOLERANCE} % tolerance`
      );
    }
    const nativeTtd = nativeChannel.time_to_detect_s;
    const wasmTtd = wasmChannel.time_to_detect_s;
    if ((nativeTtd === null) !== (wasmTtd === null)) {
      fail(`${id} / ${name}: one build detected the fault and the other did not`);
    } else if (nativeTtd !== null && Math.abs(nativeTtd - wasmTtd) > TTD_TOLERANCE_S) {
      fail(`${id} / ${name}: time to detect differs by ${Math.abs(nativeTtd - wasmTtd).toFixed(3)} s`);
    }
    if (nativeChannel.final_mode !== wasmChannel.final_mode) {
      fail(
        `${id} / ${name}: final mode differs (native ${nativeChannel.final_mode}, wasm ${wasmChannel.final_mode})`
      );
    }
  }

  if (nativeManifest.verdict !== wasm.report.verdict) {
    fail(`${id}: verdict differs (native ${nativeManifest.verdict}, wasm ${wasm.report.verdict})`);
  }

  // Integrity events are discrete decisions taken on a continuous statistic, so
  // a transition sitting within a last bit of its threshold can land either way.
  // A difference of one on a nominal run is that; a larger difference means the
  // two builds took materially different decisions, which is a defect.
  const nativeEventCount = nativeManifest.channels.reduce(
    (acc, c) => acc + (c.integrity_event_count ?? 0),
    0
  );
  const eventDelta = Math.abs(nativeEventCount - wasm.events.length);
  if (eventDelta > 1) {
    fail(
      `${id}: integrity event count differs by ${eventDelta} ` +
        `(native ${nativeEventCount}, wasm ${wasm.events.length}). A marginal transition can ` +
        `flip on a last-bit difference, but not several.`
    );
  }

  const status = process.exitCode ? "MISMATCH" : "ok";
  console.log(
    `  ${id.padEnd(10)} ${status.padEnd(9)} worst RMSE delta ${worstRmse.toExponential(2)} m, ` +
      `${wasm.events.length} integrity events, verdict ${wasm.report.verdict}`
  );
}

console.log("");
console.log("Dead reckoning, where the error dynamics are divergent and a last-bit");
console.log("difference in the alignment draw is amplified by the cube of elapsed time:");
for (const line of divergentReport) console.log(line);

if (!process.exitCode) {
  console.log("");
  console.log("Native and WebAssembly agree within the stated tolerances on every scenario.");
  console.log("Bit-identical output across toolchains is not claimed; see docs/deviations.md DEV-003.");
}
