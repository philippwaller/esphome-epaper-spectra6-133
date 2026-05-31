# Contributing to ESPHome epaper_spectra6_133

Thank you for your interest in contributing! 🙏

---

## Prerequisites

| Requirement | Details |
| --- | --- |
| **Python** | 3.x with `venv` support |
| **Git** | Required so setup can install the repository hooks |
| **Shell** | Bash-compatible shell |
| **Hardware** | ESP32-S3 with PSRAM (for on-device testing) |
| **Framework** | ESP-IDF — the driver uses the ESP-IDF SPI master API directly |
| **ESPHome** | ≥ 2026.4.0 (tested with 2026.4.1) |

---

## Local Setup

### 1. Clone the repository

```bash
git clone https://github.com/philippwaller/esphome-epaper-spectra6-133.git epaper_spectra6_133
cd epaper_spectra6_133
```

### 2. Run local setup

```bash
./scripts/setup.sh
```

This creates or updates `.venv`, installs the pinned ESPHome and dev tooling,
creates `configs/secrets.yaml` from the example when missing, and installs the
required `pre-commit` and `pre-push` Git hooks.

The generated secrets file contains placeholders for local validation only.
Replace those values before flashing real hardware. Existing `configs/secrets.yaml`
files are never overwritten.

The repository ships pre-commit hooks for:

- **end-of-file-fixer** — ensures files end with a newline
- **trailing-whitespace** — strips trailing whitespace while preserving Markdown line breaks
- **requirements-txt-fixer** — keeps `requirements*.txt` formatting consistent
- **check-yaml** — YAML syntax checks, including ESPHome custom tags
- **check-executables-have-shebangs** — validates scripts under `scripts/`
- **check-json** — validates JSON files
- **check-added-large-files** — blocks oversized files from being committed
- **check-case-conflict** — catches case-only filename conflicts
- **check-merge-conflict** — catches unresolved merge markers
- **actionlint** — validates GitHub Actions workflows
- **yamllint** — YAML lint (2-space indent, no trailing whitespace)
- **shellcheck** — Shell script linting for `scripts/`
- **ruff** and **ruff-format** — Python linting and formatting
- **markdownlint-cli2** — Markdown lint with the repository's relaxed documentation rules
- **clang-format** — C++ formatting (ESPHome style)

The required `pre-push` hooks additionally run:

- `bash ./scripts/validate-configs.sh`
- `bash ./scripts/run-host-tests.sh`

---

## Validation

### Run host-side C++ tests

```bash
bash ./scripts/run-host-tests.sh
```

Coverage reports are written to `build/coverage/` as HTML and Cobertura XML.

### Validate ESPHome configs

```bash
bash ./scripts/validate-configs.sh
```

All standalone configs under `configs/` must validate cleanly before merging.
If validation reports that ESPHome or `configs/secrets.yaml` is missing, run:

```bash
./scripts/setup.sh
```

Then retry validation.

### Build & flash over USB

```bash
./scripts/esphomew run configs/hello-world.yaml
```

### Run all pre-commit hooks

```bash
./.venv/bin/pre-commit run --all-files
```

### Choose checks based on the change

- Framebuffer, dirty-region, controller, or low-level C++ logic → `bash ./scripts/run-host-tests.sh`
- Python codegen, schema, YAML, examples, or packages → `bash ./scripts/validate-configs.sh`
- Formatting-sensitive changes → `./.venv/bin/pre-commit run --all-files`
- Hardware-relevant display-flow changes → `./scripts/esphomew run configs/hello-world.yaml`

---

## VS Code Integration

Open the workspace and use the pre-configured tasks:

| Task | Description |
| --- | --- |
| **Setup developer environment** | Prepare `.venv`, local secrets, and required Git hooks |
| **Validate example configs** | Run `esphomew config` across all standalone example YAMLs |
| **Run host tests** | Execute the host-side C++ test suite and refresh coverage output |
| **Run pre-commit hooks** | Run all configured hooks across all files |
| **Run hello-world config** | Build & flash the minimal Hello World configuration when hardware is connected |

---

## Project Structure

