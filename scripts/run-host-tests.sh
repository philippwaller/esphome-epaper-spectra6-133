#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build/tests"
COVERAGE_DIR="${ROOT_DIR}/build/coverage"
PYTHON_BIN="${ROOT_DIR}/.venv/bin/python"

if [[ ! -x "${PYTHON_BIN}" ]]; then
  echo "Project virtual environment not found. Run ./scripts/setup.sh first." >&2
  exit 1
fi

if ! "${PYTHON_BIN}" -m gcovr --version >/dev/null 2>&1; then
  echo "gcovr is not installed for ${PYTHON_BIN}. Run ./scripts/setup.sh first." >&2
  exit 1
fi

cd "${ROOT_DIR}"

rm -rf "${BUILD_DIR}"
cmake -S tests -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Debug -DENABLE_COVERAGE=ON
cmake --build "${BUILD_DIR}"
ctest --test-dir "${BUILD_DIR}" --output-on-failure

mkdir -p "${COVERAGE_DIR}"
"${PYTHON_BIN}" -m gcovr \
  --root "${ROOT_DIR}" \
  --object-directory "${BUILD_DIR}" \
  --filter "${ROOT_DIR}/components/epaper_spectra6_133" \
  --exclude "${ROOT_DIR}/tests" \
  --xml "${COVERAGE_DIR}/coverage.xml" \
  --xml-pretty \
  --html-details "${COVERAGE_DIR}/index.html" \
  --print-summary

echo "Coverage reports written to ${COVERAGE_DIR}"
