# Repository Guidelines

## Project Structure & Module Organization

This is a headless C++23 Slay the Spire 2 simulator. Public headers live in `include/sts2/`; implementations live in `src/`, split into rules, state, kernel, observation, persistence, and integration layers. CLI entry points are in `apps/`; `src/main.cpp` runs the older demo. Keep that demo separate from the new engine.

`tests/unit/` contains engine tests; `tests/parity/` holds replay tests, schemas, and recordings; `tests/oracle/` contains Python regressions. Audited definitions live in `data/builds/`; architecture and contracts live in `docs/specs/`. `tools/` contains rules generation and reference audits. `sts2-cli/` is the pinned reference submodule.

## Build, Test, and Development Commands

Requires a C++23 compiler, CMake 3.20+, Make, and Git. Tests fetch GoogleTest and nlohmann/json; replay tests require OpenSSL Crypto development headers.

```sh
cmake --preset debug                  # Configure build/debug
cmake --build --preset debug          # Build engine, tools, and tests
ctest --preset debug                  # Run registered C++ and Python tests
ctest --preset debug -L engine        # Run engine checks
ctest --preset debug -L replay_parity # Run replay checks
./build/debug/sts2_combat --auto       # Run isolated combat
```

Use the `release` configure/build/test presets for optimized builds; run `./build/release/kernel_benchmark` for combat measurements. Install `tools/oracle/requirements-test.txt` for Python schema validation.

## Coding Style & Naming Conventions

Follow surrounding code: four-space indentation, same-line braces, `snake_case` functions/variables/files, and `PascalCase` types. Use `.hpp` headers with `#pragma once`. No repository-wide formatter or linter is configured; keep compiler warnings clean. Regenerate `src/rules/setup.g.cpp` with `python3 tools/rules_compiler/setup.py` instead of editing it directly.

## Testing Guidelines

Use GoogleTest in `*_test.cpp` with descriptive `TEST(Suite, Behavior)` names; Python tests use `test_*.py`. Add regression coverage for changed behavior and run relevant CTest labels before the full suite. No numerical coverage threshold is configured. Preserve fixture provenance, pinned game versions, and exact state comparisons.

## Commit & Pull Request Guidelines

Recent commits generally use `feat:`, `fix(oracle):`, `refactor(oracle):`, or `docs:` with imperative summaries. Follow that pattern. PRs should explain behavior changes, link relevant issues or contracts, and list validation commands and results. Identify fixture or reference-version changes explicitly.

## Engine Boundaries

Keep rules/kernel transitions deterministic and independent of JSON, filesystem access, and game assemblies. Reject unsupported content explicitly. Live oracle checks require a local game installation; consult `tools/README.md` before running them.

## Agent skills

### Issue tracker

Issues and specs live in GitHub Issues. See `docs/agents/issue-tracker.md`.

### Triage labels

Use the five default triage labels. See `docs/agents/triage-labels.md`.

### Domain docs

Use a single-context layout. See `docs/agents/domain.md`.
