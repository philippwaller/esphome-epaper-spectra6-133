# Benchmarks

This project uses Google Benchmark for host-side performance checks. The
benchmark target links the same production C++ framebuffer, controller, and
display component sources used by the host tests, with the existing test stubs
standing in for ESP-IDF, ESPHome, SPI, GPIO, timers, and hardware refreshes.

No real display, Wi-Fi, serial port, ESPHome runtime, or ESP32 hardware is
required.

## Covered Hot Paths

- RGB-to-panel colour mapping with realistic mixed image pixels.
- Nibble-packed framebuffer pixel writes.
- Image-to-framebuffer conversion using production colour mapping and packing.
- Display draw calls with change tracking enabled.
- Full-frame fills and built-in display preparation patterns.
- Dirty-region comparison for unchanged, small, medium, and full-frame changes.
- Partial-region alignment for single-half, split-boundary, clipped, and full-screen regions.

The input variants cover small UI/icon-sized inputs, medium dashboard/image
regions, and the 1200x1600 worst-case panel size where applicable.

## Run Locally

```bash
bash ./scripts/run-benchmarks.sh
```

The script configures a Release CMake build in `build/benchmarks`, builds the
`epaper_spectra6_133_benchmarks` target, runs it, and writes Google Benchmark
JSON to:

```text
build/benchmarks/results.json
```

Useful variants:

```bash
# Run only dirty-region benchmarks
bash ./scripts/run-benchmarks.sh --filter 'ChangeDetection/.*'

# Use a longer sample time for local investigation
bash ./scripts/run-benchmarks.sh --min-time 0.2s

# Write results somewhere else
bash ./scripts/run-benchmarks.sh --output /tmp/epaper-benchmarks.json
```

Extra Google Benchmark arguments can be passed after `--`:

```bash
bash ./scripts/run-benchmarks.sh -- --benchmark_repetitions=3
```

## Check a Feature Branch Locally

Install the Bencher CLI before running the local regression workflow:

```bash
curl --proto '=https' --tlsv1.2 -sSfL https://bencher.dev/download/install-cli.sh | sh
bencher --version
```

Use `scripts/check-performance-regression.sh` when you want to compare
the branch you are developing against a baseline branch before opening or
updating a pull request. Run it from the feature branch, not from `main`:

```bash
scripts/check-performance-regression.sh
```

The script temporarily stashes local changes, switches to `main`, records five
baseline samples, switches back to the original feature branch, restores the
stash, and records one feature sample. Results are written to an isolated
Bencher scratch project by default, so local development runs do not update the
official repository project configured in CI.

For a faster smoke test during active development:

```bash
scripts/check-performance-regression.sh \
  --baseline-iterations 1 \
  --feature-iterations 1 \
  --filter 'ColorMapping/MixedPixels/64/64'
```

If `main` does not yet contain the benchmark infrastructure, use a temporary
baseline branch that has the benchmark files but does not include the feature
change being measured:

```bash
scripts/check-performance-regression.sh \
  --baseline-branch benchmark-baseline \
  --baseline-iterations 1 \
  --feature-iterations 1
```

To continue adding samples to the same local Bencher scratch project, pass a
fixed project slug:

```bash
scripts/check-performance-regression.sh \
  --project-slug esphome-epaper-spectra6-local-my-test
```

Use `--verbose` when debugging command output. By default, detailed logs,
Google Benchmark JSON files, and Bencher responses are stored under:

```text
build/bencher-local/
```

## CI and Bencher

The `Host C++ Benchmarks` job in `.github/workflows/ci.yml` runs the benchmark
script for relevant C++ or CI changes on trusted non-PR events, uploads
`build/benchmarks/results.json` as a workflow artifact, and tracks `main`
history in Bencher.

Pull requests use the two-workflow pattern recommended by Bencher for fork
support:

- `.github/workflows/benchmarks-pr.yml` runs benchmarks in the `pull_request`
  context and uploads only artifacts. It does not receive or use secrets.
- `.github/workflows/bencher-pr.yml` runs later in the `workflow_run` context,
  downloads the benchmark result and PR event artifacts, validates the JSON, and
  uploads the result to Bencher with repository secrets.
- `.github/workflows/bencher-pr-closed.yml` archives the synthetic Bencher PR
  branch when the pull request closes.

Bencher reporting is enabled when both of these repository settings exist:

- Repository secret: `BENCHER_API_KEY`
- Repository variable: `BENCHER_PROJECT`

CI sends the Google Benchmark JSON file to Bencher with the `cpp_google`
adapter. On `main`, it maintains a latency threshold using a percentage test:
after at least five historical samples, a benchmark alerts when it is more than
30% slower than the recent historical mean. Pull requests are tracked on a
synthetic `pr-<number>` Bencher branch, clone the `main` thresholds, and use
`--error-on-alert`, so a regression alert fails the PR check.

Fork pull requests are supported because the benchmark run and Bencher upload are
separate. Untrusted fork code can produce benchmark JSON, but the workflow that
has `BENCHER_API_KEY` only downloads and validates artifacts; it does not check
out or execute fork code.