```text
components/epaper_spectra6_133/     External component source
├── __init__.py                     Shared Python declarations
├── display.py                      ESPHome platform adapter (config schema + codegen)
├── epaper_spectra6_133.h / .cpp            ESPHome display component (lifecycle, drawing)
├── epaper_spectra6_133_constants.h         Panel geometry, colours, register addresses
├── epaper_spectra6_133_framebuffer.h / .cpp Pixel packing, colour mapping, dirty detection
├── epaper_spectra6_133_transport.h / .cpp  GPIO & SPI transport layer
└── epaper_spectra6_133_controller.h / .cpp Init sequence, refresh, partial windows

configs/                            Device configurations & secrets
├── hello-world.yaml                Minimal Hello World
├── clock.yaml                      Clock with auto partial updates
├── image.yaml                      Single-image display example
├── test-sheet.yaml                 Panel reference/calibration sheet
├── secrets.example.yaml            Template — copy to secrets.yaml
└── secrets.yaml                    (git-ignored)

packages/boards/                    Board-specific YAML packages
└── goo_display/
    └── esp32_133c02.yaml           ESP32-133C02 board defaults & pin mapping

scripts/
├── setup.sh                        Prepare local dev environment, secrets, and hooks
├── validate-configs.sh             Validate standalone ESPHome example configs
├── esphomew                        Wrapper that runs ESPHome inside the venv
├── run-host-tests.sh               Build & run C++ unit tests with coverage
└── convert_image.py                Image → 6-colour palette converter

tests/
├── CMakeLists.txt                  CMake config for host-side tests
├── framebuffer_test.cpp            Framebuffer unit tests
├── controller_test.cpp             Controller unit tests
├── component_test.cpp              Component unit tests
└── support/                        Test helpers and mocks
```

### File Naming Conventions

- Component source: `epaper_spectra6_133_<layer>.{h,cpp}`
- YAML configs: `configs/<purpose>.yaml`
- Board packages: `packages/boards/<vendor>/<board>.yaml`

---

## Pull Request Process

1. **Fork** the repository and create a feature branch from `main`.
2. **Run setup**: `./scripts/setup.sh`
3. **Make your changes** — keep PRs focused on a single concern.
4. **Validate** all standalone example configs: `bash ./scripts/validate-configs.sh`
5. **Run host C++ tests**: `bash ./scripts/run-host-tests.sh`
6. **Run hooks**: `./.venv/bin/pre-commit run --all-files`
7. **Push** and open a Pull Request against `main`.
8. All CI quality gates must pass before merge.

---

## Commit Convention

We use [Conventional Commits](https://www.conventionalcommits.org/):

```text
<type>(<optional scope>): <description>

[optional body]

[optional footer(s)]
```

### Types

| Type | Description |
| --- | --- |
| `feat` | A new feature |
| `fix` | A bug fix |
| `docs` | Documentation only |
| `chore` | Maintenance (deps, config) |
| `refactor` | Code change that neither fixes a bug nor adds a feature |
| `perf` | Performance improvement |
| `test` | Adding or fixing tests |
| `ci` | CI/CD changes |
| `build` | Build system or dependency changes |
| `revert` | Reverting a previous change |

### Breaking Changes

Use `!` after the type or add a `BREAKING CHANGE:` footer:

```text
feat!: remove deprecated clear() overload

BREAKING CHANGE: clear() no longer accepts a string argument.
```

---

## Code Style

- **C++** — ESPHome style, enforced by clang-format (pre-commit hook)
- **Python** — Standard ESPHome codegen patterns; keep `__init__.py` and `display.py` in sync
- **YAML** — 2-space indent, no trailing whitespace (yamllint)

---

## Image Conversion

Convert images to the panel's exact six-colour palette before flashing:

```bash
# Guided mode
python scripts/convert_image.py --guided

# List available presets
python scripts/convert_image.py --list-presets

# One-shot conversion
python scripts/convert_image.py \
  configs/images/your-photo.jpg \
  --preset default \
  --fit cover \
  --saturation 1.12
```

Output goes to `configs/images/<name>-epaper-<settings>.png`. After conversion,
the script prints a ready-to-paste ESPHome `image:` snippet.

---

## Releases

The release workflow uses [Release Please](https://github.com/googleapis/release-please)
to open an automated release PR based on conventional commits since the last release.
Merging that PR creates the tag and GitHub Release automatically.

---

## Reporting Bugs

Please open an issue with:

- ESPHome version
- Board / hardware description
- Minimal YAML config to reproduce
- Relevant log output (set `level: DEBUG` for the `epaper_spectra6_133` logger)

## Feature Requests

Open an issue describing:

- The problem you're trying to solve
- Your proposed solution (if any)
- Whether you'd be willing to implement it
