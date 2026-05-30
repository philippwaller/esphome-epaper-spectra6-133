#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build/tests"
COVERAGE_DIR="${ROOT_DIR}/build/coverage"

if [[ -x "${ROOT_DIR}/.venv/bin/python" ]]; then
  PYTHON_BIN="${ROOT_DIR}/.venv/bin/python"
elif command -v python >/dev/null 2>&1; then
  PYTHON_BIN="$(command -v python)"
elif command -v python3 >/dev/null 2>&1; then
  PYTHON_BIN="$(command -v python3)"
else
  echo "Python was not found." >&2
  exit 1
fi

if ! "${PYTHON_BIN}" -m gcovr --version >/dev/null 2>&1; then
  echo "gcovr is not installed for ${PYTHON_BIN}. Run scripts/bootstrap-venv.sh first, or pip install -r requirements-dev.txt." >&2
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
