#!/usr/bin/env bash
# AEROLAB RESILIENCE - calibrate the solution separation covariance inflation.
#
# Section 8.1 of the cahier des charges allows exactly one thing to be adjusted
# while looking at results: the parameters in configs/tuning.json, measured on
# the tuning seed set. This script does that measurement for the single
# parameter that needs it, and prints the table that goes into
# docs/methodology/integrity.md.
#
# What it measures: the fraction of NOMINAL runs (SCN-001, no fault) in which
# the solution separation test isolates GNSS, as a function of the inflation
# applied to (P_sub - P_full). The target is zero over the tuning set, with the
# smallest inflation that achieves it - a larger inflation buys a quieter gate
# at the cost of detection power, so it must not be set higher than needed.
#
# Usage: tools/analysis/calibrate_separation.sh [seeds] [binary]
set -u

SEEDS=${1:-200}
BIN=${2:-./build/release/bin/aerolab_bench}
WORK=$(mktemp -d)
trap 'rm -rf "${WORK}"' EXIT

printf '%-10s %-14s %-14s %-12s\n' inflation nominal_runs runs_isolating rate
printf '%s\n' "------------------------------------------------------"

for INFLATION in 1.0 1.5 2.0 2.5 3.0 4.0 6.0; do
  CFG="${WORK}/config_${INFLATION}.json"
  python3 - "$INFLATION" "$CFG" <<'PY'
import json, sys
inflation, out = float(sys.argv[1]), sys.argv[2]
cfg = json.load(open("configs/tuning.json"))
cfg["integrity"]["solution_separation_covariance_inflation"] = inflation
json.dump(cfg, open(out, "w"), indent=2)
PY

  OUT="${WORK}/run_${INFLATION}"
  "${BIN}" --scenario scenarios/SCN-001.yaml --config "${CFG}" \
           --seeds 1:"${SEEDS}" --out "${OUT}" --quiet >/dev/null 2>&1

  python3 - "${OUT}/runs.jsonl" "${INFLATION}" <<'PY'
import json, sys
rows = [json.loads(l) for l in open(sys.argv[1])]
rows = [r for r in rows if r["estimator"] == "solsep_ekf"]
bad = [r for r in rows if r["false_isolations"] > 0]
rate = len(bad) / len(rows) if rows else 0.0
print("%-10s %-14d %-14d %-12.4f" % (sys.argv[2], len(rows), len(bad), rate))
PY
done
