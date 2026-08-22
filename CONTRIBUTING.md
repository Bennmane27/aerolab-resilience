# Contributing

## Before anything else

Read `SECURITY_AND_SAFETY.md`. A contribution that adds a radio interface, a
signal model, a receiver parameter, or anything that could be pointed at real
hardware will be rejected regardless of its quality. The safety boundary is the
reason this project can exist at all.

## The rules that are not negotiable

These come from the cahier des charges and are what the project is for. A change
that breaks one of them is a defect even if every test still passes.

1. **The fault engine never sees the ground truth.** Enforced by the signature of
   `FaultInjectionEngine::apply`, checked empirically by AT-002. Do not add a
   parameter to that function.
2. **The integrity policy decides before the update is committed**, not after.
   `prepareUpdate` must stay free of side effects; there is a test for that.
3. **No estimator reads the truth during a run.** The single exception is the
   alignment seed, produced once before the run by the runner.
4. **No mean is published without its median, P95 and worst case.**
5. **A missed detection is recorded as a miss.** Never zero, never omitted.
6. **`configs/evaluation.json` is frozen.** If you change it, every published
   figure becomes invalid: say so in `CHANGELOG.md` and re-run the campaign.
7. **Tune on the tuning seed set, report on the evaluation set.** They have
   different seed bases so this cannot happen by accident.

## Style

- C++17. No dynamic allocation inside the simulation loop.
- `clang-format` is authoritative; CI checks it.
- Zero warnings at `-Wall -Wextra -Wpedantic -Wshadow -Wconversion
  -Wsign-conversion -Wdouble-promotion`. This is not aspirational — the build
  currently has none.
- Every physical quantity carries its unit in the identifier:
  `sigma_position_horizontal_m`, not `sigma_pos`.
- No anonymous threshold. If a number decides something, it comes from a
  configuration file and has a name.

## Comments

Comment the *why*, not the *what*. The most valuable comments here explain why an
obvious approach was rejected: the header of `solution_separation.hpp` explains
why a chi-square gate is structurally weak against a slow drift, and that
reasoning is worth considerably more than a description of the class.

If you find yourself writing a comment that restates the line below it, delete
it. If you find yourself unable to explain why a constant has the value it does,
that is the comment that needs writing.

## Pull requests

Cite at least one requirement ID (`SYS-`, `SIM-`, `SENS-`, `FI-`, `NAV-`, `INT-`,
`BEN-`, `UI-`, `DATA-`, `API-`, `VNV-`, `NFR-`).

```
Purpose       what changes and why
Requirements  the IDs this touches
Tests         what you added or changed
Evidence      the command that demonstrates it, and its output
Limitations   what this does not fix
```

The Limitations section is not a formality. If a change has a known weakness it
belongs in `docs/failures/known_failures.md`, not in a commit message nobody will
read again.

## Before you push

```bash
cmake --preset release && cmake --build build/release -j
ctest --preset release --output-on-failure

cmake --preset asan && cmake --build build/asan -j
ctest --preset asan --output-on-failure

for f in scenarios/SCN-*.yaml; do
  ./build/release/bin/aerolab_cli --scenario "$f" \
    --config configs/evaluation.json --out results --quiet
done
```

If you touched anything the WebAssembly build compiles:

```bash
emcmake cmake --preset wasm && cmake --build build/wasm -j
node tools/analysis/wasm_parity.mjs
```
