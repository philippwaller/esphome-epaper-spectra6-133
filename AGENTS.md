# ESPHome epaper_spectra6_133 — Agent Instructions

## What this repository is

This repository contains an ESPHome external component for 13.3″ Spectra 6 e-paper panels in the GDEP133C02-compatible display class. The component targets ESP32-S3 boards with PSRAM and uses ESP-IDF only.

The implementation combines:

- Python code for ESPHome schema validation and code generation.
- C++ code for framebuffer handling, SPI/GPIO transport, panel control, refresh orchestration, and dirty-region detection.
- YAML examples and board packages for validating real ESPHome configurations.
- Host-side C++ tests for logic that can be verified without hardware.

## Non-negotiable constraints

Respect these constraints in every change:

- ESP-IDF only. Do not add Arduino support, Arduino APIs, or Arduino-style assumptions.
- PSRAM is required. A full 1200×1600×4bpp framebuffer is approximately 960 KB.
- The panel is dual-controller: left and right halves are driven by separate ICs and share one BUSY line.
- Pixel logic must use the exact six-colour Spectra palette. Do not use `Color::is_on()` for colour decisions.
- Partial refresh must keep both ICs in a valid refresh flow. When only one half changes, the unchanged half still needs the required driver interaction.
- Avoid blocking display operations unless the public API or documentation explicitly describes that behaviour.
- After renaming or deleting component source files, stale ESPHome build artifacts under `configs/.esphome/build/` can cause misleading results and may need manual cleanup.

## Repository layout

```text
components/epaper_spectra6_133/
├── __init__.py                         # shared Python declarations and SPI host validation
├── display.py                          # ESPHome platform schema and code generation
├── epaper_spectra6_133.h/.cpp           # main ESPHome DisplayBuffer component
├── epaper_spectra6_133_constants.h      # panel geometry, palette codes, registers
├── epaper_spectra6_133_framebuffer.h/.cpp# pixel packing, colour mapping, changed-region detection
├── epaper_spectra6_133_transport.h/.cpp # SPI and GPIO transport layer
└── epaper_spectra6_133_controller.h/.cpp# init sequences, refresh, partial windows
```

## How to change the code

Prefer small, mechanical changes over broad rewrites. Keep the public API, YAML schema, C++ implementation, examples, and README aligned.

When changing Python codegen:

- Keep `__init__.py` and `display.py` consistent.
- Follow ESPHome schema/codegen conventions.
- Validate example configs after schema or YAML changes.

When changing C++ code:

- Keep hardware-facing logic explicit and conservative.
- Separate concerns between framebuffer, transport, controller, and ESPHome component orchestration.
- Avoid hidden state machines unless they are necessary and documented.
- Prefer clear names over abbreviations for public APIs.
- Document timing-sensitive, hardware-sensitive, or non-obvious behaviour directly at the implementation boundary.

When changing examples or documentation:

- Keep examples minimal, runnable, and focused on one concept.
- Do not duplicate API reference text across multiple README sections.
- Prefer user-facing explanations over implementation trivia.
- Mention hardware caveats only where they help the user make a correct decision.

## Naming rules

Use these naming patterns unless there is a strong reason not to:

- Component source files: `epaper_spectra6_133_<layer>.{h,cpp}`
- YAML configs: `configs/<purpose>.yaml`
- Board packages: `packages/boards/<vendor>/<board>.yaml`
- Public C++ methods: clear verb phrases such as `update_region`, `detect_changed_region`, `cancel`, or `is_busy`.
- Internal async/job variables: avoid leaking implementation terms into public names unless users need to reason about them.

## Documentation style

Write documentation in clear, professional English.

Use comments for:

- Public classes, methods, options, and non-obvious configuration behaviour.
- Hardware sequencing constraints.
- Timing, BUSY-pin, refresh, and partial-window behaviour.
- Decisions that would otherwise look unnecessary or accidental.

Do not add comments that merely repeat the code. Remove stale comments after refactors.

Detailed comment rules:

- Use Doxygen style for public C/C++ APIs (`/** ... */`, `@brief`, `@param`, `@return`).
- For private C/C++ helpers, add comments only when behaviour is non-obvious, hardware-specific, or timing-sensitive.
- Add Python module docstrings and concise docstrings for public functions, classes, and dataclasses.
- In ESPHome codegen modules, document what users configure and how values are validated.
- Write YAML example comments from the end-user perspective, explaining when to change an option.
- Avoid decorative comment banners, implementation history, migration notes, and marketing language.
- If behaviour, timing, configuration defaults, or API semantics change, update nearby documentation in the same change.

## Testing and validation

Use the narrowest relevant checks before handing work back:

```bash
bash ./scripts/run-host-tests.sh
bash ./scripts/validate-configs.sh
./.venv/bin/pre-commit run --all-files
./scripts/esphomew run configs/hello-world.yaml
```

Always run Python commands and tests through the project virtual environment:

- Use `./.venv/bin/python -m pytest ...` for Python tests.
- Use `./.venv/bin/python scripts/esphome-versions.py ...` for Python scripts when validation depends on project dependencies.
- Do not use `python3 -m pytest`, `python -m pytest`, or a global interpreter for project tests.
- If `./.venv/bin/python` is missing, run `./scripts/setup.sh` before validating.

Choose checks based on the change:

- Framebuffer, dirty-region, controller, or low-level C++ logic: `bash ./scripts/run-host-tests.sh`
- Python codegen, schema, YAML, examples, or packages: `bash ./scripts/validate-configs.sh`
- Formatting-sensitive changes: `./.venv/bin/pre-commit run --all-files`
- Hardware-relevant display-flow changes: `./scripts/esphomew run configs/hello-world.yaml`

Coverage reports from host tests are written to `build/coverage/`.

CI currently runs YAML lint, clang-format, host C++ tests with coverage, and standalone config validation. Do not rely on CI to find issues that can be caught locally.

## Commit style

Use Conventional Commits compatible with Release Please.

Allowed common types:

- `feat:` for user-visible capability
- `fix:` for incorrect behaviour
- `docs:` for documentation-only changes
- `refactor:` for internal restructuring without behaviour change
- `perf:` for performance improvements
- `test:` for test changes
- `ci:` for CI workflows
- `build:` for build/tooling changes
- `chore:` for maintenance
- `revert:` for reverting a previous change

Use a scope when it adds clarity, for example:

```text
fix(controller): avoid full refresh for unchanged half
refactor(framebuffer): clarify changed-region detection
```

Use `!` after the type/scope or a `BREAKING CHANGE:` footer for breaking changes.

## Before handing work back

Before considering a change complete, verify that:

- The code matches the ESPHome/ESP-IDF-only architecture.
- Public names are understandable from a user perspective.
- README, examples, schema, and implementation do not contradict each other.
- Relevant tests or validation commands have been run, or the reason for not running them is stated clearly.
- No generated, cache, or build-output files were accidentally changed.
