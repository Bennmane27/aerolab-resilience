#!/usr/bin/env bash
# AEROLAB RESILIENCE - detectability floor against drift rate.
#
# This is the headline experiment of the project.
#
# The question it answers is not "does the integrity layer work" - a 100 m step
# is caught by anything - but "how slowly does a source have to lie before each
# architecture stops noticing". A chi-square gate on the innovation is
# structurally weak against a slow drift, because the filter follows the ramp
# and the innovation stays small. Solution separation compares against a
# sub-filter that never receives the drifting source, so it does not lose power
# as the fault grows. Where the two curves part is the result.
#
# The sweep generates one scenario per drift rate, all identical except for the
# ramp amplitude, and runs the same seed set through every one of them so the
# comparison is not confounded by noise realisation.
#
# Usage: tools/analysis/drift_sweep.sh [seeds_per_point] [output_dir]
set -eu

SEEDS=${1:-200}
OUT=${2:-results/drift_sweep}
# "novision" runs the control experiment: the same sweep with the
# runway-relative vision sensor disabled. Solution separation is expected to
# hold its detection power there while the innovation gate loses its, because
# the gate depends on the other measurements holding the filter back and vision
# is the strongest of them in the horizontal plane.
VARIANT=${3:-full}
#   full        vision sensor on, GNSS-vs-vision cross check on
#   novision    vision sensor off entirely
#   nocrosscheck vision sensor on as a MEASUREMENT, cross check off. This is the
#               experiment that separates "vision constrains the filter, so the
#               innovation grows" from "the explicit cross check is what fires".
BIN=./build/release/bin/aerolab_bench
CONFIG=configs/evaluation.json
if [ "${VARIANT}" = "nocrosscheck" ]; then
  CONFIG=$(mktemp).json
  python3 -c "
import json, sys
cfg = json.load(open('configs/evaluation.json'))
cfg['integrity']['enable_vision_cross_check'] = False
json.dump(cfg, open(sys.argv[1], 'w'), indent=2)
" "${CONFIG}"
fi
RAMP_DURATION_S=45

if [ ! -x "${BIN}" ]; then
  echo "error: ${BIN} not found. Build it with:" >&2
  echo "  cmake --preset release && cmake --build build/release -j" >&2
  exit 2
fi

mkdir -p "${OUT}/scenarios"
RATES="0.1 0.2 0.35 0.5 0.75 1.0 1.5 2.0 3.0 4.5 6.0 9.0"

echo "AEROLAB drift rate sweep"
echo "  ${SEEDS} seeds per point, ramp over ${RAMP_DURATION_S} s, rates: ${RATES}"
echo

for RATE in ${RATES}; do
  OFFSET=$(python3 -c "print(${RATE} * ${RAMP_DURATION_S})")
  FILE="${OUT}/scenarios/drift_${RATE}.yaml"
  python3 - "${RATE}" "${OFFSET}" "${FILE}" "${RAMP_DURATION_S}" "${VARIANT}" <<'PY'
import sys
rate, offset, path, duration, variant = sys.argv[1:6]
template = open("scenarios/SCN-004.yaml", encoding="utf-8").read()
head, _, _ = template.partition("\nfaults:")
head = head.replace('id: SCN-004', 'id: SCN-004-R%s' % rate)
head = head.replace('name: "GNSS slow drift"', 'name: "GNSS drift at %s m/s"' % rate)
if variant == "novision":
    head = head.replace("  vision:\n    enabled: true", "  vision:\n    enabled: false")
    assert "  vision:\n    enabled: false" in head, "vision was not disabled"
fault = (
    "\nfaults:\n"
    "  - id: F-DRIFT\n"
    "    type: gnss_position_ramp\n"
    "    target: gnss\n"
    "    start_s: 25.0\n"
    "    duration_s: %s\n"
    "    amplitude_ned_m: [0.0, %s, 0.0]\n"
    "\n"
    "acceptance:\n"
    "  - id: AC-1\n"
    "    description: Sweep point. The measurement is the detection rate, so no detection is required.\n"
    "    estimator: any\n"
    "    max_position_rmse_m: 1000000.0\n"
) % (duration, offset)
open(path, "w", encoding="utf-8", newline="\n").write(head + fault)
PY

  "${BIN}" --scenario "${FILE}" --config "${CONFIG}" --seeds "1:${SEEDS}" \
           --out "${OUT}/run_${RATE}" --quiet >/dev/null 2>&1 || true
done

python3 - "${OUT}" "${RATES}" "${SEEDS}" <<'PY'
import json, os, sys, statistics

out, rates, seeds = sys.argv[1], sys.argv[2].split(), int(sys.argv[3])
ARCH = ["ekf", "integrity_ekf", "solsep_ekf"]
rows = []

for rate in rates:
    path = os.path.join(out, "run_%s" % rate, "runs.jsonl")
    if not os.path.exists(path):
        continue
    data = [json.loads(l) for l in open(path)]
    entry = {"rate": float(rate), "offset_m": float(rate) * 45}
    for arch in ARCH:
        runs = [r for r in data if r["estimator"] == arch]
        if not runs:
            continue
        detected = [r for r in runs if r["fault_detected"]]
        ttds = sorted(r["ttd_s"] for r in detected if r["ttd_s"] is not None)
        entry[arch] = {
            "runs": len(runs),
            "detection_rate": len(detected) / len(runs),
            "ttd_median": statistics.median(ttds) if ttds else None,
            "max_error_median": statistics.median(r["max_m"] for r in runs),
        }
    rows.append(entry)

with open(os.path.join(out, "sweep.json"), "w") as f:
    json.dump({"seeds_per_point": seeds, "points": rows}, f, indent=2)

with open(os.path.join(out, "sweep.csv"), "w") as f:
    f.write("drift_rate_mps,total_offset_m," +
            ",".join("%s_detection_rate,%s_ttd_median_s,%s_max_error_median_m" % (a, a, a)
                     for a in ARCH) + "\n")
    for r in rows:
        cells = []
        for a in ARCH:
            d = r.get(a)
            cells += ["", "", ""] if not d else [
                "%.4f" % d["detection_rate"],
                "" if d["ttd_median"] is None else "%.2f" % d["ttd_median"],
                "%.2f" % d["max_error_median"],
            ]
        f.write("%.3f,%.1f," % (r["rate"], r["offset_m"]) + ",".join(cells) + "\n")

print("%-10s %-9s %-24s %-24s %-24s" % ("rate m/s", "total m", "EKF (no integrity)",
                                        "innovation gating", "solution separation"))
print("%-10s %-9s %-24s %-24s %-24s" % ("", "", "detect  ttd  max err",
                                        "detect  ttd  max err", "detect  ttd  max err"))
print("-" * 96)
for r in rows:
    line = "%-10.2f %-9.0f" % (r["rate"], r["offset_m"])
    for a in ARCH:
        d = r.get(a)
        if not d:
            line += " %-24s" % "-"
        else:
            ttd = "  -  " if d["ttd_median"] is None else "%5.1f" % d["ttd_median"]
            line += " %5.0f%% %s %7.1f     " % (100 * d["detection_rate"], ttd,
                                                d["max_error_median"])
    print(line)

print()
print("Written: %s/sweep.csv and %s/sweep.json" % (out, out))
PY
