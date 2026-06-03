#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ESPHOME_BIN="${ROOT_DIR}/.venv/bin/esphome"
PYTHON_BIN="${ROOT_DIR}/.venv/bin/python"
SECRETS_FILE="${ROOT_DIR}/configs/secrets.yaml"

cd "${ROOT_DIR}"

if [[ ! -x "${ESPHOME_BIN}" ]]; then
  cat >&2 <<'EOF'
ESPHome is not installed in the local virtual environment.
This usually means the repository has not been set up yet.

Run:
  ./scripts/setup.sh

Then retry:
  ./scripts/validate-configs.sh
EOF
  exit 1
fi

configs=()
if ! resolved_configs="$("${PYTHON_BIN}" "${ROOT_DIR}/scripts/esphome-versions.py" compile-configs)"; then
  echo "Failed to resolve standalone configs." >&2
  exit 1
fi
while IFS= read -r config; do
  [[ -n "${config}" ]] || continue
  configs+=("${config}")
done <<< "${resolved_configs}"

if [[ ${#configs[@]} -eq 0 ]]; then
  echo "No standalone configs found under configs/." >&2
  exit 1
fi

if grep -q '!secret' "${configs[@]}" && [[ ! -f "${SECRETS_FILE}" ]]; then
  cat >&2 <<'EOF'
ESPHome configs reference !secret values, but configs/secrets.yaml is missing.

Run:
  ./scripts/setup.sh

Then retry:
  ./scripts/validate-configs.sh
EOF
  exit 1
fi

for config in "${configs[@]}"; do
  echo "==> Validating ${config}"
  ./scripts/esphomew config "$config"
done
