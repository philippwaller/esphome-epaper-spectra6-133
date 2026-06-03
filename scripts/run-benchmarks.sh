#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build/benchmarks}"
OUTPUT_PATH="${OUTPUT_PATH:-${BUILD_DIR}/results.json}"
BENCHMARK_MIN_TIME="${BENCHMARK_MIN_TIME:-0.05s}"
BENCHMARK_FILTER="${BENCHMARK_FILTER:-.*}"

usage() {
  cat <<'EOF'
Usage: scripts/run-benchmarks.sh [options] [-- google-benchmark-args...]

Options:
  --build-dir PATH       CMake build directory (default: build/benchmarks)
  --output PATH          Google Benchmark JSON output path (default: build/benchmarks/results.json)
  --min-time VALUE       Per-benchmark minimum run time (default: 0.05s)
  --filter REGEX         Google Benchmark filter regex (default: .*)
  -h, --help             Show this help

Environment overrides:
  BUILD_DIR, OUTPUT_PATH, BENCHMARK_MIN_TIME, BENCHMARK_FILTER
EOF
}

EXTRA_ARGS=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --build-dir)
      BUILD_DIR="$2"
      shift 2
      ;;
    --output)
      OUTPUT_PATH="$2"
      shift 2
      ;;
    --min-time)
      BENCHMARK_MIN_TIME="$2"
      shift 2
      ;;
    --filter)
      BENCHMARK_FILTER="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    --)
      shift
      EXTRA_ARGS+=("$@")
      break
      ;;
    *)
      EXTRA_ARGS+=("$1")
      shift
      ;;
  esac
done

cmake -S "${ROOT_DIR}/tests" -B "${BUILD_DIR}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF \
  -DENABLE_BENCHMARKS=ON

cmake --build "${BUILD_DIR}" --target epaper_spectra6_133_benchmarks --config Release

mkdir -p "$(dirname "${OUTPUT_PATH}")"

BENCHMARK_BIN="${BUILD_DIR}/epaper_spectra6_133_benchmarks"
if [[ ! -x "${BENCHMARK_BIN}" && -x "${BUILD_DIR}/Release/epaper_spectra6_133_benchmarks" ]]; then
  BENCHMARK_BIN="${BUILD_DIR}/Release/epaper_spectra6_133_benchmarks"
fi

BENCHMARK_ARGS=(
  --benchmark_format=console
  --benchmark_out="${OUTPUT_PATH}"
  --benchmark_out_format=json
  --benchmark_min_time="${BENCHMARK_MIN_TIME}"
  --benchmark_filter="${BENCHMARK_FILTER}"
)
if [[ ${#EXTRA_ARGS[@]} -gt 0 ]]; then
  BENCHMARK_ARGS+=("${EXTRA_ARGS[@]}")
fi

"${BENCHMARK_BIN}" "${BENCHMARK_ARGS[@]}"

echo "Benchmark results written to ${OUTPUT_PATH}"
