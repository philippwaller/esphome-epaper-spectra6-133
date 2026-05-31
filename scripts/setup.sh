#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VENV_DIR="${ROOT_DIR}/.venv"
VENV_PYTHON="${VENV_DIR}/bin/python"
PRE_COMMIT_BIN="${VENV_DIR}/bin/pre-commit"
REQUIREMENTS_DEV="${ROOT_DIR}/requirements-dev.txt"
SECRETS_EXAMPLE="${ROOT_DIR}/configs/secrets.example.yaml"
SECRETS_FILE="${ROOT_DIR}/configs/secrets.yaml"
readonly ROOT_DIR VENV_DIR VENV_PYTHON PRE_COMMIT_BIN REQUIREMENTS_DEV SECRETS_EXAMPLE SECRETS_FILE

if [[ -t 1 ]]; then
  BOLD="$(printf '\033[1m')"
  GREEN="$(printf '\033[32m')"
  RED="$(printf '\033[31m')"
  RESET="$(printf '\033[0m')"
else
  BOLD=""
  GREEN=""
  RED=""
  RESET=""
fi
readonly BOLD GREEN RED RESET

step() {
  printf '\n%s==>%s %s\n' "${BOLD}" "${RESET}" "$*"
}

ok() {
  printf '%s[ok]%s %s\n' "${GREEN}" "${RESET}" "$*"
}

die() {
  printf '%s[error]%s %s\n' "${RED}" "${RESET}" "$*" >&2
  exit 1
}

run_quietly() {
  local log_file
  log_file="$(mktemp "${TMPDIR:-/tmp}/esphome-setup.XXXXXX")"

  if "$@" >"${log_file}" 2>&1; then
    rm -f "${log_file}"
    return 0
  fi

  cat "${log_file}" >&2
  rm -f "${log_file}"
  return 1
}

prepare_python_environment() {
  if [[ ! -x "${VENV_PYTHON}" ]]; then
    if ! command -v python3 >/dev/null 2>&1; then
      echo "Python 3 was not found. Install Python 3, then rerun ./scripts/setup.sh." >&2
      return 1
    fi

    python3 -m venv "${VENV_DIR}"
  fi

  "${VENV_PYTHON}" -m pip install --upgrade pip
  "${VENV_PYTHON}" -m pip install -r "${REQUIREMENTS_DEV}"
}

cd "${ROOT_DIR}"

printf '%sESPHome Spectra 6 local setup%s\n' "${BOLD}" "${RESET}"

if [[ ! -d "${ROOT_DIR}/.git" ]]; then
  die "This setup must run from a Git checkout so required hooks can be installed."
fi

step "Prepare Python environment"
if ! run_quietly prepare_python_environment; then
  die "Python environment setup failed."
fi
ok "Python environment is ready"

if [[ ! -x "${PRE_COMMIT_BIN}" ]]; then
  die "pre-commit was not installed in ${VENV_DIR}. Check requirements-dev.txt, then rerun ./scripts/setup.sh."
fi

step "Prepare local secrets"
if [[ ! -f "${SECRETS_FILE}" ]]; then
  if [[ ! -f "${SECRETS_EXAMPLE}" ]]; then
    die "Missing ${SECRETS_EXAMPLE}; cannot create local secrets."
  fi

  cp "${SECRETS_EXAMPLE}" "${SECRETS_FILE}"
  ok "Created configs/secrets.yaml from configs/secrets.example.yaml"
else
  ok "Keeping existing configs/secrets.yaml"
fi

install_hook() {
  local name="$1"
  shift

  if ! "${PRE_COMMIT_BIN}" install "$@"; then
    die "Failed to install ${name}. If an existing hook is in the way, move it aside and rerun ./scripts/setup.sh."
  fi

  ok "${name} is installed"
}

step "Install required Git hooks"
install_hook "pre-commit hook"
install_hook "pre-push hook" --hook-type pre-push

cat <<'EOF'

Setup complete.

Your virtual environment is ready at .venv.
To activate it for the current terminal session, run:

  source .venv/bin/activate
EOF
