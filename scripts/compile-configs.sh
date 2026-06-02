#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ESPHOME_BIN="${ROOT_DIR}/.venv/bin/esphome"
PYTHON_BIN="${ROOT_DIR}/.venv/bin/python"

cd "${ROOT_DIR}"

if [[ ! -x "${ESPHOME_BIN}" ]]; then
  cat >&2 <<'EOF'
ESPHome is not installed in the local virtual environment.
This usually means the repository has not been set up yet.

Run:
  ./scripts/setup.sh

Then retry:
  ./scripts/compile-configs.sh
EOF
  exit 1
fi

ESPHOMEW_BIN="${ROOT_DIR}/scripts/esphomew"
if [[ ! -x "${ESPHOMEW_BIN}" ]]; then
  echo "Error: ${ESPHOMEW_BIN} is not executable. Run 'chmod +x scripts/esphomew' first." >&2
  exit 1
fi

configs=()
if [[ $# -gt 0 ]]; then
  configs=("$@")
else
  resolved_configs="$("${PYTHON_BIN}" "${ROOT_DIR}/scripts/esphome-versions.py" compile-configs)"
  while IFS= read -r config; do
    [[ -n "${config}" ]] || continue
    configs+=("${config}")
  done <<< "${resolved_configs}"
fi

if [[ ${#configs[@]} -eq 0 ]]; then
  echo "No compile smoke-test configs were resolved." >&2
  exit 1
fi

for config in "${configs[@]}"; do
  echo "==> Compiling ${config}"
  "${ESPHOMEW_BIN}" compile "$config"
done
