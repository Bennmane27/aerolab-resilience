// Copies the scenario and configuration files the browser needs into
// web/public/data. The Web Lab loads exactly the same files the native CLI
// does - not a hand-maintained copy - so a scenario cannot drift between the
// benchmark and the demo (SYS-006, DATA-002).
import { mkdirSync, readdirSync, copyFileSync, writeFileSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const repo = resolve(here, "..", "..");
const outDir = resolve(here, "..", "public", "data");

mkdirSync(join(outDir, "scenarios"), { recursive: true });
mkdirSync(join(outDir, "configs"), { recursive: true });

const scenarios = readdirSync(join(repo, "scenarios")).filter((f) => f.endsWith(".yaml")).sort();
for (const f of scenarios) {
  copyFileSync(join(repo, "scenarios", f), join(outDir, "scenarios", f));
}
copyFileSync(join(repo, "configs", "evaluation.json"), join(outDir, "configs", "evaluation.json"));

writeFileSync(
  join(outDir, "index.json"),
  JSON.stringify({ scenarios, config: "configs/evaluation.json" }, null, 2) + "\n"
);
console.log(`staged ${scenarios.length} scenarios and 1 config into web/public/data`);
