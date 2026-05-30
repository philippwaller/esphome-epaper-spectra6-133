#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

configs=()
while IFS= read -r config; do
  configs+=("$config")
done < <(
  find configs -maxdepth 1 -type f -name '*.yaml' \
    ! -name 'secrets.yaml' \
    ! -name 'secrets.example.yaml' \
    | sort
)

if [[ ${#configs[@]} -eq 0 ]]; then
  echo "No standalone configs found under configs/." >&2
  exit 1
fi

for config in "${configs[@]}"; do
  echo "==> Validating ${config}"
  ./scripts/esphomew config "$config"
done
